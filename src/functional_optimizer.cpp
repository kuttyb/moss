#include <algorithm>
#include <cstddef>
#include <functional>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>
#include "ast.hpp"
#include "functional_ir.hpp"
#include "functional_optimizer.hpp"
#include "compiler_stage_timer.hpp"
#include "parser_support.hpp"
#include "functional_optimizer_support.hpp"

using std::string;
using std::vector;

namespace moss {

class FunctionalOptimizer {
 public:
  explicit FunctionalOptimizer(Program& program) : program_(program) {}

  void run(bool enabled) {
    CompilerStageTimer timer("functional_optimizer");
    program_.functional_traversal_groups.clear();
    for (auto& pipeline : program_.functional_pipelines) {
      pipeline.count_uses_exact_length = false;
      pipeline.count_uses_known_size = false;
      pipeline.known_source_size = 0;
      pipeline.short_circuit_terminal = false;
      pipeline.virtual_upstream_pipeline_id = 0;
      pipeline.virtualized_into_pipeline_id = 0;
      pipeline.traversal_group_id = 0;
      pipeline.binding_name.clear();
      pipeline.binding_immutable = false;
      pipeline.binding_use_count = 0;
      pipeline.binding_materialization_reason.clear();
      pipeline.optimization_notes.clear();
      pipeline.semantic_rewrites.clear();
      pipeline.fused = enabled && pipeline.fusion_eligible;
      pipeline.lowered_provenance.clear();
      for (auto& node : pipeline.nodes) {
        node.materialization_eliminated = false;
        node.materialization = FunctionalMaterializationKind::NotApplicable;
        node.escapes = false;
        node.multiple_consumers = false;
        node.barrier_required = false;
        node.dead_stage_eliminated = false;
        node.semantic_work_eliminated = false;
        node.semantic_elimination_reason.clear();
        node.materialization_reason.clear();
      }
      initialize_semantic_plan(pipeline);
      if (pipeline.fused)
        pipeline.decision = "fused stages 1-" +
            std::to_string(pipeline.nodes.size() - 1) +
            "; intermediates eliminated";
      else if (!enabled && pipeline.fusion_eligible)
        pipeline.decision =
            "eager reference lowering (-O0); logical intermediates retained";

      // This deliberately bounded sequence is the Phase 4.6 semantic-space
      // optimizer. Each pass either reduces the number of semantic steps or
      // groups adjacent steps once, so no general fixed point is required.
      if (enabled) {
        rewrite_identity_map_filter(pipeline);
        compose_adjacent_steps(pipeline, FunctionalNodeKind::Map,
                               "compose-map");
        compose_adjacent_steps(pipeline, FunctionalNodeKind::Filter,
                               "compose-filter");
      }
      plan_terminal_lowering(pipeline, enabled);
      if (enabled) plan_terminal_aware_semantics(pipeline);
    }

    if (enabled) {
      for (auto& function : program_.functions) {
        std::set<string> known;
        for (const auto& parameter : function.params) known.insert(parameter.name);
        if (function.static_dispatch && !function.specializations.empty()) {
          for (const auto& specialization : function.specializations) {
            string context = functional_function_context(function, &specialization);
            plan_scope(function.body, context, known,
                       result_pipeline_id(function.result_functional_pipeline_ids,
                                          context),
                       function.result_expression);
          }
        } else {
          string context = functional_function_context(function);
          plan_scope(function.body, context, known,
                     result_pipeline_id(function.result_functional_pipeline_ids,
                                        context),
                     function.result_expression);
        }
      }
      for (auto& object : program_.objects) {
        for (auto& method : object.methods) {
          std::set<string> known{"self"};
          for (const auto& field : object.fields) known.insert(field.name);
          for (const auto& parameter : method.params) known.insert(parameter.name);
          string context = functional_method_context(object, method);
          plan_scope(method.body, context, known,
                     result_pipeline_id(method.result_functional_pipeline_ids,
                                        context),
                     method.result_expression);
        }
      }
      for (auto& domain : program_.domains) {
        for (auto& handler : domain.handlers) {
          std::set<string> known{"self"};
          for (const auto& field : domain.state) known.insert(field.name);
          for (const auto& parameter : handler.params) known.insert(parameter.name);
          plan_scope(handler.body, functional_handler_context(domain, handler),
                     known, 0, std::nullopt);
        }
      }
      if (program_.main)
        plan_scope(program_.main->body, "main", {}, 0, std::nullopt);
      for (auto& test : program_.tests)
        plan_scope(test.body, "test:" + test.name, {}, 0, std::nullopt);
      for (auto& benchmark : program_.benchmarks)
        plan_scope(benchmark.body, "bench:" + benchmark.name, {}, 0,
                   std::nullopt);
    }

    rebuild_provenance();
    plan_materializations(enabled);
  }

 private:
  struct ScopeOccurrence {
    size_t statement_index = 0;
    size_t pipeline_id = 0;
    Stmt* statement = nullptr;
    bool result_expression = false;
    bool new_binding = false;
    bool explicitly_immutable = false;
    string expression;
    string result_binding;
  };

  Program& program_;

  FunctionalPipeline* pipeline_by_id(size_t id) {
    auto found = std::find_if(
        program_.functional_pipelines.begin(),
        program_.functional_pipelines.end(),
        [&](const FunctionalPipeline& pipeline) {
          return pipeline.transient_id == id;
        });
    return found == program_.functional_pipelines.end() ? nullptr : &*found;
  }

  static void initialize_semantic_plan(FunctionalPipeline& pipeline) {
    pipeline.semantic_steps.clear();
    for (size_t node_index = 1; node_index < pipeline.nodes.size();
         ++node_index) {
      FunctionalSemanticStep step;
      step.kind = pipeline.nodes[node_index].kind;
      step.source_node_indices.push_back(node_index);
      step.provenance = pipeline.nodes[node_index].provenance;
      pipeline.semantic_steps.push_back(std::move(step));
    }
  }

  static void record_semantic_rewrite(
      FunctionalPipeline& pipeline, string name, string detail,
      vector<string> provenance) {
    FunctionalSemanticRewrite rewrite;
    rewrite.name = std::move(name);
    rewrite.detail = std::move(detail);
    rewrite.provenance = std::move(provenance);
    pipeline.semantic_rewrites.push_back(std::move(rewrite));
  }

  static bool step_nodes_satisfy(
      const FunctionalPipeline& pipeline,
      const FunctionalSemanticStep& step,
      const std::function<bool(const FunctionalNode&)>& predicate) {
    if (step.source_node_indices.empty()) return false;
    for (size_t node_index : step.source_node_indices) {
      if (node_index == 0 || node_index >= pipeline.nodes.size() ||
          !predicate(pipeline.nodes[node_index]))
        return false;
    }
    return true;
  }

  static bool trivial_pipeline_element_type(const string& collection_type) {
    const auto term = parsed_type(collection_type);
    if (!applied_builtin(term, {"vector", "seq"}) || term->type_args.size() != 1)
      return false;
    const auto& element = term->type_args[0];
    return primitive_term(element, "int") || primitive_term(element, "float") ||
           primitive_term(element, "bool");
  }

  static bool identity_map_step(const FunctionalPipeline& pipeline,
                                const FunctionalSemanticStep& step) {
    if (step.kind != FunctionalNodeKind::Map ||
        step.source_node_indices.size() != 1)
      return false;
    size_t node_index = step.source_node_indices.front();
    if (node_index == 0 || node_index >= pipeline.nodes.size()) return false;
    const FunctionalNode& node = pipeline.nodes[node_index];
    return trim(node.callable_expression) == "_" &&
        canonical_type_name(node.input_type) ==
            canonical_type_name(node.output_type) &&
        trivial_pipeline_element_type(node.input_type) &&
        node.ownership == Effect::Read && node.effects.fusion_safe();
  }

  // Predicate pushdown is intentionally limited to a proof the current IR
  // can make without algebra: a trivial-element `map(_)` is the identity. The
  // following predicate can therefore consume the earlier value unchanged,
  // and the redundant map can disappear. All non-identity maps remain in
  // source order.
  static void rewrite_identity_map_filter(FunctionalPipeline& pipeline) {
    for (size_t index = 0; index + 1 < pipeline.semantic_steps.size();) {
      FunctionalSemanticStep& map_step = pipeline.semantic_steps[index];
      const FunctionalSemanticStep& filter_step =
          pipeline.semantic_steps[index + 1];
      if (map_step.kind != FunctionalNodeKind::Map ||
          filter_step.kind != FunctionalNodeKind::Filter) {
        ++index;
        continue;
      }

      string disabled_reason;
      if (!pipeline.fusion_eligible)
        disabled_reason = "pipeline has " + barrier_reason(pipeline);
      else if (!identity_map_step(pipeline, map_step))
        disabled_reason = "map is not a proven identity";
      else if (!step_nodes_satisfy(
                   pipeline, filter_step,
                   [](const FunctionalNode& node) {
                     return node.effects.fusion_safe() &&
                         node.ownership == Effect::Read;
                   }))
        disabled_reason = "predicate has an observable or ownership barrier";

      if (!disabled_reason.empty()) {
        pipeline.optimization_notes.push_back(
            "semantic-opt: predicate-pushdown disabled; reason: " +
            disabled_reason);
        ++index;
        continue;
      }

      vector<string> provenance = map_step.provenance;
      provenance.insert(provenance.end(), filter_step.provenance.begin(),
                        filter_step.provenance.end());
      for (size_t node_index : map_step.source_node_indices) {
        FunctionalNode& node = pipeline.nodes[node_index];
        node.semantic_work_eliminated = true;
        node.semantic_elimination_reason =
            "predicate moved through a proven identity map";
      }
      record_semantic_rewrite(
          pipeline, "predicate-pushdown",
          "filter moved to the earlier value through map(_)", provenance);
      pipeline.optimization_notes.push_back(
          "semantic-opt: predicate-pushdown through proven identity map");
      pipeline.semantic_steps.erase(
          pipeline.semantic_steps.begin() +
          static_cast<std::ptrdiff_t>(index));
      // Keep the same index so another immediately preceding identity map can
      // be considered against the now-earlier filter.
    }
  }

  static void compose_adjacent_steps(FunctionalPipeline& pipeline,
                                     FunctionalNodeKind kind,
                                     const string& rewrite_name) {
    if (!pipeline.fusion_eligible) {
      for (size_t index = 0; index + 1 < pipeline.semantic_steps.size();
           ++index) {
        if (pipeline.semantic_steps[index].kind == kind &&
            pipeline.semantic_steps[index + 1].kind == kind) {
          pipeline.optimization_notes.push_back(
              "semantic-opt: " + rewrite_name +
              " disabled; reason: " + barrier_reason(pipeline));
          break;
        }
      }
      return;
    }

    for (size_t index = 0; index < pipeline.semantic_steps.size();) {
      if (pipeline.semantic_steps[index].kind != kind) {
        ++index;
        continue;
      }
      size_t end = index + 1;
      while (end < pipeline.semantic_steps.size() &&
             pipeline.semantic_steps[end].kind == kind &&
             step_nodes_satisfy(
                 pipeline, pipeline.semantic_steps[end],
                 [](const FunctionalNode& node) {
                   return node.effects.fusion_safe();
                 }))
        ++end;
      if (end == index + 1) {
        ++index;
        continue;
      }

      FunctionalSemanticStep composed = pipeline.semantic_steps[index];
      for (size_t part = index + 1; part < end; ++part) {
        const auto& step = pipeline.semantic_steps[part];
        composed.source_node_indices.insert(
            composed.source_node_indices.end(),
            step.source_node_indices.begin(), step.source_node_indices.end());
        composed.provenance.insert(composed.provenance.end(),
                                   step.provenance.begin(),
                                   step.provenance.end());
      }
      record_semantic_rewrite(
          pipeline, rewrite_name,
          "composed " + std::to_string(end - index) +
              " adjacent " +
              string(kind == FunctionalNodeKind::Map ? "maps" : "filters"),
          composed.provenance);
      pipeline.optimization_notes.push_back(
          "semantic-opt: " + rewrite_name + " (" +
          std::to_string(end - index) + " stages)");
      pipeline.semantic_steps.erase(
          pipeline.semantic_steps.begin() +
              static_cast<std::ptrdiff_t>(index),
          pipeline.semantic_steps.begin() +
              static_cast<std::ptrdiff_t>(end));
      pipeline.semantic_steps.insert(
          pipeline.semantic_steps.begin() +
              static_cast<std::ptrdiff_t>(index),
          std::move(composed));
      ++index;
    }
  }

  static size_t result_pipeline_id(
      const std::unordered_map<string,size_t>& ids, const string& context) {
    auto found = ids.find(context);
    return found == ids.end() ? 0 : found->second;
  }

  static size_t statement_pipeline_id(const Stmt& statement,
                                      const string& context) {
    auto found = statement.functional_pipeline_ids.find(context);
    if (found == statement.functional_pipeline_ids.end() ||
        found->second.empty())
      return 0;
    return found->second.front();
  }

  static size_t word_occurrences(const string& expression,
                                 const string& name) {
    size_t result = 0;
    bool in_string = false;
    bool escaped = false;
    for (size_t index = 0; index < expression.size();) {
      char ch = expression[index];
      if (in_string) {
        if (escaped) escaped = false;
        else if (ch == '\\') escaped = true;
        else if (ch == '"') in_string = false;
        ++index;
        continue;
      }
      if (ch == '"') {
        in_string = true;
        ++index;
        continue;
      }
      if (!(std::isalpha(static_cast<unsigned char>(ch)) || ch == '_')) {
        ++index;
        continue;
      }
      size_t end = index + 1;
      while (end < expression.size() &&
             (std::isalnum(static_cast<unsigned char>(expression[end])) ||
              expression[end] == '_'))
        ++end;
      if (expression.substr(index, end - index) == name) ++result;
      index = end;
    }
    return result;
  }

  static bool terminal_pipeline(const FunctionalPipeline& pipeline) {
    return pipeline.nodes.size() > 1 &&
        functional_terminal_kind(pipeline.nodes.back().kind);
  }

  static bool transformation_pipeline(const FunctionalPipeline& pipeline) {
    return pipeline.nodes.size() > 1 && !terminal_pipeline(pipeline);
  }

  // Sharing a traversal and omitting work have deliberately different proof
  // obligations. Potential divergence does not prevent ordinary ordered
  // fusion, but it does prevent an optimization from dropping an invocation.
  static bool callbacks_safe_to_skip(const FunctionalPipeline& pipeline) {
    if (pipeline.nodes.size() < 2) return true;
    return std::all_of(
        pipeline.nodes.begin() + 1, pipeline.nodes.end(),
        [](const FunctionalNode& node) {
          return node.effects.safe_to_skip();
        });
  }

  static bool callback_may_diverge(const FunctionalPipeline& pipeline) {
    if (pipeline.nodes.size() < 2) return false;
    return std::any_of(
        pipeline.nodes.begin() + 1, pipeline.nodes.end(),
        [](const FunctionalNode& node) {
          return node.effects.may_diverge;
        });
  }

  static bool has_map_stage(const FunctionalPipeline& pipeline) {
    return std::any_of(
        pipeline.nodes.begin(), pipeline.nodes.end(),
        [](const FunctionalNode& node) {
          return node.kind == FunctionalNodeKind::Map;
        });
  }

  static string barrier_reason(const FunctionalPipeline& pipeline) {
    for (const auto& node : pipeline.nodes) {
      const auto& effects = node.effects;
      if (effects.domain_write) return "observable domain WRITE";
      if (effects.domain_read) return "observable domain READ";
      if (effects.message) return "message send";
      if (effects.external_io) return "observable callback effect";
      if (effects.local_mutation) return "observable local mutation";
      if (effects.may_panic) return "callback may panic";
      if (!effects.raise_set.empty()) return "callback may raise";
      if (effects.unresolved) return "unresolved callback effect";
    }
    return "eager semantics required";
  }

  static string skipped_callback_barrier_reason(
      const FunctionalPipeline& pipeline) {
    if (pipeline.nodes.size() >= 2) {
      for (auto node = pipeline.nodes.begin() + 1;
           node != pipeline.nodes.end(); ++node) {
        const auto& effects = node->effects;
        if (effects.domain_write) return "observable domain WRITE";
        if (effects.domain_read) return "observable domain READ";
        if (effects.message) return "message send";
        if (effects.external_io) return "observable callback effect";
        if (effects.local_mutation) return "observable local mutation";
        if (effects.may_panic) return "callback may panic";
        if (!effects.raise_set.empty()) return "callback may raise";
        if (effects.may_diverge) return "skipped callback may diverge";
        if (effects.unresolved) return "unresolved callback effect";
      }
    }
    return "eager semantics required";
  }

  static string count_elimination_barrier_reason(
      const FunctionalPipeline& pipeline) {
    string reason = skipped_callback_barrier_reason(pipeline);
    if (reason == "skipped callback may diverge")
      return "callback may diverge";
    return reason;
  }

  static void erase_notes_with_prefix(FunctionalPipeline& pipeline,
                                      const string& prefix) {
    pipeline.optimization_notes.erase(
        std::remove_if(
            pipeline.optimization_notes.begin(),
            pipeline.optimization_notes.end(),
            [&](const string& note) { return starts_with(note, prefix); }),
        pipeline.optimization_notes.end());
  }

  static void disable_count_elimination(FunctionalPipeline& pipeline,
                                        const string& reason,
                                        bool mapped_work) {
    pipeline.count_uses_exact_length = false;
    pipeline.count_uses_known_size = false;
    pipeline.known_source_size = 0;
    erase_notes_with_prefix(pipeline, "count -> len");
    erase_notes_with_prefix(pipeline, "count -> constant");
    erase_notes_with_prefix(pipeline, "map elimination:");
    pipeline.semantic_rewrites.erase(
        std::remove_if(
            pipeline.semantic_rewrites.begin(),
            pipeline.semantic_rewrites.end(),
            [](const FunctionalSemanticRewrite& rewrite) {
              return rewrite.name == "terminal-count" ||
                  rewrite.name == "known-size-count";
            }),
        pipeline.semantic_rewrites.end());
    pipeline.optimization_notes.push_back(
        "count -> len disabled; reason: " + reason);
    bool map_was_eliminated = std::any_of(
        pipeline.nodes.begin(), pipeline.nodes.end(),
        [](const FunctionalNode& node) {
          return node.kind == FunctionalNodeKind::Map &&
              node.dead_stage_eliminated;
        });
    if (mapped_work && !map_was_eliminated)
      pipeline.optimization_notes.push_back(
          "map elimination: disabled; reason: " + reason);
  }

  static void disable_short_circuit(FunctionalPipeline& pipeline,
                                    const string& reason) {
    pipeline.short_circuit_terminal = false;
    erase_notes_with_prefix(pipeline, "short-circuit ");
    pipeline.optimization_notes.push_back(
        "short-circuit " + terminal_name(pipeline) +
        " disabled; reason: " + reason);
  }

  static string terminal_name(const FunctionalPipeline& pipeline) {
    if (pipeline.nodes.empty()) return "terminal";
    string name = functional_node_name(pipeline.nodes.back().kind);
    std::transform(name.begin(), name.end(), name.begin(),
                   [](unsigned char ch) {
                     return static_cast<char>(std::tolower(ch));
                   });
    return name;
  }

  static bool pipeline_requires_traversal(
      const FunctionalPipeline& pipeline) {
    return !pipeline.count_uses_exact_length;
  }

  static bool semantic_step_safe_to_skip(
      const FunctionalPipeline& pipeline,
      const FunctionalSemanticStep& step) {
    return step_nodes_satisfy(
        pipeline, step,
        [](const FunctionalNode& node) {
          return node.effects.safe_to_skip();
        });
  }

  static string semantic_step_skip_barrier(
      const FunctionalPipeline& pipeline,
      const FunctionalSemanticStep& step) {
    for (size_t node_index : step.source_node_indices) {
      if (node_index == 0 || node_index >= pipeline.nodes.size())
        return "invalid semantic stage";
      const auto& effects = pipeline.nodes[node_index].effects;
      if (effects.domain_write) return "observable domain WRITE";
      if (effects.domain_read) return "observable domain READ";
      if (effects.message) return "message send";
      if (effects.external_io) return "observable callback effect";
      if (effects.local_mutation) return "observable local mutation";
      if (effects.may_panic) return "callback may panic";
      if (!effects.raise_set.empty()) return "callback may raise";
      if (effects.may_diverge) return "callback may diverge";
      if (effects.unresolved) return "unresolved callback effect";
    }
    return "ownership or eager-ordering barrier";
  }

  static bool inert_literal(string value) {
    value = trim(std::move(value));
    if (value == "true" || value == "false") return true;
    size_t index = 0;
    if (!value.empty() && (value.front() == '+' || value.front() == '-'))
      ++index;
    bool digit = false;
    bool dot = false;
    for (; index < value.size(); ++index) {
      char ch = value[index];
      if (std::isdigit(static_cast<unsigned char>(ch))) {
        digit = true;
        continue;
      }
      if (ch == '.' && !dot) {
        dot = true;
        continue;
      }
      return false;
    }
    if (!digit) return false;
    try {
      size_t consumed = 0;
      if (dot)
        (void)std::stod(value, &consumed);
      else
        (void)std::stoll(value, &consumed);
      return consumed == value.size();
    } catch (const std::exception&) {
      // Do not let an optimization hide an invalid/out-of-range literal that
      // ordinary Rust lowering would reject.
      return false;
    }
  }

  static std::optional<size_t> inert_literal_vector_size(
      const string& expression) {
    string value = trim(expression);
    if (value.size() < 2 || value.front() != '[' || value.back() != ']')
      return std::nullopt;
    auto elements = split_top_level(value.substr(1, value.size() - 2), ',');
    if (elements.size() == 1 && trim(elements.front()).empty())
      return size_t{0};
    if (!std::all_of(elements.begin(), elements.end(),
                     [](const string& element) {
                       return inert_literal(element);
                     }))
      return std::nullopt;
    return elements.size();
  }

  static void eliminate_semantic_step(
      FunctionalPipeline& pipeline, size_t step_index, const string& reason,
      const string& rewrite_name) {
    FunctionalSemanticStep step = pipeline.semantic_steps[step_index];
    for (size_t node_index : step.source_node_indices) {
      FunctionalNode& node = pipeline.nodes[node_index];
      node.dead_stage_eliminated = true;
      node.semantic_work_eliminated = true;
      node.semantic_elimination_reason = reason;
    }
    record_semantic_rewrite(pipeline, rewrite_name, reason, step.provenance);
    pipeline.semantic_steps.erase(
        pipeline.semantic_steps.begin() +
        static_cast<std::ptrdiff_t>(step_index));
  }

  static bool eliminate_trailing_maps_before_count(
      FunctionalPipeline& pipeline) {
    if (pipeline.semantic_steps.empty() ||
        pipeline.semantic_steps.back().kind != FunctionalNodeKind::Count)
      return false;
    bool eliminated = false;
    while (pipeline.semantic_steps.size() >= 2) {
      size_t map_index = pipeline.semantic_steps.size() - 2;
      const auto& step = pipeline.semantic_steps[map_index];
      if (step.kind != FunctionalNodeKind::Map) break;
      if (!semantic_step_safe_to_skip(pipeline, step)) break;
      eliminate_semantic_step(
          pipeline, map_index,
          "mapped values are unused by terminal count and the callback is "
          "pure/non-failing/non-divergent",
          "dead-map");
      eliminated = true;
    }
    if (eliminated)
      pipeline.optimization_notes.push_back(
          "map stage eliminated; reason: output unused and callback is "
          "pure/non-failing/non-divergent");
    return eliminated;
  }

  static bool eliminate_trailing_maps_for_downstream_count(
      FunctionalPipeline& pipeline) {
    bool eliminated = false;
    while (!pipeline.semantic_steps.empty()) {
      size_t map_index = pipeline.semantic_steps.size() - 1;
      const auto& step = pipeline.semantic_steps[map_index];
      if (step.kind != FunctionalNodeKind::Map ||
          !semantic_step_safe_to_skip(pipeline, step))
        break;
      eliminate_semantic_step(
          pipeline, map_index,
          "mapped values are unused by downstream terminal count and the "
          "callback is pure/non-failing/non-divergent",
          "dead-map");
      eliminated = true;
    }
    if (eliminated)
      pipeline.optimization_notes.push_back(
          "map stage eliminated; reason: downstream count uses only "
          "cardinality");
    return eliminated;
  }

  void plan_terminal_lowering(FunctionalPipeline& pipeline, bool enabled) {
    bool count_terminal = !pipeline.semantic_steps.empty() &&
        pipeline.semantic_steps.back().kind == FunctionalNodeKind::Count;
    bool mapped_count = count_terminal && has_map_stage(pipeline);
    if (enabled && count_terminal) {
      if (pipeline.fusion_eligible) {
        bool eliminated = eliminate_trailing_maps_before_count(pipeline);
        bool blocked_trailing_map = pipeline.semantic_steps.size() >= 2 &&
            pipeline.semantic_steps[pipeline.semantic_steps.size() - 2].kind ==
                FunctionalNodeKind::Map;
        if (blocked_trailing_map) {
          const auto& blocked =
              pipeline.semantic_steps[pipeline.semantic_steps.size() - 2];
          disable_count_elimination(
              pipeline, semantic_step_skip_barrier(pipeline, blocked),
              mapped_count);
        } else if (pipeline.semantic_steps.size() == 1) {
          pipeline.count_uses_exact_length = true;
          vector<string> provenance;
          for (const auto& node : pipeline.nodes)
            provenance.insert(provenance.end(), node.provenance.begin(),
                              node.provenance.end());
          record_semantic_rewrite(
              pipeline, "terminal-count",
              "replaced traversal with exact source cardinality", provenance);
          pipeline.optimization_notes.push_back(
              "count -> len; reason: exact source cardinality known");
          if (auto size = inert_literal_vector_size(
                  pipeline.source_expression)) {
            pipeline.count_uses_known_size = true;
            pipeline.known_source_size = *size;
            record_semantic_rewrite(
                pipeline, "known-size-count",
                "inert vector literal has " + std::to_string(*size) +
                    " elements",
                pipeline.nodes.front().provenance);
            pipeline.optimization_notes.push_back(
                "count -> constant " + std::to_string(*size) +
                "; reason: inert literal cardinality known");
          }
        } else if (eliminated) {
          pipeline.optimization_notes.push_back(
              "terminal count retains only cardinality-changing stages");
        }
      } else if (mapped_count) {
        disable_count_elimination(
            pipeline, count_elimination_barrier_reason(pipeline), true);
      }
    }

    if (pipeline.semantic_steps.empty()) return;
    FunctionalNodeKind terminal = pipeline.semantic_steps.back().kind;
    if (terminal != FunctionalNodeKind::Any &&
        terminal != FunctionalNodeKind::All)
      return;
    string name = terminal == FunctionalNodeKind::Any ? "any" : "all";
    if (enabled && pipeline.fusion_eligible &&
        callbacks_safe_to_skip(pipeline)) {
      pipeline.short_circuit_terminal = true;
      record_semantic_rewrite(
          pipeline, "short-circuit-" + name,
          "later callback invocations are safe to skip",
          pipeline.semantic_steps.back().provenance);
      pipeline.optimization_notes.push_back(
          "short-circuit " + name + " enabled");
    } else {
      disable_short_circuit(
          pipeline, enabled
              ? skipped_callback_barrier_reason(pipeline)
              : "eager -O0 reference traversal");
    }
  }

  static void plan_terminal_aware_semantics(FunctionalPipeline& pipeline) {
    if (!pipeline.fused || pipeline.semantic_steps.empty()) return;
    FunctionalNodeKind terminal = pipeline.semantic_steps.back().kind;
    if (terminal == FunctionalNodeKind::Count &&
        !pipeline.count_uses_exact_length) {
      bool has_filter = std::any_of(
          pipeline.semantic_steps.begin(), pipeline.semantic_steps.end(),
          [](const FunctionalSemanticStep& step) {
            return step.kind == FunctionalNodeKind::Filter;
          });
      if (has_filter)
        record_semantic_rewrite(
            pipeline, "terminal-filter-count",
            "count matching elements without materializing filter output",
            pipeline.semantic_steps.back().provenance);
    } else if (terminal == FunctionalNodeKind::Sum ||
               terminal == FunctionalNodeKind::Reduce) {
      record_semantic_rewrite(
          pipeline,
          terminal == FunctionalNodeKind::Sum ? "terminal-sum"
                                               : "terminal-reduce",
          "terminal accumulator consumes upstream values without an "
          "intermediate collection",
          pipeline.semantic_steps.back().provenance);
    }
  }

  static bool statement_defines(const Stmt& statement, const string& name) {
    return (statement.kind == Stmt::Kind::Assign ||
            statement.kind == Stmt::Kind::Let ||
            statement.kind == Stmt::Kind::Var) &&
        trim(statement.a) == name;
  }

  static bool occurrence_is_terminal_assignment(
      const ScopeOccurrence& occurrence, const FunctionalPipeline& pipeline) {
    return occurrence.statement && occurrence.new_binding &&
        !occurrence.result_binding.empty() && terminal_pipeline(pipeline) &&
        (occurrence.statement->kind == Stmt::Kind::Let ||
         occurrence.statement->kind == Stmt::Kind::Assign);
  }

  vector<ScopeOccurrence> collect_scope_occurrences(
      vector<Stmt>& body, const string& context,
      std::set<string> known_bindings, size_t result_id,
      const std::optional<string>& result_expression) {
    vector<ScopeOccurrence> occurrences;
    for (size_t index = 0; index < body.size(); ++index) {
      Stmt& statement = body[index];
      if (statement.indent != 0) continue;
      size_t id = statement_pipeline_id(statement, context);
      bool binding_statement = statement.kind == Stmt::Kind::Assign ||
          statement.kind == Stmt::Kind::Let ||
          statement.kind == Stmt::Kind::Var;
      bool simple_binding = binding_statement && plain_identifier(statement.a);
      bool is_new = simple_binding && !known_bindings.count(statement.a);
      if (id) {
        ScopeOccurrence occurrence;
        occurrence.statement_index = index;
        occurrence.pipeline_id = id;
        occurrence.statement = &statement;
        occurrence.new_binding = is_new;
        occurrence.explicitly_immutable =
            statement.kind == Stmt::Kind::Let;
        occurrence.expression = statement.b;
        if (simple_binding) occurrence.result_binding = statement.a;
        occurrences.push_back(std::move(occurrence));
      }
      if (simple_binding)
        known_bindings.insert(statement.a);
      if (statement.kind == Stmt::Kind::If)
        for (const auto& joined : statement.joined_types)
          known_bindings.insert(joined.first);
    }
    if (result_id && result_expression) {
      ScopeOccurrence result;
      result.statement_index = body.size();
      result.pipeline_id = result_id;
      result.result_expression = true;
      result.expression = *result_expression;
      occurrences.push_back(std::move(result));
    }
    return occurrences;
  }

  size_t later_binding_uses(const vector<Stmt>& body, size_t producer_index,
                            const std::optional<string>& result_expression,
                            const string& binding,
                            bool& reassigned) const {
    size_t uses = 0;
    reassigned = false;
    for (size_t index = producer_index + 1; index < body.size(); ++index) {
      const Stmt& statement = body[index];
      size_t count = word_occurrences(statement.text, binding);
      if (statement_defines(statement, binding)) {
        reassigned = true;
        if (count) --count;
      }
      uses += count;
    }
    if (result_expression)
      uses += word_occurrences(*result_expression, binding);
    return uses;
  }

  void plan_cross_binding_fusion(
      vector<Stmt>& body, vector<ScopeOccurrence>& occurrences,
      const std::optional<string>& result_expression) {
    for (size_t index = 0; index < occurrences.size(); ++index) {
      ScopeOccurrence& producer_occurrence = occurrences[index];
      if (!producer_occurrence.statement ||
          producer_occurrence.result_binding.empty())
        continue;
      FunctionalPipeline* producer =
          pipeline_by_id(producer_occurrence.pipeline_id);
      if (!producer || !transformation_pipeline(*producer) ||
          producer->virtual_upstream_pipeline_id)
        continue;

      const string& binding = producer_occurrence.result_binding;
      bool reassigned = false;
      size_t uses = later_binding_uses(
          body, producer_occurrence.statement_index, result_expression,
          binding, reassigned);
      bool immutable = producer_occurrence.explicitly_immutable ||
          (producer_occurrence.new_binding && !reassigned &&
           producer_occurrence.statement->kind == Stmt::Kind::Assign);
      producer->binding_name = binding;
      producer->binding_immutable = immutable;
      producer->binding_use_count = uses;

      if (!immutable)
        producer->binding_materialization_reason =
            "binding is mutable or reassigned";
      else if (uses > 1)
        producer->binding_materialization_reason = "multiple consumers";
      else if (uses == 0)
        producer->binding_materialization_reason =
            "escapes optimization scope";
      else
        producer->binding_materialization_reason =
            "use is outside the adjacent functional region";

      if (!immutable || uses != 1 || index + 1 >= occurrences.size())
        continue;
      ScopeOccurrence& consumer_occurrence = occurrences[index + 1];
      bool adjacent = consumer_occurrence.result_expression
          ? producer_occurrence.statement_index + 1 == body.size()
          : consumer_occurrence.statement_index ==
                producer_occurrence.statement_index + 1;
      if (!adjacent) {
        bool observable = false;
        size_t end = consumer_occurrence.result_expression
            ? body.size() : consumer_occurrence.statement_index;
        for (size_t statement_index = producer_occurrence.statement_index + 1;
             statement_index < end; ++statement_index) {
          Stmt::Kind kind = body[statement_index].kind;
          observable = observable || kind == Stmt::Kind::Echo ||
              kind == Stmt::Kind::Message ||
              kind == Stmt::Kind::Call || kind == Stmt::Kind::If ||
              kind == Stmt::Kind::While;
        }
        producer->binding_materialization_reason = observable
            ? "intervening observable effect"
            : "intervening statement prevents one lexical region";
        continue;
      }
      if (consumer_occurrence.statement &&
          (consumer_occurrence.statement->kind == Stmt::Kind::Assign ||
           consumer_occurrence.statement->kind == Stmt::Kind::Let ||
           consumer_occurrence.statement->kind == Stmt::Kind::Var) &&
          !consumer_occurrence.new_binding) {
        producer->binding_materialization_reason =
            "consumer assigns an existing binding";
        continue;
      }
      FunctionalPipeline* consumer =
          pipeline_by_id(consumer_occurrence.pipeline_id);
      if (!consumer || trim(consumer->source_expression) != binding) {
        producer->binding_materialization_reason =
            "use is outside a compatible functional consumer";
        continue;
      }
      if (!producer->fusion_eligible || !consumer->fusion_eligible) {
        producer->binding_materialization_reason =
            "functional barrier: " +
            barrier_reason(!producer->fusion_eligible ? *producer : *consumer);
        continue;
      }
      if (
          producer->nodes.empty() || consumer->nodes.empty() ||
          !producer->nodes.front().effects.fusion_safe() ||
          !consumer->nodes.front().effects.fusion_safe() ||
          consumer->virtual_upstream_pipeline_id) {
        producer->binding_materialization_reason =
            "source effect or existing dataflow link requires materialization";
        continue;
      }

      producer->virtualized_into_pipeline_id = consumer->transient_id;
      consumer->virtual_upstream_pipeline_id = producer->transient_id;
      producer->fused = true;
      producer->binding_materialization_reason.clear();
      consumer->fused = true;

      vector<string> collapsed_provenance = producer->lowered_provenance;
      if (collapsed_provenance.empty()) {
        for (const auto& node : producer->nodes)
          collapsed_provenance.insert(collapsed_provenance.end(),
                                      node.provenance.begin(),
                                      node.provenance.end());
      }
      record_semantic_rewrite(
          *producer, "collapse-single-use-temporary",
          "binding '" + binding +
              "' remains semantic but needs no physical collection",
          collapsed_provenance);

      // Terminal plans were formed before lexical graphs were linked. Recheck
      // any transform that can now omit upstream callback executions against
      // the combined graph rather than just the terminal statement.
      if (consumer->count_uses_exact_length) {
        if (producer->fusion_eligible)
          eliminate_trailing_maps_for_downstream_count(*producer);
        bool upstream_preserves_exact_count =
            producer->semantic_steps.empty();
        if (upstream_preserves_exact_count) {
          if (has_map_stage(*producer) &&
              std::none_of(
                  consumer->optimization_notes.begin(),
                  consumer->optimization_notes.end(),
                  [](const string& note) {
                    return starts_with(note, "map stage eliminated");
                  }))
            consumer->optimization_notes.push_back(
                "map stage eliminated; reason: output unused and callback is "
                "pure/non-failing/non-divergent");
          if (auto size = inert_literal_vector_size(
                  producer->source_expression)) {
            consumer->count_uses_known_size = true;
            consumer->known_source_size = *size;
            record_semantic_rewrite(
                *consumer, "known-size-count",
                "virtual upstream has inert literal cardinality " +
                    std::to_string(*size),
                producer->nodes.front().provenance);
          }
        } else {
          string reason = callback_may_diverge(*producer)
              ? "callback may diverge"
              : "upstream stage changes cardinality";
          disable_count_elimination(
              *consumer, reason,
              std::any_of(
                  producer->semantic_steps.begin(),
                  producer->semantic_steps.end(),
                  [](const FunctionalSemanticStep& step) {
                    return step.kind == FunctionalNodeKind::Map;
                  }) || has_map_stage(*consumer));
        }
      }
      if (consumer->short_circuit_terminal &&
          !callbacks_safe_to_skip(*producer))
        disable_short_circuit(
            *consumer, skipped_callback_barrier_reason(*producer));

      producer->optimization_notes.push_back(
          "intermediate " + binding +
          ": virtualized across immutable binding");
      consumer->optimization_notes.push_back(
          "cross-binding fusion source: " + binding);
      record_semantic_rewrite(
          *consumer, "consume-virtual-temporary",
          "continued pipeline through single-use binding '" + binding + "'",
          collapsed_provenance);
      consumer->decision = "fused across immutable binding '" + binding +
          "'; intermediates eliminated";
    }
  }

  static bool captures_any_result(
      const FunctionalPipeline& pipeline,
      const std::set<string>& result_bindings) {
    for (const auto& node : pipeline.nodes)
      for (const auto& capture : node.captures)
        if (result_bindings.count(capture)) return true;
    return false;
  }

  void plan_shared_traversals(vector<ScopeOccurrence>& occurrences,
                              const string& context) {
    for (size_t first = 0; first < occurrences.size();) {
      ScopeOccurrence& initial = occurrences[first];
      FunctionalPipeline* initial_pipeline = pipeline_by_id(initial.pipeline_id);
      if (!initial_pipeline || initial.result_expression ||
          !occurrence_is_terminal_assignment(initial, *initial_pipeline) ||
          initial_pipeline->virtual_upstream_pipeline_id ||
          !initial_pipeline->fusion_eligible ||
          initial_pipeline->nodes.empty() ||
          !initial_pipeline->nodes.front().effects.fusion_safe() ||
          !plain_identifier(trim(initial_pipeline->source_expression))) {
        ++first;
        continue;
      }

      vector<size_t> members{first};
      for (size_t next = first + 1; next < occurrences.size(); ++next) {
        ScopeOccurrence& occurrence = occurrences[next];
        FunctionalPipeline* pipeline = pipeline_by_id(occurrence.pipeline_id);
        const ScopeOccurrence& previous = occurrences[members.back()];
        if (!pipeline || occurrence.result_expression ||
            occurrence.statement_index != previous.statement_index + 1 ||
            !occurrence_is_terminal_assignment(occurrence, *pipeline) ||
            pipeline->virtual_upstream_pipeline_id ||
            !pipeline->fusion_eligible ||
            pipeline->nodes.empty() ||
            !pipeline->nodes.front().effects.fusion_safe() ||
            trim(pipeline->source_expression) !=
                trim(initial_pipeline->source_expression))
          break;
        members.push_back(next);
      }
      if (members.size() < 2) {
        ++first;
        continue;
      }

      std::set<string> result_bindings;
      bool useful_traversal = false;
      for (size_t member : members) {
        ScopeOccurrence& occurrence = occurrences[member];
        FunctionalPipeline* pipeline = pipeline_by_id(occurrence.pipeline_id);
        result_bindings.insert(occurrence.result_binding);
        useful_traversal = useful_traversal ||
            pipeline_requires_traversal(*pipeline);
      }
      bool dependent = false;
      for (size_t member : members) {
        FunctionalPipeline* pipeline =
            pipeline_by_id(occurrences[member].pipeline_id);
        dependent = dependent || captures_any_result(*pipeline, result_bindings);
        for (const auto& result_binding : result_bindings)
          dependent = dependent ||
              word_occurrences(pipeline->expression, result_binding) != 0;
      }
      if (!useful_traversal || dependent) {
        first = members.back() + 1;
        continue;
      }

      FunctionalTraversalGroup group;
      group.transient_id = program_.functional_traversal_groups.size() + 1;
      group.context = context;
      group.line = initial.statement->line;
      group.source_file = initial.statement->source_file;
      group.source_expression = trim(initial_pipeline->source_expression);
      group.source_type = initial_pipeline->source_type;
      group.semantic_identity = context + "@" + std::to_string(group.line) +
          ":shared-source";
      for (size_t member : members) {
        ScopeOccurrence& occurrence = occurrences[member];
        FunctionalPipeline* pipeline = pipeline_by_id(occurrence.pipeline_id);
        pipeline->traversal_group_id = group.transient_id;
        pipeline->optimization_notes.push_back(
            "source traversal shared; group %" +
            std::to_string(group.transient_id));
        FunctionalTraversalConsumer consumer;
        consumer.pipeline_id = pipeline->transient_id;
        consumer.result_binding = occurrence.result_binding;
        consumer.line = occurrence.statement->line;
        consumer.mutable_binding =
            occurrence.statement->kind == Stmt::Kind::Assign;
        group.consumers.push_back(std::move(consumer));
        group.provenance.insert(group.provenance.end(),
                                pipeline->lowered_provenance.begin(),
                                pipeline->lowered_provenance.end());
      }
      std::ostringstream decision;
      decision << "source traversal shared; consumers:";
      for (size_t member : members) {
        FunctionalPipeline* pipeline = pipeline_by_id(
            occurrences[member].pipeline_id);
        decision << " " << terminal_name(*pipeline);
      }
      group.decision = decision.str();
      program_.functional_traversal_groups.push_back(std::move(group));
      first = members.back() + 1;
    }
  }

  void plan_scope(vector<Stmt>& body, const string& context,
                  const std::set<string>& known_bindings, size_t result_id,
                  const std::optional<string>& result_expression) {
    auto occurrences = collect_scope_occurrences(
        body, context, known_bindings, result_id, result_expression);
    plan_cross_binding_fusion(body, occurrences, result_expression);
    plan_shared_traversals(occurrences, context);
  }

  void rebuild_provenance() {
    for (auto& pipeline : program_.functional_pipelines) {
      pipeline.lowered_provenance.clear();
      if (pipeline.virtual_upstream_pipeline_id) {
        FunctionalPipeline* upstream =
            pipeline_by_id(pipeline.virtual_upstream_pipeline_id);
        if (upstream)
          for (const auto& node : upstream->nodes)
            pipeline.lowered_provenance.insert(
                pipeline.lowered_provenance.end(), node.provenance.begin(),
                node.provenance.end());
      }
      for (const auto& node : pipeline.nodes)
        pipeline.lowered_provenance.insert(
            pipeline.lowered_provenance.end(), node.provenance.begin(),
            node.provenance.end());
    }
    for (auto& group : program_.functional_traversal_groups) {
      group.provenance.clear();
      for (const auto& consumer : group.consumers) {
        FunctionalPipeline* pipeline = pipeline_by_id(consumer.pipeline_id);
        if (pipeline)
          group.provenance.insert(group.provenance.end(),
                                  pipeline->lowered_provenance.begin(),
                                  pipeline->lowered_provenance.end());
      }
    }
  }

  void plan_materializations(bool enabled) {
    for (auto& pipeline : program_.functional_pipelines) {
      vector<size_t> logical_nodes;
      for (size_t index = 0; index < pipeline.nodes.size(); ++index)
        if (pipeline.nodes[index].logical_materialization)
          logical_nodes.push_back(index);
      for (size_t position = 0; position < logical_nodes.size(); ++position) {
        FunctionalNode& node = pipeline.nodes[logical_nodes[position]];
        bool last = position + 1 == logical_nodes.size();
        if (node.semantic_work_eliminated) {
          node.materialization = FunctionalMaterializationKind::Virtual;
          node.materialization_reason = node.semantic_elimination_reason;
        } else if (pipeline.virtualized_into_pipeline_id) {
          node.materialization = FunctionalMaterializationKind::Virtual;
          node.materialization_reason =
              "single immutable consumer in the same lexical scope";
        } else if (!enabled || !pipeline.fused) {
          node.materialization = FunctionalMaterializationKind::Materialize;
          node.barrier_required = true;
          node.materialization_reason = !enabled
              ? "eager -O0 reference semantics"
              : "observable or failure-order barrier requires eager completion";
        } else if (terminal_pipeline(pipeline) || !last) {
          node.materialization = FunctionalMaterializationKind::Virtual;
          node.materialization_reason =
              "single in-graph consumer shares the traversal";
        } else {
          node.materialization = FunctionalMaterializationKind::Materialize;
          node.escapes = true;
          node.materialization_reason = "escapes optimization scope";
        }

        if (last && !pipeline.binding_name.empty() &&
            !pipeline.virtualized_into_pipeline_id) {
          node.materialization = FunctionalMaterializationKind::Materialize;
          node.materialization_eliminated = false;
          node.escapes = true;
          if (pipeline.binding_use_count > 1) {
            node.multiple_consumers = true;
            node.materialization_reason = "multiple consumers";
          } else if (!pipeline.binding_materialization_reason.empty())
            node.materialization_reason =
                pipeline.binding_materialization_reason;
          if (node.materialization_reason.find("effect") != string::npos ||
              node.materialization_reason.find("barrier") != string::npos)
            node.barrier_required = true;
        }
        node.materialization_eliminated =
            node.materialization == FunctionalMaterializationKind::Virtual;
      }

      if (!pipeline.binding_name.empty() &&
          !pipeline.virtualized_into_pipeline_id && !logical_nodes.empty()) {
        const FunctionalNode& final = pipeline.nodes[logical_nodes.back()];
        pipeline.optimization_notes.push_back(
            "intermediate " + pipeline.binding_name + ": " +
            functional_materialization_name(final.materialization) +
            "; reason: " + final.materialization_reason);
      }
    }
  }
};

static string observable_effect_label(const ObservableEffects& effects) {
  vector<string> labels;
  if (effects.fusion_safe()) labels.push_back("PURE");
  if (effects.local_capture_read) labels.push_back("CAPTURE_READ");
  if (effects.local_mutation) labels.push_back("LOCAL_WRITE");
  if (effects.domain_read) labels.push_back("DOMAIN_READ");
  if (effects.domain_write) labels.push_back("DOMAIN_WRITE");
  if (effects.message) labels.push_back("MESSAGE");
  if (effects.external_io) labels.push_back("IO");
  if (effects.may_panic) labels.push_back("MAY_PANIC");
  for (const auto& raised : effects.raise_set)
    labels.push_back("RAISE(" + raised.canonical() + ")");
  if (effects.may_diverge) labels.push_back("MAY_DIVERGE");
  if (effects.unresolved) labels.push_back("UNRESOLVED");
  std::ostringstream out;
  for (size_t index = 0; index < labels.size(); ++index) {
    if (index) out << "+";
    out << labels[index];
  }
  return out.str();
}

void dump_functional_ir(std::ostream& out, const Program& program,
                        bool decisions_only) {
  for (const auto& pipeline : program.functional_pipelines) {
    out << "Pipeline %" << pipeline.transient_id << " [" << pipeline.context
        << "] semantic=" << pipeline.semantic_identity
        << " line " << pipeline.line << "\n";
    if (!decisions_only) {
      for (const auto& node : pipeline.nodes) {
        out << "  %" << node.transient_id << " " << functional_node_name(node.kind)
            << " " << node.input_type;
        if (!node.output_type.empty() && node.output_type != node.input_type)
          out << " -> " << node.output_type;
        if (!node.callable_identity.empty())
          out << " callable=" << node.callable_identity;
        out << " " << observable_effect_label(node.effects)
            << " span=" << node.span.line << ":" << node.span.stage
            << " origin=" << node.semantic_identity;
        if (!node.captures.empty()) {
          out << " captures=";
          for (size_t index = 0; index < node.captures.size(); ++index) {
            if (index) out << ",";
            out << node.captures[index];
          }
        }
        if (node.materialization_eliminated)
          out << " materialization=eliminated plan=virtual";
        else if (node.logical_materialization) {
          out << " materialization=logical plan="
              << functional_materialization_name(node.materialization);
        }
        if (node.dead_stage_eliminated) out << " dead-stage=eliminated";
        if (node.semantic_work_eliminated) {
          out << " semantic-work=eliminated";
          if (!node.semantic_elimination_reason.empty())
            out << " semantic-reason=" << node.semantic_elimination_reason;
        }
        if (node.escapes) out << " escapes=yes";
        if (node.multiple_consumers) out << " multiple-consumers=yes";
        if (node.barrier_required) out << " barrier-required=yes";
        if (!node.materialization_reason.empty())
          out << " reason=" << node.materialization_reason;
        out << "\n";
      }
      out << "  facts: element-independent="
          << (pipeline.element_independent ? "yes" : "no")
          << " deterministic=" << (pipeline.deterministic ? "yes" : "no")
          << " reduction-compatible="
          << (pipeline.reduction_compatible ? "yes" : "no") << "\n";
      out << "  semantic-plan:";
      if (pipeline.semantic_steps.empty()) out << " <no element work>";
      out << "\n";
      for (const auto& step : pipeline.semantic_steps) {
        string name = functional_node_name(step.kind);
        if (step.source_node_indices.size() > 1 &&
            step.kind == FunctionalNodeKind::Map)
          name = "ComposedMap";
        else if (step.source_node_indices.size() > 1 &&
                 step.kind == FunctionalNodeKind::Filter)
          name = "ComposedFilter";
        out << "    " << name << " origins=";
        for (size_t index = 0; index < step.provenance.size(); ++index) {
          if (index) out << ",";
          out << step.provenance[index];
        }
        out << "\n";
      }
    }
    for (const auto& rewrite : pipeline.semantic_rewrites) {
      out << "  semantic-opt: " << rewrite.name;
      if (!rewrite.detail.empty()) out << "; " << rewrite.detail;
      if (!decisions_only && !rewrite.provenance.empty()) {
        out << "; provenance=";
        for (size_t index = 0; index < rewrite.provenance.size(); ++index) {
          if (index) out << ",";
          out << rewrite.provenance[index];
        }
      }
      out << "\n";
    }
    for (const auto& note : pipeline.optimization_notes)
      out << "  note: " << note << "\n";
    if (pipeline.chunk_plan.is_chunk_pipeline) {
      const auto& plan = pipeline.chunk_plan;
      out << "  chunk-plan: fileio=" << plan.fileio_receiver
          << " size=" << plan.chunk_size_expression
          << " map=" << plan.map_callable
          << " initial=" << plan.initial_expression
          << " combine=" << plan.combine_callable
          << " accumulator=" << plan.accumulator_type
          << " mapped=" << plan.mapped_type
          << " window_k=" << plan.window_k
          << " eligible=" << (plan.eligible ? "yes" : "no")
          << " reason=" << plan.eligibility_reason << "\n";
    }
    out << "  decision: " << pipeline.decision << "\n\n";
  }
  for (const auto& group : program.functional_traversal_groups) {
    out << "DataflowGroup %" << group.transient_id << " [" << group.context
        << "] semantic=" << group.semantic_identity << " line " << group.line
        << "\n  Source " << group.source_type << " expression="
        << group.source_expression << "\n";
    for (const auto& consumer : group.consumers) {
      auto pipeline = std::find_if(
          program.functional_pipelines.begin(),
          program.functional_pipelines.end(),
          [&](const FunctionalPipeline& candidate) {
            return candidate.transient_id == consumer.pipeline_id;
          });
      out << "  -> Pipeline %" << consumer.pipeline_id << " binding="
          << consumer.result_binding;
      if (pipeline != program.functional_pipelines.end())
        out << " terminal=" << functional_node_name(pipeline->nodes.back().kind);
      out << " line " << consumer.line << "\n";
    }
    if (!decisions_only) {
      out << "  provenance:";
      for (const auto& origin : group.provenance) out << " " << origin;
      out << "\n";
    }
    out << "  decision: " << group.decision << "\n\n";
  }
}


void optimize_program(Program& program, bool enabled) {
  FunctionalOptimizer(program).run(enabled);
}

}  // namespace moss
