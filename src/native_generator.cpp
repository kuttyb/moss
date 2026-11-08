#include <algorithm>
#include <cassert>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <iterator>
#include <limits>
#include <map>
#include <numeric>
#include <optional>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <tuple>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>
#include "ast.hpp"
#include "checked_body_ir.hpp"
#include "debug_map.hpp"
#include "diagnostics.hpp"
#include "functional_optimizer.hpp"
#include "functional_optimizer_support.hpp"
#include "semantic_checker_support.hpp"
#include "native_generator.hpp"
#include "native_clone_provenance.hpp"
#include "native_generator_support.hpp"
#include "type_placeholders.hpp"
#include "native_cache_identity.hpp"
#include "parser_support.hpp"
#include "synchronization.hpp"
#include "synchronization_lowering.hpp"
#include "source_free_abi_v9.hpp"
#include "source_free_abi_v10.hpp"
#include "abi_v10_records.hpp"
#include "typed_semantic_api.hpp"
#include "handler_runtime.hpp"
#include "fileio_runtime.hpp"
#include "float_format_runtime.hpp"
#include "display_runtime.hpp"
#include "executor_runtime.hpp"
#include "char_string_ordering_runtime.hpp"
#include "numeric_runtime.hpp"
#include "compiler_stage_timer.hpp"

using std::string;
using std::vector;

namespace moss {

namespace {

struct EstSourceEdit {
  std::size_t begin = 0, end = 0;
  string replacement;
};

template <typename Visitor>
void visit_est_expression(const est::ExpressionView& view, est::ExprId id,
                          const Visitor& visitor) {
  if (!view.arena || id >= view.arena->size()) return;
  visitor(id, view.arena->at(id));
  std::visit([&](const auto& node) {
    using T = std::decay_t<decltype(node)>;
    auto visit = [&](est::ExprId child) {
      visit_est_expression(view, child, visitor);
    };
    auto visit_all = [&](const vector<est::ExprId>& children) {
      for (const auto child : children) visit(child);
    };
    if constexpr (std::is_same_v<T, est::Unary>) visit(node.operand);
    else if constexpr (std::is_same_v<T, est::Binary>) {
      visit(node.left); visit(node.right);
    } else if constexpr (std::is_same_v<T, est::Call>) {
      visit(node.callee); visit_all(node.arguments);
    } else if constexpr (std::is_same_v<T, est::Take>) {
      visit_all(node.arguments);
    } else if constexpr (std::is_same_v<T, est::Member>) visit(node.base);
    else if constexpr (std::is_same_v<T, est::Index>) {
      visit(node.base); visit(node.index);
    } else if constexpr (std::is_same_v<T, est::Parenthesized>) visit(node.inner);
    else if constexpr (std::is_same_v<T, est::Message>) {
      visit(node.target); visit_all(node.arguments);
    } else if constexpr (std::is_same_v<T, est::NamedArgument>) visit(node.value);
    else if constexpr (std::is_same_v<T, est::Collection> ||
                       std::is_same_v<T, est::Tuple>) visit_all(node.elements);
    else if constexpr (std::is_same_v<T, est::Constructor>) {
      visit(node.type); visit_all(node.type_arguments); visit_all(node.arguments);
    } else if constexpr (std::is_same_v<T, est::Pipeline>) {
      visit(node.input); visit_all(node.stages);
    }
  }, view.arena->at(id));
}

string apply_est_source_edits(const string& source,
                              vector<EstSourceEdit> edits) {
  std::sort(edits.begin(), edits.end(), [](const auto& left, const auto& right) {
    if (left.begin != right.begin) return left.begin < right.begin;
    return left.end > right.end;
  });
  vector<EstSourceEdit> selected;
  std::size_t covered = 0;
  for (auto& edit : edits) {
    if (edit.begin > edit.end || edit.end > source.size() ||
        edit.begin < covered) continue;
    selected.push_back(std::move(edit));
    covered = selected.back().end;
  }
  string result = source;
  for (auto edit = selected.rbegin(); edit != selected.rend(); ++edit)
    result.replace(edit->begin, edit->end - edit->begin, edit->replacement);
  return result;
}

}  // namespace



// Generator-private post-rewrite lowering metadata. The checker-owned parsed
// functional stages remain in functional_source_pipeline.hpp.
struct LoweredFunctionalStage {
  FunctionalNodeKind kind = FunctionalNodeKind::Source;
  std::vector<ParsedFunctionalStage> operations;
};
struct LoweredFunctionalPipeline {
  std::string source;
  std::vector<LoweredFunctionalStage> stages;
};

struct FunctionalExpressionHandlesScope {
  const vector<est::Handle>*& slot;
  const vector<est::Handle>* prior;
  FunctionalExpressionHandlesScope(
      const vector<est::Handle>*& target,
      const vector<est::Handle>* current)
      : slot(target), prior(target) { slot = current; }
  ~FunctionalExpressionHandlesScope() { slot = prior; }
};

class Generator {
 public:
  Generator(const Program& p, const OptimizationPlan&,
            bool debug_build = false,
            ProgramGenerationMode mode = ProgramGenerationMode::Application,
            vector<string> rust_dependencies = {},
            const Program* resolution_program = nullptr,
            bool project_module_artifact = false,
            bool emit_static_specializations = true,
            bool public_specializations = false)
      : p_(p),
        type_scope_(p.type_context.get()),
        debug_build_(debug_build), mode_(mode),
        rust_dependencies_(std::move(rust_dependencies)),
        emit_static_specializations_(emit_static_specializations),
        public_specializations_(public_specializations),
        project_module_artifact_(project_module_artifact) {
    for (const auto& d : p.domains) domains_[d.name] = &d;
    const Program& semantic_program = resolution_program ? *resolution_program : p;
    semantic_program_ = &semantic_program;
    std::set<string> used_domain_names;
    for (const auto& domain : semantic_program.domains) used_domain_names.insert(domain.name);
    for (const auto& specialization : semantic_program.domain_specializations) {
      if (!specialization.materialized_layout) continue;
      auto source = std::find_if(
          semantic_program.domains.begin(), semantic_program.domains.end(),
          [&](const Domain& domain) {
            return domain.name == specialization.source_domain;
          });
      if (source == semantic_program.domains.end()) continue;
      Domain specialized = *source;
      string backend_name = specialization.source_domain + "__" +
          specialization.instance;
      if (used_domain_names.count(backend_name)) {
        backend_name += "__specialized_" +
            stable_hash(specialization.source_domain + "\n" +
                        specialization.instance).substr(0, 12);
        size_t suffix = 0;
        string candidate = backend_name;
        while (used_domain_names.count(candidate))
          candidate = backend_name + "_" + std::to_string(++suffix);
        backend_name = std::move(candidate);
      }
      specialized.name = backend_name;
      used_domain_names.insert(specialized.name);
      for (auto& field : specialized.state) {
        auto type = specialization.state_types.find(field.name);
        if (type != specialization.state_types.end()) field.type = type->second;
      }
      for (auto& handler : specialized.handlers) {
        auto parameters = specialization.handler_parameter_types.find(handler.name);
        if (parameters != specialization.handler_parameter_types.end()) {
          for (size_t index = 0;
               index < handler.params.size() && index < parameters->second.size();
               ++index)
            if (!parameters->second[index].empty())
              handler.params[index].set_type(parameters->second[index]);
        }
        auto reply = specialization.handler_reply_types.find(handler.name);
        if (reply != specialization.handler_reply_types.end())
          handler.set_reply_type(reply->second);
      }
      specialized_domains_.push_back(std::move(specialized));
      specialization_sources_[specialized_domains_.back().name] = specialization.source_domain;
      specialization_names_[specialization.source_domain + "\n" +
                            specialization.instance] =
          specialized_domains_.back().name;
    }
    for (const auto& d : specialized_domains_) {
      domains_[d.name] = &d;
    }
    for (const auto& o : p.objects) objects_[o.name] = &o;
    for (const auto& e : p.enums) enums_[e.name] = &e;
    for (const auto& f : p.functions) {
      functions_[f.name] = &f;
      if (!f.semantic_callable_id.empty())
        callable_id_to_function_[f.semantic_callable_id] = &f;
    }
    if (resolution_program) {
      for (const auto& d : resolution_program->domains)
        domains_.emplace(d.name, &d);
      for (const auto& o : resolution_program->objects)
        objects_.emplace(o.name, &o);
      for (const auto& e : resolution_program->enums)
        enums_.emplace(e.name, &e);
      for (const auto& f : resolution_program->functions) {
        functions_.emplace(f.name, &f);
        if (!f.semantic_callable_id.empty())
          callable_id_to_function_.emplace(f.semantic_callable_id, &f);
      }
    }
#ifdef MOSS_TEST_BUILD
    TypedSemanticModuleV1* test_semantic_facts = semantic_program.typed_semantics
        ? semantic_program.typed_semantics.get()
        : p_.typed_semantics.get();
    std::optional<SpecId> target_spec_id;
    if (const char* raw = std::getenv("MOSS_TEST_TARGET_SPEC_ID")) {
      const auto parsed = std::stoul(raw);
      if (parsed >= InvalidSpecId)
        throw std::invalid_argument("MOSS_TEST_TARGET_SPEC_ID is out of range");
      target_spec_id = static_cast<SpecId>(parsed);
    }
    auto find_target_fact = [&](std::vector<TypedLocalOwnershipFactV1>& facts) -> TypedLocalOwnershipFactV1* {
      for (auto& f : facts) {
        if (f.specialization_id != InvalidSpecId) {
          if (!target_spec_id || f.specialization_id == *target_spec_id)
            return &f;
        }
      }
      return facts.empty() ? nullptr : &facts.front();
    };
    auto find_target_req = [&](std::vector<TypedLocalOwnershipRequirementV1>& reqs) -> TypedLocalOwnershipRequirementV1* {
      for (auto& r : reqs) {
        if (r.specialization_id != InvalidSpecId) {
          if (!target_spec_id || r.specialization_id == *target_spec_id)
            return &r;
        }
      }
      return reqs.empty() ? nullptr : &reqs.front();
    };
    auto find_target_copy_fact = [&](std::vector<TypedCopyFactV1>& facts) -> TypedCopyFactV1* {
      for (auto& fact : facts) {
        if (fact.specialization_id != InvalidSpecId &&
            (!target_spec_id || fact.specialization_id == *target_spec_id))
          return &fact;
      }
      return facts.empty() ? nullptr : &facts.front();
    };
    auto find_target_copy_req = [&](std::vector<TypedCopyRequirementV1>& reqs) -> TypedCopyRequirementV1* {
      for (auto& requirement : reqs) {
        if (requirement.specialization_id != InvalidSpecId &&
            (!target_spec_id ||
             requirement.specialization_id == *target_spec_id))
          return &requirement;
      }
      return reqs.empty() ? nullptr : &reqs.front();
    };

    if (std::getenv("MOSS_TEST_CONSUMER_DROP_COPY_FACT")) {
      if (test_semantic_facts && !test_semantic_facts->copy_facts.empty())
        if (auto* fact = find_target_copy_fact(test_semantic_facts->copy_facts))
          fact->availability = TypedCopyFactV1::Availability::MissingCopyOccurrence;
    }
    if (std::getenv("MOSS_TEST_CONSUMER_PERTURB_COPY_FACT")) {
      if (test_semantic_facts && !test_semantic_facts->copy_facts.empty())
        if (auto* fact = find_target_copy_fact(test_semantic_facts->copy_facts))
          fact->required_source_access = Effect::Consume;
    }
    if (std::getenv("MOSS_TEST_CONSUMER_DELETE_COPY_REQUIREMENT")) {
      if (test_semantic_facts &&
          !test_semantic_facts->required_copy_occurrences.empty())
        test_semantic_facts->required_copy_occurrences.erase(
            test_semantic_facts->required_copy_occurrences.begin());
    }
    if (std::getenv("MOSS_TEST_CONSUMER_DUPLICATE_COPY_FACT")) {
      if (test_semantic_facts && !test_semantic_facts->copy_facts.empty())
        if (auto* fact = find_target_copy_fact(test_semantic_facts->copy_facts))
          test_semantic_facts->copy_facts.push_back(*fact);
    }
    if (std::getenv("MOSS_TEST_CONSUMER_DUPLICATE_COPY_REQUIREMENT")) {
      if (test_semantic_facts &&
          !test_semantic_facts->required_copy_occurrences.empty())
        if (auto* requirement = find_target_copy_req(
                test_semantic_facts->required_copy_occurrences))
          test_semantic_facts->required_copy_occurrences.push_back(*requirement);
    }

    if (std::getenv("MOSS_TEST_CONSUMER_DROP_OWNERSHIP_FACT")) {
      if (test_semantic_facts && !test_semantic_facts->local_ownership_facts.empty()) {
        if (auto* fact = find_target_fact(test_semantic_facts->local_ownership_facts)) {
          fact->availability = TypedLocalOwnershipFactV1::Availability::MissingEstOccurrence;
        }
      }
    }
    if (std::getenv("MOSS_TEST_CONSUMER_PERTURB_OWNERSHIP_FACT")) {
      if (test_semantic_facts && !test_semantic_facts->local_ownership_facts.empty()) {
        if (auto* fact = find_target_fact(test_semantic_facts->local_ownership_facts)) {
          fact->access = (fact->access == Effect::Read) ? Effect::Write : Effect::Read;
        }
      }
    }
    if (std::getenv("MOSS_TEST_CONSUMER_DELETE_OWNERSHIP_RECORD")) {
      if (test_semantic_facts && !test_semantic_facts->local_ownership_facts.empty()) {
        if (target_spec_id) {
          for (auto it = test_semantic_facts->local_ownership_facts.begin();
               it != test_semantic_facts->local_ownership_facts.end(); ++it) {
            if (it->specialization_id == *target_spec_id) {
              test_semantic_facts->local_ownership_facts.erase(it);
              break;
            }
          }
        } else {
          test_semantic_facts->local_ownership_facts.erase(
              test_semantic_facts->local_ownership_facts.begin());
        }
      }
    }
    if (std::getenv("MOSS_TEST_CONSUMER_PERTURB_SPECIALIZATION_ID")) {
      if (test_semantic_facts && !test_semantic_facts->local_ownership_facts.empty()) {
        if (auto* fact = find_target_fact(test_semantic_facts->local_ownership_facts)) {
          fact->specialization_id = InvalidSpecId;
        }
      }
    }
    if (std::getenv("MOSS_TEST_CONSUMER_PERTURB_SOURCE_ID")) {
      if (test_semantic_facts && !test_semantic_facts->local_ownership_facts.empty()) {
        if (auto* fact = find_target_fact(test_semantic_facts->local_ownership_facts)) {
          fact->source_binding_identity = "perturbed-source-id";
        }
      }
    }
    if (std::getenv("MOSS_TEST_CONSUMER_DUPLICATE_OWNERSHIP_FACT")) {
      if (test_semantic_facts && !test_semantic_facts->local_ownership_facts.empty()) {
        if (auto* fact = find_target_fact(test_semantic_facts->local_ownership_facts)) {
          test_semantic_facts->local_ownership_facts.push_back(*fact);
        }
      }
    }
    if (std::getenv("MOSS_TEST_CONSUMER_DUPLICATE_OWNERSHIP_REQUIREMENT")) {
      if (test_semantic_facts && !test_semantic_facts->required_local_ownership_occurrences.empty()) {
        if (auto* req = find_target_req(test_semantic_facts->required_local_ownership_occurrences)) {
          test_semantic_facts->required_local_ownership_occurrences.push_back(*req);
        }
      }
    }

    auto find_target_loop_fact = [&](std::vector<TypedLoopOwnershipFactV1>& facts) -> TypedLoopOwnershipFactV1* {
      for (auto& f : facts) {
        if (f.specialization_id != InvalidSpecId) {
          if (!target_spec_id || f.specialization_id == *target_spec_id)
            return &f;
        }
      }
      return facts.empty() ? nullptr : &facts.front();
    };
    auto find_target_loop_req = [&](std::vector<TypedLoopOwnershipRequirementV1>& reqs) -> TypedLoopOwnershipRequirementV1* {
      for (auto& r : reqs) {
        if (r.specialization_id != InvalidSpecId) {
          if (!target_spec_id || r.specialization_id == *target_spec_id)
            return &r;
        }
      }
      return reqs.empty() ? nullptr : &reqs.front();
    };

    if (std::getenv("MOSS_TEST_CONSUMER_DROP_LOOP_FACT")) {
      if (p_.typed_semantics && !p_.typed_semantics->loop_ownership_facts.empty()) {
        if (auto* fact = find_target_loop_fact(p_.typed_semantics->loop_ownership_facts)) {
          fact->availability = TypedLoopOwnershipFactV1::Availability::MissingLoopOccurrence;
        }
      }
    }
    if (std::getenv("MOSS_TEST_CONSUMER_PERTURB_LOOP_FACT")) {
      if (p_.typed_semantics && !p_.typed_semantics->loop_ownership_facts.empty()) {
        if (auto* fact = find_target_loop_fact(p_.typed_semantics->loop_ownership_facts)) {
          fact->converged_access = (fact->converged_access == Effect::Read) ? Effect::Write : Effect::Read;
        }
      }
    }
    if (std::getenv("MOSS_TEST_CONSUMER_DELETE_LOOP_RECORD")) {
      if (p_.typed_semantics && !p_.typed_semantics->loop_ownership_facts.empty()) {
        if (target_spec_id) {
          for (auto it = p_.typed_semantics->loop_ownership_facts.begin();
               it != p_.typed_semantics->loop_ownership_facts.end(); ++it) {
            if (it->specialization_id == *target_spec_id) {
              p_.typed_semantics->loop_ownership_facts.erase(it);
              break;
            }
          }
        } else {
          p_.typed_semantics->loop_ownership_facts.erase(
              p_.typed_semantics->loop_ownership_facts.begin());
        }
      }
    }
    if (std::getenv("MOSS_TEST_CONSUMER_DELETE_LOOP_REQUIREMENT")) {
      if (p_.typed_semantics && !p_.typed_semantics->required_loop_ownership_occurrences.empty()) {
        p_.typed_semantics->required_loop_ownership_occurrences.erase(
            p_.typed_semantics->required_loop_ownership_occurrences.begin());
      }
    }
    if (std::getenv("MOSS_TEST_CONSUMER_DUPLICATE_LOOP_FACT")) {
      if (p_.typed_semantics && !p_.typed_semantics->loop_ownership_facts.empty()) {
        if (auto* fact = find_target_loop_fact(p_.typed_semantics->loop_ownership_facts)) {
          p_.typed_semantics->loop_ownership_facts.push_back(*fact);
        }
      }
    }
    if (std::getenv("MOSS_TEST_CONSUMER_DUPLICATE_LOOP_REQUIREMENT")) {
      if (p_.typed_semantics && !p_.typed_semantics->required_loop_ownership_occurrences.empty()) {
        if (auto* req = find_target_loop_req(p_.typed_semantics->required_loop_ownership_occurrences)) {
          p_.typed_semantics->required_loop_ownership_occurrences.push_back(*req);
        }
      }
    }
    if (std::getenv("MOSS_TEST_CONSUMER_PERTURB_LOOP_SPECIALIZATION_ID")) {
      if (p_.typed_semantics && !p_.typed_semantics->loop_ownership_facts.empty()) {
        if (auto* fact = find_target_loop_fact(p_.typed_semantics->loop_ownership_facts)) {
          fact->specialization_id = InvalidSpecId;
        }
      }
    }

    auto find_target_ex_fact = [&](std::vector<TypedExceptionalOwnershipFactV1>& facts) -> TypedExceptionalOwnershipFactV1* {
      for (auto& f : facts) {
        if (f.specialization_id != InvalidSpecId) {
          if (!target_spec_id || f.specialization_id == *target_spec_id)
            return &f;
        }
      }
      return facts.empty() ? nullptr : &facts.front();
    };
    auto find_target_ex_req = [&](std::vector<TypedExceptionalOwnershipRequirementV1>& reqs) -> TypedExceptionalOwnershipRequirementV1* {
      for (auto& r : reqs) {
        if (r.specialization_id != InvalidSpecId) {
          if (!target_spec_id || r.specialization_id == *target_spec_id)
            return &r;
        }
      }
      return reqs.empty() ? nullptr : &reqs.front();
    };

    if (std::getenv("MOSS_TEST_CONSUMER_DROP_EXCEPTIONAL_FACT")) {
      if (p_.typed_semantics && !p_.typed_semantics->exceptional_ownership_facts.empty()) {
        if (auto* fact = find_target_ex_fact(p_.typed_semantics->exceptional_ownership_facts)) {
          fact->availability = TypedExceptionalOwnershipFactV1::Availability::MissingOccurrence;
        }
      }
    }
    if (std::getenv("MOSS_TEST_CONSUMER_PERTURB_EXCEPTIONAL_FACT")) {
      if (p_.typed_semantics && !p_.typed_semantics->exceptional_ownership_facts.empty()) {
        if (auto* fact = find_target_ex_fact(p_.typed_semantics->exceptional_ownership_facts)) {
          fact->raised_variant_identity = "perturbed-variant-id";
        }
      }
    }
    if (std::getenv("MOSS_TEST_CONSUMER_DELETE_EXCEPTIONAL_RECORD")) {
      if (p_.typed_semantics && !p_.typed_semantics->exceptional_ownership_facts.empty()) {
        if (target_spec_id) {
          for (auto it = p_.typed_semantics->exceptional_ownership_facts.begin();
               it != p_.typed_semantics->exceptional_ownership_facts.end(); ++it) {
            if (it->specialization_id == *target_spec_id) {
              p_.typed_semantics->exceptional_ownership_facts.erase(it);
              break;
            }
          }
        } else {
          p_.typed_semantics->exceptional_ownership_facts.erase(
              p_.typed_semantics->exceptional_ownership_facts.begin());
        }
      }
    }
    if (std::getenv("MOSS_TEST_CONSUMER_DELETE_EXCEPTIONAL_REQUIREMENT")) {
      if (p_.typed_semantics && !p_.typed_semantics->required_exceptional_ownership_occurrences.empty()) {
        p_.typed_semantics->required_exceptional_ownership_occurrences.erase(
            p_.typed_semantics->required_exceptional_ownership_occurrences.begin());
      }
    }
    if (std::getenv("MOSS_TEST_CONSUMER_DUPLICATE_EXCEPTIONAL_FACT")) {
      if (p_.typed_semantics && !p_.typed_semantics->exceptional_ownership_facts.empty()) {
        if (auto* fact = find_target_ex_fact(p_.typed_semantics->exceptional_ownership_facts)) {
          p_.typed_semantics->exceptional_ownership_facts.push_back(*fact);
        }
      }
    }
    if (std::getenv("MOSS_TEST_CONSUMER_DUPLICATE_EXCEPTIONAL_REQUIREMENT")) {
      if (p_.typed_semantics && !p_.typed_semantics->required_exceptional_ownership_occurrences.empty()) {
        if (auto* req = find_target_ex_req(p_.typed_semantics->required_exceptional_ownership_occurrences)) {
          p_.typed_semantics->required_exceptional_ownership_occurrences.push_back(*req);
        }
      }
    }

    if (p_.typed_semantics) {
      auto& bare_facts = p_.typed_semantics->bare_raise_transfer_facts;
      auto& bare_requirements =
          p_.typed_semantics->required_bare_raise_transfer_occurrences;
      if (std::getenv("MOSS_TEST_CONSUMER_DROP_BARE_RAISE_TRANSFER_FACT") &&
          !bare_facts.empty())
        bare_facts.erase(bare_facts.begin());
      if (std::getenv("MOSS_TEST_CONSUMER_DUPLICATE_BARE_RAISE_TRANSFER_FACT") &&
          !bare_facts.empty())
        bare_facts.push_back(bare_facts.front());
      if (std::getenv("MOSS_TEST_CONSUMER_PERTURB_BARE_RAISE_TRANSFER_FACT") &&
          !bare_facts.empty())
        bare_facts.front().transfers_original_carrier = false;
      if (std::getenv("MOSS_TEST_CONSUMER_WRONG_BARE_RAISE_TRANSFER_EDGE") &&
          !bare_facts.empty())
        ++bare_facts.front().recovery_statement_occurrence_id;
      if (std::getenv("MOSS_TEST_CONSUMER_DROP_BARE_RAISE_TRANSFER_REQUIREMENT") &&
          !bare_requirements.empty())
        bare_requirements.erase(bare_requirements.begin());
      if (std::getenv("MOSS_TEST_CONSUMER_DUPLICATE_BARE_RAISE_TRANSFER_REQUIREMENT") &&
          !bare_requirements.empty())
        bare_requirements.push_back(bare_requirements.front());
    }
    if (test_semantic_facts) {
      auto& try_facts = test_semantic_facts->try_continuation_facts;
      if (std::getenv("MOSS_TEST_CONSUMER_DROP_TRY_CONTINUATION_FACT") &&
          !try_facts.empty())
        try_facts.erase(try_facts.begin());
      if (std::getenv("MOSS_TEST_CONSUMER_DUPLICATE_TRY_CONTINUATION_FACT") &&
          !try_facts.empty())
        try_facts.push_back(try_facts.front());
      if (std::getenv("MOSS_TEST_CONSUMER_PERTURB_TRY_CONTINUATION_FACT") &&
          !try_facts.empty())
        try_facts.front().has_normal_successor =
            !try_facts.front().has_normal_successor;
    }

    auto find_target_take_fact = [&](std::vector<TypedTakeFactV1>& facts) -> TypedTakeFactV1* {
      for (auto& f : facts) {
        if (f.specialization_id != InvalidSpecId) {
          if (!target_spec_id || f.specialization_id == *target_spec_id)
            return &f;
        }
      }
      return facts.empty() ? nullptr : &facts.front();
    };
    auto find_target_take_req = [&](std::vector<TypedTakeRequirementV1>& reqs) -> TypedTakeRequirementV1* {
      for (auto& r : reqs) {
        if (r.specialization_id != InvalidSpecId) {
          if (!target_spec_id || r.specialization_id == *target_spec_id)
            return &r;
        }
      }
      return reqs.empty() ? nullptr : &reqs.front();
    };

    if (std::getenv("MOSS_TEST_CONSUMER_DROP_TAKE_FACT")) {
      if (p_.typed_semantics && !p_.typed_semantics->take_facts.empty()) {
        if (auto* fact = find_target_take_fact(p_.typed_semantics->take_facts)) {
          fact->availability = TypedTakeFactV1::Availability::MissingTakeOccurrence;
        }
      }
    }
    if (std::getenv("MOSS_TEST_CONSUMER_PERTURB_TAKE_FACT")) {
      if (p_.typed_semantics && !p_.typed_semantics->take_facts.empty()) {
        if (auto* fact = find_target_take_fact(p_.typed_semantics->take_facts)) {
          fact->required_access = Effect::Read;
        }
      }
    }
    if (std::getenv("MOSS_TEST_CONSUMER_DELETE_TAKE_RECORD")) {
      if (p_.typed_semantics && !p_.typed_semantics->take_facts.empty()) {
        p_.typed_semantics->take_facts.erase(p_.typed_semantics->take_facts.begin());
      }
    }
    if (std::getenv("MOSS_TEST_CONSUMER_DELETE_TAKE_REQUIREMENT")) {
      if (p_.typed_semantics && !p_.typed_semantics->required_take_occurrences.empty()) {
        p_.typed_semantics->required_take_occurrences.erase(
            p_.typed_semantics->required_take_occurrences.begin());
      }
    }
    if (std::getenv("MOSS_TEST_CONSUMER_DUPLICATE_TAKE_FACT")) {
      if (p_.typed_semantics && !p_.typed_semantics->take_facts.empty()) {
        if (auto* fact = find_target_take_fact(p_.typed_semantics->take_facts)) {
          p_.typed_semantics->take_facts.push_back(*fact);
        }
      }
    }
    if (std::getenv("MOSS_TEST_CONSUMER_DUPLICATE_TAKE_REQUIREMENT")) {
      if (p_.typed_semantics && !p_.typed_semantics->required_take_occurrences.empty()) {
        if (auto* req = find_target_take_req(p_.typed_semantics->required_take_occurrences)) {
          p_.typed_semantics->required_take_occurrences.push_back(*req);
        }
      }
    }
    if (std::getenv("MOSS_TEST_CONSUMER_PERTURB_TAKE_SPECIALIZATION_ID")) {
      if (p_.typed_semantics && !p_.typed_semantics->take_facts.empty()) {
        if (auto* fact = find_target_take_fact(p_.typed_semantics->take_facts)) {
          fact->specialization_id = InvalidSpecId;
        }
      }
    }
    if (std::getenv("MOSS_TEST_CONSUMER_PERTURB_TAKE_PLACE_ID")) {
      if (p_.typed_semantics && !p_.typed_semantics->take_facts.empty()) {
        if (auto* fact = find_target_take_fact(p_.typed_semantics->take_facts)) {
          fact->place_occurrence_id += 9999;
        }
      }
    }
    if (auto* target_arena_env = std::getenv("MOSS_TEST_TARGET_ARENA")) {
      auto* target_expr_env = std::getenv("MOSS_TEST_TARGET_EXPR");
      auto* target_spec_env = std::getenv("MOSS_TEST_TARGET_SPEC_ID");
      auto* mutation_env = std::getenv("MOSS_TEST_TARGET_MUTATION");
      if (target_expr_env && target_spec_env && mutation_env) {
        size_t arena_idx = std::stoull(target_arena_env);
        est::ExprId expr_id = std::stoull(target_expr_env);
        const auto parsed_spec = std::stoul(target_spec_env);
        if (parsed_spec >= InvalidSpecId)
          throw std::runtime_error("MOSS_TEST_TARGET_SPEC_ID is out of range");
        const SpecId spec_id = static_cast<SpecId>(parsed_spec);
        string mut = mutation_env;
        if (arena_idx < p_.expression_arenas.size() && p_.expression_arenas[arena_idx]) {
          auto& arena = *p_.expression_arenas[arena_idx];
          if (mut == "drop_fact") {
            arena.test_drop_occurrence_fact(expr_id, spec_id);
          } else if (mut == "drop_effect") {
            arena.test_drop_occurrence_effect(expr_id, spec_id);
          } else if (mut == "perturb_effect") {
            arena.test_perturb_occurrence_effect(expr_id, spec_id);
          } else if (mut == "drop_target") {
            arena.test_mutate_occurrence_target(expr_id, spec_id, "");
          } else if (mut == "perturb_target") {
            arena.test_mutate_occurrence_target(expr_id, spec_id, "fn:__moss_test_perturbed_target__");
          } else if (mut == "corrupt_type") {
            arena.test_mutate_occurrence_type(expr_id, spec_id, Type::primitive("bool"));
          } else if (mut == "corrupt_specialization") {
            arena.test_mutate_occurrence_specialization(expr_id, spec_id, "fn:__moss_test_wrong_spec__");
          }
          if (auto* sibling_env = std::getenv("MOSS_TEST_ISOLATION_SIBLING_EXPR")) {
            est::ExprId sibling_id = std::stoull(sibling_env);
            const auto* sibling_facts = arena.facts(sibling_id, spec_id);
            if (!sibling_facts || !sibling_facts->inferred_effect) {
              throw std::runtime_error("MOSS_INTERNAL_INVARIANT selector isolation violated: sibling facts corrupted");
            }
          }
        }
      }
    }
    if (std::getenv("MOSS_TEST_CONSUMER_CONTRADICT_SELECTOR_TYPE")) {
      for (const auto& arena : p_.expression_arenas) {
        if (arena) arena->test_contradict_selector_type(Type::primitive("bool"));
      }
    }
    if (std::getenv("MOSS_TEST_CONSUMER_CONTRADICT_SELECTOR_TARGET")) {
      for (const auto& arena : p_.expression_arenas) {
        if (arena) arena->test_contradict_selector_target("callable-v1:module-v1:corrupted::target");
      }
    }
    if (std::getenv("MOSS_TEST_CONSUMER_CONTRADICT_SELECTOR_EFFECT")) {
      for (const auto& arena : p_.expression_arenas) {
        if (arena) arena->test_contradict_selector_effect();
      }
    }
    if (std::getenv("MOSS_TEST_CONSUMER_DROP_PRIMARY_SELECTOR_FACT")) {
      for (const auto& arena : p_.expression_arenas) {
        if (arena) arena->test_drop_primary_selector_fact();
      }
    }
    if (std::getenv("MOSS_TEST_CONSUMER_DROP_SECONDARY_SELECTOR_FACT")) {
      for (const auto& arena : p_.expression_arenas) {
        if (arena) arena->test_drop_secondary_selector_fact();
      }
    }
    if (std::getenv("MOSS_TEST_CONSUMER_PERTURB_PRIMARY_EFFECT")) {
      for (const auto& arena : p_.expression_arenas) {
        if (arena) arena->test_perturb_primary_effect();
      }
    }
    if (std::getenv("MOSS_TEST_CONSUMER_DROP_SELECTOR_OP_TYPE")) {
      for (const auto& arena : p_.expression_arenas) {
        if (arena) arena->test_drop_selector_op_type();
      }
    }
    if (std::getenv("MOSS_TEST_CONSUMER_DROP_SELECTOR_TARGET")) {
      for (const auto& arena : p_.expression_arenas) {
        if (arena) arena->test_drop_selector_target();
      }
    }
    if (std::getenv("MOSS_TEST_CONSUMER_PERTURB_SELECTOR_TARGET")) {
      for (const auto& arena : p_.expression_arenas) {
        if (arena) arena->test_perturb_selector_target("callable-v1:module-v1:corrupted::unknown");
      }
    }
    if (std::getenv("MOSS_TEST_CONSUMER_DROP_SELECTOR_EFFECT")) {
      for (const auto& arena : p_.expression_arenas) {
        if (arena) arena->test_drop_selector_effect();
      }
    }
    if (std::getenv("MOSS_TEST_CONSUMER_WRONG_SELECTOR_SPECIALIZATION")) {
      throw std::runtime_error("MOSS_INTERNAL_INVARIANT selector specialization perturbation requires an exact call-edge mutation hook");
    }
#endif
  }

  string generate() {
    CompilerStageTimer timer("rust_generation");
    if (!p_.concrete_domain_graph.instances.empty() && !p_.concrete_domain_graph.closed)
      throw std::runtime_error("internal error: lowering requires a closed ConcreteDomainGraph");
    validate_synchronization_lowering(*semantic_program_);
    std::ostringstream o;
    o << "// Generated by Moss v0.1. Do not edit by hand.\n";
    o << "// Synchronous domain calls enter compiler-planned handler-level 2PL.\n";
    for (const auto& domain : p_.domains) o << "// Moss backend plan: " << domain.name << " = Handler2PL.\n";
    if (staged_abi_v10())
      for (const auto& operation : p_.key_operation_specialization_ids) {
        const auto separator = operation.first.rfind('|');
        synchronization_require(separator != string::npos &&
            p_.key_proof_ids_by_type.count(operation.first.substr(separator + 1)),
            "key operation specialization is missing its proof");
        o << "// Moss key operation specialization: " << operation.first
          << " proof="
          << p_.key_proof_ids_by_type.at(operation.first.substr(separator + 1))
          << " id=" << operation.second << "\n";
      }
    o << "#![allow(non_snake_case)]\n#![allow(non_camel_case_types)]\n#![allow(dead_code)]\n";
    o << "#![allow(unused_imports)]\n#![allow(unused_mut)]\n#![allow(unused_variables)]\n#![allow(unused_assignments)]\n\n";
    if (!p_.domains.empty() || !specialized_domains_.empty()) {
      if (staged_abi_v10()) {
        o << "use moss_runtime::moss_rt;\n";
      } else {
        o << "mod moss_rt {\n"
             "    use std::ops::Deref;\n"
             "    use std::sync::Arc;\n"
             "    pub struct DomainHandle<T> { inner: Arc<T> }\n"
             "    impl<T> DomainHandle<T> { pub fn new(inner: T) -> Self { Self { inner: Arc::new(inner) } } }\n"
             "    impl<T> Clone for DomainHandle<T> { fn clone(&self) -> Self { Self { inner: Arc::clone(&self.inner) } } }\n"
             "    impl<T> Deref for DomainHandle<T> { type Target = T; fn deref(&self) -> &T { &self.inner } }\n"
             "    pub fn share_domain<T>(handle: &DomainHandle<T>) -> DomainHandle<T> { handle.clone() }\n"
             "}\n";
      }
    }
    for (const auto& dependency : rust_dependencies_) {
      string crate = dependency == "__moss_specializations__"
          ? "moss_specializations" : tooling_name("moss_" + dependency);
      o << "extern crate " << crate << ";\n";
      // Moss's namespace pass has already qualified imported names.  Bringing
      // the dependency's public Rust surface into this crate keeps those
      // generated expressions direct calls to the materialized dependency
      // symbol; no dependency implementation is copied here.
      o << "use " << crate << "::*;\n";
    }
    o << "fn __moss_require_send<T: Send>() {}\n\n";
    o << "#[inline(always)]\n"
         "fn __moss_shl(value: i64, shift: i64) -> i64 {\n"
         "    if shift < 0 || shift >= 64 { 0 } else { value.wrapping_shl(shift as u32) }\n"
         "}\n\n"
         "#[inline(always)]\n"
         "fn __moss_shr(value: i64, shift: i64) -> i64 {\n"
         "    if shift < 0 || shift >= 64 { if value < 0 { -1 } else { 0 } } else { value.wrapping_shr(shift as u32) }\n"
         "}\n\n";

    o << handler_runtime_rust();
    o << float_format_runtime_rust();
    if (staged_abi_v10() && indexed_strings_staged()) {
      // A provider and its source-free consumers must name the same Rust
      // MossString type. The opt-in v10 path imports Team S's exact indexed
      // implementation from the compiler-owned shared runtime crate.
      o << char_ordering_runtime_rust();
      o << "pub use moss_runtime::string::{MossStr, MossString, "
           "__moss_string_length, __moss_string_char_at, "
           "__moss_string_slice, __moss_string_find};\n";
    } else {
      o << char_string_ordering_runtime_rust(indexed_strings_staged());
    }
    o << display_runtime_rust();
    if (program_uses_numeric_builtins()) o << numeric_runtime_rust();
    if (program_uses_fileio(p_)) {
      o << fileio_runtime_rust();
    }
    if (!project_module_artifact_) {
      o << fileio_root_runtime_rust();
    } else if (p_.main) {
      // A module with main may also be exported as an rlib. Only its executable
      // build owns process-wide FileIO symbols; linked library builds borrow
      // the selected executable's root runtime.
      o << "#[cfg(moss_process_root)]\nmod __moss_fileio_root_runtime {\nuse super::*;\n";
      o << fileio_root_runtime_rust();
      o << "}\n#[cfg(moss_process_root)]\npub use __moss_fileio_root_runtime::*;\n";
    }
    // Executor runtime ownership follows C's executable/provider role. The
    // Agent B FileIO root runtime remains emitted only in the process root.
    const ExecutorRuntimeRole executor_role =
        !project_module_artifact_
            ? (p_.explicit_module && !p_.main.has_value()
                ? ExecutorRuntimeRole::SelectedByExecutableBuild
                : ExecutorRuntimeRole::ProcessRoot)
            : semantic_program_->main.has_value()
                ? (p_.main.has_value()
                    ? ExecutorRuntimeRole::SelectedByExecutableBuild
                    : ExecutorRuntimeRole::Provider)
                : ExecutorRuntimeRole::SelectedByExecutableBuild;
    o << executor_runtime_rust(executor_role);
    for (const auto& e : p_.enums) gen_enum(o, e);
    gen_standard_error_external_bridges(o);
    gen_phase21_raised_type(o);
    gen_phase21_external_raised_bridges(o);
    for (const auto& t : p_.objects) gen_object(o, t);
    std::map<string, const ObjectType*> view_objects(objects_.begin(), objects_.end());
    for (const auto& entry : view_objects) {
      if (entry.second->fields.empty()) continue;
      bool owns = std::any_of(p_.objects.begin(), p_.objects.end(), [&](const auto& object) { return object.name == entry.first; });
      if (!owns && p_.explicit_module) {
        auto sep = entry.first.find("__");
        if (sep != string::npos) {
          string mod_name = entry.first.substr(0, sep);
          if (std::find(rust_dependencies_.begin(), rust_dependencies_.end(), mod_name) == rust_dependencies_.end()) {
            continue;
          }
        } else {
          continue;
        }
      }
      bool is_generic = std::any_of(entry.second->fields.begin(), entry.second->fields.end(),
          [](const Field& f) { return f.inferred || f.type.empty(); });
      if (!is_generic) gen_object_access(o, *entry.second, owns);
    }
    for (const auto& f : p_.functions) gen_function(o, f);
    auto body_owner = [&](const Domain& d) {
      return !p_.explicit_module || starts_with(d.name, p_.module_name + "__");
    };
    for (const auto& d : p_.domains) if (body_owner(d)) gen_domain_body_abi(o, d);
    for (const auto& d : specialized_domains_)
      if (owns_specialization(d) && body_owner(d)) gen_domain_body_abi(o, d);
    if (!p_.explicit_module || p_.main) {
      // The final application owns every physical layout, including source-free
      // provider domains. Provider bodies expose only semantic borrowed access.
      for (const auto& d : p_.domains) gen_ref_decl(o, d);
      for (const auto& d : specialized_domains_) if (owns_specialization(d)) gen_ref_decl(o, d);
      for (const auto& d : p_.domains) gen_nominal_handle(o, d);
      for (const auto& d : p_.domains) {
        gen_domain(o, d);
        gen_route_contract(o, d, d.name + "Ref");
        if (has_domain_specializations(d.name)) gen_route_contract(o, d, d.name + "Handle");
        if (d.exported) gen_exported_domain_bridge(o, d);
      }
      for (const auto& d : specialized_domains_) if (owns_specialization(d)) {
        gen_domain(o, d);
        // Exact specialized routes use their nominal source contract where
        // parameter/reply types are compatible (existing handle adapter).
        if (d.exported) gen_exported_domain_bridge(o, d);
      }
    }
    if (mode_ == ProgramGenerationMode::Tests)
      gen_test_harness(o);
    else if (mode_ == ProgramGenerationMode::Benchmarks)
      gen_benchmark_harness(o);
    else if (p_.main)
      gen_main(o, *p_.main);
    else
      o << "fn main() {}\n";
    return strip_unused_object_value_helpers(o.str());
  }

 private:
  static string strip_unused_object_value_helpers(string generated) {
    constexpr std::string_view begin_marker = "    // MOSS_OBJECT_VALUE_HELPER_BEGIN:";
    constexpr std::string_view end_prefix = "    // MOSS_OBJECT_VALUE_HELPER_END:";
    struct Helper {
      std::size_t begin, body_begin, body_end, end;
      string method;
    };
    std::vector<Helper> helpers;
    std::size_t cursor = 0;
    while ((cursor = generated.find(begin_marker, cursor)) != string::npos) {
      const auto name_begin = cursor + begin_marker.size();
      const auto marker_end = generated.find('\n', name_begin);
      if (marker_end == string::npos)
        throw std::runtime_error("MOSS_INTERNAL_INVARIANT malformed object value helper marker");
      const string method = generated.substr(name_begin, marker_end - name_begin);
      const auto body_begin = marker_end + 1;
      const string end_marker = string(end_prefix) + method + "\n";
      const auto end = generated.find(end_marker, body_begin);
      if (end == string::npos)
        throw std::runtime_error("MOSS_INTERNAL_INVARIANT unterminated object value helper");
      helpers.push_back({cursor, body_begin, end,
                         end + end_marker.size(), method});
      cursor = end + end_marker.size();
    }
    if (helpers.empty()) return generated;

    string without_helper_bodies;
    cursor = 0;
    for (const auto& helper : helpers) {
      without_helper_bodies.append(generated, cursor, helper.begin - cursor);
      cursor = helper.end;
    }
    without_helper_bodies.append(generated, cursor, string::npos);

    std::set<string> reachable;
    std::vector<string> pending;
    auto records_call = [](std::string_view text, const string& method) {
      return text.find("." + method + "()") != std::string_view::npos;
    };
    for (const auto& helper : helpers)
      if (records_call(without_helper_bodies, helper.method))
        pending.push_back(helper.method);
    while (!pending.empty()) {
      string method = std::move(pending.back());
      pending.pop_back();
      if (!reachable.insert(method).second) continue;
      auto definition = std::find_if(helpers.begin(), helpers.end(),
          [&](const Helper& helper) { return helper.method == method; });
      if (definition == helpers.end())
        throw std::runtime_error("MOSS_INTERNAL_INVARIANT missing object value helper dependency");
      const std::string_view body(generated.data() + definition->body_begin,
                                  definition->body_end - definition->body_begin);
      for (const auto& dependency : helpers)
        if (!reachable.count(dependency.method) &&
            records_call(body, dependency.method))
          pending.push_back(dependency.method);
    }
    for (auto helper = helpers.rbegin(); helper != helpers.rend(); ++helper)
      if (!reachable.count(helper->method))
        generated.erase(helper->begin, helper->end - helper->begin);
    return generated;
  }

  const Program& p_;
  // Keeps this program's interned declarations bound while lowering.
  moss::TypeContext::Binding type_scope_;
  bool program_uses_numeric_builtins() const {
    auto arena_uses = [](const est::Arena& arena) {
      for (est::ExprId id = 0; id < arena.size(); ++id) {
        const auto* call = std::get_if<est::Call>(&arena.at(id));
        if (!call) continue;
        const auto& callee = arena.at(call->callee);
        if (const auto* identifier = std::get_if<est::Identifier>(&callee);
            identifier && !numeric_builtin_base(identifier->name).empty())
          return true;
        if (const auto* member = std::get_if<est::Member>(&callee);
            member && !numeric_builtin_base(member->name).empty())
          return true;
      }
      return false;
    };
    if (p_.expression_arena && arena_uses(*p_.expression_arena)) return true;
    for (const auto& arena : p_.expression_arenas)
      if (arena && arena_uses(*arena)) return true;
    // Some synthesized source bodies have no retained source handles. Rebuild
    // node views for those bodies instead of searching their spellings.
    const auto expression_uses = [&](std::string_view source) {
      if (source.empty()) return false;
      const auto view = est::parse_view(source);
      return view && view->arena && arena_uses(*view->arena);
    };
    const auto body_uses = [&](const vector<Stmt>& body) {
      for (const auto& statement : body) {
        if (expression_uses(statement.a) || expression_uses(statement.b) ||
            expression_uses(statement.text)) return true;
        for (const auto& argument : statement.args)
          if (expression_uses(argument)) return true;
        if (statement.kind == Stmt::Kind::Call && statement.text.empty()) {
          string call = statement.a;
          if (!statement.b.empty()) call += "." + statement.b;
          call += "(";
          for (std::size_t index = 0; index < statement.args.size(); ++index) {
            if (index) call += ", ";
            call += statement.args[index];
          }
          call += ")";
          if (expression_uses(call)) return true;
        }
      }
      return false;
    };
    for (const auto& function : p_.functions)
      if (body_uses(function.body) ||
          (function.result_expression &&
           expression_uses(*function.result_expression))) return true;
    for (const auto& object : p_.objects)
      for (const auto& method : object.methods)
        if (body_uses(method.body) ||
            (method.result_expression &&
             expression_uses(*method.result_expression))) return true;
    for (const auto& domain : p_.domains)
      for (const auto& handler : domain.handlers)
        if (body_uses(handler.body)) return true;
    for (const auto& domain : specialized_domains_)
      for (const auto& handler : domain.handlers)
        if (body_uses(handler.body)) return true;
    if (p_.main && body_uses(p_.main->body)) return true;
    for (const auto& test : p_.tests)
      if (body_uses(test.body)) return true;
    for (const auto& benchmark : p_.benchmarks)
      if (body_uses(benchmark.body)) return true;
    return false;
  }
  const Program* semantic_program_ = nullptr;
  std::set<string> write_through_parameters_;
  // Set only while lowering a local initializer/RHS with the checker's
  // authoritative WRITE ownership fact. One-argument Map.get otherwise stays
  // an ordinary immutable lookup.
  bool checked_map_get_write_through_ = false;
  std::set<string> borrowed_function_parameters_;
  // Primitive Match payloads in checked WRITE arms are borrowed mutably, but
  // Rust value reads and assignments must dereference that borrow.
  std::set<string> mutable_match_primitive_values_;
  string construction_binding_;
  bool view_domain_ = false;
  bool view_domain_access_ = false;
  bool view_method_ = false;
  std::set<string> view_parameters_;
  // Non-Copy incoming message parameters use a borrowed internal ABI.  This
  // is physical lowering metadata only: the checked Moss fact remains an
  // immutable value snapshot.  Keeping the set while rendering a handler
  // prevents a forwarded String/Vector/etc. from becoming &&T.
  std::set<string> borrowed_message_parameters_;
  mutable bool mutable_projection_ = false;
  bool debug_build_ = false;
  ProgramGenerationMode mode_ = ProgramGenerationMode::Application;
  vector<string> rust_dependencies_;
  bool emit_static_specializations_ = true;
  bool public_specializations_ = false;
  // Only Margo's per-module projection is reused as both an rlib and the
  // selected executable. Direct `moss -o` Rust is a standalone process root.
  bool project_module_artifact_ = false;
  bool benchmark_body_ = false;
  bool exported_domain_bridge_body_ = false;
  std::unordered_map<string, const Domain*> domains_;
  vector<Domain> specialized_domains_;
  std::unordered_map<string,string> specialization_sources_;
  std::unordered_map<string,string> specialization_names_;
  std::unordered_map<string, const ObjectType*> objects_;
  std::unordered_map<string, const EnumType*> enums_;
  std::unordered_map<string, const Function*> functions_;
  std::unordered_map<string, const Function*> callable_id_to_function_;

  const Function* find_function_by_callable_id(const string& id) const {
    if (id.empty()) return nullptr;
    auto it = callable_id_to_function_.find(id);
    if (it != callable_id_to_function_.end()) return it->second;
    for (const auto& [name, fn] : functions_) {
      if (fn && fn->semantic_callable_id == id) return fn;
    }
    return nullptr;
  }

  static bool is_compile_time_callable(const string& type) {
    return callable_identity(type);
  }
  struct DomainInstanceBinding {
    // This is the semantic specialization key carried by a source binding;
    // it is intentionally independent of the generated Rust type name.
    string source_domain;
    string instance;
  };
  std::unordered_map<string, DomainInstanceBinding> domain_instance_bindings_;
  std::set<string> ambiguous_domain_instance_bindings_;
  size_t assertion_temp_ = 0;
  mutable size_t call_argument_temp_ = 0;
  mutable std::optional<uint32_t> codegen_nested_argument_occurrence_;
  mutable size_t executor_invoke_temp_ = 0;
  mutable bool root_ingress_statement_ = false;
  mutable const TypedCopyFactV1* root_ingress_copy_fact_ = nullptr;
  struct RootIngressMessageOccurrence {
    std::uint32_t arena_index{UINT32_MAX};
    est::ExprId owner_expression{UINT32_MAX};
    est::ExprId message_expression{UINT32_MAX};
    est::ExprId receiver_expression{UINT32_MAX};
    std::uint64_t source_fingerprint{0};
    std::uint64_t origin_id{0};
    std::vector<est::ExprId> argument_expressions;
  };
  struct RootIngressExpressionOccurrence {
    std::uint32_t arena_index{UINT32_MAX};
    est::ExprId expression{UINT32_MAX};
    std::uint64_t source_fingerprint{0};
    std::uint64_t origin_id{0};
    string source;
  };
  struct RootIngressQueuedMessage {
    RootIngressMessageOccurrence occurrence;
    string source;
    std::uint32_t source_begin{0};
    std::uint32_t source_end{0};
  };
  struct RootIngressMessageQueue {
    std::vector<RootIngressQueuedMessage> messages;
    size_t next{0};
  };
  mutable RootIngressMessageQueue* active_root_ingress_message_queue_ = nullptr;
  mutable size_t fileio_chunk_temp_ = 0;
  bool phase21_result_context_ = false;
  std::optional<bool> phase21_return_result_context_;
  string phase21_selected_error_;
  string phase21_selected_carrier_;
  string phase21_try_label_;
  size_t phase21_try_temp_ = 0;
  const Handler* phase21_capture_handler_ = nullptr;
  const std::set<string>* phase21_capture_locals_ = nullptr;
  const RaiseSet* phase21_capture_alternatives_ = nullptr;
  std::set<string> phase21_option_slots_;
  const Stmt* phase21_codegen_statement_ = nullptr;
  const vector<est::Handle>* active_functional_expression_handles_ = nullptr;
  struct NativeEstCursor {
    const est::Arena* arena = nullptr;
    std::uint32_t arena_index = UINT32_MAX;
    est::ExprId expression = UINT32_MAX;
  };
  mutable const NativeEstCursor* active_native_est_cursor_ = nullptr;
  struct NativeEstCursorScope {
    const NativeEstCursor*& slot;
    const NativeEstCursor* prior;
    NativeEstCursorScope(const NativeEstCursor*& target,
                         const NativeEstCursor* current)
        : slot(target), prior(target) { slot = current; }
    ~NativeEstCursorScope() { slot = prior; }
  };
  NativeEstCursor checked_statement_cursor(
      const Stmt& statement, est::ExprSlot slot,
      std::uint32_t argument_index = 0) const {
    const est::Handle* selected = nullptr;
    for (const auto& handle : statement.expression_handles) {
      if (handle.slot != slot || handle.argument_index != argument_index)
        continue;
      if (selected)
        throw std::runtime_error(
            "MOSS_INTERNAL_INVARIANT duplicate Native statement EST handle");
      selected = &handle;
    }
    const Program& checked_program = semantic_program_ ? *semantic_program_ : p_;
    if (!selected || selected->arena_index >=
            checked_program.expression_arenas.size() ||
        !checked_program.expression_arenas[selected->arena_index] ||
        selected->expr >= checked_program.expression_arenas[
            selected->arena_index]->size())
      throw std::runtime_error(
          "MOSS_INTERNAL_INVARIANT Native statement EST handle is missing or invalid");
    return {checked_program.expression_arenas[selected->arena_index].get(),
            selected->arena_index, selected->expr};
  }

  static std::vector<est::ExprId> native_syntax_children(
      const est::Arena& arena, est::ExprId expression) {
    if (expression >= arena.size())
      throw std::runtime_error(
          "MOSS_INTERNAL_INVARIANT Native EST child traversal has invalid ExprId");
    std::vector<est::ExprId> result;
    std::visit([&](const auto& node) {
      using T = std::decay_t<decltype(node)>;
      auto child = [&](est::ExprId id) { result.push_back(id); };
      auto children = [&](const auto& ids) {
        result.insert(result.end(), ids.begin(), ids.end());
      };
      if constexpr (std::is_same_v<T, est::Unary>) child(node.operand);
      else if constexpr (std::is_same_v<T, est::Binary>) {
        child(node.left); child(node.right);
      } else if constexpr (std::is_same_v<T, est::Call>) {
        child(node.callee); children(node.arguments);
      } else if constexpr (std::is_same_v<T, est::Take>) children(node.arguments);
      else if constexpr (std::is_same_v<T, est::Member>) child(node.base);
      else if constexpr (std::is_same_v<T, est::Index>) {
        child(node.base); child(node.index);
      } else if constexpr (std::is_same_v<T, est::Parenthesized>) child(node.inner);
      else if constexpr (std::is_same_v<T, est::Message>) {
        child(node.target); children(node.arguments);
      } else if constexpr (std::is_same_v<T, est::NamedArgument>) child(node.value);
      else if constexpr (std::is_same_v<T, est::Collection> ||
                         std::is_same_v<T, est::Tuple>) children(node.elements);
      else if constexpr (std::is_same_v<T, est::Constructor>) {
        child(node.type); children(node.type_arguments); children(node.arguments);
      } else if constexpr (std::is_same_v<T, est::Pipeline>) {
        child(node.input); children(node.stages);
      }
    }, arena.at(expression));
    return result;
  }

  static std::optional<NativeEstCursor> native_call_argument_cursor(
      const NativeEstCursor* parent, std::size_t argument) {
    if (!parent || !parent->arena || parent->expression >= parent->arena->size())
      return std::nullopt;
    const auto call_id = est::unparenthesized(*parent->arena,
                                              parent->expression);
    const auto* call = std::get_if<est::Call>(&parent->arena->at(call_id));
    if (!call || argument >= call->arguments.size()) return std::nullopt;
    const auto children = native_syntax_children(*parent->arena, call_id);
    if (children.size() != call->arguments.size() + 1 ||
        children[argument + 1] != call->arguments[argument])
      throw std::runtime_error(
          "MOSS_INTERNAL_INVARIANT Native EST Call child order is contradictory");
    return NativeEstCursor{parent->arena, parent->arena_index,
                            call->arguments[argument]};
  }

  static std::optional<NativeEstCursor> native_member_receiver_cursor(
      const NativeEstCursor* parent) {
    if (!parent || !parent->arena || parent->expression >= parent->arena->size())
      return std::nullopt;
    auto root = est::unparenthesized(*parent->arena, parent->expression);
    if (const auto* call = std::get_if<est::Call>(&parent->arena->at(root))) {
      auto callee = est::unparenthesized(*parent->arena, call->callee);
      if (const auto* member =
              std::get_if<est::Member>(&parent->arena->at(callee)))
        return NativeEstCursor{parent->arena, parent->arena_index,
                               member->base};
      return std::nullopt;
    }
    if (const auto* member =
            std::get_if<est::Member>(&parent->arena->at(root)))
      return NativeEstCursor{parent->arena, parent->arena_index,
                             member->base};
    return std::nullopt;
  }

  static std::optional<NativeEstCursor> native_member_method_call_cursor(
      const NativeEstCursor* parent) {
    if (!parent || !parent->arena || parent->expression >= parent->arena->size())
      return std::nullopt;
    const auto root = est::unparenthesized(*parent->arena, parent->expression);
    const auto& node = parent->arena->at(root);
    if (std::holds_alternative<est::Call>(node))
      return NativeEstCursor{parent->arena, parent->arena_index, root};
    if (const auto* member = std::get_if<est::Member>(&node)) {
      const NativeEstCursor base{parent->arena, parent->arena_index,
                                 member->base};
      return native_member_method_call_cursor(&base);
    }
    return std::nullopt;
  }
  SpecId codegen_body_spec_id_ = InvalidSpecId;
  string codegen_active_specialization_key_;
  mutable std::map<string, string> generated_copy_helper_fact_ids_;
  const FunctionSpecialization* codegen_result_specialization_ = nullptr;
  string codegen_result_expression_;

  static bool handler_has_failure_captures(const Handler& handler) {
    if (handler.provider_abi)
      for (const auto& arm : handler.provider_abi->failure_arms)
        if (!arm.captures.empty()) return true;
    for (const auto& arm : handler.failure_arms)
      if (!arm.captures.empty()) return true;
    return false;
  }

  string handler_capture_carrier_type(const Handler& handler) const {
    return handler.provider_abi
        ? handler.provider_abi->raised_carrier_rust_type
        : handler.phase21_capture_carrier_type;
  }

  string phase21_capture_carrier(const string& error,
                                 const RaiseSet* selected = nullptr) const {
    if (!phase21_capture_handler_ || !phase21_capture_locals_ ||
        !(selected ? selected : phase21_capture_alternatives_))
      throw std::runtime_error(
          "internal error: missing on_fail capture lowering context");
    const Handler& handler = *phase21_capture_handler_;
    const string carrier = handler_capture_carrier_type(handler);
    const string frame = carrier.substr(0, carrier.find("__MossRaisedCarrier_")) +
        "__MossFailureFrame_" +
        carrier.substr(carrier.find("__MossRaisedCarrier_") + 20);
    std::map<string,string> capture_types;
    for (const auto& arm : handler.failure_arms)
      for (const auto& capture : arm.captures)
        capture_types.emplace(capture.name, capture.type);
    std::ostringstream out;
    out << "#[allow(unreachable_patterns)] match " << error << " { ";
    const RaiseSet& possible = *(selected ? selected :
                                phase21_capture_alternatives_);
    for (const auto& identity : possible) {
      const string variant = phase21_rust_variant(identity);
      out << "__MossRaised::" << variant << "(__moss_payload) => "
          << carrier << "(" << frame << " { error: __MossRaised::"
          << variant << "(__moss_payload)";
      for (const auto& capture : capture_types) {
        bool selected = false;
        for (const auto& arm : handler.failure_arms)
          if (arm.exceptional_alternatives.count(identity))
            selected |= std::any_of(arm.captures.begin(), arm.captures.end(),
                [&](const FailureArm::Capture& item) {
                  return item.name == capture.first;
                });
        if (selected && !phase21_capture_locals_->count(capture.first))
          throw std::runtime_error(
              "internal error: on_fail capture is unavailable at native raise site");
        out << ", " << rust_identifier(capture.first) << ": "
            << (selected ? "Some(" +
                (phase21_option_slots_.count(capture.first)
                    ? "__moss_recovery_slot_" + capture.first +
                        ".take().unwrap_or_else(|| std::process::abort())"
                    : rust_identifier(capture.first)) + ")"
                         : "None");
      }
      out << " }), ";
    }
    out << "_ => std::process::abort() }";
    return out.str();
  }

  string phase21_propagate(const string& fallible) const {
    if (phase21_try_label_.empty()) {
      if (phase21_capture_handler_)
        return "match " + fallible + " { Ok(__moss_value) => "
            "__moss_value, Err(__moss_error) => return Err(" +
            phase21_capture_carrier("__moss_error.into()") + ") }";
      return "(" + fallible + ")?";
    }
    return "match " + fallible + " { Ok(__moss_value) => __moss_value, "
        "Err(__moss_error) => break '" + phase21_try_label_ +
        " Err(" + (phase21_capture_handler_
            ? phase21_capture_carrier("__moss_error.into()")
            : "__moss_error.into()") + ") }";
  }

  string phase21_error_exit(const string& error) const {
    if (phase21_try_label_.empty())
      return "return Err(" +
          (phase21_capture_handler_ ? phase21_capture_carrier(error) : error) +
          ")";
    return "break '" + phase21_try_label_ + " Err(" +
        (phase21_capture_handler_ ? phase21_capture_carrier(error) : error) +
        ")";
  }

  string phase21_collection_error_exit(const string& variant) const {
    const auto identity = RaisedIdentity::enum_variant(
        "std__CollectionError", variant);
    if (!phase21_capture_alternatives_ ||
        !phase21_capture_alternatives_->count(identity))
      throw std::runtime_error(
          "MOSS_INTERNAL_INVARIANT missing checked CollectionError variant fact: " +
          identity.canonical());
    const string raised = "__MossRaised::" + phase21_rust_variant(identity) +
        "(std__CollectionError::" + variant + ")";
    return phase21_error_exit(raised);
  }

  bool phase21_collection_error_checked(const string& variant) const {
    const auto identity = RaisedIdentity::enum_variant(
        "std__CollectionError", variant);
    const bool consumer_has = phase21_capture_alternatives_ &&
        phase21_capture_alternatives_->count(identity);
    const bool checker_declared = phase21_codegen_statement_ &&
        phase21_codegen_statement_->exceptional_alternatives.count(identity);
    if (checker_declared && !consumer_has)
      throw std::runtime_error(
          "MOSS_INTERNAL_INVARIANT checked CollectionError fact was dropped or perturbed: " +
          identity.canonical());
    return consumer_has;
  }

  const TypedConditionalCollectionErrorFactV1*
  resolve_conditional_collection_error_fact(
      const TypedConditionalCollectionErrorRequirementV1& requirement) const {
    if (!p_.typed_semantics)
      throw std::runtime_error(
          "MOSS_INTERNAL_INVARIANT conditional CollectionError authority unavailable");
    size_t requirement_count = 0;
    for (const auto& candidate :
         p_.typed_semantics->required_conditional_collection_error_occurrences) {
      if (candidate.operation == requirement.operation &&
          candidate.est_arena_index == requirement.est_arena_index &&
          candidate.operation_expression_occurrence_id ==
              requirement.operation_expression_occurrence_id &&
          candidate.statement_occurrence_id == requirement.statement_occurrence_id &&
          candidate.specialization_id == requirement.specialization_id &&
          candidate.specialization_id == requirement.specialization_id) {
        ++requirement_count;
      }
    }
    size_t fact_count = 0;
    for (const auto& candidate :
         p_.typed_semantics->conditional_collection_error_facts) {
      if (candidate.operation == requirement.operation &&
          candidate.est_arena_index == requirement.est_arena_index &&
          candidate.operation_expression_occurrence_id ==
              requirement.operation_expression_occurrence_id &&
          candidate.statement_occurrence_id == requirement.statement_occurrence_id &&
          candidate.specialization_id == requirement.specialization_id &&
          candidate.specialization_id == requirement.specialization_id) {
        ++fact_count;
      }
    }
#ifdef MOSS_TEST_BUILD
    if (std::getenv("MOSS_TEST_CONSUMER_DROP_CONDITIONAL_COLLECTION_ERROR_FACT"))
      fact_count = 0;
    if (std::getenv("MOSS_TEST_CONSUMER_DUPLICATE_CONDITIONAL_COLLECTION_ERROR_FACT"))
      ++fact_count;
    if (std::getenv("MOSS_TEST_CONSUMER_DROP_CONDITIONAL_COLLECTION_ERROR_REQUIREMENT"))
      --requirement_count;
    if (std::getenv("MOSS_TEST_CONSUMER_DUPLICATE_CONDITIONAL_COLLECTION_ERROR_REQUIREMENT"))
      ++requirement_count;
#endif
    if (requirement_count != 1 || fact_count != 1)
      throw std::runtime_error(
          "MOSS_INTERNAL_INVARIANT conditional CollectionError requirement/fact missing or ambiguous");
    try {
#ifdef MOSS_TEST_BUILD
      if (std::getenv("MOSS_TEST_CONSUMER_PERTURB_CONDITIONAL_COLLECTION_ERROR_FACT")) {
        auto corrupted = requirement;
        corrupted.error_variant_identity = "forged";
        (void)p_.typed_semantics->find_conditional_collection_error_fact(corrupted);
        throw std::runtime_error(
            "perturbed conditional CollectionError authority unexpectedly passed");
      }
#endif
      const auto* fact = p_.typed_semantics->find_conditional_collection_error_fact(
          requirement);
      if (!fact || !fact->is_authoritative())
        throw std::invalid_argument(
            "conditional CollectionError fact is not authoritative");
      return fact;
    } catch (const std::exception& error) {
      throw std::runtime_error(
          "MOSS_INTERNAL_INVARIANT " + string(error.what()));
    }
  }

  const TypedReturnValueFactV1* resolve_return_value_fact(
      const Stmt& statement, const string& functional_context) const {
    (void)functional_context;  // Kept only for diagnostics at legacy call sites.
    const auto* typed_semantics = semantic_program_ &&
            semantic_program_->typed_semantics
        ? semantic_program_->typed_semantics.get()
        : p_.typed_semantics.get();
    const Program& checked_program = semantic_program_ ? *semantic_program_ : p_;
    if (!typed_semantics ||
        (statement.kind != Stmt::Kind::Return &&
         statement.kind != Stmt::Kind::Reply) ||
        statement.a.empty())
      throw std::runtime_error(
          "MOSS_INTERNAL_INVARIANT checked return-value identity unavailable");
    const est::Handle* handle = nullptr;
    for (const auto& candidate : statement.expression_handles) {
      if (candidate.slot != est::ExprSlot::ReturnValue ||
          candidate.argument_index != 0) continue;
      if (handle)
        throw std::runtime_error(
            "MOSS_INTERNAL_INVARIANT duplicate checked return-value handle");
      handle = &candidate;
    }
    if (!handle || handle->arena_index >= checked_program.expression_arenas.size() ||
        !checked_program.expression_arenas[handle->arena_index])
      throw std::runtime_error(
          "MOSS_INTERNAL_INVARIANT checked return-value EST handle missing");
    if (codegen_body_spec_id_ == InvalidSpecId ||
        codegen_body_spec_id_ >= checked_program.specialization_table.size() ||
        checked_program.specialization_table[codegen_body_spec_id_].id !=
            codegen_body_spec_id_)
      throw std::runtime_error(
          "MOSS_INTERNAL_INVARIANT ReturnValue owner SpecId is missing or contradictory");
    const auto& owner_identity =
        checked_program.specialization_table[codegen_body_spec_id_];
#ifdef MOSS_TEST_BUILD
    if (std::getenv("MOSS_TEST_CONSUMER_WRONG_RETURN_VALUE_SPECIALIZATION"))
      throw std::runtime_error(
          "MOSS_INTERNAL_INVARIANT ReturnValue SpecId was perturbed");
#endif
    auto fact_is_from_exact_imported_owner =
        [&](const TypedReturnValueFactV1& candidate) {
      return candidate.specialization_id == codegen_body_spec_id_ &&
          !owner_identity.provider_module_identity.empty() &&
          candidate.provenance.defining_module_id ==
              owner_identity.provider_module_identity &&
          !statement.source_file.empty() &&
          candidate.provenance.source_file == statement.source_file;
    };
    const auto occurrence_span =
        checked_program.expression_arenas[handle->arena_index]->span(handle->expr);
    const TypedReturnValueRequirementV1* requirement = nullptr;
    size_t requirement_count = 0;
    size_t fact_count = 0;
    for (const auto& candidate :
         typed_semantics->required_return_value_occurrences) {
      if ((statement.checked_statement_occurrence_id == UINT32_MAX ||
           candidate.statement_occurrence_id ==
              statement.checked_statement_occurrence_id) &&
          candidate.est_arena_index == handle->arena_index &&
          candidate.expression_occurrence_id == handle->expr &&
          (candidate.source_free_checked_body_occurrence ||
           (candidate.expression_occurrence_span.source_begin ==
                occurrence_span.source_begin &&
            candidate.expression_occurrence_span.source_end ==
                occurrence_span.source_end &&
            candidate.expression_occurrence_span.line == occurrence_span.line &&
            candidate.expression_occurrence_span.column == occurrence_span.column &&
            candidate.expression_occurrence_span.origin_id ==
                occurrence_span.origin_id)) &&
          candidate.provenance.line == statement.line &&
          (statement.source_file.empty() ||
           candidate.provenance.source_file == statement.source_file) &&
          candidate.specialization_id == codegen_body_spec_id_) {
        requirement = &candidate;
        ++requirement_count;
      }
    }
    for (const auto& candidate : typed_semantics->return_value_facts) {
      if ((statement.checked_statement_occurrence_id == UINT32_MAX ||
           candidate.statement_occurrence_id ==
              statement.checked_statement_occurrence_id) &&
          candidate.est_arena_index == handle->arena_index &&
          candidate.expression_occurrence_id == handle->expr &&
          (candidate.source_free_checked_body_occurrence ||
           (candidate.expression_occurrence_span.source_begin ==
                occurrence_span.source_begin &&
            candidate.expression_occurrence_span.source_end ==
                occurrence_span.source_end &&
            candidate.expression_occurrence_span.line == occurrence_span.line &&
            candidate.expression_occurrence_span.column == occurrence_span.column &&
            candidate.expression_occurrence_span.origin_id ==
                occurrence_span.origin_id)) &&
          candidate.provenance.line == statement.line &&
          (statement.source_file.empty() ||
           candidate.provenance.source_file == statement.source_file) &&
          candidate.specialization_id == codegen_body_spec_id_)
        ++fact_count;
    }
#ifdef MOSS_TEST_BUILD
    if (std::getenv("MOSS_TEST_CONSUMER_DROP_RETURN_VALUE_REQUIREMENT"))
      --requirement_count;
    if (std::getenv("MOSS_TEST_CONSUMER_DUPLICATE_RETURN_VALUE_REQUIREMENT"))
      ++requirement_count;
    if (std::getenv("MOSS_TEST_CONSUMER_DROP_RETURN_VALUE_FACT")) fact_count = 0;
    if (std::getenv("MOSS_TEST_CONSUMER_DUPLICATE_RETURN_VALUE_FACT")) ++fact_count;
#endif
    if (requirement_count != 1 || fact_count != 1 || !requirement)
      throw std::runtime_error(
          "MOSS_INTERNAL_INVARIANT return-value requirement/fact missing or ambiguous");
    try {
#ifdef MOSS_TEST_BUILD
      if (std::getenv("MOSS_TEST_CONSUMER_PERTURB_RETURN_VALUE_FACT")) {
        auto corrupted = *requirement;
        corrupted.required_access = corrupted.required_access == Effect::Consume
            ? Effect::Read : Effect::Consume;
        (void)typed_semantics->find_return_value_fact(corrupted);
        throw std::runtime_error("perturbed return-value fact unexpectedly passed");
      }
      if (std::getenv(
              "MOSS_TEST_CONSUMER_PERTURB_SOURCE_FREE_RETURN_BODY_HASH")) {
        auto corrupted = *requirement;
        corrupted.checked_body_semantic_hash += ":wrong-body";
        (void)typed_semantics->find_return_value_fact(corrupted);
        throw std::runtime_error(
            "perturbed source-free ReturnValue body identity unexpectedly passed");
      }
      if (std::getenv(
              "MOSS_TEST_CONSUMER_PERTURB_SOURCE_FREE_RETURN_STATEMENT")) {
        auto corrupted = *requirement;
        ++corrupted.checked_body_statement_ordinal;
        (void)typed_semantics->find_return_value_fact(corrupted);
        throw std::runtime_error(
            "perturbed source-free ReturnValue statement ordinal unexpectedly passed");
      }
      if (std::getenv(
              "MOSS_TEST_CONSUMER_PERTURB_SOURCE_FREE_RETURN_FINGERPRINT")) {
        auto corrupted = *requirement;
        ++corrupted.expression_source_fingerprint;
        (void)typed_semantics->find_return_value_fact(corrupted);
        throw std::runtime_error(
            "perturbed source-free ReturnValue fingerprint unexpectedly passed");
      }
      if (requirement->source_free_checked_body_occurrence && std::getenv(
              "MOSS_TEST_CONSUMER_PERTURB_SOURCE_FREE_RETURN_EST_CONTEXT")) {
        auto corrupted = *requirement;
        corrupted.specialization_id = InvalidSpecId;
        (void)typed_semantics->find_return_value_fact(corrupted);
        throw std::runtime_error(
            "perturbed source-free ReturnValue EST context unexpectedly passed");
      }

      if (std::getenv("MOSS_TEST_CONSUMER_PERTURB_IMPORTED_RETURN_MODULE_ID")) {
        auto corrupted = *requirement;
        corrupted.provenance.defining_module_id += ":wrong-module";
        (void)typed_semantics->find_return_value_fact(corrupted);
        throw std::runtime_error(
            "perturbed imported ReturnValue module authority unexpectedly passed");
      }
#endif
      const auto* fact = typed_semantics->find_return_value_fact(*requirement);
      if (!fact || !fact->is_authoritative())
        throw std::invalid_argument("return-value fact is not authoritative");
      if (fact->source_free_checked_body_occurrence) {
        const Function* owner = nullptr;
        std::size_t owner_count = 0;
        for (const auto& candidate : checked_program.functions) {
          bool owns_spec = candidate.spec_id == codegen_body_spec_id_;
          for (const auto& specialization : candidate.specializations)
            owns_spec = owns_spec ||
                specialization.spec_id == codegen_body_spec_id_;
          if (owns_spec) {
            owner = &candidate;
            ++owner_count;
          }
        }
        const bool source_free_occurrence_matches = owner && handle
            ? typed_return_value_source_free_occurrence_matches(
                  *fact, *owner, statement, *handle)
            : false;
        if (owner_count != 1 || !owner ||
            !owner->imported_checked_body_projected ||
            owner_identity.provider_module_identity.empty() || !handle ||
            fact->provenance.defining_module_id !=
                owner_identity.provider_module_identity ||
            !source_free_occurrence_matches)
          throw std::invalid_argument(
              "source-free ReturnValue fact does not bind its exact checked body occurrence");
      }
      const auto& arena = *checked_program.expression_arenas[handle->arena_index];
      if (handle->expr >= arena.size() || !fact->expression_type ||
          !fact->expression_type->is_closed())
        throw std::invalid_argument(
            "return-value fact lacks a closed type for its exact specialized EST occurrence");
      const auto* occurrence = arena.facts(handle->expr,
                                            fact->specialization_id);
      if (!occurrence || !occurrence->type) {
        if (!fact_is_from_exact_imported_owner(*fact))
          throw std::invalid_argument(
              "return-value EST type fact missing outside exact imported owner");
      } else if (occurrence->type->is_closed()
              ? !occurrence->type->structurally_equal(*fact->expression_type)
              : occurrence->type->kind != TypeKind::InferenceVar) {
        throw std::invalid_argument(
            "ReturnValue fact does not bind a concrete or generic EST type at its checked context (" +
            std::to_string(fact->specialization_id) + ":" +
            occurrence->type->to_canonical_string() + " != " +
            fact->expression_type->to_canonical_string() + ")");
      }
      return fact;
    } catch (const std::exception& error) {
      throw std::runtime_error(
          "MOSS_INTERNAL_INVARIANT " + string(error.what()));
    }
  }

  const TypedCollectionViewFactV1* resolve_collection_view_fact(
      TypedCollectionViewOperationV1 operation,
      const string& receiver_expression) const {
    if (!p_.typed_semantics || !phase21_codegen_statement_) return nullptr;
    const Stmt& statement = *phase21_codegen_statement_;
    if (statement.checked_statement_occurrence_id == UINT32_MAX)
      throw std::runtime_error(
          "MOSS_INTERNAL_INVARIANT checked collection-view statement identity unavailable");
    const string method_name = operation == TypedCollectionViewOperationV1::MapKeys
        ? "keys" : "values";
    const auto source_for_handle = [&](const est::Handle& handle)
        -> std::optional<string> {
      switch (handle.slot) {
        case est::ExprSlot::Initializer:
        case est::ExprSlot::RhsValue:
        case est::ExprSlot::Iterable:
          return statement.b;
        case est::ExprSlot::ReturnValue:
        case est::ExprSlot::Condition:
        case est::ExprSlot::Scrutinee:
        case est::ExprSlot::RaisedValue:
          return statement.a;
        case est::ExprSlot::Argument:
          if (handle.argument_index < statement.args.size())
            return statement.args[handle.argument_index];
          return std::nullopt;
        case est::ExprSlot::Expression:
          return statement.text;
        default:
          return std::nullopt;
      }
    };
    const auto matches_receiver = [&](const TypedCollectionViewFactV1& candidate) {
      if (candidate.operation != operation ||
          candidate.statement_occurrence_id !=
              statement.checked_statement_occurrence_id ||
          candidate.specialization_id != codegen_body_spec_id_ ||
          candidate.specialization_id != codegen_body_spec_id_ ||
          candidate.est_arena_index >= p_.expression_arenas.size() ||
          !p_.expression_arenas[candidate.est_arena_index])
        return false;
      const auto& arena = *p_.expression_arenas[candidate.est_arena_index];
      if (candidate.operation_expression_occurrence_id >= arena.size())
        throw std::runtime_error(
            "MOSS_INTERNAL_INVARIANT collection-view operation ExprId is invalid");
      const auto* call = std::get_if<est::Call>(
          &arena.at(candidate.operation_expression_occurrence_id));
      const auto* member = call && call->arguments.empty()
          ? std::get_if<est::Member>(&arena.at(call->callee)) : nullptr;
      if (!member || member->name != method_name) return false;
      const auto receiver_span = arena.span(member->base);
      for (const auto& handle : statement.expression_handles) {
        if (handle.arena_index != candidate.est_arena_index) continue;
        auto source = source_for_handle(handle);
        if (!source || receiver_span.source_end > source->size() ||
            receiver_span.source_begin > receiver_span.source_end) continue;
        std::vector<est::ExprId> pending{handle.expr};
        std::set<est::ExprId> reachable;
        while (!pending.empty()) {
          const auto current = pending.back();
          pending.pop_back();
          if (current >= arena.size() || !reachable.insert(current).second)
            continue;
          const auto& node = arena.at(current);
          std::visit([&](const auto& value) {
            using T = std::decay_t<decltype(value)>;
            if constexpr (std::is_same_v<T, est::Call>) {
              pending.push_back(value.callee);
              pending.insert(pending.end(), value.arguments.begin(), value.arguments.end());
            } else if constexpr (std::is_same_v<T, est::Member>) pending.push_back(value.base);
            else if constexpr (std::is_same_v<T, est::Index>) {
              pending.push_back(value.base); pending.push_back(value.index);
            } else if constexpr (std::is_same_v<T, est::Parenthesized>) pending.push_back(value.inner);
            else if constexpr (std::is_same_v<T, est::Unary>) pending.push_back(value.operand);
            else if constexpr (std::is_same_v<T, est::Binary>) {
              pending.push_back(value.left); pending.push_back(value.right);
            } else if constexpr (std::is_same_v<T, est::Pipeline>) {
              pending.push_back(value.input);
              pending.insert(pending.end(), value.stages.begin(), value.stages.end());
            }
          }, node);
        }
        if (!reachable.count(candidate.operation_expression_occurrence_id))
          continue;
        const string exact_receiver = trim(source->substr(
            receiver_span.source_begin,
            receiver_span.source_end - receiver_span.source_begin));
        if (exact_receiver == trim(receiver_expression)) return true;
      }
      return false;
    };
    const TypedCollectionViewRequirementV1* requirement = nullptr;
    size_t requirement_count = 0;
    size_t fact_count = 0;
    for (const auto& candidate :
         p_.typed_semantics->required_collection_view_occurrences) {
      if (matches_receiver(candidate)) {
        requirement = &candidate;
        ++requirement_count;
      }
    }
    for (const auto& candidate : p_.typed_semantics->collection_view_facts)
      if (matches_receiver(candidate))
        ++fact_count;
#ifdef MOSS_TEST_BUILD
    if (std::getenv("MOSS_TEST_CONSUMER_DROP_COLLECTION_VIEW_REQUIREMENT"))
      --requirement_count;
    if (std::getenv("MOSS_TEST_CONSUMER_DUPLICATE_COLLECTION_VIEW_REQUIREMENT"))
      ++requirement_count;
    if (std::getenv("MOSS_TEST_CONSUMER_DROP_COLLECTION_VIEW_FACT")) fact_count = 0;
    if (std::getenv("MOSS_TEST_CONSUMER_DUPLICATE_COLLECTION_VIEW_FACT")) ++fact_count;
#endif
    if (requirement_count == 0 && fact_count == 0) return nullptr;
    if (requirement_count != 1 || fact_count != 1 || !requirement)
      throw std::runtime_error(
          "MOSS_INTERNAL_INVARIANT collection-view requirement/fact missing or ambiguous");
    try {
#ifdef MOSS_TEST_BUILD
      if (std::getenv("MOSS_TEST_CONSUMER_PERTURB_COLLECTION_VIEW_FACT")) {
        auto corrupted = *requirement;
        corrupted.required_access = Effect::Consume;
        (void)p_.typed_semantics->find_collection_view_fact(corrupted);
        throw std::runtime_error(
            "perturbed collection-view authority unexpectedly passed");
      }
#endif
      const auto* fact =
          p_.typed_semantics->find_collection_view_fact(*requirement);
      if (!fact || !fact->is_authoritative() ||
          fact->receiver_binding_identity.empty() ||
          fact->receiver_definition_identity.empty() ||
          (fact->direct_temporary &&
           fact->last_use_statement_occurrence_id !=
               fact->statement_occurrence_id) ||
          (!fact->direct_temporary &&
           (fact->view_binding_identity.empty() ||
            fact->view_definition_identity.empty())))
        throw std::invalid_argument("collection-view fact is not authoritative");
      return fact;
    } catch (const std::exception& error) {
      throw std::runtime_error(
          "MOSS_INTERNAL_INVARIANT " + string(error.what()));
    }
  }

  string root_ingress_expression_source(
      const Stmt& statement, est::ExprSlot slot,
      std::uint32_t argument_index) const {
    switch (slot) {
      case est::ExprSlot::Argument:
        if (argument_index < statement.args.size())
          return statement.args[argument_index];
        break;
      case est::ExprSlot::Initializer:
      case est::ExprSlot::RhsValue:
      case est::ExprSlot::Iterable:
        return statement.b;
      case est::ExprSlot::Condition:
      case est::ExprSlot::ReturnValue:
      case est::ExprSlot::Scrutinee:
      case est::ExprSlot::RaisedValue:
        return statement.a;
      case est::ExprSlot::Expression:
        return statement.text;
      case est::ExprSlot::LhsPlace:
        return statement.a;
      case est::ExprSlot::Receiver:
        if (statement.kind == Stmt::Kind::Message) {
          std::ostringstream source;
          source << "message " << statement.a << "." << statement.b << "(";
          for (std::size_t index = 0; index < statement.args.size(); ++index) {
            if (index) source << ", ";
            source << statement.args[index];
          }
          source << ")";
          return source.str();
        }
        break;
      case est::ExprSlot::Pattern:
        return statement.a;
    }
    throw std::runtime_error(
        "MOSS_INTERNAL_INVARIANT Root-ingress expression slot has no exact source");
  }

  std::optional<RootIngressExpressionOccurrence>
  root_ingress_expression_occurrence(const Stmt& statement,
                                     est::ExprSlot slot,
                                     std::uint32_t argument_index) const {
    const Program& checked_program = semantic_program_ ? *semantic_program_ : p_;
    const est::Handle* selected = nullptr;
    for (const auto& handle : statement.expression_handles) {
      if (handle.slot != slot || handle.argument_index != argument_index) continue;
      if (selected)
        throw std::runtime_error(
            "MOSS_INTERNAL_INVARIANT duplicate active Root-ingress message handle");
      selected = &handle;
    }
    if (!selected || selected->arena_index >=
            checked_program.expression_arenas.size() ||
        !checked_program.expression_arenas[selected->arena_index])
      return std::nullopt;
    const auto& arena = *checked_program.expression_arenas[selected->arena_index];
    if (selected->expr >= arena.size())
      throw std::runtime_error(
          "MOSS_INTERNAL_INVARIANT active Root-ingress expression handle is invalid");
    string source = root_ingress_expression_source(statement, slot,
                                                  argument_index);
    const auto span = arena.span(selected->expr);
    if (span.source_begin != 0 || span.source_end != source.size() ||
        selected->source_fingerprint != est::fingerprint(source))
      throw std::runtime_error(
          "MOSS_INTERNAL_INVARIANT Root-ingress expression handle/source mismatch");
    return RootIngressExpressionOccurrence{
        selected->arena_index, selected->expr,
        selected->source_fingerprint, selected->origin_id,
        std::move(source)};
  }

  RootIngressMessageQueue root_ingress_message_queue(
      const RootIngressExpressionOccurrence& expression) const {
    const Program& checked_program = semantic_program_ ? *semantic_program_ : p_;
    if (expression.arena_index >= checked_program.expression_arenas.size() ||
        !checked_program.expression_arenas[expression.arena_index])
      throw std::runtime_error(
          "MOSS_INTERNAL_INVARIANT Root-ingress expression arena unavailable");
    const auto& arena = *checked_program.expression_arenas[expression.arena_index];
    if (expression.expression >= arena.size())
      throw std::runtime_error(
          "MOSS_INTERNAL_INVARIANT Root-ingress expression ExprId unavailable");
    RootIngressMessageQueue queue;
    std::function<void(est::ExprId)> visit = [&](est::ExprId id) {
      if (id >= arena.size())
        throw std::runtime_error(
            "MOSS_INTERNAL_INVARIANT Root-ingress expression child ExprId invalid");
      const auto& node = arena.at(id);
      if (const auto* message = std::get_if<est::Message>(&node)) {
        const auto span = arena.span(id);
        if (span.source_begin > span.source_end ||
            span.source_end > expression.source.size())
          throw std::runtime_error(
              "MOSS_INTERNAL_INVARIANT Root-ingress message span is outside its source handle");
        auto arguments = message->arguments;
        for (auto& argument : arguments)
          argument = est::unparenthesized(arena, argument);
        queue.messages.push_back({
            RootIngressMessageOccurrence{
                expression.arena_index, expression.expression, id,
                message->target,
                expression.source_fingerprint, expression.origin_id,
                std::move(arguments)},
            expression.source.substr(span.source_begin,
                                     span.source_end - span.source_begin),
            span.source_begin, span.source_end});
      }
      std::visit([&](const auto& value) {
        using T = std::decay_t<decltype(value)>;
        auto child = [&](est::ExprId expression_id) { visit(expression_id); };
        auto children = [&](const auto& expressions) {
          for (const auto expression_id : expressions) child(expression_id);
        };
        if constexpr (std::is_same_v<T, est::Unary>) {
          child(value.operand);
        } else if constexpr (std::is_same_v<T, est::Binary>) {
          child(value.left); child(value.right);
        } else if constexpr (std::is_same_v<T, est::Call>) {
          child(value.callee); children(value.arguments);
        } else if constexpr (std::is_same_v<T, est::Take>) {
          children(value.arguments);
        } else if constexpr (std::is_same_v<T, est::Member>) {
          child(value.base);
        } else if constexpr (std::is_same_v<T, est::Index>) {
          child(value.base); child(value.index);
        } else if constexpr (std::is_same_v<T, est::Parenthesized>) {
          child(value.inner);
        } else if constexpr (std::is_same_v<T, est::Message>) {
          child(value.target); children(value.arguments);
        } else if constexpr (std::is_same_v<T, est::NamedArgument>) {
          child(value.value);
        } else if constexpr (std::is_same_v<T, est::Collection> ||
                             std::is_same_v<T, est::Tuple>) {
          children(value.elements);
        } else if constexpr (std::is_same_v<T, est::Constructor>) {
          child(value.type); children(value.type_arguments);
          children(value.arguments);
        } else if constexpr (std::is_same_v<T, est::Pipeline>) {
          child(value.input); children(value.stages);
        }
      }, node);
    };
    visit(expression.expression);
    // Preserve EST preorder: Native consumes a Message occurrence when it
    // enters that node, then recursively lowers its arguments. Parent and
    // child Message spans intentionally overlap, so source-span sorting is
    // not an occurrence order.
    std::set<std::pair<std::uint32_t, est::ExprId>> seen_occurrences;
    for (const auto& queued : queue.messages)
      if (!seen_occurrences.emplace(queued.occurrence.arena_index,
                                    queued.occurrence.message_expression).second)
        throw std::runtime_error(
            "MOSS_INTERNAL_INVARIANT duplicate Root-ingress Message ExprId in checked EST tree");
    return queue;
  }

  RootIngressMessageOccurrence consume_root_ingress_expression_occurrence(
      const string& source) const {
    if (!active_root_ingress_message_queue_ ||
        active_root_ingress_message_queue_->next >=
            active_root_ingress_message_queue_->messages.size())
      throw std::runtime_error(
          "MOSS_INTERNAL_INVARIANT active Root-ingress message ExprId unavailable");
    const auto& queued = active_root_ingress_message_queue_->messages[
        active_root_ingress_message_queue_->next++];
    if (trim(source) != trim(queued.source))
      throw std::runtime_error(
          "MOSS_INTERNAL_INVARIANT Root-ingress Native traversal disagrees with checked EST source order");
    auto occurrence = queued.occurrence;
#ifdef MOSS_TEST_BUILD
    if (std::getenv(
            "MOSS_TEST_CONSUMER_PERTURB_ROOT_INGRESS_NESTED_MESSAGE_EXPR_ID") &&
        active_root_ingress_message_queue_->next == 2)
      ++occurrence.message_expression;
#endif
    return occurrence;
  }

  std::size_t root_ingress_owner_handle_count(
      const Program& checked_program, const Stmt& statement,
      const RootIngressMessageOccurrence& occurrence,
      est::ExprId owner_expression) const {
    return std::count_if(statement.expression_handles.begin(),
        statement.expression_handles.end(), [&](const est::Handle& handle) {
          return handle.arena_index == occurrence.arena_index &&
              handle.expr == owner_expression &&
              handle.source_fingerprint == occurrence.source_fingerprint &&
              handle.origin_id == occurrence.origin_id &&
              handle.arena_index < checked_program.expression_arenas.size() &&
              checked_program.expression_arenas[handle.arena_index] &&
              handle.expr < checked_program.expression_arenas[handle.arena_index]->size();
        });
  }

  const TypedCopyFactV1* resolve_root_ingress_copy_fact(
      const RootIngressMessageOccurrence& occurrence,
      std::uint32_t argument_index) const {
    const Program& checked_program = semantic_program_ ? *semantic_program_ : p_;
    const Stmt* statement = phase21_codegen_statement_;
    if (!checked_program.typed_semantics || !statement ||
        statement->checked_statement_occurrence_id == UINT32_MAX ||
        argument_index >= occurrence.argument_expressions.size() ||
        occurrence.arena_index >= checked_program.expression_arenas.size() ||
        !checked_program.expression_arenas[occurrence.arena_index])
      throw std::runtime_error(
          "MOSS_INTERNAL_INVARIANT Root-ingress copy query identity unavailable");
    const auto& arena = *checked_program.expression_arenas[occurrence.arena_index];
    auto copy_id = est::unparenthesized(
        arena, occurrence.argument_expressions[argument_index]);
#ifdef MOSS_TEST_BUILD
    if (std::getenv(
            "MOSS_TEST_CONSUMER_PERTURB_ROOT_INGRESS_COPY_EXPR_ID"))
      ++copy_id;
#endif
    if (copy_id >= arena.size())
      throw std::runtime_error(
          "MOSS_INTERNAL_INVARIANT Root-ingress copy EST occurrence unavailable");
    if (occurrence.message_expression >= arena.size() ||
        occurrence.source_fingerprint == 0 ||
        occurrence.owner_expression >= arena.size())
      throw std::runtime_error(
          "MOSS_INTERNAL_INVARIANT Root-ingress copy message identity unavailable");
    est::ExprId owner_expression = occurrence.owner_expression;
#ifdef MOSS_TEST_BUILD
    if (std::getenv(
            "MOSS_TEST_CONSUMER_PERTURB_ROOT_INGRESS_COPY_OWNER"))
      ++owner_expression;
#endif
    if (root_ingress_owner_handle_count(checked_program, *statement,
                                        occurrence, owner_expression) != 1)
      throw std::runtime_error(
          "MOSS_INTERNAL_INVARIANT Root-ingress copy lacks its exact statement source handle");
    const auto* message = std::get_if<est::Message>(
        &arena.at(occurrence.message_expression));
    if (!message || argument_index >= message->arguments.size() ||
        est::unparenthesized(arena, message->arguments[argument_index]) != copy_id)
      throw std::runtime_error(
          "MOSS_INTERNAL_INVARIANT Root-ingress copy does not match exact message argument");
    const auto* call = std::get_if<est::Call>(&arena.at(copy_id));
    if (!call || call->arguments.size() != 1) return nullptr;
    const auto* expression_facts = arena.facts(copy_id, codegen_body_spec_id_);
    if (!expression_facts ||
        expression_facts->resolved_target != "builtin:copy-v1")
      return nullptr;
    try {
      const auto& requirement = checked_program.typed_semantics
          ->find_copy_requirement(occurrence.arena_index, copy_id,
                                  codegen_body_spec_id_);
      if (requirement.statement_occurrence_id !=
              statement->checked_statement_occurrence_id ||
          requirement.copy_occurrence_id != copy_id ||
          requirement.est_arena_index != occurrence.arena_index)
        throw std::invalid_argument(
            "Root-ingress explicit copy does not match its exact statement and argument occurrence");
      const auto* fact = checked_program.typed_semantics
          ->find_copy_fact(requirement);
      const auto source_id = est::unparenthesized(
          arena, call->arguments.front());
      const auto copy_span = arena.span(copy_id);
      const auto source_span = arena.span(source_id);
      const auto* copy_expression_facts = arena.facts(copy_id, codegen_body_spec_id_);
      const auto* source_expression_facts = arena.facts(source_id, codegen_body_spec_id_);
      auto same_span = [](const est::SourceSpan& left,
                          const est::SourceSpan& right) {
        return left.line == right.line && left.column == right.column &&
            left.origin_id == right.origin_id &&
            left.source_begin == right.source_begin &&
            left.source_end == right.source_end;
      };
      if (!fact || fact->source_occurrence_id != source_id ||
          fact->statement_occurrence_id !=
              statement->checked_statement_occurrence_id ||
          (!statement->source_file.empty() &&
           fact->provenance.source_file != statement->source_file) ||
          fact->provenance.line != statement->line ||
          !same_span(fact->copy_occurrence_span, copy_span) ||
          !same_span(fact->source_occurrence_span, source_span) ||
          !copy_expression_facts || !copy_expression_facts->type ||
          !fact->result_type ||
          !copy_expression_facts->type->structurally_equal(*fact->result_type) ||
          !source_expression_facts || !source_expression_facts->type ||
          !fact->source_type ||
          !source_expression_facts->type->structurally_equal(*fact->source_type))
        throw std::invalid_argument(
            "Root-ingress explicit copy source does not match its exact EST child");
      return fact;
    } catch (const std::exception& error) {
      throw std::runtime_error(
          "MOSS_INTERNAL_INVARIANT " + string(error.what()));
    }
  }

  const TypedRootIngressPayloadFactV1& resolve_root_ingress_payload_fact(
      const RootIngressMessageOccurrence& occurrence,
      std::uint32_t argument_index, const Domain& domain,
      const Handler& handler,
      const string& expected_type) const {
    const Program& checked_program = semantic_program_ ? *semantic_program_ : p_;
    const Stmt* statement = phase21_codegen_statement_;
    if (!checked_program.typed_semantics || !statement ||
        statement->checked_statement_occurrence_id == UINT32_MAX ||
        argument_index >= occurrence.argument_expressions.size() ||
        occurrence.arena_index >= checked_program.expression_arenas.size() ||
        !checked_program.expression_arenas[occurrence.arena_index])
      throw std::runtime_error(
          "MOSS_INTERNAL_INVARIANT Root-ingress payload query identity unavailable");
    if (root_ingress_owner_handle_count(checked_program, *statement,
                                        occurrence,
                                        occurrence.owner_expression) != 1)
      throw std::runtime_error(
          "MOSS_INTERNAL_INVARIANT Root-ingress payload owner handle missing or ambiguous");
    const auto& arena = *checked_program.expression_arenas[occurrence.arena_index];
    const auto argument_id = est::unparenthesized(
        arena, occurrence.argument_expressions[argument_index]);
    if (argument_id >= arena.size() ||
        (!std::holds_alternative<est::Constructor>(arena.at(argument_id)) &&
         !std::holds_alternative<est::Literal>(arena.at(argument_id))))
      throw std::runtime_error(
          "MOSS_INTERNAL_INVARIANT fresh Root-ingress payload expression identity unavailable");
    const TypedRootIngressPayloadRequirementV1* requirement = nullptr;
    std::size_t requirement_count = 0;
    std::size_t fact_count = 0;
    for (const auto& candidate : checked_program.typed_semantics
             ->required_root_ingress_payload_occurrences)
      if (candidate.statement_occurrence_id ==
              statement->checked_statement_occurrence_id &&
          candidate.est_arena_index == occurrence.arena_index &&
          candidate.message_expression_occurrence_id ==
              occurrence.message_expression &&
          candidate.argument_index == argument_index &&
          candidate.argument_expression_occurrence_id == argument_id &&
          candidate.source_fingerprint == occurrence.source_fingerprint &&
          candidate.specialization_id == codegen_body_spec_id_) {
        requirement = &candidate;
        ++requirement_count;
      }
    for (const auto& candidate : checked_program.typed_semantics
             ->root_ingress_payload_facts)
      if (candidate.statement_occurrence_id ==
              statement->checked_statement_occurrence_id &&
          candidate.est_arena_index == occurrence.arena_index &&
          candidate.message_expression_occurrence_id ==
              occurrence.message_expression &&
          candidate.argument_index == argument_index &&
          candidate.argument_expression_occurrence_id == argument_id &&
          candidate.source_fingerprint == occurrence.source_fingerprint &&
          candidate.specialization_id == codegen_body_spec_id_)
        ++fact_count;
#ifdef MOSS_TEST_BUILD
    if (std::getenv("MOSS_TEST_CONSUMER_DROP_ROOT_INGRESS_PAYLOAD_REQUIREMENT"))
      --requirement_count;
    if (std::getenv("MOSS_TEST_CONSUMER_DUPLICATE_ROOT_INGRESS_PAYLOAD_REQUIREMENT"))
      ++requirement_count;
    if (std::getenv("MOSS_TEST_CONSUMER_DROP_ROOT_INGRESS_PAYLOAD_FACT"))
      fact_count = 0;
    if (std::getenv("MOSS_TEST_CONSUMER_DUPLICATE_ROOT_INGRESS_PAYLOAD_FACT"))
      ++fact_count;
#endif
    if (requirement_count != 1 || fact_count != 1 || !requirement)
      throw std::runtime_error(
          "MOSS_INTERNAL_INVARIANT Root-ingress payload requirement/fact missing or ambiguous");
    try {
#ifdef MOSS_TEST_BUILD
      if (std::getenv("MOSS_TEST_CONSUMER_PERTURB_ROOT_INGRESS_PAYLOAD_FACT")) {
        auto corrupted = *requirement;
        const string field = std::getenv(
            "MOSS_TEST_CONSUMER_PERTURB_ROOT_INGRESS_PAYLOAD_FACT");
        if (field == "owner") corrupted.selected_handler_identity += ":wrong-owner";
        else if (field == "argument") ++corrupted.argument_index;
        else if (field == "type") corrupted.payload_type.reset();
        else if (field == "action")
          corrupted.action = static_cast<TypedRootIngressPayloadActionV1>(99);
        else if (field == "context") ++corrupted.specialization_id;
        else if (field == "occurrence") ++corrupted.argument_expression_occurrence_id;
        (void)checked_program.typed_semantics
            ->find_root_ingress_payload_fact(corrupted);
        throw std::runtime_error(
            "perturbed Root-ingress payload fact unexpectedly passed");
      }
#endif
      const auto& fact = checked_program.typed_semantics
          ->find_root_ingress_payload_fact(*requirement);
      const auto actual_type = arena.facts(argument_id,
          fact.specialization_id);
      const auto expected = moss::Type::parse_canonical(expected_type);
      if (!actual_type || !actual_type->type || !expected || !*expected ||
          !actual_type->type->structurally_equal(*fact.payload_type) ||
          !fact.payload_type->structurally_equal(**expected) ||
          fact.selected_handler_identity !=
              handler_semantic_identity(domain, handler) ||
          fact.action !=
              TypedRootIngressPayloadActionV1::TransferOwnedFreshRvalue ||
          fact.permits_moss_value_copy || !fact.checker_proved_fresh_rvalue)
        throw std::invalid_argument(
            "Root-ingress payload authority does not match exact handler, argument, type, and action");
      return fact;
    } catch (const std::exception& error) {
      throw std::runtime_error("MOSS_INTERNAL_INVARIANT " + string(error.what()));
    }
  }

  const TypedRootIngressArgumentDispositionFactV1*
  resolve_root_ingress_argument_disposition(
      const RootIngressMessageOccurrence& occurrence,
      std::uint32_t argument_index, const string& expected_type) const {
    const Program& checked_program = semantic_program_ ? *semantic_program_ : p_;
    const Stmt* statement = phase21_codegen_statement_;
    if (!checked_program.typed_semantics || !statement ||
        statement->checked_statement_occurrence_id == UINT32_MAX ||
        argument_index >= occurrence.argument_expressions.size() ||
        occurrence.arena_index >= checked_program.expression_arenas.size() ||
        !checked_program.expression_arenas[occurrence.arena_index])
      throw std::runtime_error(
          "MOSS_INTERNAL_INVARIANT Root-ingress argument disposition query identity unavailable");
    const auto& arena = *checked_program.expression_arenas[occurrence.arena_index];
    const auto argument_id = est::unparenthesized(
        arena, occurrence.argument_expressions[argument_index]);
    if (argument_id >= arena.size() ||
        !std::holds_alternative<est::Identifier>(arena.at(argument_id)))
      return nullptr;
    if (root_ingress_owner_handle_count(checked_program, *statement,
                                        occurrence,
                                        occurrence.owner_expression) != 1)
      throw std::runtime_error(
          "MOSS_INTERNAL_INVARIANT Root-ingress argument disposition owner handle missing or ambiguous");
    const TypedRootIngressArgumentDispositionRequirementV1* requirement = nullptr;
    std::size_t requirement_count = 0;
    std::size_t fact_count = 0;
    for (const auto& candidate : checked_program.typed_semantics
             ->required_root_ingress_argument_disposition_occurrences)
      if (candidate.statement_occurrence_id ==
              statement->checked_statement_occurrence_id &&
          candidate.est_arena_index == occurrence.arena_index &&
          candidate.owner_expression_occurrence_id ==
              occurrence.owner_expression &&
          candidate.message_expression_occurrence_id ==
              occurrence.message_expression &&
          candidate.argument_index == argument_index &&
          candidate.argument_expression_occurrence_id == argument_id &&
          candidate.source_fingerprint == occurrence.source_fingerprint &&
          candidate.specialization_id == codegen_body_spec_id_) {
        requirement = &candidate;
        ++requirement_count;
      }
    for (const auto& candidate : checked_program.typed_semantics
             ->root_ingress_argument_disposition_facts)
      if (candidate.statement_occurrence_id ==
              statement->checked_statement_occurrence_id &&
          candidate.est_arena_index == occurrence.arena_index &&
          candidate.owner_expression_occurrence_id ==
              occurrence.owner_expression &&
          candidate.message_expression_occurrence_id ==
              occurrence.message_expression &&
          candidate.argument_index == argument_index &&
          candidate.argument_expression_occurrence_id == argument_id &&
          candidate.source_fingerprint == occurrence.source_fingerprint &&
          candidate.specialization_id == codegen_body_spec_id_)
        ++fact_count;
#ifdef MOSS_TEST_BUILD
    if (std::getenv("MOSS_TEST_CONSUMER_DROP_ROOT_INGRESS_ARGUMENT_DISPOSITION_REQUIREMENT"))
      --requirement_count;
    if (std::getenv("MOSS_TEST_CONSUMER_DUPLICATE_ROOT_INGRESS_ARGUMENT_DISPOSITION_REQUIREMENT"))
      ++requirement_count;
    if (std::getenv("MOSS_TEST_CONSUMER_DROP_ROOT_INGRESS_ARGUMENT_DISPOSITION_FACT"))
      fact_count = 0;
    if (std::getenv("MOSS_TEST_CONSUMER_DUPLICATE_ROOT_INGRESS_ARGUMENT_DISPOSITION_FACT"))
      ++fact_count;
#endif
    if (requirement_count == 0 && fact_count == 0 && !requirement)
      return nullptr;
    if (requirement_count != 1 || fact_count != 1 || !requirement)
      throw std::runtime_error(
          "MOSS_INTERNAL_INVARIANT Root-ingress argument disposition requirement/fact missing or ambiguous");
    try {
#ifdef MOSS_TEST_BUILD
      if (std::getenv("MOSS_TEST_CONSUMER_PERTURB_ROOT_INGRESS_ARGUMENT_DISPOSITION_FACT")) {
        auto corrupted = *requirement;
        const string field = std::getenv(
            "MOSS_TEST_CONSUMER_PERTURB_ROOT_INGRESS_ARGUMENT_DISPOSITION_FACT");
        if (field == "owner") ++corrupted.owner_expression_occurrence_id;
        else if (field == "message") ++corrupted.message_expression_occurrence_id;
        else if (field == "argument") ++corrupted.argument_expression_occurrence_id;
        else if (field == "binding") corrupted.source_binding_identity += ":wrong";
        else if (field == "definition") corrupted.source_definition_identity += ":wrong";
        else if (field == "type") corrupted.payload_type.reset();
        else if (field == "context") ++corrupted.specialization_id;
        (void)checked_program.typed_semantics
            ->find_root_ingress_argument_disposition_fact(corrupted);
        throw std::runtime_error(
            "perturbed Root-ingress argument disposition unexpectedly passed");
      }
#endif
      const auto& fact = checked_program.typed_semantics
          ->find_root_ingress_argument_disposition_fact(*requirement);
      const auto* expression_facts = arena.facts(argument_id, fact.specialization_id);
      const auto expected = moss::Type::parse_canonical(expected_type);
      if (!expression_facts || !expression_facts->type || !expected || !*expected ||
          !expression_facts->type->structurally_equal(*fact.payload_type) ||
          !fact.payload_type->structurally_equal(**expected) ||
          fact.specialization_id != codegen_body_spec_id_ ||
          fact.required_access != Effect::Consume || !fact.source_is_owned ||
          !fact.checker_proved_available)
        throw std::invalid_argument(
            "Root-ingress argument disposition contradicts exact binding, type, context, or Consume action");
      return &fact;
    } catch (const std::exception& error) {
      throw std::runtime_error(
          "MOSS_INTERNAL_INVARIANT " + string(error.what()));
    }
  }

  const TypedRootIngressDomainCaptureFactV1&
  resolve_root_ingress_domain_capture(
      const string& receiver, const string& handler,
      const RootIngressMessageOccurrence& occurrence) const {
    const Program& checked_program = semantic_program_ ? *semantic_program_ : p_;
    const Stmt* statement = phase21_codegen_statement_;
    if (!checked_program.typed_semantics || !statement ||
        statement->checked_statement_occurrence_id == UINT32_MAX ||
        occurrence.arena_index >= checked_program.expression_arenas.size() ||
        !checked_program.expression_arenas[occurrence.arena_index])
      throw std::runtime_error(
          "MOSS_INTERNAL_INVARIANT Root-ingress capture query identity unavailable");
    if (root_ingress_owner_handle_count(checked_program, *statement,
                                        occurrence,
                                        occurrence.owner_expression) != 1)
      throw std::runtime_error(
          "MOSS_INTERNAL_INVARIANT Root-ingress capture owner handle missing or ambiguous");
    const auto& arena = *checked_program.expression_arenas[occurrence.arena_index];
    if (occurrence.message_expression >= arena.size() ||
        occurrence.receiver_expression >= arena.size())
      throw std::runtime_error(
          "MOSS_INTERNAL_INVARIANT Root-ingress active EST occurrence is invalid");
    const auto* message = std::get_if<est::Message>(
        &arena.at(occurrence.message_expression));
    const auto* target = message
        ? std::get_if<est::Identifier>(&arena.at(message->target)) : nullptr;
    if (!message || !target || message->target != occurrence.receiver_expression ||
        message->handler != handler || target->name != receiver)
      throw std::runtime_error(
          "MOSS_INTERNAL_INVARIANT active Root-ingress message does not match its exact EST handle");
    const TypedRootIngressDomainCaptureRequirementV1* requirement = nullptr;
    std::size_t requirement_count = 0;
    std::size_t fact_count = 0;
    for (const auto& candidate : checked_program.typed_semantics
             ->required_root_ingress_domain_capture_occurrences) {
      if (candidate.statement_occurrence_id ==
              statement->checked_statement_occurrence_id &&
          candidate.est_arena_index == occurrence.arena_index &&
          candidate.message_expression_occurrence_id ==
              occurrence.message_expression &&
          candidate.receiver_expression_occurrence_id ==
              occurrence.receiver_expression &&
          candidate.source_fingerprint == occurrence.source_fingerprint &&
          candidate.origin_id == occurrence.origin_id &&
          candidate.specialization_id == codegen_body_spec_id_ &&
          candidate.receiver_binding == receiver) {
        requirement = &candidate;
        ++requirement_count;
      }
    }
    for (const auto& candidate : checked_program.typed_semantics
             ->root_ingress_domain_capture_facts)
      if (candidate.statement_occurrence_id ==
              statement->checked_statement_occurrence_id &&
          candidate.est_arena_index == occurrence.arena_index &&
          candidate.message_expression_occurrence_id ==
              occurrence.message_expression &&
          candidate.receiver_expression_occurrence_id ==
              occurrence.receiver_expression &&
          candidate.source_fingerprint == occurrence.source_fingerprint &&
          candidate.origin_id == occurrence.origin_id &&
          candidate.specialization_id == codegen_body_spec_id_ &&
          candidate.receiver_binding == receiver)
        ++fact_count;
#ifdef MOSS_TEST_BUILD
    if (std::getenv("MOSS_TEST_CONSUMER_DROP_ROOT_INGRESS_CAPTURE_REQUIREMENT"))
      --requirement_count;
    if (std::getenv("MOSS_TEST_CONSUMER_DUPLICATE_ROOT_INGRESS_CAPTURE_REQUIREMENT"))
      ++requirement_count;
    if (std::getenv("MOSS_TEST_CONSUMER_DROP_ROOT_INGRESS_CAPTURE_FACT"))
      fact_count = 0;
    if (std::getenv("MOSS_TEST_CONSUMER_DUPLICATE_ROOT_INGRESS_CAPTURE_FACT"))
      ++fact_count;
#endif
    if (requirement_count != 1 || fact_count != 1 || !requirement)
      throw std::runtime_error(
          "MOSS_INTERNAL_INVARIANT Root-ingress capture requirement/fact missing or ambiguous");
    try {
#ifdef MOSS_TEST_BUILD
      if (std::getenv("MOSS_TEST_CONSUMER_PERTURB_ROOT_INGRESS_CAPTURE_FACT")) {
        auto corrupted = *requirement;
        const string field = std::getenv(
            "MOSS_TEST_CONSUMER_PERTURB_ROOT_INGRESS_CAPTURE_FACT");
        if (field == "expr-id")
          ++corrupted.message_expression_occurrence_id;
        else
          corrupted.permits_moss_value_copy = true;
        (void)checked_program.typed_semantics
            ->find_root_ingress_domain_capture_fact(corrupted);
        throw std::runtime_error(
            "perturbed Root-ingress capture fact unexpectedly passed");
      }
#endif
      const auto& fact = checked_program.typed_semantics
          ->find_root_ingress_domain_capture_fact(*requirement);
      std::size_t edge_matches = 0;
      for (const auto& edge : checked_program.semantic_call_edges)
        if (edge.source == fact.selected_call_edge_source &&
            edge.target == fact.selected_call_edge_target &&
            edge.invocation_kind == fact.selected_call_edge_invocation_kind &&
            edge.receiver == fact.selected_call_edge_receiver &&
            edge.source_file == fact.selected_call_edge_source_file &&
            edge.line == fact.selected_call_edge_line &&
            edge.occurrence == fact.selected_call_edge_occurrence &&
            edge.message_est_arena_index ==
                fact.selected_call_edge_est_arena_index &&
            edge.message_expression_occurrence_id ==
                fact.selected_call_edge_message_expression_occurrence_id &&
            edge.receiver_expression_occurrence_id ==
                fact.selected_call_edge_receiver_expression_occurrence_id &&
            edge.message_source_fingerprint ==
                fact.selected_call_edge_source_fingerprint)
          ++edge_matches;
      if (edge_matches != 1 ||
          fact.selected_call_edge_identity !=
              fact.selected_call_edge_source + "|" +
              fact.selected_call_edge_target + "|" +
              fact.selected_call_edge_source_file + ":" +
              std::to_string(fact.selected_call_edge_line) + ":" +
              std::to_string(fact.selected_call_edge_occurrence) +
              "|receiver=" + fact.selected_call_edge_receiver + "|est=" +
              std::to_string(fact.selected_call_edge_est_arena_index) + ":" +
              std::to_string(fact.selected_call_edge_message_expression_occurrence_id) +
              ":" + std::to_string(
                  fact.selected_call_edge_receiver_expression_occurrence_id) ||
          !fact.is_authoritative() || fact.permits_moss_value_copy)
        throw std::invalid_argument(
            "Root-ingress capture does not bind one exact checked message call edge");
      return fact;
    } catch (const std::exception& error) {
      throw std::runtime_error(
          "MOSS_INTERNAL_INVARIANT " + string(error.what()));
    }
  }

  const TypedEchoArgumentFactV1* resolve_echo_argument_fact(
      const Stmt& statement, std::uint32_t argument_index,
      const string& functional_context) const {
    if (statement.kind != Stmt::Kind::Echo)
      throw std::runtime_error(
          "MOSS_INTERNAL_INVARIANT checked Echo argument identity unavailable");
    const Program& checked_program = semantic_program_ ? *semantic_program_ : p_;
    const auto checked = statement.echo_types_by_spec_id.find(
        codegen_body_spec_id_);
    if (checked == statement.echo_types_by_spec_id.end())
      throw std::runtime_error(
          "MOSS_INTERNAL_INVARIANT missing exact Echo specialization SpecId");
    const auto& checked_types = checked->second;
    if (argument_index >= checked_types.size())
      throw std::runtime_error(
          "MOSS_INTERNAL_INVARIANT checked Echo argument type missing");
    const auto checked_type = moss::Type::parse_canonical(
        checked_types[argument_index]);
    const bool primitive_value = checked_type && *checked_type &&
        is_primitive_copy_type(*checked_type);
    const est::Handle* handle = nullptr;
    for (const auto& candidate : statement.expression_handles) {
      if (candidate.slot != est::ExprSlot::Argument ||
          candidate.argument_index != argument_index) continue;
      if (handle)
        throw std::runtime_error(
            "MOSS_INTERNAL_INVARIANT duplicate checked Echo argument handle");
      handle = &candidate;
    }
    (void)functional_context;  // Diagnostic label only; never an identity source.
    if ((!handle || statement.checked_statement_occurrence_id == UINT32_MAX) &&
        primitive_value)
      return nullptr;
    if (statement.checked_statement_occurrence_id == UINT32_MAX)
      throw std::runtime_error(
          "MOSS_INTERNAL_INVARIANT checked Echo argument statement identity unavailable");
    if (!handle || handle->arena_index >= checked_program.expression_arenas.size() ||
        !checked_program.expression_arenas[handle->arena_index] ||
        argument_index >= statement.args.size())
      throw std::runtime_error(
          "MOSS_INTERNAL_INVARIANT checked Echo argument EST handle missing");
    const auto* typed_semantics = semantic_program_ &&
            semantic_program_->typed_semantics
        ? semantic_program_->typed_semantics.get() : nullptr;
    if (!typed_semantics)
      throw std::runtime_error(
          "MOSS_INTERNAL_INVARIANT typed Echo argument facts are unavailable");
    try {
#ifdef MOSS_TEST_BUILD
      auto& requirements = semantic_program_->typed_semantics->required_echo_argument_occurrences;
      auto req = std::find_if(requirements.begin(), requirements.end(),
          [&](const TypedEchoArgumentRequirementV1& item) {
            return item.est_arena_index == handle->arena_index &&
                item.argument_expression_occurrence_id == handle->expr &&
                item.argument_index == argument_index &&
                item.specialization_id == codegen_body_spec_id_;
          });
      const bool borrowed_requirement = req != requirements.end() &&
          req->mode == TypedEchoArgumentModeV1::BorrowedVectorElement;
      if (borrowed_requirement && std::getenv(
              "MOSS_TEST_CONSUMER_DROP_ECHO_ARGUMENT_REQUIREMENT"))
        requirements.erase(req);
      else if (borrowed_requirement && std::getenv(
                   "MOSS_TEST_CONSUMER_DUPLICATE_ECHO_ARGUMENT_REQUIREMENT"))
        requirements.push_back(*req);
      auto& facts = semantic_program_->typed_semantics->echo_argument_facts;
      auto fact_it = std::find_if(facts.begin(), facts.end(),
          [&](const TypedEchoArgumentFactV1& item) {
            return item.est_arena_index == handle->arena_index &&
                item.argument_expression_occurrence_id == handle->expr &&
                item.argument_index == argument_index &&
                item.specialization_id == codegen_body_spec_id_;
          });
      const bool borrowed_fact = fact_it != facts.end() &&
          fact_it->mode == TypedEchoArgumentModeV1::BorrowedVectorElement;
      if (borrowed_fact && std::getenv(
              "MOSS_TEST_CONSUMER_DROP_ECHO_ARGUMENT_FACT"))
        facts.erase(fact_it);
      else if (borrowed_fact && std::getenv(
                   "MOSS_TEST_CONSUMER_DUPLICATE_ECHO_ARGUMENT_FACT"))
        facts.push_back(*fact_it);
      else if (borrowed_fact && std::getenv(
                   "MOSS_TEST_CONSUMER_PERTURB_ECHO_ARGUMENT_FACT"))
        fact_it->required_access = Effect::Consume;
#endif
      const auto& requirement = typed_semantics->find_echo_argument_requirement(
          handle->arena_index, handle->expr, argument_index,
          codegen_body_spec_id_);
      auto exact_requirement = requirement;
#ifdef MOSS_TEST_BUILD
      if (requirement.mode == TypedEchoArgumentModeV1::BorrowedVectorElement &&
          std::getenv("MOSS_TEST_CONSUMER_PERTURB_ECHO_ARGUMENT_SPEC_ID"))
        exact_requirement.specialization_id = InvalidSpecId;
#endif
      const auto& fact = typed_semantics->find_echo_argument_fact(exact_requirement);
      if (!fact.is_authoritative() || fact.required_access != Effect::Read ||
          !fact.display_result_non_escaping || fact.permits_copy)
        throw std::invalid_argument(
            "checked Echo argument fact is not authoritative for display");
      if (fact.mode == TypedEchoArgumentModeV1::BorrowedVectorElement) {
        const auto& arena = *checked_program.expression_arenas[handle->arena_index];
        est::ExprId root = handle->expr;
        while (root < arena.size()) {
          const auto* grouped = std::get_if<est::Parenthesized>(&arena.at(root));
          if (!grouped) break;
          root = grouped->inner;
        }
        if (root != fact.indexed_expression_occurrence_id ||
            root >= arena.size())
          throw std::invalid_argument(
              "borrowed Echo argument is not the exact checked index occurrence");
        const auto* index = std::get_if<est::Index>(&arena.at(root));
        if (!index || index->base != fact.receiver_occurrence_id ||
            index->index != fact.selector_occurrence_id)
          throw std::invalid_argument(
              "borrowed Echo argument receiver/selector identity mismatch");
      }
      return &fact;
    } catch (const std::exception& error) {
      throw std::runtime_error(
          "MOSS_INTERNAL_INVARIANT " + string(error.what()));
    }
  }

  const TypedProjectedCallArgumentReadFactV1*
  resolve_projected_call_argument_read_fact(
      const Stmt& statement, std::uint32_t argument_index,
      const string& functional_context) const {
    (void)functional_context;
    if (argument_index >= statement.args.size())
      throw std::runtime_error(
          "MOSS_INTERNAL_INVARIANT projected call argument index is invalid");

    // This contract covers one exact argument shape: Vector[index].member,
    // passed to a checked READ parameter. Other expressions continue through
    // their existing lowering paths.
    string projection_base, projection_field, receiver_text, index_text;
    if (!tree_parse_member_access(statement.args[argument_index],
                                  projection_base, projection_field) ||
        !tree_parse_index(projection_base, receiver_text, index_text))
      return nullptr;

    if (statement.checked_statement_occurrence_id == UINT32_MAX)
      throw std::runtime_error(
          "MOSS_INTERNAL_INVARIANT projected call argument statement identity unavailable");
    const Program& checked_program = semantic_program_ ? *semantic_program_ : p_;
    const auto* typed_semantics = checked_program.typed_semantics.get();
    if (!typed_semantics)
      throw std::runtime_error(
          "MOSS_INTERNAL_INVARIANT projected call argument facts unavailable");

    const est::Handle* handle = nullptr;
    for (const auto& candidate : statement.expression_handles) {
      if (candidate.slot != est::ExprSlot::Argument ||
          candidate.argument_index != argument_index)
        continue;
      if (handle)
        throw std::runtime_error(
            "MOSS_INTERNAL_INVARIANT duplicate projected call argument handle");
      handle = &candidate;
    }
    if (!handle || handle->arena_index >= checked_program.expression_arenas.size() ||
        !checked_program.expression_arenas[handle->arena_index])
      throw std::runtime_error(
          "MOSS_INTERNAL_INVARIANT projected call argument EST handle unavailable");
    const auto& arena = *checked_program.expression_arenas[handle->arena_index];
    if (handle->expr >= arena.size())
      throw std::runtime_error(
          "MOSS_INTERNAL_INVARIANT projected call argument EST ExprId is invalid");

    est::ExprId root = handle->expr;
    while (root < arena.size()) {
      const auto* grouped = std::get_if<est::Parenthesized>(&arena.at(root));
      if (!grouped) break;
      root = grouped->inner;
    }
    const auto* member = root < arena.size()
        ? std::get_if<est::Member>(&arena.at(root)) : nullptr;
    const auto* index = member && member->base < arena.size()
        ? std::get_if<est::Index>(&arena.at(member->base)) : nullptr;
    const auto* receiver = index && index->base < arena.size()
        ? std::get_if<est::Identifier>(&arena.at(index->base)) : nullptr;
    if (!member || !index || !receiver || root != handle->expr ||
        member->name != projection_field)
      return nullptr;
    const auto* receiver_facts = arena.facts(index->base, codegen_body_spec_id_);
    if (!receiver_facts || !receiver_facts->type)
      throw std::runtime_error(
          "MOSS_INTERNAL_INVARIANT projected call argument receiver type unavailable");
    if (receiver_facts->type->kind != TypeKind::Builtin ||
        receiver_facts->type->name != "vector")
      return nullptr;
    const string specialization = codegen_active_specialization_key_;
    const TypedProjectedCallArgumentReadRequirementV1* requirement = nullptr;
    size_t requirement_count = 0;
    for (const auto& candidate :
         typed_semantics->required_projected_call_argument_read_occurrences) {
      if (candidate.statement_occurrence_id !=
              statement.checked_statement_occurrence_id ||
          candidate.argument_index != argument_index ||
          candidate.est_arena_index != handle->arena_index ||
          candidate.call_argument_expression_occurrence_id != handle->expr ||
          candidate.specialization_id != codegen_body_spec_id_ ||
          candidate.specialization_id != codegen_body_spec_id_)
        continue;
      requirement = &candidate;
      ++requirement_count;
    }
    // A source-level Vector projection may have a different checker-owned
    // contract (for example, an unsupported target). Leave that path to its
    // own consumer; if a requirement exists, however, it must validate here.
    if (requirement_count == 0)
      throw std::runtime_error(
          "MOSS_INTERNAL_INVARIANT required projected call argument READ fact missing");
    if (requirement_count != 1 || !requirement)
      throw std::runtime_error(
          "MOSS_INTERNAL_INVARIANT projected call argument requirement is duplicate or ambiguous");
    const auto exact_requirement = *requirement;

#ifdef MOSS_TEST_BUILD
    auto& requirements = checked_program.typed_semantics
        ->required_projected_call_argument_read_occurrences;
    auto& facts = checked_program.typed_semantics
        ->projected_call_argument_read_facts;
    if (std::getenv("MOSS_TEST_CONSUMER_DROP_PROJECTED_CALL_ARGUMENT_READ_REQUIREMENT")) {
      auto it = std::find_if(requirements.begin(), requirements.end(),
          [&](const auto& row) {
            return row.statement_occurrence_id ==
                       exact_requirement.statement_occurrence_id &&
                row.est_arena_index == exact_requirement.est_arena_index &&
                row.call_argument_expression_occurrence_id ==
                    exact_requirement.call_argument_expression_occurrence_id &&
                row.argument_index == exact_requirement.argument_index &&
                row.specialization_id == exact_requirement.specialization_id &&
                row.specialization_id == exact_requirement.specialization_id;
          });
      if (it != requirements.end()) requirements.erase(it);
    }
    if (std::getenv("MOSS_TEST_CONSUMER_DUPLICATE_PROJECTED_CALL_ARGUMENT_READ_REQUIREMENT"))
      requirements.push_back(exact_requirement);
    if (std::getenv("MOSS_TEST_CONSUMER_PERTURB_PROJECTED_CALL_ARGUMENT_READ_REQUIREMENT")) {
      auto it = std::find_if(requirements.begin(), requirements.end(),
          [&](const auto& row) {
            return row.statement_occurrence_id ==
                       exact_requirement.statement_occurrence_id &&
                row.est_arena_index == exact_requirement.est_arena_index &&
                row.call_argument_expression_occurrence_id ==
                    exact_requirement.call_argument_expression_occurrence_id &&
                row.argument_index == exact_requirement.argument_index &&
                row.specialization_id == exact_requirement.specialization_id &&
                row.specialization_id == exact_requirement.specialization_id;
          });
      if (it != requirements.end()) it->required_access = Effect::Consume;
    }
    if (std::getenv("MOSS_TEST_CONSUMER_DROP_PROJECTED_CALL_ARGUMENT_READ_FACT")) {
      auto it = std::find_if(facts.begin(), facts.end(),
          [&](const auto& row) {
            return row.statement_occurrence_id ==
                       exact_requirement.statement_occurrence_id &&
                row.est_arena_index == exact_requirement.est_arena_index &&
                row.call_argument_expression_occurrence_id ==
                    exact_requirement.call_argument_expression_occurrence_id &&
                row.argument_index == exact_requirement.argument_index &&
                row.specialization_id == exact_requirement.specialization_id &&
                row.specialization_id == exact_requirement.specialization_id;
          });
      if (it != facts.end()) facts.erase(it);
    }
    if (std::getenv("MOSS_TEST_CONSUMER_DUPLICATE_PROJECTED_CALL_ARGUMENT_READ_FACT"))
      facts.push_back(exact_requirement);
    if (std::getenv("MOSS_TEST_CONSUMER_PERTURB_PROJECTED_CALL_ARGUMENT_READ_FACT")) {
      auto it = std::find_if(facts.begin(), facts.end(),
          [&](const auto& row) {
            return row.statement_occurrence_id ==
                       exact_requirement.statement_occurrence_id &&
                row.est_arena_index == exact_requirement.est_arena_index &&
                row.call_argument_expression_occurrence_id ==
                    exact_requirement.call_argument_expression_occurrence_id &&
                row.argument_index == exact_requirement.argument_index &&
                row.specialization_id == exact_requirement.specialization_id &&
                row.specialization_id == exact_requirement.specialization_id;
          });
      if (it != facts.end()) it->permits_copy = true;
    }
#endif

    try {
      const auto& fact = typed_semantics->find_projected_call_argument_read_fact(
          exact_requirement);
      const auto argument_span = arena.span(handle->expr);
      const auto projection_span = arena.span(root);
      if (!fact.is_authoritative() ||
          fact.required_access != Effect::Read || fact.permits_copy ||
          !fact.temporary_nonescaping_borrow ||
          fact.target_callable_identity != "builtin:assertEqual-v1" ||
          fact.target_parameter_access != Effect::Read ||
          fact.receiver_expression_occurrence_id != index->base ||
          fact.collection_index_expression_occurrence_id != member->base ||
          fact.index_expression_occurrence_id != index->index ||
          fact.member_projection_expression_occurrence_id != root ||
          fact.member_name != member->name ||
          fact.argument_occurrence_span.source_begin != argument_span.source_begin ||
          fact.argument_occurrence_span.source_end != argument_span.source_end ||
          fact.projection_occurrence_span.source_begin != projection_span.source_begin ||
          fact.projection_occurrence_span.source_end != projection_span.source_end)
        throw std::invalid_argument(
            "projected call argument fact does not match exact checked READ projection");
      return &fact;
    } catch (const std::exception& error) {
      throw std::runtime_error(
          "MOSS_INTERNAL_INVARIANT " + string(error.what()));
    }
  }

  const TypedIndexedCallArgumentReadFactV1*
  resolve_indexed_call_argument_read_fact(
      const Stmt& statement, std::uint32_t argument_index,
      const string& functional_context) const {
    (void)functional_context;
    if (argument_index >= statement.args.size())
      throw std::runtime_error(
          "MOSS_INTERNAL_INVARIANT indexed call argument index is invalid");
    string receiver_text, index_text, member_base, member_name;
    if (!tree_parse_index(statement.args[argument_index], receiver_text,
                          index_text) ||
        tree_parse_member_access(statement.args[argument_index], member_base,
                                 member_name))
      return nullptr;
    if (statement.checked_statement_occurrence_id == UINT32_MAX)
      throw std::runtime_error(
          "MOSS_INTERNAL_INVARIANT indexed call argument statement identity unavailable");

    const Program& checked_program = semantic_program_ ? *semantic_program_ : p_;
    const auto* typed_semantics = checked_program.typed_semantics.get();
    if (!typed_semantics)
      throw std::runtime_error(
          "MOSS_INTERNAL_INVARIANT indexed call argument facts unavailable");
    const est::Handle* handle = nullptr;
    for (const auto& candidate : statement.expression_handles) {
      if (candidate.slot != est::ExprSlot::Argument ||
          candidate.argument_index != argument_index)
        continue;
      if (handle)
        throw std::runtime_error(
            "MOSS_INTERNAL_INVARIANT duplicate indexed call argument handle");
      handle = &candidate;
    }
    if (!handle || handle->arena_index >= checked_program.expression_arenas.size() ||
        !checked_program.expression_arenas[handle->arena_index])
      throw std::runtime_error(
          "MOSS_INTERNAL_INVARIANT indexed call argument EST handle unavailable");
    const auto& arena = *checked_program.expression_arenas[handle->arena_index];
    if (handle->expr >= arena.size())
      throw std::runtime_error(
          "MOSS_INTERNAL_INVARIANT indexed call argument EST ExprId is invalid");
    est::ExprId index_id = handle->expr;
    while (index_id < arena.size()) {
      const auto* grouped = std::get_if<est::Parenthesized>(&arena.at(index_id));
      if (!grouped) break;
      index_id = grouped->inner;
    }
    const auto* index = index_id < arena.size()
        ? std::get_if<est::Index>(&arena.at(index_id)) : nullptr;
    if (!index) return nullptr;
    const auto* receiver_facts = arena.facts(index->base, codegen_body_spec_id_);
    if (!receiver_facts || !receiver_facts->type)
      throw std::runtime_error(
          "MOSS_INTERNAL_INVARIANT indexed call argument receiver type unavailable");
    if (receiver_facts->type->kind != TypeKind::Builtin ||
        receiver_facts->type->name != "vector" ||
        receiver_facts->type->type_args.size() != 1)
      return nullptr;
    const bool primitive_element = is_primitive_copy_type(
        receiver_facts->type->type_args.front());
    const auto* receiver = index->base < arena.size()
        ? std::get_if<est::Identifier>(&arena.at(index->base)) : nullptr;
    if ((!receiver || index_id != handle->expr) && !primitive_element)
      throw std::runtime_error(
          "MOSS_INTERNAL_INVARIANT nonprimitive indexed call argument lacks supported exact receiver identity");
    if (!receiver || index_id != handle->expr)
      return nullptr;

    const string specialization = codegen_active_specialization_key_;
    const TypedIndexedCallArgumentReadRequirementV1* requirement = nullptr;
    size_t requirement_count = 0;
    for (const auto& candidate :
         typed_semantics->required_indexed_call_argument_read_occurrences) {
      if (candidate.statement_occurrence_id !=
              statement.checked_statement_occurrence_id ||
          candidate.argument_index != argument_index ||
          candidate.est_arena_index != handle->arena_index ||
          candidate.call_argument_expression_occurrence_id != handle->expr ||
          candidate.specialization_id != codegen_body_spec_id_ ||
          candidate.specialization_id != codegen_body_spec_id_)
        continue;
      requirement = &candidate;
      ++requirement_count;
    }
    if (requirement_count == 0 && !primitive_element)
      throw std::runtime_error(
          "MOSS_INTERNAL_INVARIANT required indexed call argument READ fact missing");
    if (requirement_count == 0) return nullptr;
    if (requirement_count != 1 || !requirement)
      throw std::runtime_error(
          "MOSS_INTERNAL_INVARIANT indexed call argument requirement is duplicate or ambiguous");
    const auto exact_requirement = *requirement;

#ifdef MOSS_TEST_BUILD
    auto& requirements = checked_program.typed_semantics
        ->required_indexed_call_argument_read_occurrences;
    auto& facts = checked_program.typed_semantics->indexed_call_argument_read_facts;
    const auto same_query = [&](const auto& row) {
      return row.statement_occurrence_id ==
                 exact_requirement.statement_occurrence_id &&
          row.est_arena_index == exact_requirement.est_arena_index &&
          row.call_argument_expression_occurrence_id ==
              exact_requirement.call_argument_expression_occurrence_id &&
          row.argument_index == exact_requirement.argument_index &&
          row.specialization_id == exact_requirement.specialization_id &&
          row.specialization_id == exact_requirement.specialization_id;
    };
    if (std::getenv("MOSS_TEST_CONSUMER_DROP_INDEXED_CALL_ARGUMENT_READ_REQUIREMENT")) {
      auto it = std::find_if(requirements.begin(), requirements.end(), same_query);
      if (it != requirements.end()) requirements.erase(it);
    }
    if (std::getenv("MOSS_TEST_CONSUMER_DUPLICATE_INDEXED_CALL_ARGUMENT_READ_REQUIREMENT"))
      requirements.push_back(exact_requirement);
    if (std::getenv("MOSS_TEST_CONSUMER_PERTURB_INDEXED_CALL_ARGUMENT_READ_REQUIREMENT")) {
      auto it = std::find_if(requirements.begin(), requirements.end(), same_query);
      if (it != requirements.end()) it->required_access = Effect::Consume;
    }
    if (std::getenv("MOSS_TEST_CONSUMER_DROP_INDEXED_CALL_ARGUMENT_READ_FACT")) {
      auto it = std::find_if(facts.begin(), facts.end(), same_query);
      if (it != facts.end()) facts.erase(it);
    }
    if (std::getenv("MOSS_TEST_CONSUMER_DUPLICATE_INDEXED_CALL_ARGUMENT_READ_FACT"))
      facts.push_back(exact_requirement);
    if (std::getenv("MOSS_TEST_CONSUMER_PERTURB_INDEXED_CALL_ARGUMENT_READ_FACT")) {
      auto it = std::find_if(facts.begin(), facts.end(), same_query);
      if (it != facts.end()) it->permits_copy = true;
    }
#endif

    try {
      const auto& fact = typed_semantics->find_indexed_call_argument_read_fact(
          exact_requirement);
      const auto argument_span = arena.span(handle->expr);
      const auto index_span = arena.span(handle->expr);
      if (!fact.is_authoritative() || fact.required_access != Effect::Read ||
          fact.permits_copy || !fact.temporary_nonescaping_borrow ||
          fact.target_callable_identity != "builtin:assertEqual-v1" ||
          fact.target_parameter_access != Effect::Read ||
          fact.call_argument_expression_occurrence_id != handle->expr ||
          fact.collection_index_expression_occurrence_id != handle->expr ||
          fact.receiver_expression_occurrence_id != index->base ||
          fact.index_expression_occurrence_id != index->index ||
          fact.argument_occurrence_span.source_begin != argument_span.source_begin ||
          fact.argument_occurrence_span.source_end != argument_span.source_end ||
          fact.index_occurrence_span.source_begin != index_span.source_begin ||
          fact.index_occurrence_span.source_end != index_span.source_end)
        throw std::invalid_argument(
            "indexed call argument fact does not match exact checked READ index");
      return &fact;
    } catch (const std::exception& error) {
      throw std::runtime_error(
          "MOSS_INTERNAL_INVARIANT " + string(error.what()));
    }
  }

  const TypedIndexedExpressionReadFactV1*
  resolve_indexed_expression_read_fact(const Stmt& statement,
                                       const string& expression,
                                       const string& context) const {
    const Program& checked_program = semantic_program_ ? *semantic_program_ : p_;
    const auto* semantics = checked_program.typed_semantics.get();
    if (!semantics || statement.checked_statement_occurrence_id == UINT32_MAX)
      throw std::runtime_error(
          "MOSS_INTERNAL_INVARIANT indexed expression-read authority context unavailable");
    const est::Handle* handle = nullptr;
    for (const auto& candidate : statement.expression_handles)
      if (candidate.slot == est::ExprSlot::Condition &&
          candidate.argument_index == 0) {
        if (handle) throw std::runtime_error(
            "MOSS_INTERNAL_INVARIANT duplicate indexed expression-read condition handle");
        handle = &candidate;
      }
    if (!handle || handle->arena_index >= checked_program.expression_arenas.size() ||
        !checked_program.expression_arenas[handle->arena_index])
      throw std::runtime_error(
          "MOSS_INTERNAL_INVARIANT indexed expression-read EST handle unavailable");
    const auto& arena = *checked_program.expression_arenas[handle->arena_index];
    std::vector<est::ExprId> pending{handle->expr};
    std::set<est::ExprId> reachable;
    while (!pending.empty()) {
      const auto id = pending.back(); pending.pop_back();
      if (id >= arena.size() || !reachable.insert(id).second) continue;
      const auto& node = arena.at(id);
      std::visit([&](const auto& value) {
        using T = std::decay_t<decltype(value)>;
        if constexpr (std::is_same_v<T, est::Index>) {
          pending.push_back(value.base); pending.push_back(value.index);
        } else if constexpr (std::is_same_v<T, est::Call>) {
          pending.push_back(value.callee);
          pending.insert(pending.end(), value.arguments.begin(), value.arguments.end());
        } else if constexpr (std::is_same_v<T, est::Binary>) {
          pending.push_back(value.left); pending.push_back(value.right);
        } else if constexpr (std::is_same_v<T, est::Unary>) pending.push_back(value.operand);
        else if constexpr (std::is_same_v<T, est::Parenthesized>) pending.push_back(value.inner);
        else if constexpr (std::is_same_v<T, est::Member>) pending.push_back(value.base);
      }, node);
    }
    const auto exact_context = context.empty() ? string("main") : context;
    const auto specialization = codegen_active_specialization_key_;
    const TypedIndexedExpressionReadRequirementV1* requirement = nullptr;
    std::size_t matches = 0;
    for (const auto& row : semantics->required_indexed_expression_read_occurrences) {
      if (row.statement_occurrence_id != statement.checked_statement_occurrence_id ||
          row.est_arena_index != handle->arena_index ||
          row.specialization_id != codegen_body_spec_id_ ||
          row.specialization_id != codegen_body_spec_id_ ||
          !reachable.count(row.index_expression_occurrence_id)) continue;
      const auto span = arena.span(row.index_expression_occurrence_id);
      if (span.source_begin > span.source_end || span.source_end > statement.a.size() ||
          trim(statement.a.substr(span.source_begin, span.source_end - span.source_begin)) !=
              trim(expression)) continue;
      requirement = &row; ++matches;
    }
    if (matches != 1 || !requirement)
      throw std::runtime_error(
          "MOSS_INTERNAL_INVARIANT indexed expression-read fact missing or ambiguous");
    const auto selected = *requirement;
#ifdef MOSS_TEST_BUILD
    auto& requirements = checked_program.typed_semantics
        ->required_indexed_expression_read_occurrences;
    auto& facts = checked_program.typed_semantics->indexed_expression_read_facts;
    const auto same = [&](const auto& row) {
      return row.statement_occurrence_id == selected.statement_occurrence_id &&
          row.est_arena_index == selected.est_arena_index &&
          row.index_expression_occurrence_id == selected.index_expression_occurrence_id &&
          row.specialization_id == selected.specialization_id &&
          row.specialization_id == selected.specialization_id;
    };
    if (std::getenv("MOSS_TEST_CONSUMER_DROP_INDEXED_EXPRESSION_READ_REQUIREMENT")) {
      auto it = std::find_if(requirements.begin(), requirements.end(), same);
      if (it != requirements.end()) requirements.erase(it);
    }
    if (std::getenv("MOSS_TEST_CONSUMER_DUPLICATE_INDEXED_EXPRESSION_READ_REQUIREMENT"))
      requirements.push_back(selected);
    if (std::getenv("MOSS_TEST_CONSUMER_PERTURB_INDEXED_EXPRESSION_READ_REQUIREMENT")) {
      auto it = std::find_if(requirements.begin(), requirements.end(), same);
      if (it != requirements.end()) it->required_access = Effect::Consume;
    }
    if (std::getenv("MOSS_TEST_CONSUMER_DROP_INDEXED_EXPRESSION_READ_FACT")) {
      auto it = std::find_if(facts.begin(), facts.end(), same);
      if (it != facts.end()) facts.erase(it);
    }
    if (std::getenv("MOSS_TEST_CONSUMER_DUPLICATE_INDEXED_EXPRESSION_READ_FACT"))
      facts.push_back(selected);
    if (std::getenv("MOSS_TEST_CONSUMER_PERTURB_INDEXED_EXPRESSION_READ_FACT")) {
      auto it = std::find_if(facts.begin(), facts.end(), same);
      if (it != facts.end()) it->permits_copy = true;
    }
#endif
    try {
      const auto& fact = semantics->find_indexed_expression_read_fact(selected);
      const auto* index = std::get_if<est::Index>(&arena.at(fact.index_expression_occurrence_id));
      if (!index || index->base != fact.receiver_expression_occurrence_id ||
          index->index != fact.selector_expression_occurrence_id ||
          fact.required_access != Effect::Read || fact.permits_copy ||
          !fact.temporary_nonescaping_borrow ||
          fact.specialization_id != codegen_body_spec_id_ ||
          fact.specialization_id != codegen_body_spec_id_)
        throw std::invalid_argument("indexed expression-read fact does not match exact checked READ occurrence");
      return &fact;
    } catch (const std::exception& error) {
      throw std::runtime_error("MOSS_INTERNAL_INVARIANT " + string(error.what()));
    }
  }

  const TypedProjectedCollectionReadFactV1*
  resolve_projected_collection_read_fact(
      const string& projection_expression) const {
    if (!p_.typed_semantics || !phase21_codegen_statement_)
      throw std::runtime_error(
          "MOSS_INTERNAL_INVARIANT projected collection-read authority unavailable");
    const Stmt& statement = *phase21_codegen_statement_;
    if (statement.kind != Stmt::Kind::Echo ||
        statement.checked_statement_occurrence_id == UINT32_MAX)
      throw std::runtime_error(
          "MOSS_INTERNAL_INVARIANT projected collection-read query identity unavailable");
    const string specialization = codegen_active_specialization_key_;
    const TypedProjectedCollectionReadRequirementV1* requirement = nullptr;
    size_t requirement_count = 0;
    for (const auto& candidate :
         p_.typed_semantics->required_projected_collection_read_occurrences) {
      if (candidate.projection_statement_occurrence_id !=
              statement.checked_statement_occurrence_id ||
          candidate.query_statement_occurrence_id !=
              statement.checked_statement_occurrence_id ||
          candidate.specialization_id != codegen_body_spec_id_ ||
          candidate.specialization_id != codegen_body_spec_id_ ||
          candidate.est_arena_index >= p_.expression_arenas.size() ||
          !p_.expression_arenas[candidate.est_arena_index])
        continue;
      const auto& arena = *p_.expression_arenas[candidate.est_arena_index];
      if (candidate.query_source_expression_occurrence_id >= arena.size())
        throw std::runtime_error(
            "MOSS_INTERNAL_INVARIANT projected collection-read ExprId invalid");
      bool exact_source = false;
      for (const auto& handle : statement.expression_handles) {
        if (handle.arena_index != candidate.est_arena_index ||
            handle.slot != est::ExprSlot::Argument ||
            handle.argument_index >= statement.args.size()) continue;
        std::vector<est::ExprId> pending{handle.expr};
        std::set<est::ExprId> reachable;
        while (!pending.empty()) {
          const auto current = pending.back();
          pending.pop_back();
          if (current >= arena.size() || !reachable.insert(current).second)
            continue;
          const auto& node = arena.at(current);
          std::visit([&](const auto& value) {
            using T = std::decay_t<decltype(value)>;
            if constexpr (std::is_same_v<T, est::Call>) {
              pending.push_back(value.callee);
              pending.insert(pending.end(), value.arguments.begin(), value.arguments.end());
            } else if constexpr (std::is_same_v<T, est::Member>) pending.push_back(value.base);
            else if constexpr (std::is_same_v<T, est::Index>) {
              pending.push_back(value.base); pending.push_back(value.index);
            } else if constexpr (std::is_same_v<T, est::Parenthesized>) pending.push_back(value.inner);
            else if constexpr (std::is_same_v<T, est::Pipeline>) {
              pending.push_back(value.input);
              pending.insert(pending.end(), value.stages.begin(), value.stages.end());
            }
          }, node);
        }
        if (!reachable.count(candidate.query_source_expression_occurrence_id))
          continue;
        const auto span = arena.span(
            candidate.query_source_expression_occurrence_id);
        const auto& source = statement.args[handle.argument_index];
        if (span.source_begin <= span.source_end &&
            span.source_end <= source.size() &&
            trim(source.substr(span.source_begin,
                span.source_end - span.source_begin)) ==
                trim(projection_expression)) {
          exact_source = true;
          break;
        }
      }
      if (!exact_source) continue;
      requirement = &candidate;
      ++requirement_count;
    }
    size_t fact_count = 0;
    if (requirement)
      for (const auto& candidate :
           p_.typed_semantics->projected_collection_read_facts)
        if (candidate.est_arena_index == requirement->est_arena_index &&
            candidate.map_index_expression_occurrence_id ==
                requirement->map_index_expression_occurrence_id &&
            candidate.member_projection_expression_occurrence_id ==
                requirement->member_projection_expression_occurrence_id &&
            candidate.query_source_expression_occurrence_id ==
                requirement->query_source_expression_occurrence_id &&
            candidate.query_statement_occurrence_id ==
                requirement->query_statement_occurrence_id &&
            candidate.specialization_id == requirement->specialization_id)
          ++fact_count;
#ifdef MOSS_TEST_BUILD
    if (std::getenv("MOSS_TEST_CONSUMER_DROP_PROJECTED_COLLECTION_READ_REQUIREMENT"))
      --requirement_count;
    if (std::getenv("MOSS_TEST_CONSUMER_DUPLICATE_PROJECTED_COLLECTION_READ_REQUIREMENT"))
      ++requirement_count;
    if (std::getenv("MOSS_TEST_CONSUMER_DROP_PROJECTED_COLLECTION_READ_FACT")) fact_count = 0;
    if (std::getenv("MOSS_TEST_CONSUMER_DUPLICATE_PROJECTED_COLLECTION_READ_FACT")) ++fact_count;
#endif
    if (requirement_count == 0 && fact_count == 0)
      throw std::runtime_error(
          "MOSS_INTERNAL_INVARIANT projected collection-read requirement missing");
    if (requirement_count != 1 || fact_count != 1 || !requirement)
      throw std::runtime_error(
          "MOSS_INTERNAL_INVARIANT projected collection-read requirement/fact missing or ambiguous");
    try {
#ifdef MOSS_TEST_BUILD
      if (std::getenv("MOSS_TEST_CONSUMER_PERTURB_PROJECTED_COLLECTION_READ_FACT")) {
        auto corrupted = *requirement;
        corrupted.required_access = Effect::Consume;
        if (corrupted.is_authoritative())
          throw std::runtime_error(
              "perturbed projected collection-read requirement remains authoritative");
        throw std::runtime_error(
            "MOSS_INTERNAL_INVARIANT perturbed projected collection-read authority rejected");
      }
#endif
      p_.typed_semantics->require_all_phase28_authority_facts();
      const auto* result = p_.typed_semantics->projected_collection_read_facts.empty()
          ? nullptr : &p_.typed_semantics->projected_collection_read_facts.front();
      size_t exact_matches = 0;
      for (const auto& candidate :
           p_.typed_semantics->projected_collection_read_facts)
        if (candidate.est_arena_index == requirement->est_arena_index &&
            candidate.map_index_expression_occurrence_id ==
                requirement->map_index_expression_occurrence_id &&
            candidate.member_projection_expression_occurrence_id ==
                requirement->member_projection_expression_occurrence_id &&
            candidate.query_source_expression_occurrence_id ==
                requirement->query_source_expression_occurrence_id &&
            candidate.projection_statement_occurrence_id ==
                requirement->projection_statement_occurrence_id &&
            candidate.query_statement_occurrence_id ==
                requirement->query_statement_occurrence_id &&
            candidate.specialization_id == requirement->specialization_id) {
          result = &candidate;
          ++exact_matches;
        }
      if (exact_matches != 1 || !result || !result->is_authoritative())
        throw std::invalid_argument(
            "projected collection-read fact is missing, duplicate, or unauthoritative");
      return result;
    } catch (const std::exception& error) {
      throw std::runtime_error(
          "MOSS_INTERNAL_INVARIANT " + string(error.what()));
    }
  }

  struct CheckedMapWriteThrough {
    const TypedConditionalCollectionErrorFactV1* fact = nullptr;
    const est::Arena* arena = nullptr;
    est::ExprId index_expression = UINT32_MAX;
    est::ExprId lhs_expression = UINT32_MAX;
    std::uint32_t arena_index = UINT32_MAX;
  };

  std::optional<CheckedMapWriteThrough> resolve_map_write_through(
      const Stmt& statement, const string& functional_context) const {
    (void)functional_context;
    if (!p_.typed_semantics ||
        statement.kind != Stmt::Kind::Assign ||
        statement.checked_statement_occurrence_id == UINT32_MAX)
      return std::nullopt;
    const est::Handle* handle = nullptr;
    for (const auto& candidate : statement.expression_handles) {
      if (candidate.slot == est::ExprSlot::LhsPlace &&
          candidate.argument_index == 0) {
        if (handle)
          throw std::runtime_error(
              "MOSS_INTERNAL_INVARIANT duplicate checked assignment-place handle");
        handle = &candidate;
      }
    }
    if (!handle || handle->arena_index >= p_.expression_arenas.size() ||
        !p_.expression_arenas[handle->arena_index])
      return std::nullopt;
    const est::Arena& arena = *p_.expression_arenas[handle->arena_index];
    if (handle->expr >= arena.size())
      throw std::runtime_error(
          "MOSS_INTERNAL_INVARIANT assignment-place EST occurrence is invalid");
    std::vector<const TypedConditionalCollectionErrorRequirementV1*> candidates;
    for (const auto& requirement :
         p_.typed_semantics->required_conditional_collection_error_occurrences) {
      if (requirement.operation ==
              TypedConditionalCollectionErrorFactV1::Operation::MapIndexWriteThrough &&
          requirement.est_arena_index == handle->arena_index &&
          requirement.statement_occurrence_id ==
              statement.checked_statement_occurrence_id &&
          requirement.specialization_id == codegen_body_spec_id_)
        candidates.push_back(&requirement);
    }
    std::vector<const TypedConditionalCollectionErrorFactV1*> orphan_facts;
    for (const auto& fact : p_.typed_semantics->conditional_collection_error_facts) {
      if (fact.operation ==
              TypedConditionalCollectionErrorFactV1::Operation::MapIndexWriteThrough &&
          fact.est_arena_index == handle->arena_index &&
          fact.statement_occurrence_id ==
              statement.checked_statement_occurrence_id &&
          fact.specialization_id == codegen_body_spec_id_ &&
          fact.specialization_id == codegen_body_spec_id_)
        orphan_facts.push_back(&fact);
    }
    if (candidates.empty()) {
      if (!orphan_facts.empty())
        throw std::runtime_error(
            "MOSS_INTERNAL_INVARIANT conditional Map write-through fact lacks a requirement");
      if (statement.exceptional_alternatives.count(RaisedIdentity::enum_variant(
              "std__CollectionError", "MissingKey"))) {
        bool member_projection = false;
        std::vector<est::ExprId> pending{handle->expr};
        while (!pending.empty()) {
          const auto current = pending.back();
          pending.pop_back();
          if (current >= arena.size()) continue;
          const auto& node = arena.at(current);
          if (const auto* member = std::get_if<est::Member>(&node)) {
            member_projection = true;
            pending.push_back(member->base);
          } else if (const auto* index = std::get_if<est::Index>(&node)) {
            const auto* semantic = arena.facts(index->base, codegen_body_spec_id_);
            if (member_projection && semantic && semantic->type &&
                semantic->type->kind == TypeKind::Builtin &&
                semantic->type->name == "map")
              throw std::runtime_error(
                  "MOSS_INTERNAL_INVARIANT conditional Map write-through requirement missing");
            pending.push_back(index->base);
          } else if (const auto* grouped = std::get_if<est::Parenthesized>(&node)) {
            pending.push_back(grouped->inner);
          }
        }
      }
      return std::nullopt;
    }
    if (candidates.size() != 1)
      throw std::runtime_error(
          "MOSS_INTERNAL_INVARIANT conditional Map write-through requirement is ambiguous");
    const auto& requirement = *candidates.front();
    const est::ExprId index_id = requirement.operation_expression_occurrence_id;
    if (index_id >= arena.size() || !std::get_if<est::Index>(&arena.at(index_id)))
      throw std::runtime_error(
          "MOSS_INTERNAL_INVARIANT checked Map write-through occurrence is not an index");
    std::vector<est::ExprId> pending{handle->expr};
    bool reachable = false;
    while (!pending.empty()) {
      const est::ExprId current = pending.back();
      pending.pop_back();
      if (current == index_id) {
        reachable = true;
        continue;
      }
      if (current >= arena.size())
        throw std::runtime_error(
            "MOSS_INTERNAL_INVARIANT assignment-place EST child is invalid");
      const auto& node = arena.at(current);
      if (const auto* member = std::get_if<est::Member>(&node))
        pending.push_back(member->base);
      else if (const auto* index = std::get_if<est::Index>(&node)) {
        pending.push_back(index->base);
        pending.push_back(index->index);
      } else if (const auto* grouped = std::get_if<est::Parenthesized>(&node))
        pending.push_back(grouped->inner);
      else if (const auto* identifier = std::get_if<est::Identifier>(&node))
        (void)identifier;
      else if (const auto* literal = std::get_if<est::Literal>(&node))
        (void)literal;
      else if (current != index_id)
        throw std::runtime_error(
            "MOSS_INTERNAL_INVARIANT unsupported checked Map write-through place shape");
    }
    if (!reachable)
      throw std::runtime_error(
          "MOSS_INTERNAL_INVARIANT checked Map write-through is outside assignment place");
    const auto* fact = resolve_conditional_collection_error_fact(requirement);
    if (fact->key_expression_occurrence_id !=
        std::get<est::Index>(arena.at(index_id)).index)
      throw std::runtime_error(
          "MOSS_INTERNAL_INVARIANT checked Map write-through key occurrence mismatch");
    return CheckedMapWriteThrough{fact, &arena, index_id, handle->expr,
                                  handle->arena_index};
  }

  const TypedConditionalCollectionErrorFactV1*
  resolve_direct_map_read_error_fact() const {
    if (!p_.typed_semantics || !phase21_codegen_statement_) return nullptr;
    const Stmt& statement = *phase21_codegen_statement_;
    if (statement.checked_statement_occurrence_id == UINT32_MAX) return nullptr;
    const est::ExprSlot slot =
        statement.kind == Stmt::Kind::Let || statement.kind == Stmt::Kind::Var
            ? est::ExprSlot::Initializer
            : statement.kind == Stmt::Kind::Return
                ? est::ExprSlot::ReturnValue : est::ExprSlot::RhsValue;
    const est::Handle* handle = nullptr;
    for (const auto& candidate : statement.expression_handles) {
      if (candidate.slot != slot || candidate.argument_index != 0) continue;
      if (handle)
        throw std::runtime_error(
            "MOSS_INTERNAL_INVARIANT ambiguous Map read expression handle");
      handle = &candidate;
    }
    if (!handle || handle->arena_index >= p_.expression_arenas.size() ||
        !p_.expression_arenas[handle->arena_index]) return nullptr;
    const auto& arena = *p_.expression_arenas[handle->arena_index];
    est::ExprId operation = handle->expr;
    while (operation < arena.size()) {
      const auto* grouped = std::get_if<est::Parenthesized>(&arena.at(operation));
      if (!grouped) break;
      operation = grouped->inner;
    }
    const auto wanted =
        TypedConditionalCollectionErrorFactV1::Operation::MapIndexRead;
    auto same_statement_read = [&](const auto& candidate) {
      return candidate.operation == wanted &&
          candidate.est_arena_index == handle->arena_index &&
          candidate.statement_occurrence_id ==
              statement.checked_statement_occurrence_id &&
          candidate.specialization_id == codegen_body_spec_id_ &&
          candidate.specialization_id == codegen_body_spec_id_;
    };
    const auto has_unsupported_nested_read = [&](const auto& rows) {
      return std::any_of(rows.begin(), rows.end(), [&](const auto& candidate) {
        return same_statement_read(candidate) &&
            candidate.operation_expression_occurrence_id != operation;
      });
    };
    if (has_unsupported_nested_read(
            p_.typed_semantics->required_conditional_collection_error_occurrences) ||
        has_unsupported_nested_read(
            p_.typed_semantics->conditional_collection_error_facts))
      throw std::runtime_error(
          "MOSS_INTERNAL_INVARIANT unsupported nested conditional CollectionError Map read lowering");
    if (operation >= arena.size() ||
        !std::get_if<est::Index>(&arena.at(operation))) {
      const bool has_read = std::any_of(
          p_.typed_semantics->required_conditional_collection_error_occurrences.begin(),
          p_.typed_semantics->required_conditional_collection_error_occurrences.end(),
          same_statement_read) || std::any_of(
          p_.typed_semantics->conditional_collection_error_facts.begin(),
          p_.typed_semantics->conditional_collection_error_facts.end(),
          same_statement_read);
      if (has_read)
        throw std::runtime_error(
            "MOSS_INTERNAL_INVARIANT unsupported nested conditional CollectionError Map read lowering");
      return nullptr;
    }
    const TypedConditionalCollectionErrorRequirementV1* requirement = nullptr;
    size_t requirement_count = 0;
    size_t fact_count = 0;
    for (const auto& candidate :
         p_.typed_semantics->required_conditional_collection_error_occurrences) {
      if (candidate.operation == wanted &&
          candidate.est_arena_index == handle->arena_index &&
          candidate.operation_expression_occurrence_id == operation &&
          candidate.statement_occurrence_id ==
              statement.checked_statement_occurrence_id &&
          candidate.specialization_id == codegen_body_spec_id_ &&
          candidate.specialization_id == codegen_body_spec_id_) {
        requirement = &candidate;
        ++requirement_count;
      }
    }
    for (const auto& candidate :
         p_.typed_semantics->conditional_collection_error_facts) {
      if (candidate.operation == wanted &&
          candidate.est_arena_index == handle->arena_index &&
          candidate.operation_expression_occurrence_id == operation &&
          candidate.statement_occurrence_id ==
              statement.checked_statement_occurrence_id &&
          candidate.specialization_id == codegen_body_spec_id_ &&
          candidate.specialization_id == codegen_body_spec_id_)
        ++fact_count;
    }
#ifdef MOSS_TEST_BUILD
    if (std::getenv("MOSS_TEST_CONSUMER_DROP_CONDITIONAL_COLLECTION_ERROR_FACT"))
      fact_count = 0;
    if (std::getenv("MOSS_TEST_CONSUMER_DUPLICATE_CONDITIONAL_COLLECTION_ERROR_FACT"))
      ++fact_count;
    if (std::getenv("MOSS_TEST_CONSUMER_DROP_CONDITIONAL_COLLECTION_ERROR_REQUIREMENT"))
      --requirement_count;
    if (std::getenv("MOSS_TEST_CONSUMER_DUPLICATE_CONDITIONAL_COLLECTION_ERROR_REQUIREMENT"))
      ++requirement_count;
#endif
    if (requirement_count == 0 && fact_count == 0) {
      const auto* checked_receiver =
          arena.facts(std::get<est::Index>(arena.at(operation)).base, codegen_body_spec_id_);
      if (statement.exceptional_alternatives.count(RaisedIdentity::enum_variant(
              "std__CollectionError", "MissingKey")) && checked_receiver &&
          checked_receiver->type &&
          checked_receiver->type->kind == TypeKind::Builtin &&
          checked_receiver->type->name == "map")
        throw std::runtime_error(
            "MOSS_INTERNAL_INVARIANT conditional Map read requirement missing");
      return nullptr;
    }
    if (requirement_count != 1 || fact_count != 1 || !requirement)
      throw std::runtime_error(
          "MOSS_INTERNAL_INVARIANT conditional Map read requirement/fact missing or ambiguous");
#ifdef MOSS_TEST_BUILD
    if (std::getenv("MOSS_TEST_CONSUMER_PERTURB_CONDITIONAL_COLLECTION_ERROR_FACT")) {
      auto corrupted = *requirement;
      corrupted.error_variant_identity = "forged";
      try {
        (void)p_.typed_semantics->find_conditional_collection_error_fact(corrupted);
      } catch (const std::exception& error) {
        throw std::runtime_error(
            "MOSS_INTERNAL_INVARIANT " + string(error.what()));
      }
      throw std::runtime_error(
          "MOSS_INTERNAL_INVARIANT perturbed conditional CollectionError authority unexpectedly passed");
    }
#endif
    const auto* fact = resolve_conditional_collection_error_fact(*requirement);
    const auto* index = std::get_if<est::Index>(&arena.at(operation));
    if (!fact || !index ||
        fact->receiver_expression_occurrence_id != index->base ||
        fact->key_expression_occurrence_id != index->index)
      throw std::runtime_error(
          "MOSS_INTERNAL_INVARIANT conditional CollectionError Map read operands mismatch");
    return fact;
  }

  string checked_map_write_place(
      const CheckedMapWriteThrough& access, est::ExprId expression,
      est::ExprId index_expression, const Domain* domain,
      const std::set<string>& locals,
      const std::unordered_map<string, string>* types) const {
    if (expression == index_expression) return "__moss_checked_entry";
    if (expression >= access.arena->size())
      throw std::runtime_error(
          "MOSS_INTERNAL_INVARIANT checked Map write-through place child missing");
    const auto& node = access.arena->at(expression);
    if (const auto* grouped = std::get_if<est::Parenthesized>(&node))
      return "(" + checked_map_write_place(access, grouped->inner,
          index_expression, domain, locals, types) + ")";
    if (const auto* member = std::get_if<est::Member>(&node))
      return "(" + checked_map_write_place(access, member->base,
          index_expression, domain, locals, types) + ")." +
          rust_identifier(member->name);
    if (const auto* identifier = std::get_if<est::Identifier>(&node))
      return place_expr(identifier->name, domain, locals, types);
    throw std::runtime_error(
        "MOSS_INTERNAL_INVARIANT checked Map write-through lowering does not support this projection");
  }

  string emit_checked_map_write_through(
      const CheckedMapWriteThrough& access, const Stmt& statement,
      const Domain* domain, const std::set<string>& locals,
      const std::unordered_map<string, string>* types,
      size_t functional_pipeline_id) const {
    const auto& index = std::get<est::Index>(
        access.arena->at(access.index_expression));
    const auto source = est::view(*access.arena, access.lhs_expression,
                                  statement.a).source_text(access.index_expression);
    const auto receiver_source = est::view(*access.arena, access.lhs_expression,
                                           statement.a).source_text(index.base);
    const auto key_source = est::view(*access.arena, access.lhs_expression,
                                      statement.a).source_text(index.index);
    if (!source || !receiver_source || !key_source ||
        source->empty() || receiver_source->empty() || key_source->empty())
      throw std::runtime_error(
          "MOSS_INTERNAL_INVARIANT checked Map write-through source projection unavailable");
    const string storage = write_call_place(*receiver_source, domain, locals, types);
    const string key = expr(*key_source, domain, locals, types);
    string lookup;
    const string checked_key_type = canonical_type_name(access.fact->key_type);
    if (shared_protocol_map_key_from_type(checked_key_type)) {
      const string key_type = *shared_protocol_map_key_from_type(checked_key_type);
      lookup = "get_mut_with(" + shared_map_hash(key_type, "__moss_checked_key") +
          ", &__moss_checked_key, |a, b| " +
          shared_map_equal(key_type, "a", "b") + ")";
    } else if (checked_key_type == "string") {
      lookup = "get_mut(__moss_checked_key.as_str())";
    } else {
      lookup = "get_mut(&__moss_checked_key)";
    }
    const string place = checked_map_write_place(
        access, access.lhs_expression, access.index_expression,
        domain, locals, types);
    const string rhs = expr(statement.b, domain, locals, types,
                             functional_pipeline_id);
    if (!statement.exceptional_alternatives.count(RaisedIdentity::enum_variant(
            "std__CollectionError", "MissingKey")))
      throw std::runtime_error(
          "MOSS_INTERNAL_INVARIANT checked Map write-through lacks MissingKey edge");
    const string exit = phase21_collection_error_exit("MissingKey");
    if (shared_protocol_map_key_from_type(checked_key_type))
      return "{ let __moss_checked_key = " + key + "; match (" + storage +
          ")." + lookup + " { Some(__moss_checked_entry) => { " + place +
          " = " + rhs + "; }, None => " + exit + " } }";
    return "{ let __moss_checked_key = " + key + "; match (" + storage +
        ")." + lookup + " { Some(__moss_checked_entry) => { " + place +
        " = " + rhs + "; }, None => " + exit + " } }";
  }

  const TypedCollectionOutcomeFactV1* resolve_collection_outcome_fact(
      TypedCollectionOutcomeFactV1::Operation operation) const {
    if (!p_.typed_semantics || !phase21_codegen_statement_) return nullptr;
    const Stmt& statement = *phase21_codegen_statement_;
    const est::ExprSlot slot = statement.kind == Stmt::Kind::Let ||
            statement.kind == Stmt::Kind::Var
        ? est::ExprSlot::Initializer : est::ExprSlot::RhsValue;
    const est::Handle* handle = nullptr;
    for (const auto& candidate : statement.expression_handles)
      if (candidate.slot == slot && candidate.argument_index == 0) {
        handle = &candidate;
        break;
      }
    if (!handle || handle->arena_index >= p_.expression_arenas.size() ||
        !p_.expression_arenas[handle->arena_index]) return nullptr;
    const auto& arena = *p_.expression_arenas[handle->arena_index];
    est::ExprId root = handle->expr;
    while (const auto* grouped = std::get_if<est::Parenthesized>(&arena.at(root)))
      root = grouped->inner;
    const auto* call = std::get_if<est::Call>(&arena.at(root));
    if (!call) return nullptr;
    const auto* member = std::get_if<est::Member>(&arena.at(call->callee));
    if (!member || std::get_if<est::Identifier>(&arena.at(member->base)) == nullptr)
      return nullptr;
    const bool queue_pop = operation ==
            TypedCollectionOutcomeFactV1::Operation::QueuePop &&
        member->name == "pop" && call->arguments.empty();
    const bool map_remove = operation ==
            TypedCollectionOutcomeFactV1::Operation::MapRemove &&
        member->name == "remove" && call->arguments.size() == 1;
    if (!queue_pop && !map_remove) return nullptr;
    std::string specialization = codegen_active_specialization_key_;
    size_t requirement_count = 0;
    const TypedCollectionOutcomeRequirementV1* requirement = nullptr;
#ifdef MOSS_TEST_BUILD
    const bool drop_requirement =
        std::getenv("MOSS_TEST_CONSUMER_DROP_COLLECTION_OUTCOME_REQUIREMENT") != nullptr;
    const bool duplicate_requirement =
        std::getenv("MOSS_TEST_CONSUMER_DUPLICATE_COLLECTION_OUTCOME_REQUIREMENT") != nullptr;
#else
    constexpr bool drop_requirement = false;
    constexpr bool duplicate_requirement = false;
#endif
    for (const auto& candidate :
         p_.typed_semantics->required_collection_outcome_occurrences) {
      if (!drop_requirement && candidate.operation == operation &&
          candidate.operation_est_arena_index == handle->arena_index &&
          candidate.operation_expression_occurrence_id == root &&
          candidate.specialization_id == codegen_body_spec_id_ &&
          candidate.specialization_id == codegen_body_spec_id_) {
        requirement = &candidate;
        ++requirement_count;
        if (duplicate_requirement) ++requirement_count;
      }
    }
    size_t orphan_fact_count = 0;
    for (const auto& fact : p_.typed_semantics->collection_outcome_facts)
      if (fact.operation == operation &&
          fact.operation_est_arena_index == handle->arena_index &&
          fact.operation_expression_occurrence_id == root &&
          fact.specialization_id == codegen_body_spec_id_ &&
          fact.specialization_id == codegen_body_spec_id_)
        ++orphan_fact_count;
    if (requirement_count != 1 || !requirement) {
      if (requirement_count || orphan_fact_count)
        throw std::runtime_error(
            "MOSS_INTERNAL_INVARIANT checked collection outcome requirement missing or ambiguous");
      const auto* tree_facts = arena.facts(root, codegen_body_spec_id_);
      if (tree_facts && tree_facts->type &&
          tree_facts->type->kind == TypeKind::Never)
        throw std::runtime_error(
            "MOSS_INTERNAL_INVARIANT Never collection result lacks exact outcome authority");
      return nullptr;
    }
    try {
#ifdef MOSS_TEST_BUILD
      if (std::getenv("MOSS_TEST_CONSUMER_DROP_COLLECTION_OUTCOME_FACT"))
        throw std::invalid_argument("required checked collection outcome fact missing");
      if (std::getenv("MOSS_TEST_CONSUMER_DUPLICATE_COLLECTION_OUTCOME_FACT"))
        throw std::invalid_argument("duplicate checked collection outcome facts");
      if (std::getenv("MOSS_TEST_CONSUMER_PERTURB_COLLECTION_OUTCOME_FACT") ||
          std::getenv("MOSS_TEST_CONSUMER_PERTURB_COLLECTION_OUTCOME_REQUIREMENT")) {
        auto corrupted = *requirement;
        corrupted.error_variant_identity = "forged";
        (void)p_.typed_semantics->find_collection_outcome_fact(corrupted);
        throw std::invalid_argument("perturbed collection outcome authority unexpectedly passed");
      }
#endif
      return p_.typed_semantics->find_collection_outcome_fact(*requirement);
    } catch (const std::exception& error) {
      throw std::runtime_error(
          "MOSS_INTERNAL_INVARIANT " + string(error.what()));
    }
  }

  static string phase21_rust_variant(const RaisedIdentity& identity) {
    return "Raised_" + stable_hash(identity.canonical()).substr(0, 16);
  }

  string phase21_rust_error_type(const RaisedIdentity& identity) const {
    string type = canonical_type_name(identity.type);
    if (identity.kind == RaisedIdentityKind::EnumVariant &&
        !enums_.count(type)) {
      for (size_t separator = type.find("::"); separator != string::npos;
           separator = type.find("::"))
        type.replace(separator, 2, "__");
    }
    return rust_type(type);
  }

  RaiseSet phase21_program_raise_set() const {
    RaiseSet result;
    const auto merge = [&](const ObservableEffects& effects) {
      result.insert(effects.raise_set.begin(), effects.raise_set.end());
    };
    const auto statements = [&](const vector<Stmt>& body) {
      for (const auto& statement : body)
        result.insert(statement.exceptional_alternatives.begin(),
                      statement.exceptional_alternatives.end());
    };
    for (const auto& function : p_.functions) {
      merge(function.observable_effects);
      for (const auto& specialization : function.specializations)
        merge(specialization.observable_effects);
      statements(function.body);
    }
    for (const auto& object : p_.objects)
      for (const auto& method : object.methods) {
        merge(method.observable_effects);
        statements(method.body);
      }
    for (const auto& domain : p_.domains)
      for (const auto& handler : domain.handlers) {
        merge(handler.observable_effects);
        statements(handler.body);
        for (const auto& arm : handler.failure_arms)
          statements(arm.body);
      }
    if (p_.main) statements(p_.main->body);
    for (const auto& test : p_.tests) statements(test.body);
    return result;
  }

  void gen_phase21_raised_type(std::ostringstream& o) const {
    const RaiseSet raised = phase21_program_raise_set();
    if (raised.empty()) return;
    o << "#[derive(Clone, Debug)]\n";
    o << "#[allow(private_interfaces)]\n";
    o << "pub enum __MossRaised {\n";
    for (const auto& identity : raised) {
      o << "    " << phase21_rust_variant(identity) << "(";
      o << phase21_rust_error_type(identity);
      o << "),\n";
    }
    o << "}\n\n";
    for (const auto& domain : p_.domains) {
      auto module = phase21_external_module_for_symbol(domain.name);
      if (!module) continue;
      for (const auto& handler : domain.handlers)
        if (handler.provider_abi &&
            handler_has_failure_captures(handler))
          o << "use " << tooling_name("moss_" + *module) << "::"
            << handler.provider_abi->raised_carrier_rust_type << ";\n";
    }
    if (program_uses_fileio(p_)) {
      o << "#[allow(dead_code)]\n"
        << "fn __moss_raise_fileio(error: MossFileError) -> __MossRaised {\n"
        << "    match error {\n";
      for (const char* name : {"NotFound", "PermissionDenied",
                               "NotRegularFile", "InUse", "Full", "IO"}) {
        const RaisedIdentity identity =
            RaisedIdentity::enum_variant("std__FileError", name);
        o << "        MossFileError::" << name << " => ";
        if (raised.count(identity))
          o << "__MossRaised::" << phase21_rust_variant(identity)
            << "(std__FileError::" << name << "),\n";
        else
          o << "std::process::abort(),\n";
      }
      o << "    }\n}\n\n";
    }
  }

  std::optional<string> phase21_external_module_for_symbol(
      const string& symbol) const {
    for (const auto& module : p_.external_modules)
      if (starts_with(symbol, module + "__")) return module;
    for (const auto& module : rust_dependencies_)
      if (module != "__moss_specializations__" &&
          starts_with(symbol, module + "__"))
        return module;
    return std::nullopt;
  }

  string phase21_external_raised_type(const string& module) const {
    return tooling_name("moss_" + module) + "::__MossRaised";
  }

  void gen_standard_error_external_bridges(std::ostringstream& o) const {
    for (const auto& module : rust_dependencies_) {
      if (module == "__moss_specializations__") continue;
      const string crate = tooling_name("moss_" + module);
      for (const char* short_name :
           {"FileError", "ParseError", "ConversionError"}) {
        const string local = standard_error_identity(short_name);
        const string external = crate + "::" + local;
        const auto cases = standard_error_variants(local);
        o << "impl From<" << external << "> for " << local << " {\n"
          << "    fn from(value: " << external << ") -> Self { match value {\n";
        for (const auto& name : cases)
          o << "        " << external << "::" << name << " => "
            << local << "::" << name << ",\n";
        o << "    } }\n}\n";
        o << "impl From<" << local << "> for " << external << " {\n"
          << "    fn from(value: " << local << ") -> Self { match value {\n";
        for (const auto& name : cases)
          o << "        " << local << "::" << name << " => "
            << external << "::" << name << ",\n";
        o << "    } }\n}\n";
      }
    }
  }

  void gen_phase21_external_raised_bridges(std::ostringstream& o) const {
    const RaiseSet raised = phase21_program_raise_set();
    if (raised.empty()) return;
    std::map<string, RaiseSet> by_module;
    for (const auto& function : semantic_program_->functions)
      if (auto module = phase21_external_module_for_symbol(function.name))
        by_module[*module].insert(function.observable_effects.raise_set.begin(),
                                  function.observable_effects.raise_set.end());
    for (const auto& object : semantic_program_->objects)
      if (auto module = phase21_external_module_for_symbol(object.name))
        for (const auto& method : object.methods)
          by_module[*module].insert(method.observable_effects.raise_set.begin(),
                                    method.observable_effects.raise_set.end());
    for (const auto& domain : semantic_program_->domains)
      if (auto module = phase21_external_module_for_symbol(domain.name))
        for (const auto& handler : domain.handlers)
          by_module[*module].insert(handler.observable_effects.raise_set.begin(),
                                    handler.observable_effects.raise_set.end());
    for (const auto& entry : by_module) {
      RaiseSet reachable;
      for (const auto& identity : entry.second)
        if (raised.count(identity)) reachable.insert(identity);
      if (reachable.empty()) continue;
      const string provider = phase21_external_raised_type(entry.first);
      auto bridge_payload = [&](const RaisedIdentity& identity,
                                bool from_provider) {
        const auto variants = standard_error_variants(identity.type);
        if (variants.empty()) return string("payload");
        const string external = tooling_name("moss_" + entry.first) +
            "::" + identity.type;
        const string source = from_provider ? external : identity.type;
        const string target = from_provider ? identity.type : external;
        string conversion = "match payload { ";
        for (const auto& variant : variants)
          conversion += source + "::" + variant + " => " + target +
              "::" + variant + ", ";
        return conversion + "}";
      };
      o << "impl From<" << provider << "> for __MossRaised {\n"
        << "    fn from(value: " << provider << ") -> Self {\n"
        << "        #[allow(unreachable_patterns)]\n"
        << "        match value {\n";
      for (const auto& identity : reachable) {
        const string variant = phase21_rust_variant(identity);
        o << "            " << provider << "::" << variant
          << "(payload) => __MossRaised::" << variant << "("
          << bridge_payload(identity, true) << "),\n";
      }
      o << "            _ => std::process::abort(),\n"
        << "        }\n    }\n}\n";
      o << "impl From<__MossRaised> for " << provider << " {\n"
        << "    fn from(value: __MossRaised) -> Self {\n"
        << "        #[allow(unreachable_patterns)]\n"
        << "        match value {\n";
      for (const auto& identity : reachable) {
        const string variant = phase21_rust_variant(identity);
        o << "            __MossRaised::" << variant << "(payload) => "
          << provider << "::" << variant << "("
          << bridge_payload(identity, false) << "),\n";
      }
      o << "            _ => std::process::abort(),\n"
        << "        }\n    }\n}\n\n";
    }
  }

  bool owns_specialization(const Domain& specialized) const {
    const string& source = specialization_sources_.at(specialized.name);
    return std::any_of(p_.domains.begin(), p_.domains.end(),
        [&](const Domain& domain) { return domain.name == source; });
  }

  bool has_domain_specializations(const string& domain) const {
    return std::any_of(
        p_.domain_specializations.begin(), p_.domain_specializations.end(),
        [&](const DomainSpecialization& specialization) {
          return specialization.materialized_layout && specialization.source_domain == domain;
        });
  }

  bool is_domain_specialization_instance(const string& source_domain,
                                         const string& instance) const {
    return std::any_of(
        p_.domain_specializations.begin(), p_.domain_specializations.end(),
        [&](const DomainSpecialization& specialization) {
          return specialization.materialized_layout && specialization.source_domain == source_domain &&
              specialization.instance == instance;
        });
  }

  string nominalized_generated_argument_type(
      const string& expression, const string& inferred) const {
    string binding = trim(expression);
    auto identity = domain_instance_bindings_.find(binding);
    if (tree_plain_identifier(binding) && identity != domain_instance_bindings_.end() &&
        is_domain_specialization_instance(identity->second.source_domain,
                                          identity->second.instance))
      return identity->second.source_domain;
    return inferred;
  }

  void remember_domain_instance_binding(const string& binding,
                                        const string& source_domain,
                                        const string& instance) {
    if (ambiguous_domain_instance_bindings_.count(binding)) return;
    auto found = domain_instance_bindings_.find(binding);
    if (found == domain_instance_bindings_.end()) {
      domain_instance_bindings_[binding] = {source_domain, instance};
      return;
    }
    if (found->second.source_domain != source_domain ||
        found->second.instance != instance) {
      domain_instance_bindings_.erase(found);
      ambiguous_domain_instance_bindings_.insert(binding);
    }
  }

  void forget_domain_instance_binding(const string& binding) {
    domain_instance_bindings_.erase(binding);
    ambiguous_domain_instance_bindings_.erase(binding);
  }

  static string comment_text(string text) {
    string out;
    out.reserve(text.size());
    for (size_t i = 0; i < text.size(); ++i) {
      if (text[i] == '\r' || text[i] == '\n') {
        out.push_back(' ');
      } else if (text[i] == '*' && i + 1 < text.size() && text[i + 1] == '/') {
        out += "* /";
        ++i;
      } else {
        out.push_back(text[i]);
      }
    }
    return out;
  }

  static string handler_signature(const Handler& h) {
    if (!h.header.empty()) return h.header;
    std::ostringstream out;
    out << "on " << h.name << "(";
    for (size_t i = 0; i < h.params.size(); ++i) {
      if (i) out << ", ";
      out << h.params[i].name << ": " << h.params[i].type;
    }
    out << ")";
    if (h.reply_type) out << " -> " << *h.reply_type;
    return out.str();
  }

  static string state_field_signature(const Field& f) {
    if (!f.header.empty()) return f.header;
    string out = "var " + f.name + ": " + f.type;
    if (!f.init.empty()) out += " = " + f.init;
    return out;
  }

  static void source_comment(std::ostringstream& o, int spaces, int line,
                             const string& source) {
    if (line <= 0) return;
    o << string(spaces, ' ') << "// Moss line " << line;
    if (!source.empty()) o << ": " << comment_text(source);
    o << "\n";
  }

  static void backend_comment(std::ostringstream& o, int spaces, const string& text) {
    o << string(spaces, ' ') << "// Moss backend: " << comment_text(text) << "\n";
  }

  static void tooling_begin(std::ostringstream& o, int spaces,
                            const string& kind, const string& semantic_identity,
                            const string& generated_symbol,
                            const string& native_symbol = "") {
    o << string(spaces, ' ') << "// Moss tooling begin|" << kind << "|"
      << semantic_identity << "|" << generated_symbol << "|" << native_symbol
      << "\n";
  }

  static void tooling_end(std::ostringstream& o, int spaces,
                          const string& semantic_identity) {
    o << string(spaces, ' ') << "// Moss tooling end|" << semantic_identity
      << "\n";
  }

  void debug_symbol_attributes(std::ostringstream& o, int spaces,
                               const string& native_symbol,
                               bool can_export = true) const {
    if (debug_build_) o << string(spaces, ' ') << "#[inline(never)]\n";
    if (can_export)
      o << string(spaces, ' ') << "#[export_name = \"" << native_symbol
        << "\"]\n";
  }

  static string function_semantic_identity(
      const Function& function,
      const FunctionSpecialization* specialization = nullptr) {
    string identity = functional_function_context(function, specialization) +
        "@" + std::to_string(function.line);
    if (specialization)
      identity += "#" + specialization->stable_type_key;
    return identity;
  }

  static string method_semantic_identity(const ObjectType& object,
                                         const Method& method) {
    return functional_method_context(object, method) + "@" +
        std::to_string(method.line);
  }

  static string handler_semantic_identity(const Domain& domain,
                                          const Handler& handler) {
    return functional_handler_context(domain, handler) + "@" +
        std::to_string(handler.line);
  }

  // Mirrors Checker::echo_display_class for an already-checked Vector type.
  static bool native_echo_vector_displayable(const string& t) {
    string x = canonical_type_name(t);
    if (x == "int" || x == "float" || x == "bool" || x == "char" || x == "string" ||
        x == "vector" || internal_semantic_placeholder(x))
      return true;
    if (starts_with(x, "vector[") && ends_with(x, "]"))
      return native_echo_vector_displayable(trim(x.substr(7, x.size() - 8)));
    return false;
  }

  // The statically selected display-runtime formatter for one Vector element
  // type: a helper function for a scalar, or a closure that formats a nested
  // Vector with its own element formatter. nullopt when the type is open.
  static std::optional<string> native_echo_element(const string& t) {
    string x = canonical_type_name(t);
    if (x == "int") return string("__moss_display_int");
    if (x == "float") return string("__moss_display_float");
    if (x == "bool") return string("__moss_display_bool");
    if (x == "char") return string("__moss_display_char");
    if (x == "string") return string("__moss_display_string");
    if (starts_with(x, "vector[") && ends_with(x, "]")) {
      auto inner = native_echo_element(trim(x.substr(7, x.size() - 8)));
      if (!inner) return std::nullopt;
      return "|o: &mut String, v: &std::vec::Vec<_>| __moss_display_vector_into(o, v, &" +
             *inner + ")";
    }
    return std::nullopt;
  }

  bool shared_nominal_map_key(const string& key) const {
    const string concrete = canonical_type_name(key);
    return staged_abi_v10() &&
        p_.key_proof_ids_by_type.count(concrete) &&
        (objects_.count(concrete) || is_nominal_instance_type(concrete));
  }

  bool shared_protocol_map_key(const string& key) const {
    const string concrete = canonical_type_name(key);
    return shared_nominal_map_key(concrete) ||
        (staged_abi_v10() && concrete == "float" &&
         p_.key_proof_ids_by_type.count(concrete));
  }

  std::optional<string> shared_protocol_map_key_from_type(
      const string& type) const {
    auto elements = map_key_value_types(canonical_type_name(type));
    if (elements && shared_protocol_map_key(elements->first))
      return elements->first;
    return std::nullopt;
  }

  string rust_type(const string& t) const {
    string x = canonical_type_name(t);
    if (x == "int") return "i64";
    if (x == "float") return "f64";
    if (x == "bool") return "bool";
    if (x == "char") return "char";
    if (x == "string") return "MossStr";
    // Phase 20: runtime-facing semantic names reserved in
    // docs/ROOT_RUNTIME_ABI.md ("Runtime-facing semantic types"). Agent
    // D does not define these Rust types; Agent B's crate does.
    if (x == "FileIO") return "FileIO";
    if (x == "Range") return "Range";
    if (x == "RangeBatch") return "RangeBatch";
    if (x == "vector") return "std::vec::Vec<T>";
    if (x == "map") return "std::collections::HashMap<K, V>";
    if (x == "queue") return "std::collections::VecDeque<T>";
    if (x == "unit") return "()";
    if (domains_.count(x))
      return has_domain_specializations(x) ? x + "Handle" : x + "Ref";
    if (is_nominal_instance_type(x)) {
      string tname = nominal_template_name(x);
      auto subs = nominal_field_substitutions(x);
      string res = tname + "<";
      for (size_t i = 0; i < subs.size(); ++i) {
        if (i > 0) res += ", ";
        res += rust_type(subs[i].second);
      }
      res += ">";
      return res;
    }
    if (objects_.count(x) || enums_.count(x)) return x;
    if (starts_with(x, "seq[") && ends_with(x, "]"))
      return "std::vec::Vec<" + rust_type(x.substr(4, x.size()-5)) + ">";
    if (starts_with(x, "vector[") && ends_with(x, "]")) return "std::vec::Vec<" + rust_type(x.substr(7, x.size()-8)) + ">";
    if (starts_with(x, "queue[") && ends_with(x, "]")) {
      const string item = trim(x.substr(6, x.size() - 7));
      if (canonical_type_name(item) == "never") {
        bool authorized = false;
        if (p_.typed_semantics) {
          for (const auto& requirement :
               p_.typed_semantics->required_collection_outcome_occurrences) {
            if (requirement.operation !=
                TypedCollectionOutcomeFactV1::Operation::QueuePop) continue;
            const auto* fact = p_.typed_semantics->find_collection_outcome_fact(
                requirement);
            if (fact && fact->receiver_type &&
                fact->receiver_type->kind == TypeKind::Builtin &&
                fact->receiver_type->name == "queue" &&
                fact->receiver_type->type_args.size() == 1 &&
                fact->receiver_type->type_args.front()->kind == TypeKind::Never)
              authorized = true;
          }
        }
        if (!authorized)
          throw std::runtime_error(
              "MOSS_INTERNAL_INVARIANT Queue[Never] storage lacks exact checked outcome authority");
        return "std::collections::VecDeque<std::convert::Infallible>";
      }
      return "std::collections::VecDeque<" + rust_type(item) + ">";
    }
    if (staged_abi_v10() && starts_with(x, "set[") && ends_with(x, "]"))
      return "moss_runtime::MossSet<" + rust_type(x.substr(4, x.size()-5)) + ">";
    if (starts_with(x, "map[") && ends_with(x, "]")) {
      auto parsed = parsed_type_arguments(x);
      if (!parsed || parsed->size() != 2) return x;
      const auto& ps = *parsed;
      if (canonical_type_name(ps[1]) == "never") {
        bool authorized = false;
        if (p_.typed_semantics) {
          for (const auto& requirement :
               p_.typed_semantics->required_collection_outcome_occurrences) {
            if (requirement.operation !=
                TypedCollectionOutcomeFactV1::Operation::MapRemove) continue;
            const auto* fact = p_.typed_semantics->find_collection_outcome_fact(
                requirement);
            if (fact && fact->receiver_type &&
                fact->receiver_type->kind == TypeKind::Builtin &&
                fact->receiver_type->name == "map" &&
                fact->receiver_type->type_args.size() == 2 &&
                fact->receiver_type->type_args[1]->kind == TypeKind::Never &&
                fact->receiver_type->type_args[0]->to_canonical_string() ==
                    canonical_type_name(ps[0]))
              authorized = true;
          }
        }
        if (!authorized)
          throw std::runtime_error(
              "MOSS_INTERNAL_INVARIANT Map[K, Never] storage lacks exact checked outcome authority");
        return "std::collections::HashMap<" + rust_type(ps[0]) +
            ", std::convert::Infallible>";
      }
      if (shared_protocol_map_key(ps[0]))
        return "moss_runtime::MossMap<" + rust_type(ps[0]) + ", " +
               rust_type(ps[1]) + ">";
      return "std::collections::HashMap<" + rust_type(ps[0]) + ", " + rust_type(ps[1]) + ">";
    }
    if (x == "never")
      throw std::runtime_error(
          "MOSS_INTERNAL_INVARIANT unbounded internal Never type cannot be lowered");
    if (starts_with(x, "option[") && ends_with(x, "]"))
      return "Option<" + rust_type(x.substr(7, x.size()-8)) + ">";
    if (starts_with(x, "table[") && ends_with(x, "]")) {
      auto parsed = parsed_type_arguments(x);
      if (!parsed || parsed->size() != 2) return x;
      const auto& ps = *parsed;
      return "std::collections::HashMap<" + rust_type(ps[0]) + ", " + rust_type(ps[1]) + ">";
    }
    return x;
  }

  std::set<string> constraint_ops(const Function& f, const string& subject) const {
    std::set<string> out;
    for (const auto& c : f.constraints) if (c.subject == subject) {
      if (c.kind == ConstraintKind::Operator) out.insert(c.detail);
      if (c.kind == ConstraintKind::Indexable) out.insert("[]");
    }
    return out;
  }

  string default_value(const string& type) const {
    string t = canonical_type_name(type);
    if (t == "int") return "0";
    if (t == "float") return "0.0";
    if (t == "bool") return "false";
    if (t == "char") return "'\\0'";
    if (t == "string") return "MossStr::new()";
    if (starts_with(t, "seq[")) return "std::vec::Vec::new()";
    if (starts_with(t, "table[")) return "std::collections::HashMap::new()";
    if (starts_with(t, "option[")) return "None";
    auto object = objects_.find(t);
    if (object != objects_.end()) {
      string result = rust_type(t) + " { ";
      for (const auto& field : object->second->fields)
        result += field.name + ": " + default_value(field.type) + ", ";
      return result + "}";
    }
    return "Default::default()";
  }

  bool copy_type(const string& type) const {
    return copy_type_name(canonical_type_name(type));
  }

  bool is_primitive_copy_type(const moss::TypeRef& type) const {
    if (!type || type->kind != moss::TypeKind::Primitive) return false;
    const auto& name = type->name;
    return name == "Int" || name == "int" ||
           name == "Float" || name == "float" ||
           name == "Bool" || name == "bool" ||
           name == "Char" || name == "char" ||
           name == "Byte" || name == "byte" ||
           name == "i64" || name == "f64" || name == "u8";
  }

  std::optional<est::ExpressionView> expression_view_from_est(
      std::string_view expression,
      const vector<est::Handle>* handles = nullptr) const {
#ifdef MOSS_TEST_BUILD
    est::record_expression_text_lookup();
#endif
    const auto fingerprint = est::fingerprint(string(expression));
    const auto* selected = handles ? handles
        : active_functional_expression_handles_;
    if (selected) {
      for (const auto& handle : *selected) {
        if (handle.source_fingerprint != fingerprint ||
            handle.arena_index >= p_.expression_arenas.size() ||
            !p_.expression_arenas[handle.arena_index]) continue;
        const auto& arena = *p_.expression_arenas[handle.arena_index];
        const auto span = arena.span(handle.expr);
        // A fingerprint can match an identical expression nested in a larger
        // source slot. Its node spans are relative to that parent buffer, so
        // pairing it with the child text breaks EST projections (notably
        // arithmetic in a Root message argument). Reparse unless this handle
        // is rooted at the complete supplied expression.
        if (span.source_begin != 0 || span.source_end != expression.size())
          continue;
        return est::view(arena, handle.expr, expression);
      }
    }
#ifdef MOSS_TEST_BUILD
    est::record_consumer_reparse(est::ConsumerReparseSite::NativeExpressionView);
#endif
    return est::parse_view(expression);
  }

  bool tree_plain_identifier(std::string_view expression) const {
    auto syntax = expression_view_from_est(expression);
    if (!syntax) return false;
    string name;
    return est::project_identifier(*syntax, name);
  }

  bool tree_empty_constructor_named(std::string_view expression,
                                    std::string_view name) const {
    auto syntax = expression_view_from_est(expression);
    return syntax && est::empty_constructor_named(*syntax, name);
  }

  bool tree_empty_vector_literal(std::string_view expression) const {
    auto syntax = expression_view_from_est(expression);
    return syntax && est::empty_vector_literal(*syntax);
  }

  bool tree_placeholder_expression(std::string_view expression) const {
    auto syntax = expression_view_from_est(expression);
    return syntax && est::placeholder_expression(*syntax);
  }

  string replace_est_placeholders(const string& source,
                                  const string& replacement) const {
    auto syntax = expression_view_from_est(source);
    if (!syntax || !syntax->arena) return source;
    vector<EstSourceEdit> edits;
    visit_est_expression(*syntax, syntax->root,
        [&](est::ExprId id, const est::Node& node) {
      if (!std::holds_alternative<est::Placeholder>(node)) return;
      const auto span = syntax->arena->span(id);
      edits.push_back({span.source_begin, span.source_end, replacement});
    });
    return apply_est_source_edits(source, std::move(edits));
  }

  string rewrite_est_expression_source(
      const string& source, const Domain* domain,
      const std::set<string>& locals,
      const std::unordered_map<string,string>* types) const {
    auto syntax = expression_view_from_est(source);
    if (!syntax || !syntax->arena) return source;
    std::set<string> state_names;
    if (domain && !view_domain_ && !view_method_) {
      for (const auto& field : domain->state) state_names.insert(field.name);
      for (const auto& route : domain->routes) state_names.insert(route.name);
    }
    vector<EstSourceEdit> edits;
    visit_est_expression(*syntax, syntax->root,
        [&](est::ExprId id, const est::Node& node) {
      const auto span = syntax->arena->span(id);
      if (const auto* identifier = std::get_if<est::Identifier>(&node)) {
        const string name = identifier->name;
        const string token_source = syntax->source_text(id).value_or(name);
        std::optional<string> replacement;
        if (view_domain_ || view_method_ || !view_parameters_.empty())
          replacement = view_place(token_source, domain, locals, types);
        if (!replacement && state_names.count(name) && !locals.count(name))
          replacement = (objects_.count(domain->name) ? "self." : "state.") + name;
        if (!replacement && write_through_parameters_.count(name))
          replacement = "(*" + name + ")";
        if (!replacement && rust_identifier(name) != name)
          replacement = rust_identifier(name);
        if (replacement)
          edits.push_back({span.source_begin, span.source_end,
                           std::move(*replacement)});
      } else if (const auto* member = std::get_if<est::Member>(&node)) {
        const string node_source = syntax->source_text(id).value_or(string());
        if ((view_domain_ || view_method_ || !view_parameters_.empty()))
          if (auto place = view_place(node_source, domain, locals, types)) {
            edits.push_back({span.source_begin, span.source_end,
                             std::move(*place)});
            return;
          }
        const string escaped = rust_identifier(member->name);
        if (escaped != member->name &&
            member->name.size() <= span.source_end - span.source_begin) {
          const auto begin = span.source_end - member->name.size();
          edits.push_back({begin, span.source_end, escaped});
        }
      }
    });
    return apply_est_source_edits(source, std::move(edits));
  }

  est::LiteralClass tree_literal_class(std::string_view expression) const {
    auto syntax = expression_view_from_est(expression);
    return syntax ? est::root_literal_class(*syntax) : est::LiteralClass::None;
  }

  bool tree_decimal_integer_literal(std::string_view expression) const {
    auto syntax = expression_view_from_est(expression);
    return syntax && est::decimal_integer_literal(*syntax, true, false);
  }

  std::optional<DecodedNumber> tree_numeric_literal_value(
      std::string_view expression) const {
    auto syntax = expression_view_from_est(expression);
    return syntax ? est::numeric_literal_value(*syntax)
                  : std::nullopt;
  }

  std::optional<string> tree_empty_typed_vector_constructor(
      std::string_view expression) const {
    auto syntax = expression_view_from_est(expression);
    if (!syntax) return std::nullopt;
    auto type = est::project_empty_constructor_type(*syntax);
    if (!type) return std::nullopt;
    string canonical = canonical_type_name(*type);
    return starts_with(canonical, "vector[") && ends_with(canonical, "]")
        ? std::optional<string>(std::move(canonical)) : std::nullopt;
  }

  std::optional<string> tree_identifier_name(
      std::string_view expression) const {
    auto syntax = expression_view_from_est(expression);
    if (!syntax) return std::nullopt;
    string name;
    return est::project_identifier(*syntax, name)
        ? std::optional<string>(std::move(name)) : std::nullopt;
  }

  bool tree_expressions_equal(std::string_view left,
                              std::string_view right) const {
    auto left_tree = expression_view_from_est(left);
    auto right_tree = expression_view_from_est(right);
    return left_tree && right_tree &&
        est::structurally_equal(*left_tree, *right_tree);
  }

  bool tree_empty_string_literal(std::string_view expression) const {
    auto syntax = expression_view_from_est(expression);
    if (!syntax || !syntax->arena || syntax->root >= syntax->arena->size())
      return false;
    const auto* literal = std::get_if<est::Literal>(
        &syntax->arena->at(syntax->root));
    return literal && literal->spelling == "\"\"";
  }

  std::optional<vector<string>> tree_member_path(
      std::string_view expression) const {
    auto syntax = expression_view_from_est(expression);
    if (!syntax || !syntax->arena || syntax->root >= syntax->arena->size())
      return std::nullopt;
    vector<string> reverse;
    est::ExprId current = syntax->root;
    while (current < syntax->arena->size()) {
      const auto& node = syntax->arena->at(current);
      if (const auto* member = std::get_if<est::Member>(&node)) {
        reverse.push_back(member->name);
        current = member->base;
      } else if (const auto* identifier =
                     std::get_if<est::Identifier>(&node)) {
        reverse.push_back(identifier->name);
        std::reverse(reverse.begin(), reverse.end());
        return reverse;
      } else {
        return std::nullopt;
      }
    }
    return std::nullopt;
  }

  std::optional<vector<string>> tree_tuple_elements(
      std::string_view expression) const {
    auto syntax = expression_view_from_est(expression);
    return syntax ? est::project_tuple_elements(*syntax) : std::nullopt;
  }

  bool tree_parse_simple_call(const string& expression, string& callee,
                              vector<string>& arguments) const {
    auto syntax = expression_view_from_est(expression);
    return syntax && est::project_simple_call(*syntax, callee, arguments);
  }

  bool tree_parse_member_call(const string& expression, string& receiver,
                              string& member, vector<string>& arguments,
                              bool* outputs_written = nullptr) const {
    auto syntax = expression_view_from_est(expression);
    if (!syntax) {
      if (outputs_written) *outputs_written = false;
      return false;
    }
    return est::project_member_call(*syntax, receiver, member, arguments,
                                    outputs_written);
  }

  bool tree_parse_member_access(const string& expression, string& base,
                               string& member) const {
    auto syntax = expression_view_from_est(expression);
    return syntax && est::project_member_access(*syntax, base, member);
  }

  bool has_nested_index_member_projection(const string& expression) const {
    string current = expression;
    string base, member;
    bool outer_member = true;
    while (tree_parse_member_access(current, base, member)) {
      if (!outer_member) {
        string index_base, index_expression;
        if (tree_parse_index(base, index_base, index_expression))
          return true;
      }
      outer_member = false;
      current = std::move(base);
    }
    return false;
  }

  bool tree_parse_unary(const string& expression, string& operation,
                        string& operand) const {
    auto syntax = expression_view_from_est(expression);
    return syntax && est::project_unary(*syntax, operation, operand);
  }

  bool tree_parse_message(const string& expression, string& target,
                          string& handler, vector<string>& arguments) const {
    auto syntax = expression_view_from_est(expression);
    return syntax && est::project_message(
        *syntax, target, handler, arguments);
  }

  bool tree_parse_fileio_open(const string& expression,
                              vector<string>& arguments) const {
    auto syntax = expression_view_from_est(expression);
    return syntax && est::project_fileio_open(*syntax, arguments);
  }

  bool tree_parse_fileio_chunks_source(const string& expression,
                                       string& receiver,
                                       string& size_expression) const {
    auto syntax = expression_view_from_est(expression);
    return syntax && est::project_fileio_chunks_source(
        *syntax, receiver, size_expression);
  }

  bool tree_parse_index(const string& expression, string& base,
                        string& index) const {
    auto syntax = expression_view_from_est(expression);
    return syntax && est::project_index(*syntax, base, index);
  }

  bool tree_parse_named_argument(const string& expression, string& name,
                                 string& value) const {
    auto syntax = est::parse_named_argument_view(expression);
    return syntax && est::project_named_argument(*syntax, name, value);
  }

  std::optional<vector<string>> tree_parse_vector_literal(
      const string& expression) const {
    auto syntax = expression_view_from_est(expression);
    return syntax ? est::project_vector_literal(*syntax) : std::nullopt;
  }

  bool tree_parse_enum_case_expression(const string& expression,
                                       string& enum_name, string& case_name,
                                       vector<string>& fields,
                                       bool& called) const {
    auto syntax = expression_view_from_est(expression);
    return syntax && est::project_enum_case(*syntax, enum_name, case_name,
                                            fields, &called);
  }

  bool tree_parse_bound_method_callable(const string& expression,
                                        string& receiver,
                                        string& method) const {
    auto syntax = expression_view_from_est(expression);
    return syntax && est::project_bound_method(*syntax, receiver, method);
  }

  bool tree_parse_executor_chain(
      const string& expression,
      vector<std::pair<string, vector<string>>>& calls) const {
    auto syntax = expression_view_from_est(expression);
    return syntax && est::project_executor_chain(*syntax, calls);
  }

  string tree_strip_expression_grouping(const string& expression) const {
    auto syntax = expression_view_from_est(expression);
    return syntax ? est::strip_grouping(*syntax) : expression;
  }

  string tree_normalize_pipeline(const string& expression) const {
    auto syntax = expression_view_from_est(expression);
    return syntax ? est::normalize_pipeline_tree(*syntax) : expression;
  }

  bool tree_expression_mentions_identifier(
      const string& expression, const string& identifier) const {
    auto syntax = expression_view_from_est(expression);
    return syntax && est::expression_mentions(*syntax, identifier);
  }

  std::optional<vector<string>> parsed_type_arguments(
      const string& type) const {
    auto parsed = Type::parse_canonical(type);
    if (!parsed) return std::nullopt;
    vector<string> result;
    result.reserve((*parsed)->type_args.size());
    for (const auto& argument : (*parsed)->type_args)
      result.push_back(argument->to_canonical_string());
    return result;
  }

  bool borrowable_type(const string& type) const {
    string t = canonical_type_name(type);
    if (t.empty() || contains_internal_placeholder(t) || copy_type(t) || domains_.count(t)) return false;
    return true;
  }

  ConcreteParameterABI function_parameter_abi(
      const Function& function, size_t index, const string& concrete_type,
      const FunctionSpecialization* specialization = nullptr) const {
    Effect effect = specialization && index < specialization->parameter_effects.size()
        ? specialization->parameter_effects[index]
        : specialized_iterator_parameter_effect(function, index, concrete_type, objects_);
    if (specialization) return concrete_parameter_abi(effect, concrete_type);
    // Preserve unspecialized/explicitly typed ABI, including primitive WRITE.
    bool borrow = effect == Effect::Write ||
        (effect != Effect::Consume && borrowable_type(concrete_type));
    auto ops = constraint_ops(function, function.params[index].name);
    if (effect == Effect::Read && function.params[index].type.empty() &&
        !ops.empty() && !ops.count("[]")) borrow = false;
    return {effect, !borrow ? ParameterPassingMode::Value :
        effect == Effect::Write ? ParameterPassingMode::MutableBorrow :
                                 ParameterPassingMode::SharedBorrow};
  }

  Effect function_effect(const Function& function, size_t index,
                         const string& concrete_type = {},
                         const FunctionSpecialization* specialization = nullptr) const {
    return function_parameter_abi(function, index, concrete_type, specialization).effect;
  }

  static Effect method_effect(const Method& method, size_t index) {
    return index < method.parameter_effects.size()
        ? method.parameter_effects[index] : Effect::Read;
  }

  static SpecId checked_method_body_spec_id(const Method& method) {
    if (method.specializations.size() != 1 ||
        method.specializations.front().spec_id == InvalidSpecId)
      throw std::runtime_error(
          "MOSS_INTERNAL_INVARIANT method body lacks one concrete checked SpecId");
    return method.specializations.front().spec_id;
  }

  SpecId checked_handler_body_spec_id(const Domain& domain,
                                     const Handler& handler) const {
    const Program& checked_program = semantic_program_ ? *semantic_program_ : p_;
    const auto source_entry = this->specialization_sources_.find(domain.name);
    const string source_name = source_entry == this->specialization_sources_.end()
        ? domain.name : source_entry->second;
    const auto source = std::find_if(
        checked_program.domains.begin(), checked_program.domains.end(),
        [&](const Domain& candidate) { return candidate.name == source_name; });
    if (source == checked_program.domains.end())
      throw std::runtime_error(
          "MOSS_INTERNAL_INVARIANT checked handler source declaration is missing");
    size_t handler_index = source->handlers.size();
    size_t declaration_matches = 0;
    for (size_t index = 0; index < source->handlers.size(); ++index) {
      if (source->handlers[index].spec_id == handler.spec_id) {
        handler_index = index;
        ++declaration_matches;
      }
    }
    if (handler.spec_id == InvalidSpecId ||
        handler.spec_id >= checked_program.specialization_table.size() ||
        declaration_matches != 1 || handler_index >= source->handlers.size())
      throw std::runtime_error(
          "MOSS_INTERNAL_INVARIANT checked handler declaration SpecId is missing or ambiguous");

    SpecId selected = handler.spec_id;
    if (source_entry != this->specialization_sources_.end()) {
      const DomainSpecialization* owner = nullptr;
      size_t owner_count = 0;
      for (const auto& candidate : checked_program.domain_specializations) {
        const auto generated = specialization_names_.find(
            candidate.source_domain + "\n" + candidate.instance);
        if (!candidate.materialized_layout ||
            candidate.source_domain != source_name ||
            generated == specialization_names_.end() ||
            generated->second != domain.name)
          continue;
        owner = &candidate;
        ++owner_count;
      }
      if (owner_count != 1 || !owner ||
          owner->handler_spec_ids.size() != source->handlers.size())
        throw std::runtime_error(
            "MOSS_INTERNAL_INVARIANT checked domain handler specialization is missing or ambiguous");
      selected = owner->handler_spec_ids[handler_index];
    }
    if (selected == InvalidSpecId ||
        selected >= checked_program.specialization_table.size())
      throw std::runtime_error(
          "MOSS_INTERNAL_INVARIANT checked handler body SpecId is invalid");
    const auto& selected_row = checked_program.specialization_table[selected];
    const auto& declaration_row =
        checked_program.specialization_table[handler.spec_id];
    if (selected_row.id != selected ||
        declaration_row.id != handler.spec_id ||
        selected_row.semantic_callable_identity !=
            declaration_row.semantic_callable_identity ||
        selected_row.module_identity != declaration_row.module_identity)
      throw std::runtime_error(
          "MOSS_INTERNAL_INVARIANT checked handler body SpecId contradicts its declaration");
    return selected;
  }

  const FunctionSpecialization* call_specialization(
      const Function& function, const vector<string>& arguments,
      const std::unordered_map<string,string>* types,
      const NativeEstCursor* occurrence = nullptr) const {
    (void)arguments;
    (void)types;
    if (!function.static_dispatch) return nullptr;
    const Program& checked_program = semantic_program_ ? *semantic_program_ : p_;
    const NativeEstCursor* exact = occurrence ? occurrence : active_native_est_cursor_;
    if (!exact || !exact->arena || exact->arena_index == UINT32_MAX ||
        exact->expression == UINT32_MAX ||
        exact->expression >= exact->arena->size())
      throw std::runtime_error(
          "MOSS_INTERNAL_INVARIANT static Native call lacks exact EST cursor");
    const auto call_expression = est::unparenthesized(
        *exact->arena, exact->expression);
    if (!std::holds_alternative<est::Call>(exact->arena->at(call_expression)))
      throw std::runtime_error(
          "MOSS_INTERNAL_INVARIANT static Native call cursor is not a Call node");
    if (codegen_body_spec_id_ == InvalidSpecId ||
        codegen_body_spec_id_ >= checked_program.specialization_table.size() ||
        checked_program.specialization_table[codegen_body_spec_id_].id !=
            codegen_body_spec_id_)
      throw std::runtime_error(
          "MOSS_INTERNAL_INVARIANT Native body SpecId is missing or contradictory");
    const auto lookup = select_exact_semantic_call_edge(
        checked_program, codegen_body_spec_id_, exact->arena_index,
        call_expression);
    if (lookup.kind != ExactSemanticCallEdgeLookup::Kind::Unique ||
        !lookup.edge)
      throw std::runtime_error(
          "MOSS_INTERNAL_INVARIANT exact Native call edge is missing or duplicated");
    const auto& edge = *lookup.edge;
    if (edge.target_kind != SemanticCallTargetKind::Static ||
        edge.caller_spec_id != codegen_body_spec_id_ ||
        edge.callee_spec_id == InvalidSpecId ||
        edge.callee_spec_id >= checked_program.specialization_table.size())
      throw std::runtime_error(
          "MOSS_INTERNAL_INVARIANT exact Native call edge is unresolved or contradictory");
    const auto& target = checked_program.specialization_table[edge.callee_spec_id];
    if (target.id != edge.callee_spec_id ||
        target.semantic_callable_identity != function.semantic_callable_id)
      throw std::runtime_error(
          "MOSS_INTERNAL_INVARIANT checked Native call target identity disagrees with declaration");
    const FunctionSpecialization* selected = nullptr;
    for (const auto& candidate : function.specializations) {
      if (candidate.spec_id != edge.callee_spec_id) continue;
      if (selected)
        throw std::runtime_error(
            "MOSS_INTERNAL_INVARIANT duplicate FunctionSpecialization SpecId");
      selected = &candidate;
    }
    if (!selected && function.spec_id != edge.callee_spec_id)
      throw std::runtime_error(
          "MOSS_INTERNAL_INVARIANT checked callee SpecId is absent from function body");
    return selected;
  }

  const Method* checked_method_for_occurrence(
      std::string_view method_name,
      const NativeEstCursor* occurrence = nullptr) const {
    const Program& checked_program = semantic_program_ ? *semantic_program_ : p_;
    const NativeEstCursor* exact = occurrence ? occurrence : active_native_est_cursor_;
    if (!exact || !exact->arena || exact->arena_index == UINT32_MAX ||
        exact->expression == UINT32_MAX ||
        exact->expression >= exact->arena->size() ||
        codegen_body_spec_id_ == InvalidSpecId)
      throw std::runtime_error(
          "MOSS_INTERNAL_INVARIANT static Native method lacks exact caller/EST identity");
    const auto call_expression = est::unparenthesized(
        *exact->arena, exact->expression);
    if (!std::holds_alternative<est::Call>(exact->arena->at(call_expression)))
      throw std::runtime_error(
          "MOSS_INTERNAL_INVARIANT static Native method cursor is not a Call node (ExprId=" +
          std::to_string(call_expression) + ", node-kind=" +
          std::to_string(exact->arena->at(call_expression).index()) +
          ", span=" + std::to_string(exact->arena->span(call_expression).source_begin) +
          ".." + std::to_string(exact->arena->span(call_expression).source_end) +
          (std::holds_alternative<est::Member>(exact->arena->at(call_expression))
              ? ", member=" + std::get<est::Member>(exact->arena->at(call_expression)).name +
                ", base=" + std::to_string(std::get<est::Member>(exact->arena->at(call_expression)).base)
              : "") + ")");
    const auto lookup = select_exact_semantic_call_edge(
        checked_program, codegen_body_spec_id_, exact->arena_index,
        call_expression);
    if (lookup.kind != ExactSemanticCallEdgeLookup::Kind::Unique ||
        !lookup.edge || lookup.edge->target_kind != SemanticCallTargetKind::Static ||
        lookup.edge->callee_spec_id == InvalidSpecId ||
        lookup.edge->callee_spec_id >= checked_program.specialization_table.size())
      throw std::runtime_error(
          "MOSS_INTERNAL_INVARIANT exact Native method edge is missing or contradictory");
    const auto& row = checked_program.specialization_table[
        lookup.edge->callee_spec_id];
    if (row.id != lookup.edge->callee_spec_id)
      throw std::runtime_error(
          "MOSS_INTERNAL_INVARIANT checked Native method table row is contradictory");
    const Method* selected = nullptr;
    std::size_t matches = 0;
    for (const auto& object : objects_)
      for (const auto& candidate : object.second->methods) {
        if (candidate.name != method_name ||
            candidate.semantic_callable_id != row.semantic_callable_identity)
          continue;
        for (const auto& specialization : candidate.specializations)
          if (specialization.spec_id == lookup.edge->callee_spec_id) {
            selected = &candidate;
            ++matches;
          }
      }
    if (matches != 1 || !selected)
      throw std::runtime_error(
          "MOSS_INTERNAL_INVARIANT checked Native method SpecId has no unique body");
    return selected;
  }

  const Method* checked_method_body_for_spec_id(SpecId spec_id) const {
    const Program& checked_program = semantic_program_ ? *semantic_program_ : p_;
    if (spec_id == InvalidSpecId ||
        spec_id >= checked_program.specialization_table.size())
      throw std::runtime_error(
          "MOSS_INTERNAL_INVARIANT checked method body SpecId is invalid");
    const auto& row = checked_program.specialization_table[spec_id];
    if (row.id != spec_id)
      throw std::runtime_error(
          "MOSS_INTERNAL_INVARIANT checked method body table row is contradictory");
    const Method* selected = nullptr;
    std::size_t matches = 0;
    for (const auto& object : objects_)
      for (const auto& candidate : object.second->methods) {
        if (candidate.semantic_callable_id != row.semantic_callable_identity)
          continue;
        for (const auto& specialization : candidate.specializations)
          if (specialization.spec_id == spec_id) {
            selected = &candidate;
            ++matches;
          }
      }
    if (matches != 1 || !selected)
      throw std::runtime_error(
          "MOSS_INTERNAL_INVARIANT method body SpecId has no unique declaration");
    return selected;
  }

  string nominal_domain_handle_argument(
      const string& expression, const string& type, const Domain* d,
      const std::set<string>& locals,
      const std::unordered_map<string,string>* types,
      size_t functional_pipeline_id = 0) const {
    string rendered = expr(expression, d, locals, types, functional_pipeline_id);
    string nominal = canonical_type_name(type);
    string binding = trim(expression);
    auto identity = domain_instance_bindings_.find(binding);
    if (types && tree_plain_identifier(binding) && domains_.count(nominal) &&
        identity != domain_instance_bindings_.end() &&
        identity->second.source_domain == nominal &&
        is_domain_specialization_instance(nominal, identity->second.instance)) {
      rendered = nominal + "Handle::from(" + rendered + ")";
    }
    return rendered;
  }

  // key-protocol-v1 scalar operations lowered to the exact Moss operation
  // for the statically known type; no Rust trait or adapter is involved.
  static string scalar_protocol_rust(const string& op, const string& type,
                                     const string& a, const string& b = "") {
    const string t = canonical_type_name(type);
    if (op == "less") {
      if (t == "float") return "moss_float_less(" + a + ", " + b + ")";
      return "(&(" + a + ") < &(" + b + "))";
    }
    if (op == "isequal") {
      if (t == "float") return "moss_float_isequal(" + a + ", " + b + ")";
      return "(&(" + a + ") == &(" + b + "))";
    }
    if (t == "float") return "moss_float_hash(" + a + ")";
    if (t == "int") return "(" + a + ")";
    if (t == "bool") return "(if " + a + " { 1i64 } else { 0i64 })";
    if (t == "char") return "((" + a + ") as u32 as i64)";
    return "moss_string_hash(&(" + a + "))";
  }

  string shared_map_hash(const string& key_type, const string& key) const {
    return shared_nominal_map_key(key_type) ? "(" + key + ").hash()"
        : scalar_protocol_rust("hash", key_type, key);
  }

  string shared_map_equal(const string& key_type, const string& a,
                          const string& b) const {
    return shared_nominal_map_key(key_type) ? "(" + a + ").isequal(" + b + ")"
        : scalar_protocol_rust("isequal", key_type, "*" + a, "*" + b);
  }

  // Rust comparator for a key-protocol-v1 total-order scalar. Float uses
  // the Julia-like total order; the other scalars' Rust Ord already matches
  // Moss `less` (Bool false < true, String by UTF-8 bytes = code points).
  static string total_order_compare_rust(const string& key_type,
                                         const string& left, const string& right) {
    if (canonical_type_name(key_type) == "float")
      return "moss_float_total_cmp(" + left + ", " + right + ")";
    return "(" + left + ").cmp(&(" + right + "))";
  }

  // Lowers sort(v), sort(v, by: key), and sort(v, by: Reverse(key)) to a
  // stable in-place Rust sort. The checker has proven the key pure, so each
  // key is lowered as an ordinary specialized Moss call.
  string sort_call_rust(const vector<string>& args, const Domain* d,
                        const std::set<string>& locals,
                        const std::unordered_map<string,string>* types) const {
    string target = write_call_place(args[0], d, locals, types);
    auto vector_type = generated_expr_type(args[0], types);
    string canonical = vector_type ? canonical_type_name(*vector_type) : "";
    string element = starts_with(canonical, "vector[")
        ? canonical_type_name(canonical.substr(7, canonical.size() - 8)) : "";
    if (args.size() == 1)
      return "(" + target + ").sort_by(|__moss_a, __moss_b| " +
             total_order_compare_rust(element, "*__moss_a", "*__moss_b") + ")";
    string label, key;
    tree_parse_named_argument(args[1], label, key);
    bool reverse = false;
    string reverse_callee;
    vector<string> reverse_args;
    if (tree_parse_simple_call(key, reverse_callee, reverse_args) &&
        reverse_callee == "Reverse" && reverse_args.size() == 1) {
      reverse = true;
      key = trim(reverse_args.front());
    }
    const bool copy = copy_type(element);
    auto key_locals = locals;
    key_locals.insert("__moss_sort_a");
    key_locals.insert("__moss_sort_b");
    std::unordered_map<string,string> key_types = types ? *types
        : std::unordered_map<string,string>{};
    key_types["__moss_sort_a"] = element;
    key_types["__moss_sort_b"] = element;
    string key_a = expr(key + "(__moss_sort_a)", d, key_locals, &key_types);
    string key_b = expr(key + "(__moss_sort_b)", d, key_locals, &key_types);
    string key_type = generated_expr_type(key + "(__moss_sort_a)", &key_types)
        .value_or("");
    string compare = reverse
        ? total_order_compare_rust(key_type, "__moss_kb", "__moss_ka")
        : total_order_compare_rust(key_type, "__moss_ka", "__moss_kb");
    // A key that consumes its argument receives an owned copy; the vector
    // element itself is never moved out during the sort.
    auto key_function = functions_.find(key);
    const bool consumes = !copy && key_function != functions_.end() &&
        sort_key_effect(*key_function->second, element) == Effect::Consume;
    string bind = copy ? "*__moss_a" : consumes ? "(*__moss_a).clone()" : "__moss_a";
    string bind_b = copy ? "*__moss_b" : consumes ? "(*__moss_b).clone()" : "__moss_b";
    return "(" + target + ").sort_by(|__moss_a, __moss_b| { "
           "let __moss_sort_a = " + bind + "; let __moss_sort_b = " + bind_b + "; "
           "let __moss_ka = " + key_a + "; let __moss_kb = " + key_b + "; " +
           compare + " })";
  }

  string write_call_place(const string& argument, const Domain* d,
                          const std::set<string>& locals,
                          const std::unordered_map<string,string>* types) const {
    string value = tree_strip_expression_grouping(trim(argument));
    // Borrowed object views need their mutable accessors. For paths that
    // cross a container, recursively lower the base as a place, never as an
    // owned read expression (index reads may clone non-Copy elements).
    if (view_place(value, d, locals, types))
      return place_expr(value, d, locals, types);
    string base, index;
    if (!tree_parse_index(value, base, index)) {
      string field;
      if (tree_parse_member_access(value, base, field)) {
        return "(" + write_call_place(base, d, locals, types) +
            ")." + rust_identifier(field);
      }
      return place_expr(value, d, locals, types);
    }
    string storage = write_call_place(base, d, locals, types);
    string key = expr(index, d, locals, types);
    auto type = generated_expr_type(base, types);
    if (type && shared_protocol_map_key_from_type(*type)) {
      const string key_type = *shared_protocol_map_key_from_type(*type);
      return "*({ let __moss_key = " + key + "; (" + storage +
          ").get_mut_with(" + shared_map_hash(key_type, "__moss_key") +
          ", &__moss_key, |a, b| " + shared_map_equal(key_type, "a", "b") +
          ").expect(\"Moss missing map key\") })";
    }
    if (type && (starts_with(canonical_type_name(*type), "map[") || canonical_type_name(*type) == "map"))
      return "*(" + storage + ").get_mut(&(" + key + ")).expect(\"Moss missing map key\")";
    if (type && (canonical_type_name(*type) == "RangeBatch" || canonical_type_name(*type) == "Range"))
      return "(" + storage + ").get(" + key + ")";
    return "(" + storage + ")[(" + key + ") as usize]";
  }

  string read_call_place(const string& argument, const Domain* d,
                         const std::set<string>& locals,
                         const std::unordered_map<string,string>* types) const {
    string proj_base, proj_field;
    if (tree_parse_member_access(trim(argument), proj_base, proj_field) &&
        tree_plain_identifier(proj_field)) {
      string p_b, p_i;
      if (tree_parse_index(proj_base, p_b, p_i)) {
        auto projection_type = generated_expr_type(proj_base, types);
        if (projection_type) {
          auto object = objects_.find(nominal_template_name(*projection_type));
          if (object != objects_.end()) {
            auto field = std::find_if(object->second->fields.begin(),
                object->second->fields.end(), [&](const Field& candidate) {
                  return candidate.name == proj_field;
                });
            if (field != object->second->fields.end()) {
              return "(" + read_call_place(proj_base, d, locals, types) + ")." +
                  rust_identifier(proj_field);
            }
          }
        }
      }
    }
    string base, index;
    if (!tree_parse_index(trim(argument), base, index)) return expr(argument, d, locals, types);
    string storage = read_call_place(base, d, locals, types);
    string key = expr(index, d, locals, types);
    auto type = generated_expr_type(base, types);
    if (const auto* conditional = resolve_direct_map_read_error_fact()) {
      const string key_type = canonical_type_name(conditional->key_type);
      string lookup;
      if (shared_protocol_map_key_from_type(key_type)) {
        const string shared_key = *shared_protocol_map_key_from_type(key_type);
        lookup = "get_with(" + shared_map_hash(shared_key, "__moss_checked_key") +
            ", &__moss_checked_key, |a, b| " +
            shared_map_equal(shared_key, "a", "b") + ")";
      } else if (key_type == "string") {
        lookup = "get(__moss_checked_key.as_str())";
      } else {
        lookup = "get(&__moss_checked_key)";
      }
      const string exit = phase21_collection_error_exit(
          conditional->error_variant_identity);
      return "*({ let __moss_checked_key = " + key + "; match (" + storage +
          ")." + lookup + " { Some(__moss_entry) => __moss_entry, None => " +
          exit + " } })";
    }
    if (type && shared_protocol_map_key_from_type(*type)) {
      const string key_type = *shared_protocol_map_key_from_type(*type);
      return "*({ let __moss_key = " + key + "; (" + storage +
          ").get_with(" + shared_map_hash(key_type, "__moss_key") +
          ", &__moss_key, |a, b| " + shared_map_equal(key_type, "a", "b") +
          ").expect(\"Moss missing map key\") })";
    }
    if (type && (starts_with(canonical_type_name(*type), "map[") || canonical_type_name(*type) == "map"))
      return "*(" + storage + ").get(&(" + key + ")).expect(\"Moss missing map key\")";
    if (type && (canonical_type_name(*type) == "RangeBatch" || canonical_type_name(*type) == "Range"))
      return "(" + storage + ").get(" + key + ")";
    return "(" + storage + ")[(" + key + ") as usize]";
  }

  string function_call_argument(const Function& function, size_t index,
                                const string& argument, const Domain* d,
                                const std::set<string>& locals,
                                const std::unordered_map<string,string>* types,
                                size_t functional_pipeline_id = 0,
                                const FunctionSpecialization* specialization = nullptr,
                                const NativeEstCursor* occurrence = nullptr) const {
    struct NativeEstCursorScope {
      const NativeEstCursor*& slot;
      const NativeEstCursor* prior;
      NativeEstCursorScope(const NativeEstCursor*& target,
                           const NativeEstCursor* current)
          : slot(target), prior(target) { slot = current; }
      ~NativeEstCursorScope() { slot = prior; }
    } native_est_cursor_scope(active_native_est_cursor_,
                              occurrence ? occurrence
                                         : active_native_est_cursor_);
    if (index >= function.params.size())
      return nominal_domain_handle_argument(argument, "", d, locals, types,
                                            functional_pipeline_id);
    string actual_type = generated_expr_type(argument, types).value_or("");
    auto abi = function_parameter_abi(function, index, actual_type, specialization);
    Effect effect = abi.effect;
    const auto argument_binding = tree_identifier_name(argument);
    if (effect == Effect::Consume &&
        argument_binding && phase21_option_slots_.count(*argument_binding))
      return "__moss_recovery_slot_" + *argument_binding +
          ".take().unwrap_or_else(|| std::process::abort())";
    string rendered = nominal_domain_handle_argument(
        argument, function.params[index].type, d, locals, types,
        functional_pipeline_id);
    if (abi.passing == ParameterPassingMode::Value) return rendered;
    // Non-object payloads arrive through the internal handler ABI as `&T`.
    // Preserve that borrow when handing it to a READ helper. Object payloads
    // retain the extra reference below because view helpers take
    // `&impl MossAccess_T`.
    if (effect == Effect::Read && borrowed_message_parameter(argument) &&
        !view_object_type(actual_type))
      return rendered;
    if (effect == Effect::Write) return "&mut (" + write_call_place(argument, d, locals, types) + ")";
    return "&(" + read_call_place(argument, d, locals, types) + ")";
  }

  string method_call_argument(const Method& method, size_t index,
                              const string& argument, const Domain* d,
                              const std::set<string>& locals,
                              const std::unordered_map<string,string>* types,
                              size_t functional_pipeline_id = 0) const {
    if (index >= method.params.size())
      return nominal_domain_handle_argument(argument, "", d, locals, types,
                                            functional_pipeline_id);
    Effect effect = method_effect(method, index);
#ifdef MOSS_TEST_BUILD
    if (std::getenv("MOSS_TEST_TRACE_NATIVE_METHOD_ABI")) {
      std::cerr << "NATIVE_METHOD_ARG method=" << method.semantic_callable_id
                << " index=" << index << " effect="
                << static_cast<unsigned>(effect) << " effects=";
      for (const auto item : method.parameter_effects)
        std::cerr << static_cast<unsigned>(item) << ",";
      std::cerr << " spec-rows=";
      for (const auto& row : method.specializations)
        std::cerr << row.spec_id << ",";
      std::cerr << "\n";
    }
#endif
    const auto argument_binding = tree_identifier_name(argument);
    if (effect == Effect::Consume &&
        argument_binding && phase21_option_slots_.count(*argument_binding))
      return "__moss_recovery_slot_" + *argument_binding +
          ".take().unwrap_or_else(|| std::process::abort())";
    string rendered = nominal_domain_handle_argument(
        argument, method.params[index].type, d, locals, types,
        functional_pipeline_id);
    string type = method.params[index].type;
    if (effect == Effect::Consume || (effect != Effect::Write && !borrowable_type(type))) return rendered;
    if (effect == Effect::Read && borrowed_message_parameter(argument) &&
        !view_object_type(generated_expr_type(argument, types).value_or("")))
      return rendered;
    if (effect == Effect::Write) return "&mut (" + write_call_place(argument, d, locals, types) + ")";
    return "&(" + read_call_place(argument, d, locals, types) + ")";
  }

  struct GeneratedBinaryExpression {
    string left;
    string op;
    string right;
  };

  bool generated_integer_literal(const string& expression) const {
    return tree_decimal_integer_literal(expression);
  }

  string generated_integer_literal_as_float(string expression) const {
    // An Int literal in a Float context converts exactly as `as f64` does.
    auto decoded = tree_numeric_literal_value(expression);
    if (!decoded || decoded->kind != NumericLiteral::Kind::Int)
      throw std::runtime_error("internal error: numeric literal '" + expression +
                               "' was not validated by the lexer");
    DecodedNumber as_float;
    as_float.kind = NumericLiteral::Kind::Float;
    as_float.floating = static_cast<double>(decoded->integer);
    return rust_numeric_literal(as_float);
  }

  std::optional<GeneratedBinaryExpression> tree_generated_binary(
      const string& expression, const vector<string>& operators) const {
    auto syntax = expression_view_from_est(expression);
    if (!syntax) return std::nullopt;
    vector<string> tree_operators;
    tree_operators.reserve(operators.size());
    for (const auto& op : operators) tree_operators.push_back(trim(op));
    std::pair<string,string> operands;
    string operation;
    if (!est::project_binary(*syntax, tree_operators, operands, operation))
      return std::nullopt;
    return GeneratedBinaryExpression{std::move(operands.first),
                                     std::move(operation),
                                     std::move(operands.second)};
  }

  static std::optional<string> generated_functional_element_type(
      const string& type) {
    const auto term = parsed_type(type);
    if (applied_builtin(term, {"vector", "seq"}) && term->type_args.size() == 1)
      return term->type_args[0]->to_canonical_string();
    return std::nullopt;
  }

  string resolved_callable_identity(
      string callable, const std::unordered_map<string,string>* types) const {
    callable = trim(std::move(callable));
    if (types) {
      auto local = types->find(callable);
      if (local != types->end() && callable_identity(local->second)) {
        if (starts_with(local->second, "callable-v1:"))
          return local->second;
        return local->second.substr(9);
      }
    }
    if ((moss_test_drop_fact("call-targets") ||
         moss_test_perturb_fact("call-targets")) &&
        functions_.count(callable))
      throw std::runtime_error(
          "MOSS_INTERNAL_INVARIANT checked call target fact was dropped or perturbed");
    return callable;
  }

  bool generated_expression_uses(const string& expression,
                                const string& name) const {
    auto syntax = expression_view_from_est(expression);
    return syntax && est::contains_identifier(*syntax, name);
  }

  std::optional<string> generated_callable_result(
      const string& callable, const vector<string>& input_types,
      const std::unordered_map<string,string>* types,
      const ParsedFunctionalStage* checked_stage = nullptr) const {
    (void)callable;
    (void)input_types;
    (void)types;
    if (!checked_stage || checked_stage->arena_index == UINT32_MAX ||
        checked_stage->expression_id == UINT32_MAX)
      return std::nullopt;
    const FunctionalNode* node = nullptr;
    const Program& checked_program = semantic_program_ ? *semantic_program_ : p_;
    for (const auto& pipeline : checked_program.functional_pipelines) {
      if (pipeline.specialization_id != codegen_body_spec_id_) continue;
      for (const auto& candidate : pipeline.nodes) {
        if (candidate.est_arena_index != checked_stage->arena_index ||
            candidate.est_expression_id != checked_stage->expression_id)
          continue;
        if (node)
          throw std::runtime_error(
              "MOSS_INTERNAL_INVARIANT duplicate checked functional stage identity");
        node = &candidate;
      }
    }
    if (!node || node->output_type.empty()) return std::nullopt;
    return node->output_type;
  }

  std::optional<ParsedFunctionalPipeline> functional_pipeline_from_est(
      std::string_view expression,
      const vector<est::Handle>* handles = nullptr) const {
    (void)handles;
    const auto resolve_stage = [](std::string_view name)
        -> std::optional<FunctionalNodeKind> {
      for (const auto& operation : functional_operations())
        if (name == operation.name) return operation.kind;
      return std::nullopt;
    };
    const NativeEstCursor* cursor = active_native_est_cursor_;
    if (!cursor || !cursor->arena || cursor->arena_index == UINT32_MAX ||
        cursor->expression == UINT32_MAX ||
        cursor->expression >= cursor->arena->size()) {
      return std::nullopt;
    }
    const auto root = est::unparenthesized(*cursor->arena, cursor->expression);
    if (!std::holds_alternative<est::Pipeline>(cursor->arena->at(root)))
      return std::nullopt;
    return est::functional_pipeline(*cursor->arena, root, expression,
                                    resolve_stage);
  }

  std::optional<string> generated_functional_pipeline_type(
      const string& expression,
      const std::unordered_map<string,string>* types) const {
    auto pipeline = functional_pipeline_from_est(expression);
    if (!pipeline) return std::nullopt;
    auto source_type = generated_expr_type(pipeline->source, types);
    if (!source_type) return std::nullopt;
    auto element = generated_functional_element_type(*source_type);
    if (!element) return std::nullopt;
    string current = *element;
    string output = "vector[" + current + "]";
    for (const auto& stage : pipeline->stages) {
      switch (stage.kind) {
        case FunctionalNodeKind::Map: {
          if (stage.arguments.size() != 1) return std::nullopt;
          auto result = generated_callable_result(stage.arguments.front(),
                                                  {current}, types, &stage);
          if (!result) return std::nullopt;
          current = *result;
          output = "vector[" + current + "]";
          break;
        }
        case FunctionalNodeKind::Filter:
          output = "vector[" + current + "]";
          break;
        case FunctionalNodeKind::Reduce:
          if (stage.arguments.size() != 2) return std::nullopt;
          output = generated_expr_type(stage.arguments.front(), types).value_or("");
          break;
        case FunctionalNodeKind::Sum:
          output = current;
          break;
        case FunctionalNodeKind::Count:
          output = "int";
          break;
        case FunctionalNodeKind::Any:
        case FunctionalNodeKind::All:
          output = "bool";
          break;
        case FunctionalNodeKind::Source:
          return std::nullopt;
      }
    }
    return output.empty() ? std::nullopt : std::optional<string>(output);
  }

  std::optional<string> generated_expr_type(
      const string& expression,
      const std::unordered_map<string,string>* types) const {
#ifdef MOSS_TEST_BUILD
    if (est::test_coverage_counters_enabled()) {
      auto record_type_fact_coverage = [&] {
#ifdef MOSS_TEST_BUILD
        est::record_expression_text_lookup();
#endif
        const auto fingerprint = est::fingerprint(expression);
        const vector<est::Handle>* handles =
            active_functional_expression_handles_;
        const est::Arena* arena = nullptr;
        est::ExprId root = 0;
        if (handles) {
          for (const auto& handle : *handles) {
            if (handle.source_fingerprint != fingerprint ||
                handle.arena_index >= p_.expression_arenas.size() ||
                !p_.expression_arenas[handle.arena_index]) continue;
            const auto* candidate = p_.expression_arenas[handle.arena_index].get();
            const auto span = candidate->span(handle.expr);
            if (span.source_begin != 0 || span.source_end != expression.size())
              continue;
            arena = candidate;
            root = handle.expr;
            break;
          }
        }
        est::Arena temporary;
        if (!arena) {
          try {
            root = est::parse_expression(temporary, expression);
            arena = &temporary;
          } catch (const std::invalid_argument&) {
            est::record_generated_type_fact_coverage("unparsed", false);
            return;
          }
        }
        const auto* facts = arena->facts(root, codegen_body_spec_id_);
        const bool present = facts && facts->type;
        const auto& node = arena->at(root);
        const char* kind = std::visit([](const auto& value) -> const char* {
          using T = std::decay_t<decltype(value)>;
          if constexpr (std::is_same_v<T, est::Literal>) return "literal";
          else if constexpr (std::is_same_v<T, est::Identifier>) return "identifier";
          else if constexpr (std::is_same_v<T, est::Unary>) return "unary";
          else if constexpr (std::is_same_v<T, est::Binary>) return "binary";
          else if constexpr (std::is_same_v<T, est::Call>) return "call";
          else if constexpr (std::is_same_v<T, est::Take>) return "take";
          else if constexpr (std::is_same_v<T, est::Member>) return "member";
          else if constexpr (std::is_same_v<T, est::Index>) return "index";
          else if constexpr (std::is_same_v<T, est::Parenthesized>) return "parenthesized";
          else if constexpr (std::is_same_v<T, est::Message>) return "message";
          else if constexpr (std::is_same_v<T, est::NamedArgument>) return "named_argument";
          else if constexpr (std::is_same_v<T, est::Placeholder>) return "placeholder";
          else if constexpr (std::is_same_v<T, est::Collection>) return "collection";
          else if constexpr (std::is_same_v<T, est::Tuple>) return "tuple";
          else if constexpr (std::is_same_v<T, est::Constructor>) return "constructor";
          else if constexpr (std::is_same_v<T, est::Pipeline>) return "pipeline";
          else return "missing";
        }, node);
        est::record_generated_type_fact_coverage(kind, present);
      };
      try {
        record_type_fact_coverage();
      } catch (...) {
        // Test-only coverage accounting must not change emission behavior.
      }
    }
#endif
    string original = tree_strip_expression_grouping(expression);
    {
      string callee;
      vector<string> arguments;
      if (tree_parse_simple_call(original, callee, arguments) && callee == "replace" &&
          arguments.size() == 2)
        return generated_expr_type(arguments[0], types);
      if (tree_parse_simple_call(original, callee, arguments) && !functions_.count(callee)) {
        if (callee == "Char" && arguments.size() == 1) return string("char");
        if ((callee == "less" || callee == "isequal") && arguments.size() == 2)
          return string("bool");
        if (callee == "hash" && arguments.size() == 1) return string("int");
      }
    }
    string message_target, message_handler;
    vector<string> message_arguments;
    if (types && tree_parse_message(original, message_target, message_handler,
                                    message_arguments)) {
      string receiver = message_target;
      string handler = message_handler;
      vector<string> arguments = std::move(message_arguments);
      if (tree_plain_identifier(receiver)) {
        auto type = types->find(receiver);
        if (type != types->end()) {
          auto domain = domains_.find(type->second);
          if (domain != domains_.end())
            if (const Handler* target = find_handler(*domain->second, handler))
              return target->reply_type.value_or("unit");
        }
      }
      return std::nullopt;
    }
    if (auto pipeline = generated_functional_pipeline_type(original, types))
      return pipeline;
    string value = tree_strip_expression_grouping(tree_normalize_pipeline(std::move(original)));
    {
      string enum_name, case_name;
      vector<string> fields;
      bool called = false;
      if (tree_parse_enum_case_expression(value, enum_name, case_name, fields, called) &&
          enums_.count(enum_name))
        return enum_name;
    }
    const auto literal = tree_literal_class(value);
    if (literal == est::LiteralClass::Boolean) return string("bool");
    if (literal == est::LiteralClass::Character) return string("char");
    if (tree_identifier_name(value) == std::optional<string>("None"))
      return string("_none");
    if (literal == est::LiteralClass::String)
      return string("string");
    if (types) {
      auto identifier = tree_identifier_name(value);
      if (!identifier && tree_placeholder_expression(value)) identifier = "_";
      if (identifier) {
        auto local = types->find(*identifier);
        if (local != types->end() && !local->second.empty() &&
            !contains_internal_placeholder(local->second))
          return canonical_type_name(local->second);
      }
    }
    string unary_operation, unary_operand;
    if (tree_parse_unary(value, unary_operation, unary_operand) &&
        unary_operation == "not") {
      auto operand = generated_expr_type(unary_operand, types);
      return operand && canonical_type_name(*operand) == "bool"
          ? std::optional<string>("bool") : std::nullopt;
    }
    if (tree_plain_identifier(value) && functions_.count(value))
      return "callable:" + value;
    if (auto typed_vector = tree_empty_typed_vector_constructor(value)) return *typed_vector;
    if (generated_integer_literal(value))
      return string("int");
    if (literal == est::LiteralClass::Float) return string("float");
    if (auto elements = tree_parse_vector_literal(value)) {
      if (elements->empty()) return string("vector");
      string element_type;
      for (const auto& element : *elements) {
        auto current = generated_expr_type(element, types);
        if (!current) return std::nullopt;
        string concrete = canonical_type_name(*current);
        if (element_type.empty()) element_type = concrete;
        else if (element_type != concrete) return std::nullopt;
      }
      return "vector[" + element_type + "]";
    }

    string receiver, method_name;
    vector<string> method_args;
    if (tree_parse_member_call(value, receiver, method_name, method_args)) {
      if (staged_abi_v10() && tree_empty_constructor_named(receiver, "Map") &&
          method_name == "get" && method_args.size() == 2)
        return generated_expr_type(method_args[1], types);
      string canon_recv = canonical_type_name(receiver);
      if (canon_recv == "int" || canon_recv == "float" || canon_recv == "char" ||
          canon_recv == "string" || canon_recv == "bool") {
        if ((method_name == "less" || method_name == "isequal") && method_args.size() == 2)
          return string("bool");
        if (method_name == "hash" && method_args.size() == 1)
          return string("int");
        if (method_name == "to_string" && method_args.size() == 1)
          return string("string");
        if (canon_recv == "char" && method_name == "codepoint" && method_args.size() == 1)
          return string("int");
      }
      auto receiver_type = generated_expr_type(receiver, types);
      if (receiver_type) {
        string concrete_receiver = canonical_type_name(*receiver_type);
        // Phase 20: Executor configuration/start chain and FileIO operations;
        // mirrors the checker's inferred_expr_type handling.
        if (concrete_receiver == "executor" &&
            (executor_config_methods().count(method_name) || method_name == "start"))
          return string("executor");
        if (concrete_receiver == "FileIO") {
          if (method_name == "read" && method_args.size() == 2) return string("Range");
          if (method_name == "read" && method_args.size() == 1) return string("RangeBatch");
          if (method_name == "chunks" && method_args.size() == 1) return string("seq[Range]");
          if ((method_name == "write" && method_args.size() == 2) ||
              (method_name == "sync" && method_args.size() <= 1) ||
              (method_name == "close" && method_args.empty()))
            return string("unit");
        }
        if (concrete_receiver == "Range" && method_name == "length" && method_args.empty())
          return string("int");
        if (concrete_receiver == "Range" && method_name == "slice" && method_args.size() == 2)
          return string("Range");
        if (auto result = builtin_operation_result(concrete_receiver, method_name,
                                                    method_args.size()))
          return result;
        const auto call_occurrence =
            native_member_method_call_cursor(active_native_est_cursor_);
        auto match = checked_method_for_occurrence(
            method_name, call_occurrence ? &*call_occurrence : nullptr);
        if (match && match->return_type)
          return canonical_type_name(*match->return_type);
      }
    }

    string callee;
    vector<string> call_args;
    if (tree_parse_simple_call(value, callee, call_args)) {
      if (objects_.count(callee)) {
        const ObjectType* obj = objects_.at(callee);
        bool has_generic_field = std::any_of(obj->fields.begin(), obj->fields.end(),
            [](const Field& f) { return f.inferred || f.type.empty(); });
        if (!has_generic_field) return callee;
        std::map<string, string> supplied_types;
        for (const auto& arg : call_args) {
          string fn, fv;
          if (tree_parse_named_argument(arg, fn, fv)) {
            auto ft = generated_expr_type(fv, types);
            if (ft) supplied_types[fn] = canonical_type_name(*ft);
          }
        }
        string instance_type = callee + "[";
        bool first = true;
        for (const auto& f : obj->fields) {
          if (!f.inferred && !f.type.empty()) continue;
          if (!first) instance_type += ",";
          first = false;
          auto it = supplied_types.find(f.name);
          string ft = (it != supplied_types.end()) ? it->second : (f.type.empty() ? "_" : canonical_type_name(f.type));
          instance_type += f.name + "=" + ft;
        }
        instance_type += "]";
        return instance_type;
      }
      if (callee == "sqrt" && call_args.size() == 1) {
        auto argument = generated_expr_type(call_args.front(), types);
        return argument && canonical_type_name(*argument) == "float"
            ? std::optional<string>("float") : std::nullopt;
      }
      if (callee == "Some" && call_args.size() == 1) {
        auto value_type = generated_expr_type(call_args.front(), types);
        return value_type ? "option[" + *value_type + "]" : "option[_]";
      }
      if (callee == "copy" && call_args.size() == 1) {
        return generated_expr_type(call_args.front(), types);
      }
      if (callee == "range" && (call_args.size() == 2 || call_args.size() == 3))
        return "range[int]";
      string callee_id = resolved_callable_identity(callee, types);
      const Function* function_target = nullptr;
      if (starts_with(callee_id, "callable-v1:")) {
        function_target = find_function_by_callable_id(callee_id);
      } else {
        auto function = functions_.find(callee_id);
        if (function != functions_.end()) function_target = function->second;
      }
      if (function_target && function_target->return_type) {
        vector<string> argument_types;
        for (std::size_t index = 0; index < call_args.size(); ++index) {
          auto child_cursor = native_call_argument_cursor(
              active_native_est_cursor_, index);
          NativeEstCursorScope child_cursor_scope(
              active_native_est_cursor_,
              child_cursor ? &*child_cursor : nullptr);
          const auto& argument = call_args[index];
          argument_types.push_back(generated_expr_type(argument, types).value_or(""));
        }
        if (function_target->static_dispatch) {
          const auto* specialization = call_specialization(
              *function_target, call_args, types,
              active_native_est_cursor_);
          if (specialization)
            return specialization->return_type;
          return std::nullopt;
        }
        string result = canonical_type_name(*function_target->return_type);
        if (open_generic(result)) {
          string parameter = result.substr(9);
          for (size_t index = 0; index < function_target->params.size(); ++index)
            if (function_target->params[index].name == parameter &&
                index < argument_types.size() && !argument_types[index].empty())
              return argument_types[index];
          return std::nullopt;
        }
        if (!contains_internal_placeholder(result)) return result;
      }
    }

    string member_base, member_name;
    if (tree_parse_member_access(value, member_base, member_name)) {
      auto base = generated_expr_type(member_base, types);
      if (base) {
        string tname = nominal_template_name(*base);
        auto object = objects_.find(tname);
        if (object != objects_.end()) {
          string field_name = member_name;
          if (auto sub_type = nominal_field_type(*base, field_name)) {
            return canonical_type_name(*sub_type);
          }
          for (const auto& field : object->second->fields)
            if (field.name == field_name)
              return canonical_type_name(field.type);
        }
      }
    }
    string index_base, index_expr;
    if (tree_parse_index(value, index_base, index_expr)) {
      auto base_type = generated_expr_type(index_base, types);
      if (base_type) {
        string concrete = canonical_type_name(*base_type);
        if (concrete == "RangeBatch") return string("Range");
        if (auto element = indexed_element(parsed_type(concrete)))
          return element;
      }
    }
    if (tree_generated_binary(value, {" or "}) ||
        tree_generated_binary(value, {" xor "}) ||
        tree_generated_binary(value, {" and "}) ||
        tree_generated_binary(value, {"==", "!=", "<=", ">=", "<", ">"}))
      return string("bool");
    for (const auto& ops : vector<vector<string>>{{"|"}, {"^"}, {"&"}, {"<<", ">>"}}) {
      if (auto binary = tree_generated_binary(value, ops)) {
        auto left = generated_expr_type(binary->left, types);
        auto right = generated_expr_type(binary->right, types);
        if (left && right &&
            canonical_type_name(*left) == "int" &&
            canonical_type_name(*right) == "int")
          return string("int");
      }
    }
    for (const auto& operators : vector<vector<string>>{{"+", "-"}, {"*", "/", "%"}}) {
      auto binary = tree_generated_binary(value, operators);
      if (!binary) continue;
      auto left = generated_expr_type(binary->left, types);
      auto right = generated_expr_type(binary->right, types);
      if (left && right) {
        if (*left == "string" && *right == "string" && binary->op == "+")
          return string("string");
        if ((*left == "int" || *left == "float") &&
            (*right == "int" || *right == "float"))
          return (*left == "float" || *right == "float") ? "float" : "int";
      }
    }
    if (tree_parse_unary(value, unary_operation, unary_operand) &&
        (unary_operation == "-" || unary_operation == "+")) {
      auto operand = generated_expr_type(unary_operand, types);
      if (operand && (canonical_type_name(*operand) == "int" ||
                      canonical_type_name(*operand) == "float"))
        return operand;
    }
    if (tree_parse_unary(value, unary_operation, unary_operand) &&
        unary_operation == "~") {
      auto operand = generated_expr_type(unary_operand, types);
      if (operand && canonical_type_name(*operand) == "int")
        return string("int");
    }
    return std::nullopt;
  }

  static string generated_type_list(const vector<string>& types) {
    std::ostringstream out;
    for (size_t index = 0; index < types.size(); ++index) {
      if (index) out << ", ";
      out << (types[index].empty() ? "unresolved" : types[index]);
    }
    return out.str();
  }

  bool is_pure_state_read_expression(const string& expression) const {
    string value = trim(expression);
    // This prelude is solely a Rust evaluation-order repair.  Do not move a
    // source call or message across an earlier WRITE argument.
    string target, handler;
    vector<string> message_arguments;
    if (tree_parse_message(value, target, handler, message_arguments)) return false;
    string callee, receiver, method;
    vector<string> arguments;
    return !tree_parse_simple_call(value, callee, arguments) &&
        !tree_parse_member_call(value, receiver, method, arguments);
  }

  string render_known_function_call(
      const Function& function, const string& name, const vector<string>& arguments,
      const Domain* domain, const std::set<string>& locals,
      const std::unordered_map<string,string>* types,
      const NativeEstCursor* occurrence = nullptr) const {
    const auto* specialization = call_specialization(
        function, arguments, types, occurrence);
    const auto external_module = function.static_dispatch ? std::nullopt :
        phase21_external_module_for_symbol(name);
    const bool cross_crate = external_module.has_value();
    const bool standard_write_bridge = cross_crate &&
        std::any_of(function.params.begin(), function.params.end(),
            [&](const Param& parameter) {
              const size_t index = static_cast<size_t>(
                  &parameter - function.params.data());
              return !standard_error_variants(canonical_type_name(
                  parameter.type)).empty() &&
                  function_parameter_abi(function, index, parameter.type,
                      specialization).effect == Effect::Write;
            });
    std::ostringstream bridge_before, bridge_after;
    vector<string> temporaries(arguments.size());
    if (domain) {
      for (size_t write_index = 0;
           write_index < arguments.size() && write_index < function.params.size();
           ++write_index) {
        if (function_effect(function, write_index,
                generated_expr_type(arguments[write_index], types).value_or(""), specialization)
            != Effect::Write) continue;
        string written = trim(arguments[write_index]);
        bool writes_direct_field = !locals.count(written) &&
            std::any_of(domain->state.begin(), domain->state.end(),
                        [&](const Field& field) { return field.name == written; });
        if (!writes_direct_field) continue;
        for (size_t read_index = write_index + 1;
             read_index < arguments.size() && read_index < function.params.size();
             ++read_index) {
          if (!is_pure_state_read_expression(arguments[read_index]))
            continue;
          auto argument_type = generated_expr_type(arguments[read_index], types);
          if (!argument_type || !copy_type(*argument_type))
            continue;
          bool reads_sibling = false;
          for (const auto& field : domain->state) {
            if (field.name != written &&
                tree_expression_mentions_identifier(arguments[read_index], field.name)) {
              reads_sibling = true;
              break;
            }
          }
          if (reads_sibling)
            temporaries[read_index] = "__moss_call_argument_" +
                std::to_string(call_argument_temp_++);
        }
      }
    }

    std::ostringstream call;
    call << viewed_function_name(name, arguments, domain, locals, types) << "(";
    auto nested_occurrence = [&](size_t index) {
      uint32_t occurrence = 0;
      for (size_t prior = 0; prior < index; ++prior)
        if (tree_expressions_equal(arguments[prior], arguments[index])) ++occurrence;
      return occurrence;
    };
    struct NestedArgumentScope {
      std::optional<uint32_t>& slot;
      std::optional<uint32_t> previous;
      NestedArgumentScope(std::optional<uint32_t>& target, uint32_t occurrence)
          : slot(target), previous(target) { slot = occurrence; }
      ~NestedArgumentScope() { slot = previous; }
    };
    size_t emitted_arguments = 0;
    for (size_t index = 0; index < arguments.size(); ++index) {
      const NativeEstCursor* parent_cursor = occurrence
          ? occurrence : active_native_est_cursor_;
      auto child_cursor = native_call_argument_cursor(parent_cursor, index);
      if (parent_cursor && !child_cursor)
        throw std::runtime_error(
            "MOSS_INTERNAL_INVARIANT Native call argument lacks exact EST child");
      NestedArgumentScope nested_scope(codegen_nested_argument_occurrence_,
                                       nested_occurrence(index));
      bool compile_time_callable = is_compile_time_callable(
          generated_expr_type(arguments[index], types).value_or(""));
      if (compile_time_callable) continue;
      if (emitted_arguments++) call << ", ";
      {
        string rendered = temporaries[index].empty()
            ? function_call_argument(function, index, arguments[index], domain,
                                     locals, types, 0, specialization,
                                     child_cursor ? &*child_cursor : nullptr)
            : temporaries[index];
        if (cross_crate && index < function.params.size() &&
            !standard_error_variants(
                canonical_type_name(function.params[index].type)).empty()) {
          const auto abi = function_parameter_abi(function, index,
              generated_expr_type(arguments[index], types).value_or(""),
              specialization);
          const string external_type = tooling_name("moss_" +
              *external_module) + "::" +
              canonical_type_name(function.params[index].type);
          if (abi.effect == Effect::Write) {
            const string prefix = "__moss_standard_write_" +
                std::to_string(call_argument_temp_++);
            const string local_ref = prefix + "_local";
            const string provider_value = prefix + "_provider";
            bridge_before << "let " << local_ref << " = " << rendered
              << "; let mut " << provider_value << " = " << external_type
              << "::from((*" << local_ref << ").clone()); ";
            bridge_after << "*" << local_ref << " = "
              << canonical_type_name(function.params[index].type)
              << "::from(" << provider_value << "); ";
            rendered = "&mut " + provider_value;
          } else
            rendered = abi.passing == ParameterPassingMode::Value
                ? external_type + "::from(" + rendered + ")"
                : "&(" + external_type +
                    "::from((* (" + rendered + ")).clone()))";
        }
        if (standard_write_bridge &&
            !(cross_crate && index < function.params.size() &&
              !standard_error_variants(canonical_type_name(
                  function.params[index].type)).empty() &&
              function_parameter_abi(function, index,
                  generated_expr_type(arguments[index], types).value_or(""),
                  specialization).effect == Effect::Write)) {
          const string staged = "__moss_call_argument_" +
              std::to_string(call_argument_temp_++);
          bridge_before << "let " << staged << " = " << rendered << "; ";
          rendered = staged;
        }
        call << rendered;
      }
    }
    call << ")";
    if (!bridge_before.str().empty()) {
      const string native_call = call.str();
      call.str("");
      call.clear();
      call << "{ " << bridge_before.str() << "let __moss_bridge_result = "
        << native_call << "; " << bridge_after.str()
        << "__moss_bridge_result }";
    }
    bool needs_prelude = std::any_of(
        temporaries.begin(), temporaries.end(),
        [](const string& temporary) { return !temporary.empty(); });
    const bool raises = !(specialization ? specialization->observable_effects
                                       : function.observable_effects).raise_set.empty();
    const string result_type = specialization ? specialization->return_type :
        function.return_type.value_or("");
    const bool convert_result = cross_crate &&
        !standard_error_variants(canonical_type_name(result_type)).empty();
    if (convert_result) {
      const string rendered = call.str();
      call.str("");
      call.clear();
      call << (raises ? "(" + rendered + ").map(" + result_type + "::from)"
                      : result_type + "::from(" + rendered + ")");
    }
    if (raises) {
      if (!phase21_result_context_)
        throw std::runtime_error(
            "internal error: checked raising function call lacks a Phase 21 outcome context");
      const string propagated = phase21_propagate(call.str());
      call.str("");
      call.clear();
      call << propagated;
    }
    if (!needs_prelude) return call.str();

    std::ostringstream lowered;
    lowered << "{ ";
    for (size_t index = 0; index < arguments.size(); ++index)
      if (!temporaries[index].empty()) {
        NestedArgumentScope nested_scope(codegen_nested_argument_occurrence_,
                                         nested_occurrence(index));
        lowered << "let " << temporaries[index] << " = "
                << expr(arguments[index], domain, locals, types) << "; ";
      }
    lowered << call.str() << " }";
    return lowered.str();
  }

  string emitted_function_name(
      const string& name, const vector<string>& arguments,
      const std::unordered_map<string,string>* types) const {
    const Function* fn = nullptr;
    if (starts_with(name, "callable-v1:")) {
      fn = find_function_by_callable_id(name);
    } else {
      auto function = functions_.find(name);
      if (function != functions_.end()) fn = function->second;
    }
    if (!fn || !fn->static_dispatch)
      return fn ? fn->name : name;
    const auto* specialization = call_specialization(
        *fn, arguments, types, active_native_est_cursor_);
    if (!specialization)
      throw std::runtime_error("missing static specialization for function '" + (fn ? fn->name : name) +
                               "' at its checked call site");
    return specialization->generated_name;
  }

  struct FunctionalValue {
    string expression;
    string type;
    bool reference = false;
  };

  const FunctionalPipeline* planned_functional_pipeline(
      size_t functional_pipeline_id) const {
    if (functional_pipeline_id == 0) return nullptr;
    auto planned = std::find_if(
        p_.functional_pipelines.begin(), p_.functional_pipelines.end(),
        [&](const FunctionalPipeline& candidate) {
          return candidate.transient_id == functional_pipeline_id;
        });
    if (planned == p_.functional_pipelines.end())
      throw std::runtime_error(
          "internal error: checked functional_pipeline_id has no IR plan");
    return &*planned;
  }

  const FunctionalTraversalGroup* functional_traversal_group(
      size_t group_id) const {
    if (!group_id) return nullptr;
    auto found = std::find_if(
        p_.functional_traversal_groups.begin(),
        p_.functional_traversal_groups.end(),
        [&](const FunctionalTraversalGroup& group) {
          return group.transient_id == group_id;
        });
    if (found == p_.functional_traversal_groups.end())
      throw std::runtime_error(
          "internal error: functional traversal group has no IR plan");
    return &*found;
  }

  static LoweredFunctionalPipeline lower_functional_semantic_plan(
      const ParsedFunctionalPipeline& parsed,
      const FunctionalPipeline& planned) {
    LoweredFunctionalPipeline lowered;
    lowered.source = parsed.source;
    for (const auto& semantic_step : planned.semantic_steps) {
      if (semantic_step.kind == FunctionalNodeKind::Source ||
          semantic_step.source_node_indices.empty())
        throw std::runtime_error(
            "internal error: invalid functional semantic step");
      LoweredFunctionalStage stage;
      stage.kind = semantic_step.kind;
      for (size_t node_index : semantic_step.source_node_indices) {
        if (node_index == 0 || node_index > parsed.stages.size() ||
            node_index >= planned.nodes.size())
          throw std::runtime_error(
              "internal error: functional semantic step has stale source node");
        const ParsedFunctionalStage& operation =
            parsed.stages[node_index - 1];
        if (operation.kind != semantic_step.kind ||
            planned.nodes[node_index].kind != semantic_step.kind)
          throw std::runtime_error(
              "internal error: functional semantic rewrite changed operator kind");
        stage.operations.push_back(operation);
      }
      lowered.stages.push_back(std::move(stage));
    }
    return lowered;
  }

  static const ParsedFunctionalStage& terminal_operation(
      const LoweredFunctionalPipeline& pipeline) {
    if (pipeline.stages.empty() ||
        pipeline.stages.back().operations.size() != 1 ||
        !functional_terminal_kind(pipeline.stages.back().kind))
      throw std::runtime_error(
          "internal error: lowered functional pipeline has no terminal");
    return pipeline.stages.back().operations.front();
  }

  const FunctionalNode& checked_functional_stage_node(
      const ParsedFunctionalStage& stage) const {
    if (stage.arena_index == UINT32_MAX || stage.expression_id == UINT32_MAX)
      throw std::runtime_error(
          "MOSS_INTERNAL_INVARIANT functional callback lacks exact EST stage identity");
    const FunctionalNode* selected = nullptr;
    const Program& checked_program = semantic_program_ ? *semantic_program_ : p_;
    for (const auto& pipeline : checked_program.functional_pipelines) {
      if (pipeline.specialization_id != codegen_body_spec_id_) continue;
      for (const auto& node : pipeline.nodes) {
        if (node.est_arena_index != stage.arena_index ||
            node.est_expression_id != stage.expression_id)
          continue;
        if (selected)
          throw std::runtime_error(
              "MOSS_INTERNAL_INVARIANT duplicate checked functional stage identity");
        selected = &node;
      }
    }
    if (!selected)
      throw std::runtime_error(
          "MOSS_INTERNAL_INVARIANT checked functional stage identity is missing");
    return *selected;
  }

  SpecId checked_functional_callback_spec_id(
      const ParsedFunctionalStage& stage) const {
    const auto& node = checked_functional_stage_node(stage);
    if (node.callable_argument_index == UINT32_MAX ||
        node.callable_argument_index >= node.est_argument_expression_ids.size())
      throw std::runtime_error(
          "MOSS_INTERNAL_INVARIANT functional callback argument slot is missing or contradictory");
    const Program& checked_program = semantic_program_ ? *semantic_program_ : p_;
    const auto lookup = select_exact_semantic_call_edge(
        checked_program, codegen_body_spec_id_, stage.arena_index,
        stage.expression_id);
    if (lookup.kind != ExactSemanticCallEdgeLookup::Kind::Unique ||
        !lookup.edge)
      throw std::runtime_error(
          "MOSS_INTERNAL_INVARIANT functional callback Call edge is missing or duplicated");
    const auto& edge = *lookup.edge;
    if (edge.caller_spec_id != codegen_body_spec_id_ ||
        edge.call_est_arena_index != stage.arena_index ||
        edge.call_expression_occurrence_id != stage.expression_id ||
        edge.callback_argument_index != node.callable_argument_index ||
        edge.argument_callable_spec_ids.size() !=
            node.est_argument_expression_ids.size())
      throw std::runtime_error(
          "MOSS_INTERNAL_INVARIANT functional callback Call edge contradicts checked stage identity");
    if (edge.target_kind != SemanticCallTargetKind::Static)
      throw std::runtime_error(
          "MOSS_INTERNAL_INVARIANT Native functional callback lacks a static checked target");
    const SpecId callback_spec_id =
        edge.argument_callable_spec_ids[node.callable_argument_index];
    if (callback_spec_id == InvalidSpecId ||
        callback_spec_id >= checked_program.specialization_table.size() ||
        checked_program.specialization_table[callback_spec_id].id !=
            callback_spec_id)
      throw std::runtime_error(
          "MOSS_INTERNAL_INVARIANT functional callback argument SpecId is missing or contradictory");
    return callback_spec_id;
  }

  string render_functional_callable(
      const string& callable, const vector<FunctionalValue>& arguments,
      const Domain* domain, const std::set<string>& locals,
      const std::unordered_map<string,string>* types,
      bool propagate_raises = true,
      bool checked_placeholder_result = false,
      const ParsedFunctionalStage* checked_stage = nullptr) const {
    if (generated_expression_uses(callable, "_")) {
      if (arguments.size() != 1)
        throw std::runtime_error("internal error: placeholder functional arity");
      string replaced = replace_est_placeholders(
          callable, arguments.front().expression);
      std::unordered_map<string,string> callback_types = types
          ? *types : std::unordered_map<string,string>{};
      callback_types[arguments.front().expression] = arguments.front().type;
      auto callback_locals = locals;
      callback_locals.insert(arguments.front().expression);
      // A checked chunk mapper runs in its own Rust callback result. Its
      // expression must propagate into that result, then the surrounding
      // chunk lowering decides whether to commit the indexed error. In
      // particular, a lexical try label or Root capture exit outside this
      // callback is not reachable from inside the generated closure.
      if (checked_placeholder_result && !propagate_raises) {
        Generator callback = *this;
        callback.phase21_try_label_.clear();
        callback.phase21_capture_handler_ = nullptr;
        string rendered = callback.expr(replaced, domain, callback_locals,
                                        &callback_types);
        return "(|| { let __moss_callback_value = " + rendered +
            "; Ok::<_, __MossRaised>(__moss_callback_value) })()";
      }
      return expr(replaced, domain, callback_locals, &callback_types);
    }

    string bound_receiver, bound_method;
    if (tree_parse_bound_method_callable(callable, bound_receiver, bound_method)) {
      if (!checked_stage)
        throw std::runtime_error(
            "MOSS_INTERNAL_INVARIANT bound functional method lacks checked stage identity");
      const SpecId target_spec_id =
          checked_functional_callback_spec_id(*checked_stage);
      const Method* method = nullptr;
      for (const auto& object : objects_)
        for (const auto& candidate : object.second->methods)
          for (const auto& specialization : candidate.specializations)
            if (specialization.spec_id == target_spec_id) {
              if (method)
                throw std::runtime_error(
                    "MOSS_INTERNAL_INVARIANT duplicate checked functional method SpecId");
              method = &candidate;
            }
      if (!method)
        throw std::runtime_error(
            "MOSS_INTERNAL_INVARIANT checked functional method SpecId has no declaration");
      if (method->name != bound_method)
        throw std::runtime_error(
            "MOSS_INTERNAL_INVARIANT checked functional method identity contradicts the callable syntax");
      std::ostringstream rendered;
      rendered << expr(bound_receiver, domain, locals, types) << "."
               << bound_method << "(";
      for (size_t index = 0; index < arguments.size(); ++index) {
        if (index) rendered << ", ";
        const auto& argument = arguments[index];
        Effect effect = method_effect(*method, index);
        bool borrow = effect != Effect::Consume &&
            (effect == Effect::Write || borrowable_type(argument.type));
        if (domains_.count(canonical_type_name(argument.type))) {
          if (argument.reference)
            rendered << "moss_rt::share_domain(&(*" << argument.expression << "))";
          else
            rendered << argument.expression;
        } else if (borrow && !argument.reference)
          rendered << (effect == Effect::Write ? "&mut (" : "&(")
                   << argument.expression << ")";
        else
          rendered << argument.expression;
      }
      rendered << ")";
      if (!method->observable_effects.raise_set.empty()) {
        if (!phase21_result_context_)
          throw std::runtime_error(
              "checked raising pipeline method lacks a Phase 21 outcome context");
        return propagate_raises ? phase21_propagate(rendered.str())
                                : rendered.str();
      }
      return rendered.str();
    }

    if (!checked_stage)
      throw std::runtime_error(
          "MOSS_INTERNAL_INVARIANT functional callable lacks exact checked stage identity");
    const SpecId target_spec_id = checked_functional_callback_spec_id(*checked_stage);
    const Program& checked_program = semantic_program_ ? *semantic_program_ : p_;
    const auto& target_row = checked_program.specialization_table[target_spec_id];
    const Function* function_target = find_function_by_callable_id(
        target_row.semantic_callable_identity);
    if (!function_target)
      throw std::runtime_error(
          "MOSS_INTERNAL_INVARIANT functional callback SpecId has no function declaration");
    string emitted = function_target->name;
    const FunctionSpecialization* concrete = nullptr;
    if (target_spec_id != function_target->spec_id) {
      for (const auto& candidate : function_target->specializations)
        if (candidate.spec_id == target_spec_id) {
          if (concrete)
            throw std::runtime_error(
                "MOSS_INTERNAL_INVARIANT duplicate functional callback specialization SpecId");
          concrete = &candidate;
        }
      if (!concrete)
        throw std::runtime_error(
            "MOSS_INTERNAL_INVARIANT functional callback SpecId has no specialization body");
      emitted = concrete->generated_name;
    } else if (function_target->static_dispatch) {
      throw std::runtime_error(
          "MOSS_INTERNAL_INVARIANT static functional callback has no checked specialization SpecId");
    }
    std::ostringstream rendered;
    rendered << emitted << "(";
    for (size_t index = 0; index < arguments.size(); ++index) {
      if (index) rendered << ", ";
      const auto& argument = arguments[index];
      auto abi = function_parameter_abi(*function_target, index, argument.type, concrete);
      Effect effect = abi.effect;
      bool borrow = abi.passing != ParameterPassingMode::Value;
      if (domains_.count(canonical_type_name(argument.type))) {
        if (argument.reference)
          rendered << "moss_rt::share_domain(&(*" << argument.expression << "))";
        else
          rendered << argument.expression;
      } else if (borrow && !argument.reference)
        rendered << (effect == Effect::Write ? "&mut (" : "&(")
                 << argument.expression << ")";
      else
        rendered << argument.expression;
    }
    rendered << ")";
    const bool raises = !(concrete ? concrete->observable_effects
                                  : function_target->observable_effects)
                             .raise_set.empty();
    if (raises) {
      if (!phase21_result_context_)
        throw std::runtime_error(
            "checked raising pipeline callable lacks a Phase 21 outcome context");
      return propagate_raises ? phase21_propagate(rendered.str())
                              : rendered.str();
    }
    return rendered.str();
  }

  static string functional_zero(const string& type) {
    return canonical_type_name(type) == "float" ? "0.0_f64" : "0_i64";
  }

  string functional_terminal_initializer(
      const ParsedFunctionalStage& terminal, const string& element_type,
      const Domain* domain, const std::set<string>& locals,
      const std::unordered_map<string,string>* types) const {
    switch (terminal.kind) {
      case FunctionalNodeKind::Reduce:
        return expr(terminal.arguments.front(), domain, locals, types);
      case FunctionalNodeKind::Sum:
        return functional_zero(element_type);
      case FunctionalNodeKind::Count:
        return "0_i64";
      case FunctionalNodeKind::Any:
        return "false";
      case FunctionalNodeKind::All:
        return "true";
      case FunctionalNodeKind::Source:
      case FunctionalNodeKind::Map:
      case FunctionalNodeKind::Filter:
        break;
    }
    throw std::runtime_error(
        "internal error: non-terminal functional traversal consumer");
  }

  void gen_shared_functional_traversal(
      std::ostringstream& out, const FunctionalTraversalGroup& group,
      const vector<Stmt>& statements, size_t statement_index,
      const Domain* domain, std::set<string>& locals,
      std::unordered_map<string,string>& types, int spaces,
      const string& context) const {
    if (group.consumers.size() < 2 ||
        statement_index + group.consumers.size() > statements.size())
      throw std::runtime_error(
          "internal error: invalid shared functional traversal region");

    vector<const FunctionalPipeline*> plans;
    vector<LoweredFunctionalPipeline> pipelines;
    vector<string> final_element_types;
    plans.reserve(group.consumers.size());
    pipelines.reserve(group.consumers.size());
    final_element_types.reserve(group.consumers.size());
    for (size_t index = 0; index < group.consumers.size(); ++index) {
      const auto& consumer = group.consumers[index];
      const Stmt& statement = statements[statement_index + index];
      size_t exact_id = statement_functional_pipeline_id(statement, context, 0);
      const auto result_binding = tree_identifier_name(statement.a);
      if (exact_id != consumer.pipeline_id || !result_binding ||
          *result_binding != consumer.result_binding)
        throw std::runtime_error(
            "internal error: stale shared functional traversal region");
      const FunctionalPipeline* plan = planned_functional_pipeline(exact_id);
      auto parsed = functional_pipeline_from_est(
          statement.b, &statement.expression_handles);
      if (!plan || !parsed || parsed->stages.empty() ||
          !functional_terminal_kind(parsed->stages.back().kind))
        throw std::runtime_error(
            "internal error: invalid shared functional consumer");
      string element_type = generated_functional_element_type(
          group.source_type).value_or("");
      if (element_type.empty())
        throw std::runtime_error(
            "internal error: untyped shared functional source");
      LoweredFunctionalPipeline lowered =
          lower_functional_semantic_plan(*parsed, *plan);
      if (lowered.stages.empty() ||
          !functional_terminal_kind(lowered.stages.back().kind))
        throw std::runtime_error(
            "internal error: rewritten shared functional consumer lost terminal");
      for (const auto& stage : lowered.stages) {
        if (stage.kind != FunctionalNodeKind::Map) continue;
        for (const auto& operation : stage.operations) {
          auto result = generated_callable_result(
              operation.arguments.front(), {element_type}, &types, &operation);
          if (!result)
            throw std::runtime_error(
                "internal error: untyped shared functional map");
          element_type = *result;
        }
      }
      plans.push_back(plan);
      pipelines.push_back(std::move(lowered));
      final_element_types.push_back(std::move(element_type));
    }

    for (size_t index = 1; index < group.consumers.size(); ++index)
      source_comment(out, spaces, statements[statement_index + index].line,
                     statements[statement_index + index].text);
    backend_comment(
        out, spaces,
        "SHARED FUNCTIONAL SOURCE TRAVERSAL (" +
            std::to_string(group.consumers.size()) +
            " terminal consumers); one dataflow DAG loop");
    out << string(spaces, ' ') << "let (";
    for (size_t index = 0; index < group.consumers.size(); ++index) {
      if (index) out << ", ";
      if (group.consumers[index].mutable_binding) out << "mut ";
      out << group.consumers[index].result_binding;
    }
    out << ") = {\n";
    string inner(spaces + 4, ' ');
    string deep(spaces + 8, ' ');
    out << inner << "// Moss dataflow group %" << group.transient_id
        << ", semantic " << group.semantic_identity << "; provenance";
    for (const auto& origin : group.provenance) out << " " << origin;
    out << "\n";
    out << inner << "let __moss_shared_source_" << group.transient_id
        << " = &(" << expr(group.source_expression, domain, locals, &types)
        << ");\n";

    for (size_t index = 0; index < group.consumers.size(); ++index) {
      const auto& terminal = terminal_operation(pipelines[index]);
      out << inner << "let mut __moss_shared_result_" << group.transient_id
          << "_" << index << " = ";
      if (plans[index]->count_uses_exact_length)
        out << "__moss_shared_source_" << group.transient_id
            << ".len() as i64";
      else
        out << functional_terminal_initializer(
            terminal, final_element_types[index], domain, locals, &types);
      out << ";\n";
    }

    bool has_traversal = std::any_of(
        plans.begin(), plans.end(), [](const FunctionalPipeline* plan) {
          return !plan->count_uses_exact_length;
        });
    auto source_element = generated_functional_element_type(group.source_type);
    if (!source_element)
      throw std::runtime_error(
          "internal error: untyped shared functional source element");
    if (has_traversal) {
      out << inner << "for __moss_shared_item_ref_" << group.transient_id
          << " in __moss_shared_source_" << group.transient_id
          << ".iter() {\n";
      for (size_t consumer_index = 0;
           consumer_index < group.consumers.size(); ++consumer_index) {
        if (plans[consumer_index]->count_uses_exact_length) continue;
        const auto& pipeline = pipelines[consumer_index];
        const auto& terminal = terminal_operation(pipeline);
        string result = "__moss_shared_result_" +
            std::to_string(group.transient_id) + "_" +
            std::to_string(consumer_index);
        // A completed any/all branch may stop receiving elements only when
        // the semantic plan proves every skipped callback non-observable,
        // non-failing, and non-divergent. Otherwise the shared loop continues
        // invoking that branch exactly as eager execution would.
        bool guarded_any = terminal.kind == FunctionalNodeKind::Any &&
            plans[consumer_index]->short_circuit_terminal;
        bool guarded_all = terminal.kind == FunctionalNodeKind::All &&
            plans[consumer_index]->short_circuit_terminal;
        if (guarded_any || guarded_all)
          out << deep << "if " << (guarded_any ? "!" : "") << result
              << " {\n";
        string block_indent(spaces + (guarded_any || guarded_all ? 16 : 12),
                            ' ');
        string statement_indent(spaces +
            (guarded_any || guarded_all ? 20 : 16), ' ');
        string label = "'__moss_consumer_" +
            std::to_string(group.transient_id) + "_" +
            std::to_string(consumer_index);
        out << block_indent << label << ": {\n";
        string current_expression = "__moss_shared_value_" +
            std::to_string(group.transient_id) + "_" +
            std::to_string(consumer_index) + "_0";
        bool source_copy = copy_type(*source_element);
        out << statement_indent << "let " << current_expression << " = "
            << (source_copy ? "*" : "") << "__moss_shared_item_ref_"
            << group.transient_id << ";\n";
        FunctionalValue current{current_expression, *source_element,
                                !source_copy};
        size_t map_number = 0;
        for (const auto& stage : pipeline.stages) {
          switch (stage.kind) {
            case FunctionalNodeKind::Map: {
              for (const auto& operation : stage.operations) {
                string next = "__moss_shared_value_" +
                    std::to_string(group.transient_id) + "_" +
                    std::to_string(consumer_index) + "_" +
                    std::to_string(++map_number);
                out << statement_indent << "let " << next << " = "
                    << render_functional_callable(
                           operation.arguments.front(), {current}, domain,
                           locals, &types, true, false, &operation)
                    << ";\n";
                auto mapped_type = generated_callable_result(
              operation.arguments.front(), {current.type}, &types, &operation);
                if (!mapped_type)
                  throw std::runtime_error(
                      "internal error: untyped shared functional map result");
                current = {next, *mapped_type, false};
              }
              break;
            }
            case FunctionalNodeKind::Filter: {
              out << statement_indent << "if !(";
              for (size_t predicate = 0;
                   predicate < stage.operations.size(); ++predicate) {
                if (predicate) out << " && ";
                out << "("
                    << render_functional_callable(
                           stage.operations[predicate].arguments.front(),
                           {current}, domain, locals, &types, true, false,
                           &stage.operations[predicate])
                    << ")";
              }
              out << ") { break " << label << "; }\n";
              break;
            }
            case FunctionalNodeKind::Reduce: {
              const auto& operation = stage.operations.front();
              auto accumulator_type = generated_expr_type(
                  operation.arguments.front(), &types).value_or(current.type);
              out << statement_indent << result << " = "
                  << render_functional_callable(
                         operation.arguments[1],
                         {{result, accumulator_type, false}, current},
                         domain, locals, &types, true, false, &operation)
                  << ";\n";
              break;
            }
            case FunctionalNodeKind::Sum:
              if (canonical_type_name(current.type) == "int")
                out << statement_indent << result << " = " << result
                    << ".wrapping_add(" << current.expression << ");\n";
              else
                out << statement_indent << result << " += "
                    << current.expression << ";\n";
              break;
            case FunctionalNodeKind::Count:
              out << statement_indent << result << " = " << result
                  << ".wrapping_add(1_i64);\n";
              break;
            case FunctionalNodeKind::Any: {
              const auto& operation = stage.operations.front();
              string predicate = operation.arguments.empty()
                  ? current.expression
                  : render_functional_callable(
                        operation.arguments.front(), {current}, domain,
                        locals, &types, true, false, &operation);
              out << statement_indent << "if " << predicate << " { "
                  << result << " = true; }\n";
              break;
            }
            case FunctionalNodeKind::All: {
              const auto& operation = stage.operations.front();
              string predicate = operation.arguments.empty()
                  ? current.expression
                  : render_functional_callable(
                        operation.arguments.front(), {current}, domain,
                        locals, &types, true, false, &operation);
              out << statement_indent << "if !(" << predicate << ") { "
                  << result << " = false; }\n";
              break;
            }
            case FunctionalNodeKind::Source:
              break;
          }
        }
        out << block_indent << "}\n";
        if (guarded_any || guarded_all) out << deep << "}\n";
      }
      out << inner << "}\n";
    }
    out << inner << "(";
    for (size_t index = 0; index < group.consumers.size(); ++index) {
      if (index) out << ", ";
      out << "__moss_shared_result_" << group.transient_id << "_" << index;
    }
    out << ")\n" << string(spaces, ' ') << "};\n";

    for (size_t index = 0; index < group.consumers.size(); ++index) {
      const auto& consumer = group.consumers[index];
      locals.insert(consumer.result_binding);
      types[consumer.result_binding] = plans[index]->output_type;
    }
  }

  string gen_eager_functional_pipeline(
      const ParsedFunctionalPipeline& pipeline, const Domain* domain,
      const std::set<string>& locals,
      const std::unordered_map<string,string>* types,
      const FunctionalPipeline* planned) const {
    auto source_type = generated_expr_type(pipeline.source, types);
    auto element = source_type
        ? generated_functional_element_type(*source_type) : std::nullopt;
    if (!element)
      throw std::runtime_error("internal error: untyped functional source");
    string current_type = *element;
    string current_collection = "__moss_pipeline_source";
    std::ostringstream out;
    out << "{\n        // Moss backend: EAGER FUNCTIONAL PIPELINE reference semantics";
    if (planned) {
      out << " (plan %" << planned->transient_id
          << ", semantic " << planned->semantic_identity << ")";
    }
    out << "\n";
    if (planned && !planned->nodes.empty())
      out << "        // Moss functional origin|"
          << planned->nodes.front().semantic_identity << "\n";
    out << "        let __moss_pipeline_source = &("
        << expr(pipeline.source, domain, locals, types) << ");\n";

    size_t stage_number = 0;
    for (size_t pipeline_stage = 0; pipeline_stage < pipeline.stages.size();
         ++pipeline_stage) {
      const auto& stage = pipeline.stages[pipeline_stage];
      if (stage.kind != FunctionalNodeKind::Map &&
          stage.kind != FunctionalNodeKind::Filter)
        continue;
      if (planned && pipeline_stage + 1 < planned->nodes.size())
        out << "        // Moss functional origin|"
            << planned->nodes[pipeline_stage + 1].semantic_identity << "\n";
      string output = "__moss_stage_" + std::to_string(stage_number);
      string item_ref = "__moss_item_ref_" + std::to_string(stage_number);
      string value = "__moss_value_" + std::to_string(stage_number);
      bool item_copy = copy_type(current_type);
      out << "        let mut " << output << " = std::vec::Vec::new();\n"
          << "        for " << item_ref << " in " << current_collection
          << ".iter() {\n"
          << "            let " << value << " = "
          << (item_copy ? "*" : "") << item_ref << ";\n";
      FunctionalValue input{value, current_type, !item_copy};
      if (stage.kind == FunctionalNodeKind::Map) {
        string mapped = render_functional_callable(
            stage.arguments.front(), {input}, domain, locals, types,
            true, false, &stage);
        out << "            " << output << ".push(" << mapped << ");\n";
        auto result = generated_callable_result(stage.arguments.front(),
                                                {current_type}, types, &stage);
        if (!result)
          throw std::runtime_error("internal error: untyped eager map result");
        current_type = *result;
      } else {
        string predicate = render_functional_callable(
            stage.arguments.front(), {input}, domain, locals, types,
            true, false, &stage);
        out << "            if " << predicate << " { " << output << ".push("
            << value << "); }\n";
      }
      out << "        }\n";
      current_collection = output;
      ++stage_number;
    }

    const auto& terminal = pipeline.stages.back();
    bool has_terminal = terminal.kind == FunctionalNodeKind::Reduce ||
        terminal.kind == FunctionalNodeKind::Sum ||
        terminal.kind == FunctionalNodeKind::Count ||
        terminal.kind == FunctionalNodeKind::Any ||
        terminal.kind == FunctionalNodeKind::All;
    if (!has_terminal) {
      out << "        " << current_collection << "\n    }";
      return out.str();
    }

    string accumulator = "__moss_result";
    if (planned && pipeline.stages.size() < planned->nodes.size())
      out << "        // Moss functional origin|"
          << planned->nodes[pipeline.stages.size()].semantic_identity << "\n";
    if (terminal.kind == FunctionalNodeKind::Reduce)
      out << "        let mut " << accumulator << " = "
          << expr(terminal.arguments.front(), domain, locals, types) << ";\n";
    else if (terminal.kind == FunctionalNodeKind::Sum)
      out << "        let mut " << accumulator << " = "
          << functional_zero(current_type) << ";\n";
    else if (terminal.kind == FunctionalNodeKind::Count)
      out << "        let mut " << accumulator << " = 0_i64;\n";
    else
      out << "        let mut " << accumulator << " = "
          << (terminal.kind == FunctionalNodeKind::All ? "true" : "false")
          << ";\n";

    string item_ref = "__moss_terminal_ref";
    string value = "__moss_terminal_value";
    bool item_copy = copy_type(current_type);
    out << "        for " << item_ref << " in " << current_collection
        << ".iter() {\n"
        << "            let " << value << " = "
        << (item_copy ? "*" : "") << item_ref << ";\n";
    FunctionalValue input{value, current_type, !item_copy};
    if (terminal.kind == FunctionalNodeKind::Reduce) {
      auto accumulator_type = generated_expr_type(terminal.arguments.front(), types)
          .value_or(current_type);
      out << "            " << accumulator << " = "
          << render_functional_callable(
                 terminal.arguments[1],
                 {{accumulator, accumulator_type, false}, input},
                 domain, locals, types, true, false, &terminal)
          << ";\n";
    } else if (terminal.kind == FunctionalNodeKind::Sum) {
      if (canonical_type_name(current_type) == "int")
        out << "            " << accumulator << " = " << accumulator
            << ".wrapping_add(" << value << ");\n";
      else
        out << "            " << accumulator << " += " << value << ";\n";
    } else if (terminal.kind == FunctionalNodeKind::Count) {
      out << "            " << accumulator << " = " << accumulator
          << ".wrapping_add(1_i64);\n";
    } else {
      string predicate = terminal.arguments.empty()
          ? value
          : render_functional_callable(terminal.arguments.front(), {input},
                                       domain, locals, types, true, false,
                                       &terminal);
      if (terminal.kind == FunctionalNodeKind::Any)
        out << "            if " << predicate << " { " << accumulator
            << " = true; }\n";
      else
        out << "            if !(" << predicate << ") { " << accumulator
            << " = false; }\n";
    }
    out << "        }\n        " << accumulator << "\n    }";
    return out.str();
  }

  string gen_fused_functional_pipeline(
      const LoweredFunctionalPipeline& pipeline, const Domain* domain,
      const std::set<string>& locals,
      const std::unordered_map<string,string>* types,
      const FunctionalPipeline* planned) const {
    auto source_type = generated_expr_type(pipeline.source, types);
    auto element = source_type
        ? generated_functional_element_type(*source_type) : std::nullopt;
    if (!element)
      throw std::runtime_error("internal error: untyped fused functional source");
    string current_type = *element;
    string final_element_type = current_type;
    for (const auto& stage : pipeline.stages) {
      if (stage.kind != FunctionalNodeKind::Map) continue;
      for (const auto& operation : stage.operations) {
        auto result = generated_callable_result(
              operation.arguments.front(), {final_element_type}, types,
              &operation);
        if (!result)
          throw std::runtime_error("internal error: untyped fused map result");
        final_element_type = *result;
      }
    }
    FunctionalNodeKind last_kind = pipeline.stages.empty()
        ? FunctionalNodeKind::Source : pipeline.stages.back().kind;
    bool terminal = functional_terminal_kind(last_kind);
    const ParsedFunctionalStage* last = terminal
        ? &terminal_operation(pipeline) : nullptr;
    std::ostringstream out;
    out << "{\n        // Moss backend: FUSED FUNCTIONAL PIPELINE; one explicit loop, intermediates eliminated";
    if (planned) {
      out << "; plan %" << planned->transient_id << ", semantic "
          << planned->semantic_identity << "; provenance";
      for (const auto& origin : planned->lowered_provenance)
        out << " " << origin;
    }
    out << "\n"
        << "        let __moss_pipeline_source = &("
        << expr(pipeline.source, domain, locals, types) << ");\n";
    if (!terminal)
      out << "        let mut __moss_result = std::vec::Vec::new();\n";
    else if (last_kind == FunctionalNodeKind::Reduce)
      out << "        let mut __moss_result = "
          << expr(last->arguments.front(), domain, locals, types) << ";\n";
    else if (last_kind == FunctionalNodeKind::Sum)
      out << "        let mut __moss_result = "
          << functional_zero(final_element_type) << ";\n";
    else if (last_kind == FunctionalNodeKind::Count)
      out << "        let mut __moss_result = 0_i64;\n";
    else
      out << "        let mut __moss_result = "
          << (last_kind == FunctionalNodeKind::All ? "true" : "false")
          << ";\n";
    bool source_copy = copy_type(current_type);
    out << "        for __moss_item_ref in __moss_pipeline_source.iter() {\n"
        << "            let __moss_value_0 = "
        << (source_copy ? "*" : "") << "__moss_item_ref;\n";
    FunctionalValue current{"__moss_value_0", current_type, !source_copy};
    size_t map_number = 0;
    for (const auto& stage : pipeline.stages) {
      switch (stage.kind) {
        case FunctionalNodeKind::Map: {
          for (const auto& operation : stage.operations) {
            string next = "__moss_value_" + std::to_string(++map_number);
            out << "            let " << next << " = "
                << render_functional_callable(
                       operation.arguments.front(), {current}, domain,
                       locals, types, true, false, &operation)
                << ";\n";
            auto result = generated_callable_result(
                operation.arguments.front(), {current.type}, types,
                &operation);
            if (!result)
              throw std::runtime_error(
                  "internal error: untyped fused map result");
            current = {next, *result, false};
          }
          break;
        }
        case FunctionalNodeKind::Filter: {
          out << "            if !(";
          for (size_t predicate = 0;
               predicate < stage.operations.size(); ++predicate) {
            if (predicate) out << " && ";
            out << "("
                << render_functional_callable(
                       stage.operations[predicate].arguments.front(),
                       {current}, domain, locals, types, true, false,
                       &stage.operations[predicate])
                << ")";
          }
          out << ") { continue; }\n";
          break;
        }
        case FunctionalNodeKind::Reduce: {
          const auto& operation = stage.operations.front();
          auto accumulator_type = generated_expr_type(
              operation.arguments.front(), types).value_or(current.type);
          out << "            __moss_result = "
              << render_functional_callable(
                     operation.arguments[1],
                     {{"__moss_result", accumulator_type, false}, current},
                     domain, locals, types, true, false, &operation)
              << ";\n";
          break;
        }
        case FunctionalNodeKind::Sum:
          if (canonical_type_name(current.type) == "int")
            out << "            __moss_result = __moss_result.wrapping_add("
                << current.expression << ");\n";
          else
            out << "            __moss_result += " << current.expression << ";\n";
          break;
        case FunctionalNodeKind::Count:
          out << "            __moss_result = __moss_result.wrapping_add(1_i64);\n";
          break;
        case FunctionalNodeKind::Any: {
          const auto& operation = stage.operations.front();
          string predicate = operation.arguments.empty()
              ? current.expression
              : render_functional_callable(
                    operation.arguments.front(), {current}, domain, locals,
                    types, true, false, &operation);
          out << "            if " << predicate
              << " { __moss_result = true;";
          if (planned && planned->short_circuit_terminal) out << " break;";
          out << " }\n";
          break;
        }
        case FunctionalNodeKind::All: {
          const auto& operation = stage.operations.front();
          string predicate = operation.arguments.empty()
              ? current.expression
              : render_functional_callable(
                    operation.arguments.front(), {current}, domain, locals,
                    types, true, false, &operation);
          out << "            if !(" << predicate
              << ") { __moss_result = false;";
          if (planned && planned->short_circuit_terminal) out << " break;";
          out << " }\n";
          break;
        }
        case FunctionalNodeKind::Source:
          break;
      }
    }
    if (!terminal)
      out << "            __moss_result.push(" << current.expression << ");\n";
    out << "        }\n        __moss_result\n    }";
    return out.str();
  }

  std::optional<string> functional_expr(
      const string& expression, const Domain* domain,
      const std::set<string>& locals,
      const std::unordered_map<string,string>* types,
      size_t functional_pipeline_id) const {
    auto pipeline = functional_pipeline_from_est(expression);
    if (!pipeline && expression.find("|>") != string::npos)
      throw std::runtime_error(
          "MOSS_INTERNAL_INVARIANT functional pipeline lacks exact EST cursor");
    if (!pipeline) return std::nullopt;
    if (auto chunk_lowering = gen_fileio_chunk_pipeline(
            *pipeline, domain, locals, types, functional_pipeline_id))
      return chunk_lowering;
    auto source_type = generated_expr_type(pipeline->source, types);
    if (!source_type || !generated_functional_element_type(*source_type))
      return std::nullopt;
    const FunctionalPipeline* planned =
        planned_functional_pipeline(functional_pipeline_id);
    // A pipeline nested in an ordinary expression does not have a standalone
    // statement plan.  It still has fully checked semantics, so use the
    // baseline eager lowering rather than requiring an unrelated outer
    // expression to masquerade as a pipeline statement.
    if (!planned)
      return gen_eager_functional_pipeline(*pipeline, domain, locals, types,
                                           nullptr);
    LoweredFunctionalPipeline lowered =
        lower_functional_semantic_plan(*pipeline, *planned);
    if (planned->virtual_upstream_pipeline_id) {
      const FunctionalPipeline* upstream = planned_functional_pipeline(
          planned->virtual_upstream_pipeline_id);
      auto upstream_parsed = upstream
          ? functional_pipeline_from_est(upstream->expression) : std::nullopt;
      if (!upstream_parsed || upstream_parsed->stages.empty() ||
          functional_terminal_kind(upstream_parsed->stages.back().kind))
        throw std::runtime_error(
            "internal error: invalid virtual functional upstream plan");
      LoweredFunctionalPipeline upstream_lowered =
          lower_functional_semantic_plan(*upstream_parsed, *upstream);
      vector<LoweredFunctionalStage> stages =
          std::move(upstream_lowered.stages);
      stages.insert(stages.end(), lowered.stages.begin(), lowered.stages.end());
      lowered.source = upstream_parsed->source;
      lowered.stages = std::move(stages);
    }
    if (planned->count_uses_exact_length) {
      std::ostringstream out;
      if (planned->count_uses_known_size)
        out << "{\n        // Moss backend: COUNT -> KNOWN SIZE; inert literal work eliminated";
      else
        out << "{\n        // Moss backend: COUNT -> EXACT LENGTH; mapped outputs are dead";
      out << "; plan %" << planned->transient_id << ", semantic "
          << planned->semantic_identity << "; provenance";
      for (const auto& origin : planned->lowered_provenance)
        out << " " << origin;
      if (planned->count_uses_known_size)
        out << "\n        " << planned->known_source_size << "_i64\n    }";
      else
        out << "\n"
            << "        let __moss_pipeline_source = &("
            << expr(lowered.source, domain, locals, types) << ");\n"
            << "        __moss_pipeline_source.len() as i64\n    }";
      return out.str();
    }
    if (planned && planned->fused)
      return gen_fused_functional_pipeline(lowered, domain, locals, types,
                                           planned);
    return gen_eager_functional_pipeline(*pipeline, domain, locals, types,
                                         planned);
  }

  string expr(string e, const Domain* d, const std::set<string>& locals,
              const std::unordered_map<string,string>* types = nullptr,
              size_t functional_pipeline_id = 0,
              const vector<size_t>* message_argument_plans = nullptr,
              std::optional<RootIngressExpressionOccurrence>
                  root_ingress_occurrence = std::nullopt,
              const NativeEstCursor* occurrence = nullptr) const {
    struct NativeEstCursorScope {
      const NativeEstCursor*& slot;
      const NativeEstCursor* prior;
      NativeEstCursorScope(const NativeEstCursor*& target,
                           const NativeEstCursor* current)
          : slot(target), prior(target) { slot = current; }
      ~NativeEstCursorScope() { slot = prior; }
    } native_est_cursor_scope(active_native_est_cursor_,
                              occurrence ? occurrence
                                         : active_native_est_cursor_);
    if (!root_ingress_statement_ || !root_ingress_occurrence)
      return expr_lower(std::move(e), d, locals, types,
                        functional_pipeline_id, message_argument_plans);
    if (active_root_ingress_message_queue_)
      throw std::runtime_error(
          "MOSS_INTERNAL_INVARIANT nested Root-ingress Native expression queue");
    auto queue = root_ingress_message_queue(*root_ingress_occurrence);
    struct RootIngressQueueScope {
      RootIngressMessageQueue*& slot;
      RootIngressMessageQueue* prior;
      RootIngressMessageQueue& queue;
      RootIngressQueueScope(RootIngressMessageQueue*& target,
                            RootIngressMessageQueue& current)
          : slot(target), prior(target), queue(current) { slot = &queue; }
      ~RootIngressQueueScope() { slot = prior; }
    } root_ingress_queue_scope(active_root_ingress_message_queue_, queue);
    string rendered = expr_lower(std::move(e), d, locals, types,
                                 functional_pipeline_id,
                                 message_argument_plans);
    if (queue.next != queue.messages.size())
      throw std::runtime_error(
          "MOSS_INTERNAL_INVARIANT Native lowering did not consume every checked Root-ingress message ExprId");
    return rendered;
  }

  string expr_lower(string e, const Domain* d,
              const std::set<string>& locals,
              const std::unordered_map<string,string>* types = nullptr,
              size_t functional_pipeline_id = 0,
              const vector<size_t>* message_argument_plans = nullptr) const {
    // Moss parentheses are grouping syntax, not an observable expression node.
    // Remove only complete balanced outer groups before lowering. Nested groups
    // still protect precedence until their recursive expr() call reaches them.
    e = tree_strip_expression_grouping(std::move(e));
    const auto root_name = tree_identifier_name(e);
    if (root_name && phase21_option_slots_.count(*root_name))
      return "__moss_recovery_slot_" + *root_name +
          (phase21_codegen_statement_ &&
           phase21_codegen_statement_->ownership_consumed_bindings.count(*root_name)
              ? ".take()" : ".as_ref()") +
          ".unwrap_or_else(|| std::process::abort())";
    // Keep this dereference bare; enclosing expression lowering supplies
    // precedence parentheses where needed. Parenthesizing here duplicates
    // those wrappers and triggers rustc's unused_parens warning.
    if (root_name && mutable_match_primitive_values_.count(*root_name)) {
      const string rendered_payload = "*" + rust_identifier(*root_name);
      return rendered_payload;
    }
    if (auto place = view_place(e, d, locals, types)) return *place;
    if (root_name && write_through_parameters_.count(*root_name) && types &&
        types->count(*root_name) && copy_type(types->at(*root_name)))
      return "*" + *root_name;
    // `message` is a Moss expression as well as a statement.  Assignment
    // statements have a dedicated lowering path, but expression positions
    // (for example `echo message d.Get()`) must retain the same synchronous
    // completion and reply semantics through synchronized handler entry.
    string message_target, message_handler;
    vector<string> message_arguments;
    if (tree_parse_message(e, message_target, message_handler,
                           message_arguments)) {
      string receiver = message_target;
      string handler = message_handler;
      vector<string> arguments = std::move(message_arguments);
      if (!types || !tree_plain_identifier(receiver))
        throw std::runtime_error("internal error: malformed message expression");
      auto receiver_type = types->find(receiver);
      if (receiver_type == types->end())
        throw std::runtime_error("internal error: unresolved message receiver");
      auto domain_it = domains_.find(canonical_type_name(receiver_type->second));
      if (domain_it == domains_.end())
        throw std::runtime_error("internal error: message expression receiver is not a domain");
      const Domain& target_domain = *domain_it->second;
      const Handler* target_handler = find_handler(target_domain, handler);
      if (!target_handler)
        throw std::runtime_error("internal error: unresolved message expression handler");
      if (root_ingress_statement_) {
        const auto root_ingress_occurrence =
            consume_root_ingress_expression_occurrence(e);
        const auto& capture_fact = resolve_root_ingress_domain_capture(
            receiver, handler, root_ingress_occurrence);
        const auto capture_id = native_root_ingress_capture_identity(
            capture_fact);
        return root_ingress_message(receiver, handler, arguments, target_domain,
                                    *target_handler, d, locals, types,
                                    message_argument_plans, capture_id,
                                    root_ingress_occurrence);
      }
      const bool raises = !target_handler->observable_effects.raise_set.empty();
      if (raises && !phase21_result_context_)
        throw std::runtime_error(
            "internal error: checked raising message lacks a Phase 21 outcome context");
      if (target_domain.exported && !exported_domain_bridge_body_) {
        std::ostringstream call;
        const string receiver_value = expr(receiver, d, locals, types);
        call << target_domain.name << "RefExportedBridge::__moss_message_"
             << handler << "(" << (view_domain_ ? receiver_value
                  : "&(" + receiver_value + ")");
        if (!arguments.empty()) call << ", ";
        for (size_t index = 0; index < arguments.size(); ++index) {
          if (index) call << ", ";
          call << shared_message_arg(arguments[index], target_handler->params.at(index).type,
                                     d, locals, types,
                                     message_argument_plans ? message_argument_plans->at(index) : 0);
        }
        call << ")";
        return raises ? phase21_propagate(call.str()) : call.str();
      }
      std::ostringstream rendered;
      const string receiver_value = expr(receiver, d, locals, types);
      rendered << target_domain.name << "Messages::" << handler
               << "_shared(" << (view_domain_ ? receiver_value
                    : "&( " + receiver_value + " )");
      if (!arguments.empty()) rendered << ", ";
      for (size_t index = 0; index < arguments.size(); ++index) {
        if (index) rendered << ", ";
        rendered << shared_message_arg(arguments[index], target_handler->params.at(index).type,
                                       d, locals, types,
                                       message_argument_plans ? message_argument_plans->at(index) : 0);
      }
      rendered << ")";
      if (raises) {
        const string propagated = phase21_propagate(rendered.str());
        rendered.str("");
        rendered.clear();
        rendered << propagated;
      }
      if (target_handler->reply_type)
        rendered << ".unwrap_or_else(|| std::process::abort())";
      return rendered.str();
    }
    if (auto functional = functional_expr(
            e, d, locals, types, functional_pipeline_id))
      return *functional;
    e = tree_normalize_pipeline(std::move(e));
    // Minimal surface rewrites.
    const auto literal = tree_literal_class(e);
    if (literal == est::LiteralClass::Boolean) return e;
    if (tree_identifier_name(e) == std::optional<string>("None")) return "None";
    // Numeric literals are emitted from the shared decoder's value as a
    // canonical suffixed Rust literal (`16_i64`, `2.5_f64`), never from Moss
    // spelling.
    if (generated_integer_literal(e) || literal == est::LiteralClass::Float) {
      if (auto numeric = tree_numeric_literal_value(e))
        return rust_numeric_literal(*numeric);
      throw std::runtime_error("internal error: numeric literal '" + e +
                               "' was not validated by the lexer");
    }
    if (auto typed_vector = tree_empty_typed_vector_constructor(e)) {
      string element = trim(typed_vector->substr(7, typed_vector->size() - 8));
      return "std::vec::Vec::<" + rust_type(element) + ">::new()";
    }
    auto expression_shape = expression_view_from_est(e);
    if (expression_shape && est::empty_constructor_named(*expression_shape, "Map")) return staged_abi_v10()
        ? "std::default::Default::default()"
        : "std::collections::HashMap::new()";
    if (expression_shape && staged_abi_v10() && est::empty_constructor_named(*expression_shape, "Set"))
      return "moss_runtime::MossSet::new()";
    if (expression_shape && est::empty_constructor_named(*expression_shape, "Queue"))
      return "std::collections::VecDeque::new()";
    {
      string callee;
      vector<string> arguments;
      if (tree_parse_simple_call(e, callee, arguments) && callee == "replace" &&
          arguments.size() == 2)
        return "{ let __moss_replacement = " +
            expr(arguments[1], d, locals, types) + "; std::mem::replace(&mut " +
            write_call_place(arguments[0], d, locals, types) +
            ", __moss_replacement) }";
    }
    {
      string enum_name, case_name;
      vector<string> fields;
      bool called = false;
      if (tree_parse_enum_case_expression(e, enum_name, case_name,
                                          fields, called) &&
          enums_.count(enum_name)) {
        std::ostringstream rendered;
        rendered << enum_name << "::" << rust_identifier(case_name);
        if (!fields.empty()) {
          rendered << " { ";
          for (size_t index = 0; index < fields.size(); ++index) {
            string name, value;
            if (!tree_parse_named_argument(fields[index], name, value))
              throw std::runtime_error("unchecked enum constructor field");
            if (index) rendered << ", ";
            rendered << rust_identifier(name) << ": "
                     << expr(value, d, locals, types);
          }
          rendered << " }";
        }
        return rendered.str();
      }
    }

    for (const auto& boolean_operator :
         vector<std::pair<string, string>>{{" or ", "||"},
                                                {" xor ", "^"},
                                                {" and ", "&&"}}) {
      if (auto binary = tree_generated_binary(e, {boolean_operator.first}))
        return "(" + expr(binary->left, d, locals, types) + ") " +
            boolean_operator.second + " (" +
            expr(binary->right, d, locals, types) + ")";
    }
    string unary_operation, unary_operand;
    if (tree_parse_unary(e, unary_operation, unary_operand) &&
        unary_operation == "not")
      return "!(" + expr(unary_operand, d, locals, types) + ")";
    if (auto comparison =
            tree_generated_binary(e, {"==", "!=", "<=", ">=", "<", ">"})) {
      string left = expr(comparison->left, d, locals, types);
      string right = expr(comparison->right, d, locals, types);
      left = value_from_borrowed_message_parameter(comparison->left, left, types);
      right = value_from_borrowed_message_parameter(comparison->right, right, types);
      auto left_type = generated_expr_type(comparison->left, types);
      auto right_type = generated_expr_type(comparison->right, types);
      if (left_type && canonical_type_name(*left_type) == "string") {
        left = "(" + left + ").as_str()";
      }
      if (right_type && canonical_type_name(*right_type) == "string") {
        right = "(" + right + ").as_str()";
      }
      if (left_type && canonical_type_name(*left_type) == "float" &&
          right_type && canonical_type_name(*right_type) == "int") {
        if (generated_integer_literal(comparison->right))
          right = generated_integer_literal_as_float(comparison->right);
        else
          right = "((" + right + ") as f64)";
      }
      if (right_type && canonical_type_name(*right_type) == "float" &&
          left_type && canonical_type_name(*left_type) == "int") {
        if (generated_integer_literal(comparison->left))
          left = generated_integer_literal_as_float(comparison->left);
        else
          left = "((" + left + ") as f64)";
      }
      return "(" + left + ") " + comparison->op + " (" + right + ")";
    }
    if (auto binary = tree_generated_binary(e, {"|"})) {
      string left = expr(binary->left, d, locals, types);
      string right = expr(binary->right, d, locals, types);
      left = value_from_borrowed_message_parameter(binary->left, left, types);
      right = value_from_borrowed_message_parameter(binary->right, right, types);
      return "(" + left + ") | (" + right + ")";
    }
    if (auto binary = tree_generated_binary(e, {"^"})) {
      string left = expr(binary->left, d, locals, types);
      string right = expr(binary->right, d, locals, types);
      left = value_from_borrowed_message_parameter(binary->left, left, types);
      right = value_from_borrowed_message_parameter(binary->right, right, types);
      return "(" + left + ") ^ (" + right + ")";
    }
    if (auto binary = tree_generated_binary(e, {"&"})) {
      string left = expr(binary->left, d, locals, types);
      string right = expr(binary->right, d, locals, types);
      left = value_from_borrowed_message_parameter(binary->left, left, types);
      right = value_from_borrowed_message_parameter(binary->right, right, types);
      return "(" + left + ") & (" + right + ")";
    }
    if (auto binary = tree_generated_binary(e, {"<<", ">>"})) {
      string left = expr(binary->left, d, locals, types);
      string right = expr(binary->right, d, locals, types);
      left = value_from_borrowed_message_parameter(binary->left, left, types);
      right = value_from_borrowed_message_parameter(binary->right, right, types);
      string fn_name = binary->op == "<<" ? "__moss_shl" : "__moss_shr";
      return fn_name + "(" + left + ", " + right + ")";
    }
    for (const auto& operators : vector<vector<string>>{{"+", "-"}, {"*", "/", "%"}}) {
      auto binary = tree_generated_binary(e, operators);
      if (!binary) continue;
      string left = expr(binary->left, d, locals, types);
      string right = expr(binary->right, d, locals, types);
      left = value_from_borrowed_message_parameter(binary->left, left, types);
      right = value_from_borrowed_message_parameter(binary->right, right, types);
      auto left_type = generated_expr_type(binary->left, types);
      auto right_type = generated_expr_type(binary->right, types);
      bool string_concatenation = binary->op == "+" &&
          left_type && right_type &&
          canonical_type_name(*left_type) == "string" &&
          canonical_type_name(*right_type) == "string";
      if (string_concatenation) {
        vector<string> operands;
        std::function<void(string)> collect = [&](string source) {
          source = tree_strip_expression_grouping(std::move(source));
          auto nested = tree_generated_binary(source, {"+"});
          auto nested_left = nested
              ? generated_expr_type(nested->left, types) : std::nullopt;
          auto nested_right = nested
              ? generated_expr_type(nested->right, types) : std::nullopt;
          if (nested && nested_left && nested_right &&
              canonical_type_name(*nested_left) == "string" &&
              canonical_type_name(*nested_right) == "string") {
            collect(nested->left);
            collect(nested->right);
          } else {
            operands.push_back(std::move(source));
          }
        };
        collect(e);
        std::ostringstream joined;
        joined << "{ ";
        for (size_t index = 0; index < operands.size(); ++index) {
          const string operand = trim(operands[index]);
          const bool literal = tree_literal_class(operand) ==
              est::LiteralClass::String;
          joined << "let __moss_concat_" << index << " = ";
          if (literal) joined << operand << "; ";
          else
            joined << "&(" << expr(operands[index], d, locals, types)
                   << "); ";
        }
        joined << "let mut __moss_concat_result = MossStr::with_capacity(";
        for (size_t index = 0; index < operands.size(); ++index) {
          if (index) joined << ".saturating_add(";
          joined << "__moss_concat_" << index << ".len()";
        }
        for (size_t index = 1; index < operands.size(); ++index) joined << ")";
        joined << "); ";
        for (size_t index = 0; index < operands.size(); ++index)
          joined << "__moss_concat_result.push_str(__moss_concat_" << index
                 << (tree_literal_class(operands[index]) ==
                         est::LiteralClass::String
                         ? "); " : ".as_str()); ");
        joined << "__moss_concat_result }";
        return joined.str();
      }
      bool integer_operation = left_type && right_type &&
          canonical_type_name(*left_type) == "int" &&
          canonical_type_name(*right_type) == "int";
      if (integer_operation) {
        string method = binary->op == "+" ? "wrapping_add"
            : binary->op == "-" ? "wrapping_sub"
            : binary->op == "*" ? "wrapping_mul"
            : binary->op == "/" ? "wrapping_div" : "wrapping_rem";
        return "(" + left + ")." + method + "(" + right + ")";
      }
      if (left_type && canonical_type_name(*left_type) == "float" &&
          right_type && canonical_type_name(*right_type) == "int") {
        if (generated_integer_literal(binary->right))
          right = generated_integer_literal_as_float(binary->right);
        else
          right = "((" + right + ") as f64)";
      }
      if (right_type && canonical_type_name(*right_type) == "float" &&
          left_type && canonical_type_name(*left_type) == "int") {
        if (generated_integer_literal(binary->left))
          left = generated_integer_literal_as_float(binary->left);
        else
          left = "((" + left + ") as f64)";
      }
      return "(" + left + ") " + binary->op + " (" + right + ")";
    }
    // Moss Int arithmetic wraps; Rust's `-x` panics on Int min in debug.
    if (tree_parse_unary(e, unary_operation, unary_operand) &&
        unary_operation == "-") {
      const string inner = unary_operand;
      if (generated_integer_literal(inner))
        if (auto numeric = tree_numeric_literal_value("-" + inner))
          return rust_numeric_literal(*numeric);
      auto operand_type = generated_expr_type(inner, types);
      if (operand_type && canonical_type_name(*operand_type) == "int") {
        string operand = expr(inner, d, locals, types);
        operand = value_from_borrowed_message_parameter(inner, operand, types);
        return "(" + operand + ").wrapping_neg()";
      }
      // Float: negate the whole postfix operand (P25-S-ARCH-022), so `-v[0]`
      // is `-(v[0])`, never `(-v)[0]`.
      if (operand_type && canonical_type_name(*operand_type) == "float") {
        string operand = expr(inner, d, locals, types);
        operand = value_from_borrowed_message_parameter(inner, operand, types);
        return "-(" + operand + ")";
      }
    }
    if (tree_parse_unary(e, unary_operation, unary_operand) &&
        unary_operation == "~") {
      string operand = expr(unary_operand, d, locals, types);
      operand = value_from_borrowed_message_parameter(unary_operand, operand, types);
      return "!(" + operand + ")";
    }
    // Only classify a quoted source form as a single literal after top-level
    // operators have had a chance to split it. Expressions such as
    // "a" + "b" and "a" == "b" also begin and end with quote characters.
    if (tree_literal_class(e) == est::LiteralClass::String)
      return "MossStr::from(" + e + ")";
    if (tree_literal_class(e) == est::LiteralClass::Character)
      return e;
    if (auto parts = tree_parse_vector_literal(e)) {
      std::ostringstream r; r << "vec![";
      for (size_t i=0;i<parts->size();++i) {
        if (i) r << ", ";
        string value = expr(parts->at(i), d, locals, types);
        auto value_type = generated_expr_type(parts->at(i), types);
        if (value_type && domains_.count(canonical_type_name(*value_type)))
          value = "moss_rt::share_domain(&(" + value + "))";
        r << value;
      }
      r << "]"; return r.str();
    }
    string ib, ii;
    if (tree_parse_index(e, ib, ii)) {
      auto base_type = generated_expr_type(ib, types);
      if (base_type && shared_protocol_map_key_from_type(*base_type)) {
        const string key_type = *shared_protocol_map_key_from_type(*base_type);
        const string key = expr(ii, d, locals, types);
        const auto key_binding = tree_identifier_name(ii);
        const string borrowed = key_binding &&
            borrowed_function_parameters_.count(*key_binding)
            ? "__moss_key" : "&__moss_key";
        return "{ let __moss_key = " + key + "; (" +
            expr(ib, d, locals, types) +
            ").get_with(" + shared_map_hash(key_type, "__moss_key") +
            ", " + borrowed + ", |a, b| " +
            shared_map_equal(key_type, "a", "b") +
            ").expect(\"Moss missing map key\").clone() }";
      }
      if (base_type && canonical_type_name(*base_type) == "RangeBatch")
        return "(" + expr(ib, d, locals, types) + ").get(" +
            expr(ii, d, locals, types) + ")";
      if (base_type && canonical_type_name(*base_type) == "Range")
        return "(" + expr(ib, d, locals, types) + ").get(" +
            expr(ii, d, locals, types) + ")";
      bool string_index = tree_literal_class(ii) == est::LiteralClass::String;
      string ir = string_index ? ii : expr(ii, d, locals, types);
      bool map_index = base_type &&
          (canonical_type_name(*base_type) == "map" ||
           starts_with(canonical_type_name(*base_type), "map["));
      if (!string_index && !map_index) ir = "(" + ir + ") as usize";
      else if (map_index && !string_index) {
        // HashMap<String, _> accepts `str` as a borrowed lookup key. This
        // works for both an owned String and an already-borrowed &String,
        // whereas another `&` would emit an invalid &&String lookup.
        const auto key_binding = tree_identifier_name(ii);
        auto key_type = generated_expr_type(ii, types);
        if (key_type && canonical_type_name(*key_type) == "string")
          ir = "(" + ir + ").as_str()";
        else if (!key_binding || !borrowed_function_parameters_.count(*key_binding))
          ir = "&(" + ir + ")";
      }
      if (map_index && phase21_result_context_ &&
          phase21_collection_error_checked("MissingKey")) {
        const auto* conditional_read = resolve_direct_map_read_error_fact();
        if (!conditional_read)
          throw std::runtime_error(
              "MOSS_INTERNAL_INVARIANT conditional CollectionError Map read fact missing");
        if (auto checked_map_types = map_key_value_types(*base_type);
            checked_map_types &&
            canonical_type_name(conditional_read->key_type) !=
                canonical_type_name(checked_map_types->first))
          throw std::runtime_error(
              "MOSS_INTERNAL_INVARIANT conditional CollectionError Map read key type mismatch");
        string value = "*__moss_entry";
        if (auto map_types = map_key_value_types(*base_type);
            map_types && !copy_type(map_types->second)) {
          const auto* ownership = phase21_codegen_statement_
              ? resolve_local_ownership_fact(*phase21_codegen_statement_,
                    "")
              : nullptr;
          if (!ownership || ownership->access != Effect::Read)
            throw std::runtime_error(
                "MOSS_INTERNAL_INVARIANT nonprimitive Map index read requires exact checked READ ownership authority");
          // Keep the exact entry as a place; the caller's checked READ lowering
          // borrows it. The conditional-error fact only authorizes the miss edge.
          value = "*__moss_entry";
        }
        return "match (" + expr(ib, d, locals, types) + ").get(" + ir +
            ") { Some(__moss_entry) => " + value + ", None => " +
            phase21_collection_error_exit("MissingKey") + " }";
      }
      string indexed = "(" + expr(ib, d, locals, types) + ")[" + ir + "]";
      const auto indexed_type = generated_expr_type(e, types);
      if (indexed_type && !copy_type(*indexed_type)) {
        const auto* ownership = phase21_codegen_statement_
            ? resolve_local_ownership_fact(*phase21_codegen_statement_,
                  "")
            : nullptr;
        if (ownership && (ownership->access == Effect::Read ||
                          ownership->access == Effect::Write))
          return indexed;
        if (phase21_codegen_statement_ &&
            (phase21_codegen_statement_->kind == Stmt::Kind::If ||
             phase21_codegen_statement_->kind == Stmt::Kind::While)) {
          const auto* authority = resolve_indexed_expression_read_fact(
              *phase21_codegen_statement_, e, "");
          const auto checked_receiver_type = generated_expr_type(ib, types);
          if (!authority || !checked_receiver_type ||
              canonical_type_name(authority->result_type) !=
                  canonical_type_name(*indexed_type) ||
              canonical_type_name(authority->receiver_type) !=
                  canonical_type_name(*checked_receiver_type))
            throw std::runtime_error(
                "MOSS_INTERNAL_INVARIANT indexed expression-read TypeRef contradicts exact checked lowering type");
          return indexed;
        }
        throw std::runtime_error(
            "MOSS_INTERNAL_INVARIANT nonprimitive indexed read lacks exact checked READ/WRITE, Copy, or projected-borrow authority");
      }
      return indexed;
    }

    string projection_base, projection_field;
    if (tree_parse_member_access(e, projection_base, projection_field)) {
      if (tree_plain_identifier(projection_field)) {
        auto projection_type = generated_expr_type(projection_base, types);
        if (projection_type) {
          auto object = objects_.find(nominal_template_name(*projection_type));
          if (object != objects_.end()) {
            auto field = std::find_if(object->second->fields.begin(),
                object->second->fields.end(), [&](const Field& candidate) {
                  return candidate.name == projection_field;
                });
            if (field != object->second->fields.end()) {
              string p_b, p_i;
              const bool indexed_base = tree_parse_index(projection_base, p_b, p_i);
              string base_rendered = indexed_base
                  ? read_call_place(projection_base, d, locals, types)
                  : expr(projection_base, d, locals, types);
              string projected = "(" + base_rendered + ")." + rust_identifier(projection_field);
              if (indexed_base && !copy_type(field->type)) {
                string indexed_collection, indexed_key;
                const bool direct_index = tree_parse_index(
                    projection_base, indexed_collection, indexed_key);
                const auto indexed_collection_type = direct_index
                    ? generated_expr_type(indexed_collection, types)
                    : std::nullopt;
                const bool map_projection = indexed_collection_type &&
                    map_key_value_types(*indexed_collection_type).has_value();
                if (map_projection) {
                  const auto* authority =
                      resolve_projected_collection_read_fact(e);
                  if (!authority ||
                      canonical_type_name(authority->projected_collection_type
                          ? authority->projected_collection_type->to_canonical_string()
                          : "") != canonical_type_name(field->type))
                    throw std::runtime_error(
                        "MOSS_INTERNAL_INVARIANT projected Map collection-read fact type mismatch");
                  return "&(" + projected + ")";
                }
              }
              return projected;
            }
          }
        }
      }
    }

    string member_receiver, member_name;
    vector<string> member_arguments;
    if (tree_parse_member_call(e, member_receiver, member_name, member_arguments)) {
      const std::optional<NativeEstCursor> member_call_occurrence =
          native_member_method_call_cursor(active_native_est_cursor_);
      const auto member_receiver_occurrence =
          native_member_receiver_cursor(active_native_est_cursor_);
      struct NativeMemberReceiverScope {
        const NativeEstCursor*& slot;
        const NativeEstCursor* prior;
        NativeMemberReceiverScope(const NativeEstCursor*& target,
                                  const NativeEstCursor* current)
            : slot(target), prior(target) { slot = current; }
        ~NativeMemberReceiverScope() { slot = prior; }
      } member_receiver_scope(active_native_est_cursor_,
          member_receiver_occurrence ? &*member_receiver_occurrence
                                     : active_native_est_cursor_);
      string canon_recv = canonical_type_name(member_receiver);
      if (canon_recv == "int" || canon_recv == "float" || canon_recv == "char" || canon_recv == "string" || canon_recv == "bool") {
        if ((member_name == "less" || member_name == "isequal") &&
            member_arguments.size() == 2)
          return scalar_protocol_rust(member_name, canon_recv,
                                      expr(member_arguments[0], d, locals, types),
                                      expr(member_arguments[1], d, locals, types));
        if (member_name == "hash" && member_arguments.size() == 1)
          return scalar_protocol_rust("hash", canon_recv,
                                      expr(member_arguments[0], d, locals, types));
        if (member_name == "to_string" && member_arguments.size() == 1) {
          return "MossStr::from((" + expr(member_arguments[0], d, locals, types) + ").to_string())";
        }
        if (canon_recv == "char" && member_name == "codepoint" && member_arguments.size() == 1) {
          return "((" + expr(member_arguments[0], d, locals, types) + ") as u32 as i64)";
        }
      }
      string receiver_expression = method_receiver_place(member_receiver, member_name, member_arguments, d, locals, types);
      if (starts_with(receiver_expression, "*")) receiver_expression = "(" + receiver_expression + ")";
      auto receiver_type = generated_expr_type(member_receiver, types);
      if (staged_abi_v10() && tree_empty_constructor_named(member_receiver, "Map") &&
          member_name == "get" &&
          member_arguments.size() == 2) {
        auto key = generated_expr_type(member_arguments[0], types);
        auto value = generated_expr_type(member_arguments[1], types);
        if (key && value) {
          receiver_type = "map[" + canonical_type_name(*key) + "," +
              canonical_type_name(*value) + "]";
          receiver_expression = "<" + rust_type(*receiver_type) + ">::new()";
        }
      }
      if (receiver_type) {
        string concrete = canonical_type_name(*receiver_type);
        if (collection_named(parsed_type(concrete), {"queue"}) &&
            member_name == "pop" && member_arguments.empty()) {
          if (resolve_collection_outcome_fact(
                  TypedCollectionOutcomeFactV1::Operation::QueuePop)) {
            if (!phase21_result_context_ ||
                !phase21_collection_error_checked("Empty"))
              throw std::runtime_error(
                  "MOSS_INTERNAL_INVARIANT checked Queue.pop outcome lacks its error context");
            return phase21_collection_error_exit("Empty");
          }
        }
        if (collection_named(parsed_type(concrete), {"map"}) &&
            member_name == "remove" && member_arguments.size() == 1) {
          if (resolve_collection_outcome_fact(
                  TypedCollectionOutcomeFactV1::Operation::MapRemove)) {
            if (!phase21_result_context_ ||
                !phase21_collection_error_checked("MissingKey"))
              throw std::runtime_error(
                  "MOSS_INTERNAL_INVARIANT checked Map.remove outcome lacks its error context");
            const string key = expr(member_arguments.front(), d, locals, types);
            return "{ let __moss_checked_collection_key = " + key + "; " +
                phase21_collection_error_exit("MissingKey") + " }";
          }
        }
        if (concrete == "FileIO" && member_name == "read" &&
            member_arguments.size() == 2) {
          string read = receiver_expression +
              (phase21_result_context_ ? ".read_checked(" : ".read(") +
              expr(member_arguments[0], d, locals, types) + ", " +
              expr(member_arguments[1], d, locals, types) + ")";
          return phase21_result_context_
              ? phase21_propagate(read + ".map_err(__moss_raise_fileio)")
              : read;
        }
        if (concrete == "FileIO" && member_name == "write" &&
            member_arguments.size() == 2) {
          string call = receiver_expression +
              (phase21_result_context_ ? ".write_checked(" : ".write(") +
              expr(member_arguments[0], d, locals, types) + ", &( " +
              expr(member_arguments[1], d, locals, types) + " ))";
          return phase21_result_context_
              ? phase21_propagate(call + ".map_err(__moss_raise_fileio)")
              : call;
        }
        if (concrete == "FileIO" && member_name == "open" &&
            member_arguments.size() == 2) {
          string call = receiver_expression +
              (phase21_result_context_ ? ".open_in_place_checked(&( "
                                       : ".open_in_place(&( ") +
              expr(member_arguments[0], d, locals, types) + " ), " +
              rust_string_literal(trim(member_arguments[1])) + ")";
          return phase21_result_context_
              ? phase21_propagate(call + ".map_err(__moss_raise_fileio)")
              : call;
        }
        if (concrete == "FileIO" && member_name == "sync" &&
            member_arguments.size() <= 1) {
          bool dataonly = !member_arguments.empty() &&
              tree_identifier_name(member_arguments[0]) ==
                  std::optional<string>("dataonly");
          string call = receiver_expression +
              (phase21_result_context_
                  ? ".sync_checked(" + string(dataonly ? "true" : "false") + ")"
                  : dataonly ? ".sync_dataonly()" : ".sync()");
          return phase21_result_context_
              ? phase21_propagate(call + ".map_err(__moss_raise_fileio)")
              : call;
        }
        if (concrete == "FileIO" && member_name == "close" &&
            member_arguments.empty()) {
          string call = receiver_expression +
              (phase21_result_context_ ? ".close_checked()" : ".close()");
          return phase21_result_context_
              ? phase21_propagate(call + ".map_err(__moss_raise_fileio)")
              : call;
        }
        // Agent A checks the one-argument read form as a bounded RangeBatch.
        // This branch only adapts that checked form to Agent B's read_batch
        // ABI; it deliberately does not repeat request legality or bound
        // analysis here.
        if (concrete == "FileIO" && member_name == "read" &&
            member_arguments.size() == 1) {
          const size_t batch_id = fileio_chunk_temp_++;
          string receiver_temp = "__moss_fileio_batch_receiver_" +
              std::to_string(batch_id);
          std::ostringstream lowered;
          lowered << "{ let " << receiver_temp << " = &("
                  << receiver_expression << "); ";
          string requests = tree_strip_expression_grouping(
              trim(member_arguments.front()));
          bool inline_requests = tree_parse_vector_literal(requests).has_value();
          if (!inline_requests) {
            auto request_type = generated_expr_type(requests, types);
            string concrete_request_type = request_type
                ? canonical_type_name(*request_type) : string();
            if (!starts_with(concrete_request_type, "vector[") ||
                !ends_with(concrete_request_type, "]"))
              throw std::runtime_error(
                  "internal error: checked FileIO batch requests lost their vector type");
            string record_type = trim(concrete_request_type.substr(
                7, concrete_request_type.size() - 8));
            auto record = objects_.find(canonical_type_name(record_type));
            if (record == objects_.end())
              throw std::runtime_error(
                  "internal error: checked FileIO batch request record is unavailable");
            auto offset_field = std::find_if(record->second->fields.begin(),
                record->second->fields.end(), [](const Field& field) {
                  return field.name == "offset";
                });
            auto size_field = std::find_if(record->second->fields.begin(),
                record->second->fields.end(), [](const Field& field) {
                  return field.name == "size";
                });
            if (offset_field == record->second->fields.end() ||
                size_field == record->second->fields.end())
              throw std::runtime_error(
                  "internal error: checked FileIO batch request fields are unavailable");
            string requests_temp = "__moss_fileio_batch_records_" +
                std::to_string(batch_id);
            string pairs_temp = "__moss_fileio_batch_pairs_" +
                std::to_string(batch_id);
            lowered << "let " << requests_temp << " = &("
                    << expr(requests, d, locals, types) << "); "
                    << "let " << pairs_temp << " = " << requests_temp
                    << ".iter().map(|__moss_request| (__moss_request."
                    << rust_identifier(offset_field->name)
                    << ", __moss_request."
                    << rust_identifier(size_field->name)
                    << ")).collect::<std::vec::Vec<_>>(); "
                    << receiver_temp
                    << (phase21_result_context_ ? ".read_batch_checked(&"
                                                : ".read_batch(&")
                    << pairs_temp << ") }";
            return phase21_result_context_
                ? phase21_propagate("(" + lowered.str() +
                    ").map_err(__moss_raise_fileio)")
                : lowered.str();
          }

          vector<string> entries = tree_parse_vector_literal(requests)
              .value_or(vector<string>{});
          vector<std::pair<string, string>> coordinate_temps;
          for (size_t entry_index = 0; entry_index < entries.size();
               ++entry_index) {
            string entry = trim(entries[entry_index]);
            vector<string> coordinates;
            if (auto tuple_coordinates = tree_tuple_elements(entry)) {
              coordinates = std::move(*tuple_coordinates);
            } else {
              string constructor;
              vector<string> fields;
              if (tree_parse_simple_call(entry, constructor, fields) &&
                  objects_.count(canonical_type_name(constructor))) {
                std::map<string, string> named;
                for (const auto& field : fields) {
                  string name, expression;
                  if (tree_parse_named_argument(field, name, expression))
                    named[name] = expression;
                }
                if (named.count("offset") && named.count("size") &&
                    fields.size() == 2)
                  coordinates = {named["offset"], named["size"]};
              }
            }
            if (coordinates.size() != 2)
              throw std::runtime_error(
                  "internal error: checked FileIO batch entry was not a request pair");
            string offset_temp = "__moss_fileio_batch_offset_" +
                std::to_string(batch_id) + "_" + std::to_string(entry_index);
            string size_temp = "__moss_fileio_batch_size_" +
                std::to_string(batch_id) + "_" + std::to_string(entry_index);
            lowered << "let " << offset_temp << " = "
                    << expr(coordinates[0], d, locals, types) << "; let "
                    << size_temp << " = "
                    << expr(coordinates[1], d, locals, types) << "; ";
            coordinate_temps.emplace_back(std::move(offset_temp),
                                          std::move(size_temp));
          }
          lowered << receiver_temp
                  << (phase21_result_context_ ? ".read_batch_checked(&["
                                              : ".read_batch(&[");
          for (size_t index = 0; index < coordinate_temps.size(); ++index) {
            if (index) lowered << ", ";
            lowered << "(" << coordinate_temps[index].first << ", "
                    << coordinate_temps[index].second << ")";
          }
          lowered << "]) }";
          return phase21_result_context_
              ? phase21_propagate("(" + lowered.str() +
                  ").map_err(__moss_raise_fileio)")
              : lowered.str();
        }
        // Phase 20: Moss `range.length()` lowers to Agent B's runtime
        // `Range::len() -> i64`; Agent B need not expose a `.length()` alias.
        if ((concrete == "Range" || concrete == "RangeBatch") && member_name == "length" && member_arguments.empty())
          return "(" + receiver_expression + ").len()";
        if (concrete == "Range" && member_name == "slice" && member_arguments.size() == 2)
          return "(" + receiver_expression + ").slice(" +
              expr(member_arguments[0], d, locals, types) + ", " +
              expr(member_arguments[1], d, locals, types) + ")";
        if (concrete == "string") {
          string source = "(" + receiver_expression + ")";
          if (member_name == "length" && member_arguments.empty())
            return "__moss_string_length(&" + source + ")";
          if (member_name == "chars" && member_arguments.empty())
            return source + ".chars().collect::<std::vec::Vec<char>>()";
          if (member_name == "char_at" && member_arguments.size() == 1)
            return "__moss_string_char_at(&(" + source + "), " + expr(member_arguments[0], d, locals, types) + ")";
          if ((member_name == "less" || member_name == "isequal") &&
              member_arguments.size() == 1)
            return scalar_protocol_rust(member_name, concrete, source,
                                        expr(member_arguments[0], d, locals, types));
          if (member_name == "hash" && member_arguments.empty())
            return scalar_protocol_rust("hash", concrete, source);
          if (member_name == "find" && member_arguments.size() == 1)
            return "{ let __moss_pattern = " + expr(member_arguments[0], d, locals, types) +
                "; __moss_string_find(&" + source + ", __moss_pattern.as_str()) }";
          if (member_name == "slice" && member_arguments.size() == 2)
            return "__moss_string_slice(&" + source + ", " +
                expr(member_arguments[0], d, locals, types) + ", " +
                expr(member_arguments[1], d, locals, types) + ")";
          if (member_name == "ascii_lower" && member_arguments.empty())
            return "MossStr::from(" + source + ".to_ascii_lowercase())";
          if (member_name == "ascii_upper" && member_arguments.empty())
            return "MossStr::from(" + source + ".to_ascii_uppercase())";
          if (member_name == "reserve" && member_arguments.size() == 1)
            return "{ let __moss_string = &mut (" +
                write_call_place(member_receiver, d, locals, types) +
                "); let __moss_additional = " +
                expr(member_arguments[0], d, locals, types) +
                "; if __moss_additional > 0 { __moss_string.reserve(__moss_additional as usize); } }";
          if (member_name == "append" && member_arguments.size() == 1)
            return "{ let __moss_string = &mut (" +
                write_call_place(member_receiver, d, locals, types) +
                "); let __moss_part = " + [&]() {
                  const string part = trim(member_arguments[0]);
                  return tree_literal_class(part) == est::LiteralClass::String
                      ? part : "&(" +
                      expr(member_arguments[0], d, locals, types) + ")";
                }() + "; __moss_string.push_str(" + [&]() {
                  return string("__moss_part") +
                      (tree_literal_class(member_arguments[0]) ==
                               est::LiteralClass::String
                           ? "" : ".as_str()");
                }() + "); }";
          if (member_name == "split" && member_arguments.size() == 1)
            return "{ let __moss_separator = " + expr(member_arguments[0], d, locals, types) +
                "; if __moss_separator.is_empty() { std::process::abort(); } " + source +
                ".split(__moss_separator.as_str()).map(MossStr::from).collect::<std::vec::Vec<MossStr>>() }";
          if (member_name == "join" && member_arguments.size() == 1)
            return "MossStr::from((" + expr(member_arguments[0], d, locals, types) + ").join(" + source + ".as_str()))";
        }
        if (concrete == "char") {
          string source = "(" + receiver_expression + ")";
          if (member_name == "to_string" && member_arguments.empty())
            return "MossStr::from(" + source + ".to_string())";
          if (member_name == "codepoint" && member_arguments.empty())
            return "((" + source + ") as u32 as i64)";
          if ((member_name == "less" || member_name == "isequal") &&
              member_arguments.size() == 1)
            return scalar_protocol_rust(member_name, concrete, source,
                                        expr(member_arguments[0], d, locals, types));
          if (member_name == "hash" && member_arguments.empty())
            return scalar_protocol_rust("hash", concrete, source);
        }
        if (concrete == "int") {
          string source = "(" + receiver_expression + ")";
          if (member_name == "to_string" && member_arguments.empty())
            return "MossStr::from(" + source + ".to_string())";
          if ((member_name == "less" || member_name == "isequal") &&
              member_arguments.size() == 1)
            return scalar_protocol_rust(member_name, concrete, source,
                                        expr(member_arguments[0], d, locals, types));
          if (member_name == "hash" && member_arguments.empty())
            return scalar_protocol_rust("hash", concrete, source);
        }
        if (concrete == "float") {
          string source = "(" + receiver_expression + ")";
          if (member_name == "to_string" && member_arguments.empty())
            return "MossStr::from(" + source + ".to_string())";
          if ((member_name == "less" || member_name == "isequal") &&
              member_arguments.size() == 1)
            return scalar_protocol_rust(member_name, concrete, source,
                                        expr(member_arguments[0], d, locals, types));
          if (member_name == "hash" && member_arguments.empty())
            return scalar_protocol_rust("hash", concrete, source);
        }
        if (concrete == "bool") {
          string source = "(" + receiver_expression + ")";
          if (member_name == "to_string" && member_arguments.empty())
            return "MossStr::from(" + source + ".to_string())";
          if ((member_name == "less" || member_name == "isequal") &&
              member_arguments.size() == 1)
            return scalar_protocol_rust(member_name, concrete, source,
                                        expr(member_arguments[0], d, locals, types));
          if (member_name == "hash" && member_arguments.empty())
            return scalar_protocol_rust("hash", concrete, source);
        }
        if (staged_abi_v10() && starts_with(concrete, "set[") &&
            ends_with(concrete, "]")) {
          const string key_type = concrete.substr(4, concrete.size() - 5);
          const bool nominal = shared_nominal_map_key(key_type);
          const string equal = nominal ? "a.isequal(b)"
              : tree_strip_expression_grouping(
                    scalar_protocol_rust("isequal", key_type, "*a", "*b"));
          if (member_name == "values" && member_arguments.empty())
            return "(" + receiver_expression + ").keys()";
          if ((member_name == "insert" || member_name == "contains") &&
              member_arguments.size() == 1) {
            string key = expr(member_arguments[0], d, locals, types);
            if (nominal && is_object_view(member_arguments[0], d, locals, types))
              key = "(" + key + ")." + access_value_method(key_type) + "()";
            const auto key_binding = tree_identifier_name(member_arguments[0]);
            const string borrowed = key_binding &&
                borrowed_function_parameters_.count(*key_binding)
                ? "__moss_key" : "&__moss_key";
            const string hash = nominal ? "__moss_key.hash()"
                : tree_strip_expression_grouping(
                      scalar_protocol_rust("hash", key_type, "__moss_key"));
            if (member_name == "insert")
              return "{ let __moss_key = " + key + "; let __moss_hash = " +
                  hash + "; (" + write_call_place(member_receiver, d, locals, types) +
                  ").insert_with(__moss_hash, __moss_key, |a, b| " + equal + "); }";
            return "{ let __moss_key = " + key + "; (" + receiver_expression +
                ").contains_with(" + hash + ", " + borrowed +
                ", |a, b| " + equal + ") }";
          }
        }
        if (auto map_types = map_key_value_types(concrete)) {
          const bool shared_key = shared_protocol_map_key(map_types->first);
          if (member_name == "delete" && member_arguments.size() == 3) {
            string key = expr(member_arguments[0], d, locals, types);
            string fallback = expr(member_arguments[1], d, locals, types);
            string flag = write_call_place(member_arguments[2], d, locals, types);
            if (shared_key) {
              const bool view_key = is_object_view(member_arguments[0], d, locals, types);
              if (view_key)
                key = "(" + key + ")." + access_value_method(map_types->first) + "()";
              const auto key_binding = tree_identifier_name(member_arguments[0]);
              const string borrowed = !view_key && key_binding &&
                  borrowed_function_parameters_.count(*key_binding)
                  ? "__moss_key" : "&__moss_key";
              return "{ let __moss_key = " + key +
                  "; let __moss_fallback = " + fallback +
                  "; let __moss_removed = (" + receiver_expression +
                  ").remove_with(" + shared_map_hash(map_types->first, "__moss_key") +
                  ", " + borrowed + ", |a, b| " +
                  shared_map_equal(map_types->first, "a", "b") +
                  "); let _ = &( " + flag +
                  " ); " + flag + " = __moss_removed.is_some(); " +
                  "__moss_removed.unwrap_or(__moss_fallback) }";
            }
            string lookup = map_types->first == "string" ? "__moss_key.as_str()" : "&__moss_key";
            return "{ let __moss_key = " + key + "; let __moss_fallback = " + fallback +
                "; let __moss_removed = (" + receiver_expression + ").remove(" + lookup +
                "); let _ = &(" + flag + "); " + flag + " = __moss_removed.is_some(); " +
                "__moss_removed.unwrap_or(__moss_fallback) }";
          }
          if (member_name == "get" && member_arguments.size() == 2) {
            string key = expr(member_arguments[0], d, locals, types);
            if (shared_key) {
              string fallback = expr(member_arguments[1], d, locals, types);
              const bool view_key = is_object_view(member_arguments[0], d, locals, types);
              if (view_key)
                key = "(" + key + ")." + access_value_method(map_types->first) + "()";
              const auto key_binding = tree_identifier_name(member_arguments[0]);
              const string borrowed = !view_key && key_binding &&
                  borrowed_function_parameters_.count(*key_binding)
                  ? "__moss_key" : "&__moss_key";
              if (!copy_type(map_types->second))
                return "{ let __moss_key = " + key +
                    "; (" +
                    receiver_expression + ").get_with(" +
                    shared_map_hash(map_types->first, "__moss_key") +
                    ", " + borrowed + ", |a, b| " +
                    shared_map_equal(map_types->first, "a", "b") +
                    ").unwrap_or(&(" + fallback + ")) }";
              return "{ let __moss_key = " + key +
                  "; (" + receiver_expression +
                  ").get_with(" + shared_map_hash(map_types->first, "__moss_key") +
                  ", " + borrowed + ", |a, b| " +
                  shared_map_equal(map_types->first, "a", "b") +
                  ").copied().unwrap_or(" + fallback + ") }";
            }
            if (map_types->first == "string")
              key = "(" + key + ").as_str()";
            else
              key = "&( " + key + " )";
            string fallback = expr(member_arguments[1], d, locals, types);
            if (!copy_type(map_types->second))
              return "(" + receiver_expression + ").get(" + key +
                  ").unwrap_or(&(" + fallback + "))";
            return "(" + receiver_expression + ").get(" + key + ").copied().unwrap_or(" + fallback + ")";
          }
          if (member_name == "get" && member_arguments.size() == 1) {
            string key = expr(member_arguments.front(), d, locals, types);
            const string lookup = map_types->first == "string"
                ? "(" + key + ").as_str()" : "&( " + key + " )";
            const string method = checked_map_get_write_through_
                ? "get_mut" : "get";
            if (phase21_result_context_ &&
                phase21_collection_error_checked("MissingKey")) {
              const string lookup_result = "(" + receiver_expression + ")." +
                  method + "(" + lookup + ")";
              string value = "__moss_entry";
              if (!checked_map_get_write_through_ &&
                  copy_type(map_types->second))
                value = "*__moss_entry";
              return "match " + lookup_result + " { Some(__moss_entry) => " +
                  value + ", None => " +
                  phase21_collection_error_exit("MissingKey") + " }";
            }
            string result = "(" + receiver_expression + ")." + method +
                "(" + lookup + ").unwrap_or_else(|| std::process::abort())";
            if (!checked_map_get_write_through_ &&
                copy_type(map_types->second))
              result = "* (" + result + ")";
            return result;
          }
          if (member_name == "remove" && member_arguments.size() == 1) {
            string key = expr(member_arguments.front(), d, locals, types);
            const auto* outcome = resolve_collection_outcome_fact(
                TypedCollectionOutcomeFactV1::Operation::MapRemove);
            if (outcome) {
              if (!phase21_result_context_ ||
                  !phase21_collection_error_checked("MissingKey"))
                throw std::runtime_error(
                    "MOSS_INTERNAL_INVARIANT checked Map.remove outcome lacks its error context");
              return "{ let __moss_checked_collection_key = " + key + "; " +
                  phase21_collection_error_exit("MissingKey") + " }";
            }
            const string lookup = map_types->first == "string"
                ? "(" + key + ").as_str()" : "&( " + key + " )";
            const string remove = "(" + receiver_expression + ").remove(" +
                lookup + ")";
            if (phase21_result_context_ &&
                phase21_collection_error_checked("MissingKey"))
              return "match " + remove + " { Some(__moss_removed) => " +
                  "__moss_removed, None => " +
                  phase21_collection_error_exit("MissingKey") + " }";
            return remove + ".unwrap_or_else(|| std::process::abort())";
          }
          if (member_name == "keys" && member_arguments.empty()) {
            if (resolve_collection_view_fact(
                    TypedCollectionViewOperationV1::MapKeys,
                    member_receiver))
              return "(" + receiver_expression + ").keys().collect::<std::vec::Vec<_>>()";
            if (!copy_type(map_types->first))
              throw std::runtime_error(
                  "MOSS_INTERNAL_INVARIANT nonprimitive Map.keys requires checked nonescaping CollectionView authority");
            if (shared_key)
              return "(" + receiver_expression + ").keys()";
            if (map_types->first == "float")
              return "(" + receiver_expression + ").keys().map(|k| k.0).collect::<std::vec::Vec<_>>()";
            return "(" + receiver_expression + ").keys().copied().collect::<std::vec::Vec<_>>()";
          }
          if (member_name == "values" && member_arguments.empty()) {
            if (resolve_collection_view_fact(
                    TypedCollectionViewOperationV1::MapValues,
                    member_receiver))
              return "(" + receiver_expression + ").values().collect::<std::vec::Vec<_>>()";
            if (!copy_type(map_types->second))
              throw std::runtime_error(
                  "MOSS_INTERNAL_INVARIANT nonprimitive Map.values requires checked nonescaping CollectionView authority");
            return shared_key
                ? "(" + receiver_expression + ").values()"
                : "(" + receiver_expression + ").values().copied().collect::<std::vec::Vec<_>>()";
          }
        }
        if (collection_named(parsed_type(concrete), {"queue"}))
          member_name = member_name == "push" ? "push_back" : member_name == "pop" ? "pop_front" : member_name;
        const string source_method_name =
            member_name == "push_back" ? "push" :
            member_name == "pop_front" ? "pop" : member_name;
        const Method* checked_method = objects_.count(concrete)
            ? checked_method_for_occurrence(source_method_name,
                member_call_occurrence ? &*member_call_occurrence : nullptr)
            : nullptr;
        if (const Method* method = checked_method) {
          const auto external_method_module =
              phase21_external_module_for_symbol(concrete);
          // A consuming concrete method cannot be invoked through either a
          // field-only access view or an ordinary borrowed function parameter.
          // The former has no method surface at all; the latter must retain
          // Moss's READ helper contract. Materialize the checked object value
          // at this native boundary before selecting the owned method.
          if (method->receiver_effect == Effect::Consume) {
            if (is_object_view(member_receiver, d, locals, types))
              receiver_expression = "(" + receiver_expression + ")." +
                  access_value_method(receiver_type.value_or("")) + "()";
            else if (tree_identifier_name(member_receiver) &&
                     borrowed_function_parameters_.count(
                         *tree_identifier_name(member_receiver)))
              receiver_expression = "(*(" + receiver_expression + ")).clone()";
          }
          vector<string> argument_temporaries(member_arguments.size());
          if (method->receiver_effect != Effect::Read) {
            for (size_t index = 0; index < member_arguments.size(); ++index) {
              string nested_receiver, nested_method;
              vector<string> nested_arguments;
              if (tree_parse_member_call(member_arguments[index], nested_receiver,
                                    nested_method, nested_arguments) &&
                  tree_expressions_equal(nested_receiver, member_receiver))
                argument_temporaries[index] = "__moss_call_argument_" +
                    std::to_string(call_argument_temp_++);
            }
          }
          const bool standard_write_bridge = external_method_module &&
              std::any_of(method->params.begin(), method->params.end(),
                  [&](const Param& parameter) {
                    const size_t index = static_cast<size_t>(
                        &parameter - method->params.data());
                    return !standard_error_variants(canonical_type_name(
                        parameter.type)).empty() &&
                        method_effect(*method, index) == Effect::Write;
                  });
          std::ostringstream bridge_before, bridge_after, rendered;
          rendered << (standard_write_bridge ? "__moss_bridge_receiver" :
              receiver_expression) << "." << viewed_method_name(
                  member_receiver, member_name, member_arguments, d, locals,
                  types) << "(";
          for (size_t index = 0; index < member_arguments.size(); ++index) {
            if (index) rendered << ", ";
            string argument = method_call_argument(*method, index,
                argument_temporaries[index].empty() ? member_arguments[index]
                                                    : argument_temporaries[index],
                d, locals, types);
            bool standard_write_argument = false;
            if (external_method_module && index < method->params.size() &&
                !standard_error_variants(canonical_type_name(
                    method->params[index].type)).empty()) {
              const string external_type = tooling_name("moss_" +
                  *external_method_module) + "::" +
                  canonical_type_name(method->params[index].type);
              const Effect effect = method_effect(*method, index);
              if (effect == Effect::Write) {
                standard_write_argument = true;
                const string prefix = "__moss_standard_write_" +
                    std::to_string(call_argument_temp_++);
                const string local_ref = prefix + "_local";
                const string provider_value = prefix + "_provider";
                bridge_before << "let " << local_ref << " = " << argument
                  << "; let mut " << provider_value << " = " << external_type
                  << "::from((*" << local_ref << ").clone()); ";
                bridge_after << "*" << local_ref << " = "
                  << canonical_type_name(method->params[index].type)
                  << "::from(" << provider_value << "); ";
                argument = "&mut " + provider_value;
              } else
                argument = effect == Effect::Read
                    ? "&" + external_type + "::from((*(" + argument + ")).clone())"
                    : external_type + "::from(" + argument + ")";
            }
            if (standard_write_bridge && !standard_write_argument) {
              const string staged = "__moss_call_argument_" +
                  std::to_string(call_argument_temp_++);
              bridge_before << "let " << staged << " = " << argument << "; ";
              argument = staged;
            }
            rendered << argument;
          }
          rendered << ")";
          string result = rendered.str();
          if (standard_write_bridge) {
            string receiver_binding = method->receiver_effect == Effect::Consume
                ? receiver_expression
                : (method->receiver_effect == Effect::Write ? "&mut (" :
                    "&(") + receiver_expression + ")";
            result = "{ let __moss_bridge_receiver = " + receiver_binding +
                "; " + bridge_before.str() + "let __moss_bridge_result = " +
                result + "; " + bridge_after.str() + "__moss_bridge_result }";
          }
          const string return_type = method->return_type.value_or("");
          const bool standard_return = external_method_module &&
              !standard_error_variants(canonical_type_name(return_type)).empty();
          if (standard_return)
            result = method->observable_effects.raise_set.empty()
                ? return_type + "::from(" + result + ")"
                : "(" + result + ").map(" + return_type + "::from)";
          if (!method->observable_effects.raise_set.empty()) {
            if (!phase21_result_context_)
              throw std::runtime_error(
                  "internal error: checked raising method call lacks a Phase 21 outcome context");
            result = phase21_propagate(result);
          }
          if (std::any_of(argument_temporaries.begin(),
                          argument_temporaries.end(),
                          [](const string& value) { return !value.empty(); })) {
            std::ostringstream sequenced;
            sequenced << "{ ";
            for (size_t index = 0; index < member_arguments.size(); ++index)
              if (!argument_temporaries[index].empty())
                sequenced << "let " << argument_temporaries[index] << " = "
                          << expr(member_arguments[index], d, locals, types)
                          << "; ";
            sequenced << result << " }";
            result = sequenced.str();
          }
          if (collection_named(parsed_type(concrete), {"vector", "queue"}) &&
              (member_name == "pop" || member_name == "pop_front"))
            result += ".unwrap_or_else(|| std::process::abort())";
          return result;
        }
        if (collection_named(parsed_type(concrete), {"vector", "queue"}) &&
            (member_name == "pop" || member_name == "pop_front") &&
            member_arguments.empty()) {
          if (member_name == "pop") {
            const auto* outcome = resolve_collection_outcome_fact(
                TypedCollectionOutcomeFactV1::Operation::QueuePop);
            if (outcome) {
              if (!phase21_result_context_ ||
                  !phase21_collection_error_checked("Empty"))
                throw std::runtime_error(
                    "MOSS_INTERNAL_INVARIANT checked Queue.pop outcome lacks its error context");
              return phase21_collection_error_exit("Empty");
            }
          }
          const string pop = receiver_expression + "." +
              viewed_method_name(member_receiver, member_name, member_arguments,
                                 d, locals, types) + "()";
          if (phase21_result_context_ &&
              phase21_collection_error_checked("Empty"))
            return "match " + pop + " { Some(__moss_item) => __moss_item, "
                "None => " + phase21_collection_error_exit("Empty") + " }";
          return pop + ".unwrap_or_else(|| std::process::abort())";
        }
        if (collection_named(parsed_type(concrete), {"vector"}) &&
            (member_name == "remove" || member_name == "swap_remove") &&
            member_arguments.size() == 1) {
          const string index = expr(member_arguments.front(), d, locals, types);
          const string place = write_call_place(member_receiver, d, locals, types);
          if (phase21_result_context_ &&
              phase21_collection_error_checked("InvalidIndex"))
            return "{ let __moss_collection_index = (" + index +
                ") as usize; let __moss_collection = &mut (" + place +
                "); if __moss_collection_index < __moss_collection.len() { "
                "__moss_collection." + member_name +
                "(__moss_collection_index) } else { " +
                phase21_collection_error_exit("InvalidIndex") + " } }";
          return receiver_expression + "." + member_name + "((" + index +
              ") as usize)";
        }
        if (collection_named(parsed_type(concrete), {"map"}) &&
            member_name == "remove" && member_arguments.size() == 1) {
          const auto* outcome = resolve_collection_outcome_fact(
              TypedCollectionOutcomeFactV1::Operation::MapRemove);
          if (outcome) {
            if (!phase21_result_context_ ||
                !phase21_collection_error_checked("MissingKey"))
              throw std::runtime_error(
                  "MOSS_INTERNAL_INVARIANT checked Map.remove outcome lacks its error context");
            return phase21_collection_error_exit("MissingKey");
          }
          return receiver_expression + ".remove(&(" +
              expr(member_arguments.front(), d, locals, types) + "))" +
              ".unwrap_or_else(|| std::process::abort())";
        }
      }
      // Trait/duck-typed calls are already statically specialized by the
      // checker; retain their ordinary Rust method spelling when the concrete
      // method is not available to this expression pass.
      std::ostringstream rendered;
      rendered << receiver_expression << "." << viewed_method_name(member_receiver, member_name, member_arguments, d, locals, types) << "(";
      for (size_t index = 0; index < member_arguments.size(); ++index) {
        if (index) rendered << ", ";
        rendered << expr(member_arguments[index], d, locals, types);
      }
      rendered << ")";
      return rendered.str();
    }

    string builtin;
    vector<string> builtin_args;
    if (tree_parse_simple_call(e, builtin, builtin_args)) {
      if (builtin == "Some" && builtin_args.size() == 1)
        return "Some(" + expr(builtin_args.front(), d, locals, types) + ")";
      if (builtin == "copy" && builtin_args.size() == 1) {
        const auto& checked_copy = resolve_copy_fact(e);
        if (!checked_copy.is_authoritative())
          throw std::runtime_error(
              "MOSS_INTERNAL_INVARIANT explicit copy fact is not authoritative");
        const auto source_type = generated_expr_type(builtin_args.front(), types);
        if (!source_type)
          throw std::runtime_error(
              "MOSS_INTERNAL_INVARIANT explicit copy source type is unavailable");
        return lower_fact_authorized_copy(checked_copy, builtin_args.front(),
                                          *source_type, d, locals, types);
      }
      if (builtin == "sqrt" && builtin_args.size() == 1)
        return "{ let __moss_sqrt_value: f64 = " +
            expr(builtin_args.front(), d, locals, types) +
            "; __moss_sqrt_value.sqrt() }";
      if ((builtin == "is_nan" || builtin == "is_finite") &&
          builtin_args.size() == 1)
        return "{ let __moss_float_value: f64 = " +
            expr(builtin_args.front(), d, locals, types) +
            "; __moss_float_value." + builtin + "() }";
      const std::string_view numeric_name = numeric_builtin_base(builtin);
      if (!numeric_name.empty() && builtin_args.size() == 1) {
        if (!phase21_result_context_)
          throw std::runtime_error("internal error: numeric raise lacks outcome context");
        const string value = expr(builtin_args.front(), d, locals, types);
        const string error_type = numeric_builtin_error_type(builtin);
        const string invalid = numeric_name == "to_int" ? "NonFinite" : "Invalid";
        const string overflow = "Overflow";
        const string first_variant = phase21_rust_variant(
            RaisedIdentity::enum_variant(error_type, invalid));
        const string second_variant = phase21_rust_variant(
            RaisedIdentity::enum_variant(error_type, overflow));
        string call = "__moss_" + string(numeric_name) + "(" + value +
            (numeric_name == "to_int" ? "" : ".as_str()") + ")";
        string mapping = call + ".map_err(|__moss_numeric_error| "
            "if __moss_numeric_error == 0 { __MossRaised::" + first_variant +
            "(" + error_type + "::" + invalid + ") } else { __MossRaised::" +
            second_variant + "(" + error_type + "::" + overflow + ") })";
        if (numeric_name == "parse_float")
          mapping = call + ".map_err(|_| __MossRaised::" +
              first_variant + "(" + error_type + "::" + invalid + "))";
        return phase21_propagate(mapping);
      }
      if (builtin == "sum" && builtin_args.size() == 1) {
        auto argument_type = generated_expr_type(builtin_args.front(), types);
        string element_type;
        if (argument_type && starts_with(*argument_type, "vector[") &&
            ends_with(*argument_type, "]"))
          element_type = canonical_type_name(argument_type->substr(
              7, argument_type->size() - 8));
        else if (argument_type && starts_with(*argument_type, "seq[") &&
            ends_with(*argument_type, "]"))
          element_type = canonical_type_name(argument_type->substr(
              4, argument_type->size() - 5));
        string result = expr(builtin_args.front(), d, locals, types) +
            ".iter().copied()";
        if (element_type == "int")
          return result +
              ".fold(0_i64, |__moss_sum, __moss_value| "
              "__moss_sum.wrapping_add(__moss_value))";
        if (!element_type.empty())
          return result + ".sum::<" + rust_type(element_type) + ">()";
        return result + ".sum()";
      }
      if (builtin == "Char" && builtin_args.size() == 1 && !functions_.count(builtin)) {
        return "__moss_char_from_codepoint(" + expr(builtin_args.front(), d, locals, types) + ")";
      }
      if ((builtin == "less" || builtin == "isequal") && builtin_args.size() == 2 &&
          !functions_.count(builtin))
        return scalar_protocol_rust(
            builtin, generated_expr_type(builtin_args[0], types).value_or(""),
            expr(builtin_args[0], d, locals, types),
            expr(builtin_args[1], d, locals, types));
      if (builtin == "hash" && builtin_args.size() == 1 && !functions_.count(builtin))
        return scalar_protocol_rust(
            "hash", generated_expr_type(builtin_args[0], types).value_or(""),
            expr(builtin_args.front(), d, locals, types));
      if (builtin == "sort" && !functions_.count(builtin) &&
          (builtin_args.size() == 1 || builtin_args.size() == 2))
        return sort_call_rust(builtin_args, d, locals, types);
    }

    // Named value-object construction. Moss prefers `=` here; `:` remains a
    // compatibility spelling for existing sources.
    string head;
    vector<string> parts;
    if (tree_parse_simple_call(e, head, parts)) {
      auto oit = objects_.find(head);
      if (oit != objects_.end()) {
        std::unordered_map<string,string> values;
        if (parts.size() == 1 && parts[0].empty()) parts.clear();
        for (const auto& part : parts) {
          string field_name, field_value;
          if (!tree_parse_named_argument(part, field_name, field_value))
            throw std::runtime_error("object constructor fields must be named");
          values[field_name] = field_value;
        }
        std::ostringstream r;
        r << head << " { ";
        bool first = true;
        for (const auto& f : oit->second->fields) {
          auto vit = values.find(f.name);
          if (vit == values.end()) throw std::runtime_error("missing object constructor field: " + head + "." + f.name);
          string v = expr(vit->second, d, locals, types);
          if (!first) r << ", ";
          first = false;
          r << rust_identifier(f.name) << ": " << v;
        }
        r << " }";
        return r.str();
      }
      vector<string> call_args;
      if (tree_parse_simple_call(e, head, call_args)) {
        head = resolved_callable_identity(head, types);
        const Function* function_target = nullptr;
        if (starts_with(head, "callable-v1:")) {
          function_target = find_function_by_callable_id(head);
        } else {
          auto it = functions_.find(head);
          if (it != functions_.end()) function_target = it->second;
        }
        bool known_function = (function_target != nullptr);
        if (known_function)
          return render_known_function_call(*function_target, function_target->name, call_args,
                                            d, locals, types);
        const Method* sibling_method = nullptr;
        if (d && objects_.count(d->name) && !known_function)
          sibling_method = checked_method_for_occurrence(
              head, active_native_est_cursor_);
        if (!known_function && !sibling_method && head == "take") {
          throw std::runtime_error(
              "MOSS_INTERNAL_INVARIANT unhandled take expression in native lowering");
        }
        vector<string> sibling_temporaries(call_args.size());
        if (sibling_method && sibling_method->receiver_effect != Effect::Read) {
          for (size_t i = 0; i < call_args.size(); ++i) {
            string nested_name; vector<string> nested_args;
            if (tree_parse_simple_call(call_args[i], nested_name, nested_args) &&
                objects_.count(d->name)) {
              auto nested_cursor = native_call_argument_cursor(
                  active_native_est_cursor_, i);
              const auto* nested_method = nested_cursor
                  ? checked_method_for_occurrence(nested_name, &*nested_cursor)
                  : nullptr;
              if (nested_method &&
                  nested_method->receiver_effect != Effect::Read)
                sibling_temporaries[i] = "__moss_call_argument_" +
                    std::to_string(call_argument_temp_++);
            }
          }
        }
        std::ostringstream r; if (d && objects_.count(d->name) && !known_function) r << "self.";
        r << viewed_function_name(head, call_args, d, locals, types) << "(";
        size_t emitted_arguments = 0;
        for (size_t i = 0; i < call_args.size(); ++i) {
          bool compile_time_callable = known_function &&
              is_compile_time_callable(generated_expr_type(call_args[i], types).value_or(""));
          if (compile_time_callable) continue;
          if (emitted_arguments++) r << ", ";
          if (sibling_method)
            r << method_call_argument(*sibling_method, i,
                sibling_temporaries[i].empty() ? call_args[i]
                                               : sibling_temporaries[i],
                d, locals, types);
          else
            r << expr(call_args[i], d, locals, types);
        }
        r << ")";
        if (sibling_method &&
            !sibling_method->observable_effects.raise_set.empty()) {
          if (!phase21_result_context_)
            throw std::runtime_error(
                "internal error: checked raising sibling method call lacks a Phase 21 outcome context");
          const string propagated = phase21_propagate(r.str());
          r.str("");
          r.clear();
          r << propagated;
        }
        if (std::any_of(sibling_temporaries.begin(), sibling_temporaries.end(),
                        [](const string& value) { return !value.empty(); })) {
          std::ostringstream sequenced;
          sequenced << "{ ";
          for (size_t i = 0; i < call_args.size(); ++i)
            if (!sibling_temporaries[i].empty())
              sequenced << "let " << sibling_temporaries[i] << " = "
                        << expr(call_args[i], d, locals, types) << "; ";
          sequenced << r.str() << " }";
          return sequenced.str();
        }
        return r.str();
      }
    }

    return rewrite_est_expression_source(e, d, locals, types);
  }


  static string fresh_generated_name(const string& base, std::set<string>& used) {
    string candidate = base;
    for (size_t suffix = 0; used.count(candidate); ++suffix)
      candidate = base + "_" + std::to_string(suffix);
    used.insert(candidate);
    return candidate;
  }

  // Message values have one Moss-level rule (an immutable value snapshot),
  // but two backend representations.  The owned form is retained at an ABI
  // boundary and for replies.  The borrowed forms are valid only while an
  // internal synchronous `_shared` call is active; Handler2PL retains every
  // guard needed by a state view until that nested call returns.
  enum class MessagePayloadLowering {
    CopyValue,
    BorrowOwned,
    BorrowView,
    MaterializeOwned,
  };

  bool borrowed_message_parameter(const string& expression) const {
    const auto binding = tree_identifier_name(expression);
    return binding && borrowed_message_parameters_.count(*binding);
  }

  bool borrowed_message_type(const string& type) const {
    const string concrete = canonical_type_name(type);
    return !concrete.empty() && !copy_type(concrete) && concrete != "_";
  }

  string shared_message_parameter(const Param& parameter) const {
    const string type = canonical_type_name(parameter.type);
    if (!borrowed_message_type(type))
      return parameter.name + ": " + rust_type(type);
    // A view is a static access capability. The internal ABI receives an
    // immutable reference whether this is an owned object, a temporary state
    // projection, or an already-borrowed incoming payload.
    if (view_object_type(type))
      return parameter.name + ": &impl " + access_trait(type);
    return parameter.name + ": &" + rust_type(type);
  }

  string shared_message_forward_argument(const Param& parameter,
                                         bool from_owned_boundary) const {
    if (!borrowed_message_type(parameter.type)) return parameter.name;
    // An exported/native boundary owns its Rust argument.  It deliberately
    // materializes that boundary, then lends it to the controlled internal
    // call.  `_shared` forwarding already receives the required borrowed or
    // access-view representation.
    return from_owned_boundary ? "&" + parameter.name : parameter.name;
  }

  MessagePayloadLowering message_payload_lowering(
      const string& expression, const string& type, const Domain* d,
      const std::set<string>& locals,
      const std::unordered_map<string,string>* types,
      bool owned_boundary) const {
    if (owned_boundary) return MessagePayloadLowering::MaterializeOwned;
    if (!borrowed_message_type(type)) return MessagePayloadLowering::CopyValue;
    if (is_object_view(expression, d, locals, types))
      return MessagePayloadLowering::BorrowView;
    return MessagePayloadLowering::BorrowOwned;
  }

  string message_arg(const string& e, const string& type, const Domain* d,
                     const std::set<string>& locals,
                     const std::unordered_map<string,string>* types = nullptr,
                     size_t functional_pipeline_id = 0,
                     const TypedReturnValueFactV1* return_fact = nullptr,
                     const TypedRootIngressPayloadFactV1* root_payload_fact = nullptr,
                     const TypedRootIngressArgumentDispositionFactV1*
                         argument_disposition_fact = nullptr) const {
    string r = nominal_domain_handle_argument(e, type, d, locals, types, functional_pipeline_id);
    string t = trim(type);
    if (copy_type(t)) return r;
    string callee;
    vector<string> arguments;
    if (tree_parse_simple_call(e, callee, arguments) && callee == "copy" &&
        arguments.size() == 1) {
      // expr() above required the exact checker TypedCopyFactV1 and emitted
      // the authorized independent owner. Transfer that fresh result into the
      // root message closure without cloning it again.
      return r;
    }
    if (return_fact && return_fact->is_authoritative() &&
        return_fact->action ==
            TypedReturnValueActionV1::TransferOwnedValue &&
        return_fact->required_access == Effect::Consume)
      return r;
    if (root_payload_fact && root_payload_fact->is_authoritative() &&
        root_payload_fact->action ==
            TypedRootIngressPayloadActionV1::TransferOwnedFreshRvalue &&
        !root_payload_fact->permits_moss_value_copy)
      return r;
    if (argument_disposition_fact &&
        argument_disposition_fact->is_authoritative() &&
        argument_disposition_fact->action ==
            TypedRootIngressArgumentDispositionFactV1::Action::TransferOwnedBinding)
      return r;
    throw std::runtime_error(
        "MOSS_INTERNAL_INVARIANT root message payload needs a checked copy(x) fact or exact consume authority");
  }

  string shared_message_arg(const string& e, const string& type,
                            const Domain* d, const std::set<string>& locals,
                            const std::unordered_map<string,string>* types = nullptr,
                            size_t functional_pipeline_id = 0) const {
    const auto lowering = message_payload_lowering(
        e, type, d, locals, types, false);
    string rendered = nominal_domain_handle_argument(
        e, type, d, locals, types, functional_pipeline_id);
    switch (lowering) {
      case MessagePayloadLowering::CopyValue:
        return rendered;
      case MessagePayloadLowering::BorrowOwned:
        // A temporary owner is naturally extended through this synchronous
        // call.  No clone implements the Moss snapshot here.
        return borrowed_message_parameter(e) ? trim(e) : "&(" + rendered + ")";
      case MessagePayloadLowering::BorrowView:
        // An incoming object payload is already `&impl MossAccess_T` and can
        // be forwarded directly. State projections are compact values holding
        // field borrows, so lend the temporary view for this nested call.
        if (borrowed_message_parameter(e)) return trim(e);
        if (auto place = view_place(e, d, locals, types)) return "&(" + *place + ")";
        return "&(" + rendered + ")";
      case MessagePayloadLowering::MaterializeOwned:
        break;
    }
    throw std::runtime_error("internal error: shared message selected owned boundary");
  }

  string value_from_borrowed_message_parameter(
      const string& source, const string& rendered,
      const std::unordered_map<string,string>* types) const {
    auto type = generated_expr_type(source, types);
    if (borrowed_message_parameter(source) && type &&
        !view_object_type(*type))
      return "*(" + rendered + ")";
    return rendered;
  }

  void gen_enum(std::ostringstream& o, const EnumType& declaration) {
    string enum_identity = "enum:" + declaration.name + "@" +
        std::to_string(declaration.line);
    tooling_begin(o, 0, "enum", enum_identity, declaration.name);
    source_comment(o, 0, declaration.line, declaration.header);
    o << "#[derive(Clone, Debug)]\n"
      << ((declaration.exported || p_.explicit_module) ? "pub " : "")
      << "enum " << declaration.name << " {\n";
    for (const auto& item : declaration.cases) {
      string case_identity = "enum_case:" + declaration.name + "." +
          item.name + "@" + std::to_string(item.line);
      tooling_begin(o, 4, "enum_case", case_identity,
                    declaration.name + "::" + rust_identifier(item.name));
      source_comment(o, 4, item.line, item.name);
      o << "    " << rust_identifier(item.name);
      if (!item.fields.empty()) {
        o << " { ";
        for (size_t index = 0; index < item.fields.size(); ++index) {
          if (index) o << ", ";
          o << rust_identifier(item.fields[index].name) << ": "
            << rust_type(item.fields[index].type);
        }
        o << " }";
      }
      o << ",\n";
      tooling_end(o, 4, case_identity);
    }
    o << "}\n\n";
    o << "impl std::fmt::Display for " << declaration.name << " {\n"
      << "    fn fmt(&self, formatter: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {\n"
      << "        match self {\n";
    for (const auto& item : declaration.cases) {
      o << "            " << declaration.name << "::"
        << rust_identifier(item.name);
      if (!item.fields.empty()) {
        o << " { ";
        for (size_t index = 0; index < item.fields.size(); ++index) {
          if (index) o << ", ";
          o << rust_identifier(item.fields[index].name);
        }
        o << " }";
      }
      o << " => write!(formatter, \"" << declaration.name << "."
        << item.name;
      if (!item.fields.empty()) {
        o << "(";
        for (size_t index = 0; index < item.fields.size(); ++index) {
          if (index) o << ", ";
          const string field_type = canonical_type_name(item.fields[index].type);
          const bool display_field = field_type == "int" ||
              field_type == "float" || field_type == "bool" ||
              field_type == "string" || enums_.count(field_type);
          o << item.fields[index].name
            << (display_field ? ": {}" : ": {:?}");
        }
        o << ")\"";
        for (const auto& field : item.fields)
          o << ", " << rust_identifier(field.name);
        o << "),\n";
      } else {
        o << "\"),\n";
      }
    }
    o << "        }\n"
      << "    }\n"
      << "}\n\n";
    tooling_end(o, 0, enum_identity);
  }

  void gen_object(std::ostringstream& o, const ObjectType& t) {
    bool is_generic = std::any_of(t.fields.begin(), t.fields.end(),
        [](const Field& f) { return f.inferred || f.type.empty(); });
    string type_identity = "type:" + t.name + "@" + std::to_string(t.line);
    tooling_begin(o, 0, "type", type_identity, t.name);
    source_comment(o, 0, t.line, t.header.empty() ? "type " + t.name : t.header);
    backend_comment(o, 0, "value representation for the Moss object; object ownership stays in Moss");

    string type_params;
    if (is_generic) {
      type_params += "<";
      size_t gi = 0;
      for (const auto& f : t.fields) {
        if (f.inferred || f.type.empty()) {
          if (gi > 0) type_params += ", ";
          type_params += "F" + std::to_string(gi);
          gi++;
        }
      }
      type_params += ">";
    }

    o << "#[derive(Clone, Debug)]\n"
      << ((t.exported || p_.explicit_module) ? "pub " : "") << "struct " << t.name << type_params << " {\n";
    size_t gi = 0;
    for (const auto& f : t.fields) {
      source_comment(o, 4, f.line, f.header.empty() ? f.name + ": " + f.type : f.header);
      string ftype;
      if (f.inferred || f.type.empty()) {
        ftype = "F" + std::to_string(gi++);
      } else {
        ftype = rust_type(f.type);
      }
      o << "    " << ((t.exported || p_.explicit_module) ? "pub " : "")
        << rust_identifier(f.name) << ": " << ftype << ",\n";
    }
    o << "}\n\n";
    tooling_end(o, 0, type_identity);
    // Native provider storage decomposition moves fields without exposing Moss
    // field access or requiring the consumer to read Rust-private fields.
    if (is_generic) {
      o << "impl" << type_params << " " << t.name << type_params << " {\n    "
        << ((t.exported || p_.explicit_module) ? "pub " : "") << "fn __moss_into_parts(self) -> (";
      gi = 0;
      for (const auto& field : t.fields) {
        if (field.inferred || field.type.empty()) o << "F" << std::to_string(gi++) << ",";
        else o << rust_type(field.type) << ",";
      }
      o << ") { (";
      for (const auto& field : t.fields) o << "self." << rust_identifier(field.name) << ",";
      o << ") }\n}\n";
    } else {
      o << "impl " << t.name << " {\n    " << ((t.exported || p_.explicit_module) ? "pub " : "") << "fn __moss_into_parts(self) -> (";
      for (const auto& field : t.fields) o << rust_type(field.type) << ",";
      o << ") { (";
      for (const auto& field : t.fields) o << "self." << rust_identifier(field.name) << ",";
      o << ") }\n}\n";
    }
    for (const auto& method : t.methods) for (bool view : {false, true}) {
      bool needs_view = false;
      for (size_t i = 0; i < method.params.size(); ++i)
        needs_view |= method_effect(method, i) != Effect::Consume && view_object_type(method.params[i].type);
      if (view && !needs_view) continue;
      const string method_name = (view ? "__moss_view_" : "") + method.name;
      string semantic_identity = method_semantic_identity(t, method);
      string generated_symbol = t.name + "::" + method_name;
      string native_symbol = tooling_native_symbol(
          "method", t.name + "__" + method.name, semantic_identity);
      tooling_begin(o, 0, "method", semantic_identity, generated_symbol,
                    view ? "" : native_symbol);
      o << "impl" << (is_generic ? type_params : "") << " " << t.name << (is_generic ? type_params : "") << " {\n";
      source_comment(o, 4, method.line,
                     "fn " + method.name);
      debug_symbol_attributes(o, 4, native_symbol, !view);
      const bool raises = !method.observable_effects.raise_set.empty();
      if (raises) o << "    #[allow(unreachable_code)]\n";
      o << "    " << ((t.exported || p_.explicit_module) ? "pub " : "")
        << "fn " << method_name;
      if (method.receiver_effect == Effect::Consume) o << "(self";
      else if (method.receiver_effect == Effect::Write) o << "(&mut self";
      else o << "(&self";
      for (const auto& p : method.params) {
        if (p.type.empty()) throw std::runtime_error("unresolved concrete method parameter type: " + method.name + "." + p.name);
        size_t parameter_index = static_cast<size_t>(&p - method.params.data());
        Effect effect = method_effect(method, parameter_index);
        string parameter_type = view && effect != Effect::Consume && view_object_type(p.type)
            ? "impl " + access_trait(p.type) : rust_type(p.type);
        if (is_generic && p.type == t.name) {
          parameter_type = t.name + type_params;
        }
        if (effect != Effect::Consume && (effect == Effect::Write || borrowable_type(p.type))) {
          o << ", " << (effect == Effect::Write ? "" : "") << p.name << ": "
            << (effect == Effect::Write ? "&mut " : "&") << parameter_type;
        } else {
          o << ", mut " << p.name << ": " << parameter_type;
        }
      }
      if (method.return_type) {
        o << ") -> ";
        if (raises) o << "Result<";
        string ret_type = rust_type(*method.return_type);
        if (is_generic && *method.return_type == t.name) {
          ret_type = t.name + type_params;
        }
        o << ret_type;
        if (raises) o << ", __MossRaised>";
        o << " {\n";
      } else {
        o << ")";
        if (raises) o << " -> Result<(), __MossRaised>";
        o << " {\n";
      }
      write_through_parameters_.clear(); view_parameters_.clear();
      for (size_t index = 0; index < method.params.size(); ++index) {
        if (view && method_effect(method, index) != Effect::Consume && view_object_type(method.params[index].type))
          view_parameters_.insert(method.params[index].name);
        else if (method_effect(method, index) == Effect::Write)
          write_through_parameters_.insert(method.params[index].name);
      }
      Domain receiver;
      receiver.name = t.name;
      receiver.state = t.fields;
      std::set<string> locals;
      std::unordered_map<string,string> types{{"self", t.name}};
      for (const auto& field : t.fields) types[field.name] = field.type;
      for (const auto& p : method.params) { locals.insert(p.name); types[p.name] = p.type; }
      const bool saved_phase21_result_context = phase21_result_context_;
      phase21_result_context_ = raises;
      gen_stmts(o, method.body, &receiver, nullptr, "", locals, types, 2,
                false, true, functional_method_context(t, method),
                checked_method_body_spec_id(method));
      if (method.result_expression) {
        const SpecId method_body_spec_id = checked_method_body_spec_id(method);
        auto plan = method.result_functional_pipeline_ids.find(
            method_body_spec_id);
        FunctionalExpressionHandlesScope result_handles_scope(
            active_functional_expression_handles_,
            &method.expression_handles);
        source_comment(o, 8,
                       method.result_line ? method.result_line : method.line,
                       *method.result_expression);
        o << "        ";
        if (raises) o << "Ok(";
        o << expr(*method.result_expression, &receiver, locals, &types,
                  plan == method.result_functional_pipeline_ids.end()
                      ? 0 : plan->second)
          ;
        if (raises) o << ")";
        o << "\n";
      } else if (raises && !method.return_type) {
        o << "        Ok(())\n";
      } else if (method.return_type &&
                 canonical_type_name(*method.return_type) != "unit") {
        // The checker requires every source path to return. Rust does not
        // infer that a checked try's Ok arm is unreachable after `return`.
        o << "        #[allow(unreachable_code)]\n"
          << "        { std::process::abort() }\n";
      }
      phase21_result_context_ = saved_phase21_result_context;
      o << "    }\n";
      o << "}\n\n";
      tooling_end(o, 0, semantic_identity);
      write_through_parameters_.clear(); view_parameters_.clear();
    }
  }

  void gen_function_instance(std::ostringstream& o, const Function& f,
                             const FunctionSpecialization* specialization, bool view = false) {
    struct ActiveSpecializationScope {
      string& slot;
      string prior;
      ActiveSpecializationScope(string& target,
                                const FunctionSpecialization* current)
          : slot(target), prior(target) {
        slot = current ? current->stable_type_key : string();
      }
      ~ActiveSpecializationScope() { slot = std::move(prior); }
    } active_specialization_scope(codegen_active_specialization_key_,
                                  specialization);
    borrowed_function_parameters_.clear();
    bool container_generic = !specialization &&
        std::any_of(f.params.begin(), f.params.end(), [](const Param& p) {
          return p.type == "vector" || p.type == "queue" || p.type == "map";
        });
    string semantic_identity = function_semantic_identity(f, specialization);
    string generated_symbol = specialization ? specialization->generated_name : f.name;
    if (view) generated_symbol = "__moss_view_" + generated_symbol;
    // A specialization can be projected into distinct provider/consumer rlibs.
    // Preserve native debug-map symbols, but scope them to the emitting module
    // artifact so those otherwise-identical semantic projections cannot collide
    // An open result stays a Rust type parameter even in a static
    // specialization (e.g. a consumer's `Provider.fresh()`): each Moss call
    // site binds it independently, exactly as for a source-backed `fresh()`.
    const string open_result_type = specialization
        ? specialization->return_type : f.return_type.value_or("unit");
    bool open_return = open_result_type == "vector" ||
        open_result_type == "queue" || open_result_type == "map";
    bool can_export = !view && !open_return &&
        (specialization || (!f.generic && !container_generic));
    string native_identity = semantic_identity;
    if (specialization && p_.explicit_module && !p_.module_name.empty())
      native_identity += "@artifact:" + p_.module_name;
    string native_symbol = tooling_native_symbol(
        "function", f.name, native_identity);
    tooling_begin(o, 0, "function", semantic_identity, generated_symbol,
                  can_export ? native_symbol : "");
    source_comment(o, 0, f.line, f.header.empty() ? "fn " + f.name : f.header);
    backend_comment(o, 0, specialization
        ? "STATIC specialization: concrete local function selected before Rust generation"
        : "LOCAL function: ordinary intra-domain call; no domain dispatch");
    debug_symbol_attributes(o, 0, native_symbol, can_export);
    const bool raises = !(specialization ? specialization->observable_effects
                                       : f.observable_effects).raise_set.empty();
    if (raises)
      o << "#[allow(unreachable_code)]\n";
    bool materialized_export = f.exported && !specialization &&
        !f.generic && !container_generic && !open_return;
    o << ((materialized_export || (specialization && public_specializations_)) ? "pub " : "")
      << "fn " << generated_symbol;
    std::unordered_map<string, size_t> view_type_counts;
    if (view) {
      for (size_t index = 0; index < f.params.size(); ++index) {
        string pt = specialization ? specialization->parameter_types[index]
                                   : f.params[index].type;
        auto abi = function_parameter_abi(f, index, pt, specialization);
        if (abi.effect != Effect::Consume && view_object_type(pt))
          view_type_counts[pt]++;
      }
    }
    bool has_shared_view_generics = false;
    for (const auto& [vt, count] : view_type_counts) {
      if (count > 1) has_shared_view_generics = true;
    }
    bool has_generics = open_return || (!specialization && (f.generic || container_generic)) || has_shared_view_generics;
    if (has_generics) {
      o << "<";
      size_t gi = 0;
      for (const auto& p : f.params)
        if (!specialization &&
            (constraint_ops(f, p.name).size() || p.type == "vector" || p.type == "queue")) {
          if (gi++) o << ", ";
          o << "T_" << p.name;
        }
      if (open_return) {
        if (gi++) o << ", ";
        if (*f.return_type == "map") o << "K, V";
        else o << "T";
      }
      if (has_shared_view_generics) {
        for (const auto& [vt, count] : view_type_counts) {
          if (count > 1) {
            if (gi++) o << ", ";
            o << "T_" << vt << ": " << access_trait(vt);
          }
        }
      }
      o << ">";
    }
    o << "(";
    size_t emitted_parameters = 0;
    for (size_t index = 0; index < f.params.size(); ++index) {
      string pt = specialization ? specialization->parameter_types[index]
                                 : f.params[index].type;
      // A callable parameter in a static higher-order specialization is a
      // compile-time identity, not a Rust function value.
      if (specialization && callable_identity(pt)) continue;
      if (emitted_parameters++) o << ", ";
      auto ops = constraint_ops(f, f.params[index].name);
      string parameter_rust_type;
      if (!specialization && pt.empty() && !ops.empty()) {
        if (ops.count("[]")) parameter_rust_type = "std::vec::Vec<T_" + f.params[index].name + ">";
        else parameter_rust_type = "T_" + f.params[index].name;
      } else if (!specialization && pt == "vector") {
        parameter_rust_type = "std::vec::Vec<T_" + f.params[index].name + ">";
      } else if (!specialization && pt == "queue") {
        parameter_rust_type = "std::collections::VecDeque<T_" + f.params[index].name + ">";
      } else if (!specialization && pt == "map") {
        parameter_rust_type = "std::collections::HashMap<K_" + f.params[index].name + ", V_" + f.params[index].name + ">";
      } else {
        parameter_rust_type = rust_type(pt);
      }
      auto abi = function_parameter_abi(f, index, pt, specialization);
      Effect effect = abi.effect;
      if (view && effect != Effect::Consume && view_object_type(pt)) {
        if (view_type_counts[pt] > 1) {
          o << f.params[index].name << ": " << (effect == Effect::Write ? "&mut " : "&") << "T_" << pt;
        } else {
          o << f.params[index].name << ": " << (effect == Effect::Write ? "&mut " : "&") << "impl " << access_trait(pt);
        }
        continue;
      }
      bool borrow = abi.passing != ParameterPassingMode::Value;
      if (borrow && !copy_type(pt))
        borrowed_function_parameters_.insert(f.params[index].name);
      bool mutates_owned_parameter = effect == Effect::Consume && !view &&
          (specialization ? index < specialization->parameter_mutations.size() &&
              specialization->parameter_mutations[index] :
              index < f.parameter_mutations.size() && f.parameter_mutations[index]);
      o << ((borrow && effect == Effect::Write) || mutates_owned_parameter ? "mut " : "")
        << f.params[index].name << ": "
        << (borrow ? (effect == Effect::Write ? "&mut " : "&") : "")
        << parameter_rust_type;
    }
    string return_type = specialization ? specialization->return_type
                                        : f.return_type.value_or("unit");
    if (return_type != "unit") {
      string rr = return_type;
      if (!specialization && open_generic(rr)) rr = "T_" + rr.substr(9);
      else if (!specialization && starts_with(rr, "_element:_element:")) rr = "T_" + rr.substr(17);
      else if (!specialization && starts_with(rr, "_element:element:")) rr = "T_" + rr.substr(17);
      o << ") -> ";
      if (raises) o << "Result<";
      o << (rr == return_type ? rust_type(rr) : rr);
      if (raises) o << ", __MossRaised>";
      if (!specialization && (f.generic || container_generic || open_return)) {
        std::ostringstream where_clause;
        size_t bi = 0;
        for (const auto& p : f.params) {
          auto ops = constraint_ops(f, p.name);
          if (ops.empty() && p.type != "vector" && p.type != "queue") continue;
          if (bi++) where_clause << ", ";
          where_clause << "T_" << p.name << ": ";
          bool idx = ops.count("[]");
          if (idx) where_clause << "Clone";
          else {
            string op = ops.count("+") ? "Add" : ops.count("*") ? "Mul" : ops.count("-") ? "Sub" : "Div";
            where_clause << "std::ops::" << op << "<Output = T_" << p.name << "> + Copy";
          }
        }
        if (bi > 0) o << " where " << where_clause.str();
      }
      o << " {\n";
    }
    else {
      o << ")";
      if (raises) o << " -> Result<(), __MossRaised>";
      o << " {\n";
    }
    std::set<string> locals;
    std::unordered_map<string,string> types;
    for (size_t index = 0; index < f.params.size(); ++index) {
      locals.insert(f.params[index].name);
      types[f.params[index].name] = specialization
          ? specialization->parameter_types[index] : f.params[index].type;
    }
    write_through_parameters_.clear(); view_parameters_.clear();
    for (size_t index = 0; index < f.params.size(); ++index) {
      if (view && function_effect(f, index, types.at(f.params[index].name), specialization) != Effect::Consume && view_object_type(types.at(f.params[index].name)))
        view_parameters_.insert(f.params[index].name);
      else if (function_effect(f, index, types.at(f.params[index].name), specialization) == Effect::Write)
        write_through_parameters_.insert(f.params[index].name);
    }
    const bool saved_phase21_result_context = phase21_result_context_;
    phase21_result_context_ = raises;
    gen_stmts(o, f.body, nullptr, nullptr, "", locals, types, 1, false,
              true, "",
              specialization ? specialization->spec_id : f.spec_id);
    if (f.result_expression) {
      struct ResultCodegenScope {
        const FunctionSpecialization*& specialization_slot;
        string& expression_slot;
        const FunctionSpecialization* prior_specialization;
        string prior_expression;
        ResultCodegenScope(const FunctionSpecialization*& selected,
                           string& source, const FunctionSpecialization* current,
                           const string& expression)
            : specialization_slot(selected), expression_slot(source),
              prior_specialization(selected), prior_expression(source) {
          specialization_slot = current;
          expression_slot = expression;
        }
        ~ResultCodegenScope() {
          specialization_slot = prior_specialization;
          expression_slot = std::move(prior_expression);
        }
      } result_scope(codegen_result_specialization_,
                     codegen_result_expression_, specialization,
                     *f.result_expression);
      FunctionalExpressionHandlesScope result_handles_scope(
          active_functional_expression_handles_, &f.expression_handles);
      source_comment(o, 4, f.result_line, *f.result_expression);
      const SpecId result_spec_id = specialization
          ? specialization->spec_id : f.spec_id;
      auto plan = f.result_functional_pipeline_ids.find(result_spec_id);
      string rendered = expr(*f.result_expression, nullptr, locals, &types,
                            plan == f.result_functional_pipeline_ids.end()
                                ? 0 : plan->second);
      if (view_object_type(return_type)) {
        const auto expr_binding = tree_identifier_name(*f.result_expression);
        auto param_it = std::find_if(f.params.begin(), f.params.end(),
                                     [&](const Param& p) {
          return expr_binding && p.name == *expr_binding;
        });
        if (param_it != f.params.end()) {
          size_t p_idx = std::distance(f.params.begin(), param_it);
          if (function_effect(f, p_idx, types.at(f.params[p_idx].name), specialization) != Effect::Consume) {
            if (view) rendered = *expr_binding + "." + access_value_method(return_type) + "()";
            else rendered = "(" + rendered + ").clone()";
          }
        }
      }
      o << "    ";
      if (raises) o << "Ok(";
      o << rendered;
      if (raises) o << ")";
      o << "\n";
    } else if (raises && return_type == "unit") {
      o << "    Ok(())\n";
    } else if (return_type != "unit") {
      // The checker requires every source path to return. Keep a diverging
      // native fallback for a try whose normal outcome is unreachable.
      o << "    #[allow(unreachable_code)]\n"
        << "    { std::process::abort() }\n";
    }
    phase21_result_context_ = saved_phase21_result_context;
    o << "}\n\n";
    tooling_end(o, 0, semantic_identity);
    write_through_parameters_.clear(); view_parameters_.clear();
    borrowed_function_parameters_.clear();
    bool needs_view = false;
    for (size_t i = 0; i < f.params.size(); ++i)
      needs_view |= function_effect(f, i, types.at(f.params[i].name), specialization) != Effect::Consume &&
          view_object_type(specialization ? specialization->parameter_types[i] : f.params[i].type);
    if (!view && needs_view) gen_function_instance(o, f, specialization, true);
  }

  void gen_function(std::ostringstream& o, const Function& f) {
    if (f.static_dispatch) {
      if (!emit_static_specializations_) return;
      for (const auto& specialization : f.specializations)
        gen_function_instance(o, f, &specialization);
      return;
    }
    gen_function_instance(o, f, nullptr);
  }

  void gen_ref_decl(std::ostringstream& o, const Domain& d) {
    o << (d.exported ? "pub " : "") << "struct " << d.name << "RefData {\n"
      << "    state: std::sync::Arc<" << d.name << "Runtime>,\n";
    for (const auto& route : d.routes)
      o << "    " << route.name << ": " << rust_type(route.type) << ",\n";
    o << "}\n"
      << (d.exported ? "pub " : "") << "type " << d.name
      << "Ref = moss_rt::DomainHandle<" << d.name << "RefData>;\n\n";
  }

  static string reply_binding(const Domain& d, const Handler& h) {
    std::set<string> used;
    for (const auto& f : d.state) used.insert(f.name);
    for (const auto& p : h.params) used.insert(p.name);
    for (const auto& s : h.body) {
      if (s.kind == Stmt::Kind::Let || s.kind == Stmt::Kind::Var)
        used.insert(s.a);
    }
    string name = "__moss_reply";
    for (size_t suffix = 0; used.count(name); ++suffix)
      name = "__moss_reply_" + std::to_string(suffix);
    return name;
  }

  static bool handler_needs_label(const Handler& h) {
    return std::any_of(h.body.begin(), h.body.end(), [](const Stmt& s) {
      return s.kind == Stmt::Kind::Return || s.kind == Stmt::Kind::Reply;
    });
  }

  string handler_internal_return_type(const Handler& handler) const {
    string normal = handler.reply_type
        ? "Option<" + rust_type(*handler.reply_type) + ">"
        : "()";
    return handler.observable_effects.raise_set.empty()
        ? normal
        : "Result<" + normal + ", " +
            (handler_has_failure_captures(handler)
                ? handler_capture_carrier_type(handler)
                : "__MossRaised") + ">";
  }

  string handler_message_return_type(const Handler& handler) const {
    string normal = rust_type(handler.reply_type.value_or("unit"));
    return handler.observable_effects.raise_set.empty()
        ? normal
        : "Result<" + normal + ", __MossRaised>";
  }

#include "handler_lowering.inc"
#include "borrowed_views.inc"
#include "executor_invoke_codegen.inc"
#include "file_chunk_codegen.inc"

  static string nominal_handle_variant(const string& domain) {
    return "Specialized_" + stable_hash(domain).substr(0, 12);
  }

  void gen_nominal_handle(std::ostringstream& o, const Domain& source) {
    if (!has_domain_specializations(source.name)) return;
    vector<const Domain*> routes{&source};
    for (const auto& specialization : p_.domain_specializations) {
      if (specialization.source_domain != source.name) continue;
      auto generated = specialization_names_.find(
          specialization.source_domain + "\n" + specialization.instance);
      if (generated == specialization_names_.end()) continue;
      auto specialized = std::find_if(
          specialized_domains_.begin(), specialized_domains_.end(),
          [&](const Domain& domain) { return domain.name == generated->second; });
      if (specialized != specialized_domains_.end()) routes.push_back(&*specialized);
    }
    // The route list above is keyed by semantic specialization records. It
    // deliberately does not infer membership from generated Rust names: a
    // source domain may collide with a would-be specialized layout spelling.
    if (routes.size() < 2) return;

    const string handle = source.name + "Handle";
    const string route = handle + "Route";
    o << "// Moss backend: nominal " << source.name
      << " handle routes statically to each materialized instance\n";
    o << "enum " << route << " {\n";
    o << "    Base(" << source.name << "Ref),\n";
    for (size_t index = 1; index < routes.size(); ++index)
      o << "    " << nominal_handle_variant(routes[index]->name) << "("
        << routes[index]->name << "Ref),\n";
    o << "}\n";
    o << (source.exported ? "pub " : "") << "struct " << handle
      << "Data { route: " << route << " }\n"
      << (source.exported ? "pub " : "") << "type " << handle
      << " = moss_rt::DomainHandle<" << handle << "Data>;\n\n";
    const string handle_methods = handle + "GeneratedMethods";
    o << (source.exported ? "pub " : "") << "trait " << handle_methods << " {\n";
    for (const auto& handler : source.handlers) {
      for (const string& prefix : {string(""), string("__moss_message_")}) {
        o << "    fn " << prefix << handler.name
          << (prefix.empty() ? "_shared" : "") << "(&self";
        for (const auto& parameter : handler.params)
          o << ", " << shared_message_parameter(parameter);
        o << ") -> " << (prefix.empty()
            ? handler_internal_return_type(handler)
            : handler_message_return_type(handler)) << ";\n";
      }
    }
    o << "}\n";
    o << "impl From<" << source.name << "Ref> for " << handle << " {\n"
      << "    fn from(value: " << source.name << "Ref) -> Self { Self::new(" << handle
      << "Data { route: " << route << "::Base(value) }) }\n}\n";
    for (size_t index = 1; index < routes.size(); ++index) {
      o << "impl From<" << routes[index]->name << "Ref> for " << handle << " {\n"
        << "    fn from(value: " << routes[index]->name << "Ref) -> Self { Self::new(" << handle << "Data { route: "
        << route << "::" << nominal_handle_variant(routes[index]->name)
        << "(value) }) }\n}\n";
    }
    o << "\nimpl " << handle_methods << " for moss_rt::DomainHandle<"
      << handle << "Data> {\n";
    for (const auto& handler : source.handlers) {
      const Handler* route_handler = find_handler(*routes.front(), handler.name);
      if (!route_handler) continue;
      bool compatible = true;
      for (size_t index = 0; index < handler.params.size(); ++index) {
        string expected = rust_type(handler.params[index].type);
        for (size_t route_index = 1; route_index < routes.size(); ++route_index) {
          const Handler* candidate = find_handler(*routes[route_index], handler.name);
          if (!candidate || index >= candidate->params.size() ||
              rust_type(candidate->params[index].type) != expected)
            compatible = false;
        }
      }
      for (size_t route_index = 1; route_index < routes.size(); ++route_index) {
        const Handler* candidate = find_handler(*routes[route_index], handler.name);
        if (!candidate || candidate->reply_type.has_value() !=
                handler.reply_type.has_value() ||
            (candidate->reply_type && handler.reply_type &&
             rust_type(*candidate->reply_type) != rust_type(*handler.reply_type)))
          compatible = false;
      }
      if (!compatible) continue;
      if (source.exported) {
        o << "    fn __moss_message_" << handler.name << "(&self";
        string arguments;
        for (const auto& parameter : handler.params) {
          o << ", " << shared_message_parameter(parameter);
          if (!arguments.empty()) arguments += ", ";
          arguments += parameter.name;
        }
        o << ") -> " << handler_message_return_type(handler)
          << " {\n        match &self.route {\n";
        for (size_t index = 0; index < routes.size(); ++index)
          o << "            " << route << "::"
            << (index == 0 ? "Base" : nominal_handle_variant(routes[index]->name))
            << "(inner) => " << routes[index]->name << "RefExportedBridge::__moss_message_"
            << handler.name << "(inner" << (arguments.empty() ? "" : ", ")
            << arguments << "),\n";
        o << "        }\n    }\n";
      }

        o << "    fn " << handler.name << "_shared(&self";
        for (const auto& parameter : handler.params)
          o << ", " << shared_message_parameter(parameter);
        o << ") -> " << handler_internal_return_type(handler)
          << " {\n        match &self.route {\n";
        for (size_t index = 0; index < routes.size(); ++index) {
          o << "            " << route << "::" << (index == 0 ? "Base" : nominal_handle_variant(routes[index]->name))
            << "(inner) => " << routes[index]->name << handler.name
            << "SharedMethods::" << handler.name << "_shared(inner";
          if (!handler.params.empty()) o << ", ";
          for (const auto& parameter : handler.params) o << parameter.name << ", ";
          o << "),\n";
        }
        o << "        }\n    }\n";
          }
    o << "}\n\n";
  }

  // Expose a synchronous provider entry over its private state representation.
  // The target is fixed by the checked concrete route graph.
  void gen_exported_domain_bridge(std::ostringstream& out, const Domain& domain) {
    const string bridge_trait = domain.name + "RefExportedBridge";
    out << "pub trait " << bridge_trait << " {\n";
    for (const auto& handler : domain.handlers) {
      out << "    fn __moss_message_" << handler.name << "(&self";
      for (const auto& parameter : handler.params)
        out << ", " << shared_message_parameter(parameter);
      out << ") -> " << handler_message_return_type(handler) << ";\n";
    }
    out << "}\nimpl " << bridge_trait << " for moss_rt::DomainHandle<"
        << domain.name << "RefData> {\n";
    exported_domain_bridge_body_ = true;
    for (const auto& handler : domain.handlers) {
      const auto saved_borrowed_message_parameters = borrowed_message_parameters_;
      const auto saved_view_parameters = view_parameters_;
      std::set<string> locals{"self"};
      std::unordered_map<string,string> types{{"self", domain.name}};
      string call = "message self." + handler.name + "(";
      out << "    fn __moss_message_" << handler.name << "(&self";
      for (size_t index = 0; index < handler.params.size(); ++index) {
        const auto& parameter = handler.params[index];
        out << ", " << shared_message_parameter(parameter);
        locals.insert(parameter.name);
        types[parameter.name] = parameter.type;
        if (borrowed_message_type(parameter.type))
          borrowed_message_parameters_.insert(parameter.name);
        if (view_object_type(parameter.type))
          view_parameters_.insert(parameter.name);
        if (index) call += ", ";
        call += parameter.name;
      }
      call += ")";
      const bool saved_phase21_result_context = phase21_result_context_;
      phase21_result_context_ = !handler.observable_effects.raise_set.empty();
      out << ") -> " << handler_message_return_type(handler) << " {\n"
          << "        ";
      if (handler_has_failure_captures(handler)) {
        out << domain.name << handler.name << "SharedMethods::"
            << handler.name << "_shared(self";
        if (!handler.params.empty()) out << ", ";
        for (const auto& parameter : handler.params)
          out << parameter.name << ", ";
        out << ")";
        if (handler.reply_type)
          out << ".map(|value| value.unwrap_or_else(|| std::process::abort()))";
        if (auto module = phase21_external_module_for_symbol(domain.name))
          out << ".map_err(|carrier| __MossRaised::from("
              << phase21_external_raised_type(*module)
              << "::from(carrier)))";
        else
          out << ".map_err(__MossRaised::from)";
      } else {
        if (!handler.observable_effects.raise_set.empty()) out << "Ok(";
        out << expr(call, nullptr, locals, &types);
        if (!handler.observable_effects.raise_set.empty()) out << ")";
      }
      out << "\n    }\n";
      phase21_result_context_ = saved_phase21_result_context;
      borrowed_message_parameters_ = saved_borrowed_message_parameters;
      view_parameters_ = saved_view_parameters;
    }
    exported_domain_bridge_body_ = false;
    out << "}\n\n";
  }

  void gen_domain(std::ostringstream& o, const Domain& d) { gen_planned_domain(o, d); }

  static string rust_string_literal(const string& value) {
    return "\"" + debug_json_escape(value) + "\"";
  }

  static string test_or_benchmark_function_name(
      const string& prefix, const string& semantic_identity) {
    return "__moss_" + prefix + "_" +
        stable_hash(semantic_identity).substr(0, 12);
  }

  static void gen_protocol_hex_helper(std::ostringstream& o) {
    o << "fn __moss_protocol_hex(value: &str) -> String {\n";
    o << "    let mut output = String::with_capacity(value.len() * 2);\n";
    o << "    for byte in value.as_bytes() { use std::fmt::Write as _; let _ = write!(&mut output, \"{:02x}\", byte); }\n";
    o << "    output\n}\n\n";
  }

  void gen_test_harness(std::ostringstream& o) {
    gen_protocol_hex_helper(o);
    const RaiseSet raised = phase21_program_raise_set();
    const bool typed_outcomes = !raised.empty();
    for (const auto& test : p_.tests) {
      string function_name = test_or_benchmark_function_name(
          "test", test.semantic_identity);
      tooling_begin(o, 0, "test", test.semantic_identity, function_name);
      source_comment(o, 0, test.line, test.header);
      o << "#[inline(never)]\n#[allow(unreachable_code)]\nfn "
        << function_name << "()";
      if (typed_outcomes) o << " -> Result<(), __MossRaised>";
      o << " {\n";
      std::set<string> locals;
      std::unordered_map<string,string> types;
      const bool saved_result_context = phase21_result_context_;
      phase21_result_context_ = typed_outcomes;
      gen_stmts(o, test.body, nullptr, nullptr, "", locals, types, 1,
                false, false,
                "test:" + test.name, test.spec_id);
      phase21_result_context_ = saved_result_context;
      if (typed_outcomes) o << "    Ok(())\n";
      o << "}\n";
      tooling_end(o, 0, test.semantic_identity);
      o << "\n";
    }

    o << "fn main() {\n";
    o << "    let previous_hook = std::panic::take_hook();\n";
    o << "    std::panic::set_hook(Box::new(|_| {}));\n";
    o << "    let mut failures = 0_usize;\n";
    if (typed_outcomes) {
      o << "    fn __moss_test_error_identity(error: &__MossRaised) -> &'static str {\n"
        << "        match error {\n";
      for (const auto& identity : raised)
        o << "            __MossRaised::" << phase21_rust_variant(identity)
          << "(_) => " << rust_string_literal(identity.canonical()) << ",\n";
      o << "        }\n    }\n";
    }
    for (const auto& test : p_.tests) {
      string function_name = test_or_benchmark_function_name(
          "test", test.semantic_identity);
      o << "    let outcome = std::panic::catch_unwind(std::panic::AssertUnwindSafe(|| "
        << function_name << "()));\n";
      o << "    match outcome {\n";
      o << "        " << (typed_outcomes ? "Ok(Ok(()))" : "Ok(())")
        << " => println!(\"MOSS_TEST|PASS|{}|{}|\", "
        << "__moss_protocol_hex(" << rust_string_literal(test.semantic_identity)
        << "), __moss_protocol_hex(" << rust_string_literal(test.name)
        << ")),\n";
      if (typed_outcomes) {
        o << "        Ok(Err(error)) => {\n"
          << "            failures += 1;\n"
          << "            let detail = format!(\"unhandled typed raise {}\", __moss_test_error_identity(&error));\n"
          << "            println!(\"MOSS_TEST|FAIL|{}|{}|{}\", "
          << "__moss_protocol_hex(" << rust_string_literal(test.semantic_identity)
          << "), __moss_protocol_hex(" << rust_string_literal(test.name)
          << "), __moss_protocol_hex(&detail));\n"
          << "        }\n";
      }
      o << "        Err(payload) => {\n";
      o << "            failures += 1;\n";
      o << "            let detail: &str = if let Some(value) = payload.downcast_ref::<String>() { value.as_str() } else if let Some(value) = payload.downcast_ref::<&str>() { value } else { \"test panicked without a Moss assertion message\" };\n";
      o << "            println!(\"MOSS_TEST|FAIL|{}|{}|{}\", "
        << "__moss_protocol_hex(" << rust_string_literal(test.semantic_identity)
        << "), __moss_protocol_hex(" << rust_string_literal(test.name)
        << "), __moss_protocol_hex(detail));\n";
      o << "        }\n";
      o << "    }\n";
    }
    o << "    std::panic::set_hook(previous_hook);\n";
    o << "    if failures != 0 { std::process::exit(1); }\n";
    o << "}\n";
  }

  void gen_benchmark_harness(std::ostringstream& o) {
    gen_protocol_hex_helper(o);
    const RaiseSet raised = phase21_program_raise_set();
    const bool typed_outcomes = !raised.empty();
    for (const auto& benchmark : p_.benchmarks) {
      string function_name = test_or_benchmark_function_name(
          "bench", benchmark.semantic_identity);
      tooling_begin(o, 0, "benchmark", benchmark.semantic_identity,
                    function_name);
      source_comment(o, 0, benchmark.line, benchmark.header);
      o << "#[inline(never)]\n#[allow(unreachable_code)]\nfn " << function_name << "()";
      if (typed_outcomes) o << " -> Result<(), __MossRaised>";
      o << " {\n";
      std::set<string> locals;
      std::unordered_map<string,string> types;
      bool previous_benchmark_body = benchmark_body_;
      const bool saved_result_context = phase21_result_context_;
      benchmark_body_ = true;
      phase21_result_context_ = typed_outcomes;
      gen_stmts(o, benchmark.body, nullptr, nullptr, "", locals, types, 1,
                false, false,
                "bench:" + benchmark.name, benchmark.spec_id);
      benchmark_body_ = previous_benchmark_body;
      phase21_result_context_ = saved_result_context;
      std::set<string> consumed_locals;
      for (const auto& statement : benchmark.body)
        consumed_locals.insert(statement.ownership_consumed_bindings.begin(),
                               statement.ownership_consumed_bindings.end());
      for (const auto& local : locals)
        if (!consumed_locals.count(local))
          o << "    std::hint::black_box(&" << local << ");\n";
      if (typed_outcomes) o << "    Ok(())\n";
      o << "}\n";
      tooling_end(o, 0, benchmark.semantic_identity);
      o << "\n";
    }

    o << "fn main() {\n";
    o << "    const WARMUP: usize = 5;\n";
    o << "    const SAMPLES: usize = 31;\n";
    o << "    const ITERATIONS: usize = 1000;\n";
    o << "    let previous_hook = std::panic::take_hook();\n";
    o << "    std::panic::set_hook(Box::new(|_| {}));\n";
    for (const auto& benchmark : p_.benchmarks) {
      string function_name = test_or_benchmark_function_name(
          "bench", benchmark.semantic_identity);
      o << "    for _ in 0..WARMUP { for _ in 0..ITERATIONS { let _ = std::hint::black_box("
        << function_name << "()); } }\n";
      o << "    let mut samples: std::vec::Vec<u128> = std::vec::Vec::with_capacity(SAMPLES);\n";
      o << "    for _ in 0..SAMPLES {\n";
      o << "        let started = std::time::Instant::now();\n";
      o << "        for _ in 0..ITERATIONS { let _ = std::hint::black_box("
        << function_name << "()); }\n";
      o << "        let elapsed = started.elapsed().as_nanos();\n";
      o << "        samples.push((elapsed + ITERATIONS as u128 - 1) / ITERATIONS as u128);\n";
      o << "    }\n";
      o << "    samples.sort_unstable();\n";
      o << "    let p25 = samples[SAMPLES / 4];\n";
      o << "    let median = samples[SAMPLES / 2];\n";
      o << "    let p75 = samples[(SAMPLES * 3) / 4];\n";
      o << "    let sample_text = samples.iter().map(|value| value.to_string()).collect::<std::vec::Vec<_>>().join(\",\");\n";
      o << "    println!(\"MOSS_BENCH|{}|{}|{}|{}|{}|{}|{}|{}|{}\", "
        << "__moss_protocol_hex(" << rust_string_literal(benchmark.semantic_identity)
        << "), __moss_protocol_hex(" << rust_string_literal(benchmark.name)
        << "), median, p25, p75, SAMPLES, WARMUP, ITERATIONS, sample_text);\n";
    }
    o << "    std::panic::set_hook(previous_hook);\n";
    o << "}\n";
  }

  void gen_main(std::ostringstream& o, const MainProc& m) {
    string semantic_identity = "main@" + std::to_string(m.line);
    tooling_begin(o, 0, "main", semantic_identity, "main",
                  "moss__main");
    source_comment(o, 0, m.line, m.header.empty() ? "proc main()" : m.header);
    backend_comment(o, 0, "main entry point; domain calls below retain their statically selected lowering");
    // An explicit module may also be compiled as a provider rlib. Its main
    // belongs to the executable build only: even a local rlib main body may
    // reference process-root FileIO and executor items absent from the rlib.
    if (project_module_artifact_) {
      o << "#[cfg(moss_process_root)]\n";
      if (debug_build_) o << "#[inline(never)]\n";
      o << "#[cfg_attr(moss_process_root, export_name = \"moss__main\")]\n";
    } else {
      debug_symbol_attributes(o, 0, "moss__main");
    }
    o << "fn main() {\n";
    if (program_uses_fileio(p_))
      o << "    moss_set_solo_hooks(executor_fileio_solo_enter_callback(), "
           "executor_fileio_solo_leave_callback());\n";
    std::set<string> locals;
    std::unordered_map<string,string> types;
    // Domain construction is a structural composition prefix.  Emit that
    // prefix in the graph's deterministic topological order so a route may
    // name a concrete instance whose source binding appears later in main.
    // This keeps source order from becoming a hidden dependency while leaving
    // ordinary executable statements in their source order.
    vector<Stmt> constructions;
    vector<Stmt> executable;
    for (const auto& statement : m.body) {
      if (domain_construction(statement.b)) constructions.push_back(statement);
      else executable.push_back(statement);
    }
    std::unordered_map<string,int> domain_ranks;
    std::unordered_map<string,string> domain_identities;
    for (const auto& instance : p_.concrete_domain_graph.instances) {
      domain_ranks[instance.binding] = instance.domain_rank;
      domain_identities[instance.binding] = instance.identity;
    }
    std::stable_sort(constructions.begin(), constructions.end(),
                     [&](const Stmt& left, const Stmt& right) {
                       // A route target must exist before its owner can be
                       // constructed; domain_rank itself points from owner
                       // to target, so construction runs in reverse rank.
                       return domain_ranks[left.a] > domain_ranks[right.a];
                     });
    domain_instance_bindings_.clear();
    ambiguous_domain_instance_bindings_.clear();
    const SpecId previous_main_spec_id = codegen_body_spec_id_;
    codegen_body_spec_id_ = m.spec_id;
    size_t construction_index = 0;
    gen_block(o, constructions, construction_index, 0, nullptr, nullptr, "", locals, types, 1,
              false, false, {}, "main");
    size_t index = 0;
    gen_block(o, executable, index, 0, nullptr, nullptr, "", locals, types, 1,
              false, false, {}, "main");
    codegen_body_spec_id_ = previous_main_spec_id;
    o << "}\n";
    tooling_end(o, 0, semantic_identity);
  }

  size_t statement_functional_pipeline_id(
      const Stmt& statement, const string& diagnostic_context,
      size_t slot) const {
    (void)diagnostic_context;
    auto plans = statement.functional_pipeline_ids_by_spec_id.find(
        codegen_body_spec_id_);
    if (plans == statement.functional_pipeline_ids_by_spec_id.end() ||
        slot >= plans->second.size())
      return 0;
    return plans->second[slot];
  }

  const TypedLocalOwnershipFactV1* resolve_local_ownership_fact(
      const Stmt& s, const string& functional_context) const {
    (void)functional_context;
    const Program& checked_program = semantic_program_ ? *semantic_program_ : p_;
    if (!checked_program.typed_semantics) return nullptr;

    const est::ExprSlot slot =
        (s.kind == Stmt::Kind::Let || s.kind == Stmt::Kind::Var)
            ? est::ExprSlot::Initializer
            : est::ExprSlot::RhsValue;
    const est::Handle* matched_handle = nullptr;
    for (const auto& handle : s.expression_handles) {
      if (handle.slot == slot && handle.argument_index == 0) {
        matched_handle = &handle;
        break;
      }
    }
    if (!matched_handle) return nullptr;

    // Resolve requirement and validated fact through Team A's typed API:
    const TypedLocalOwnershipRequirementV1* requirement = nullptr;
    const TypedLocalOwnershipFactV1* found_fact = nullptr;
    try {
      requirement = checked_program.typed_semantics->find_local_ownership_requirement(
          matched_handle->arena_index, matched_handle->expr,
          codegen_body_spec_id_);
      if (!requirement) {
        return nullptr;
      }
      found_fact = &checked_program.typed_semantics->find_local_ownership_fact(*requirement);
    } catch (const std::exception& e) {
      throw std::runtime_error(
          "MOSS_INTERNAL_INVARIANT " + std::string(e.what()));
    }

    if (!found_fact) {
      throw std::runtime_error(
          "MOSS_INTERNAL_INVARIANT required local ownership fact record missing");
    }

    // Intentionally unsupported facts decline bounded inference;
    // preserve explicitly authorized legacy path within documented bounds.
    if (found_fact->is_intentionally_unsupported()) {
      return nullptr;
    }

    // Validate authoritative gate:
    if (!found_fact->is_authoritative()) {
      throw std::runtime_error(
          "MOSS_INTERNAL_INVARIANT local ownership fact unavailable availability=" +
          std::to_string(static_cast<int>(found_fact->availability)));
    }

    // Verify EST arena and facts:
    const est::Arena* arena = nullptr;
    if (found_fact->est_arena_index < checked_program.expression_arenas.size() &&
        checked_program.expression_arenas[found_fact->est_arena_index]) {
      arena = checked_program.expression_arenas[found_fact->est_arena_index].get();
    } else if (found_fact->est_arena_index == 0 && checked_program.expression_arena) {
      arena = checked_program.expression_arena.get();
    }
    if (!arena) {
      throw std::runtime_error("MOSS_INTERNAL_INVARIANT missing EST arena");
    }

    const auto* tree_facts = arena->facts(found_fact->expression_occurrence_id,
                                          found_fact->specialization_id);
    if (!tree_facts || !tree_facts->inferred_effect ||
        *tree_facts->inferred_effect != found_fact->access) {
      throw std::runtime_error(
          "MOSS_INTERNAL_INVARIANT contradictory EST ownership fact in arena");
    }

    return found_fact;
  }

  const TypedTryContinuationFactV1& resolve_try_continuation_fact(
      const Stmt& statement, const string& functional_context) const {
    (void)functional_context;  // Diagnostic label only.
    const Program& checked_program = semantic_program_ ? *semantic_program_ : p_;
    if (!checked_program.typed_semantics ||
        statement.checked_statement_occurrence_id == UINT32_MAX)
      throw std::runtime_error(
          "MOSS_INTERNAL_INVARIANT checked Try continuation identity unavailable");
    const TypedTryContinuationRequirementV1* requirement = nullptr;
    for (const auto& candidate : checked_program.typed_semantics
             ->required_try_continuation_occurrences) {
      if (candidate.statement_occurrence_id !=
              statement.checked_statement_occurrence_id ||
          candidate.specialization_id != codegen_body_spec_id_)
        continue;
      if (requirement)
        throw std::runtime_error(
            "MOSS_INTERNAL_INVARIANT duplicate required Try continuation identity");
      requirement = &candidate;
    }
    if (!requirement)
      throw std::runtime_error(
          "MOSS_INTERNAL_INVARIANT required Try continuation fact missing "
          "(statement=" +
          std::to_string(statement.checked_statement_occurrence_id) +
          ", SpecId=" + std::to_string(codegen_body_spec_id_) + ")");
    try {
      return checked_program.typed_semantics->find_try_continuation_fact(
          *requirement);
    } catch (const std::exception& error) {
      throw std::runtime_error(
          "MOSS_INTERNAL_INVARIANT " + string(error.what()));
    }
  }

  const TypedLocalTransferFactV1* resolve_local_transfer_fact(
      const Stmt& statement, const string& functional_context) const {
    (void)functional_context;
    if (statement.kind != Stmt::Kind::Assign ||
        !tree_plain_identifier(statement.a) ||
        !tree_plain_identifier(statement.b))
      return nullptr;
    const Program& checked_program = semantic_program_ ? *semantic_program_ : p_;
    if (!checked_program.typed_semantics) return nullptr;
    if (statement.checked_statement_occurrence_id == UINT32_MAX)
      throw std::runtime_error(
          "MOSS_INTERNAL_INVARIANT local transfer statement identity unavailable");

    const est::Handle* rhs_handle = nullptr;
    for (const auto& handle : statement.expression_handles) {
      if (handle.slot != est::ExprSlot::RhsValue || handle.argument_index != 0)
        continue;
      if (rhs_handle)
        throw std::runtime_error(
            "MOSS_INTERNAL_INVARIANT duplicate local transfer RHS handle");
      rhs_handle = &handle;
    }
    if (!rhs_handle)
      throw std::runtime_error(
          "MOSS_INTERNAL_INVARIANT local transfer RHS handle unavailable");
    if (rhs_handle->arena_index >= checked_program.expression_arenas.size() ||
        !checked_program.expression_arenas[rhs_handle->arena_index])
      throw std::runtime_error(
          "MOSS_INTERNAL_INVARIANT local transfer EST arena unavailable");
    auto& required = checked_program.typed_semantics->required_local_transfer_occurrences;
    auto same_query = [&](const TypedLocalTransferFactV1& candidate) {
      return candidate.est_arena_index == rhs_handle->arena_index &&
          candidate.rhs_expression_occurrence_id == rhs_handle->expr &&
          candidate.statement_occurrence_id ==
              statement.checked_statement_occurrence_id &&
          candidate.specialization_id == codegen_body_spec_id_ &&
          candidate.specialization_id == codegen_body_spec_id_;
    };
#ifdef MOSS_TEST_BUILD
    auto& facts = checked_program.typed_semantics->local_transfer_facts;
    const bool require_fact =
        std::getenv("MOSS_TEST_CONSUMER_REQUIRE_LOCAL_TRANSFER_FACT") != nullptr;
    if (std::getenv("MOSS_TEST_CONSUMER_DROP_LOCAL_TRANSFER_REQUIREMENT")) {
      auto found = std::find_if(required.begin(), required.end(), same_query);
      if (found != required.end()) required.erase(found);
    }
    if (std::getenv("MOSS_TEST_CONSUMER_DUPLICATE_LOCAL_TRANSFER_REQUIREMENT")) {
      auto found = std::find_if(required.begin(), required.end(), same_query);
      if (found != required.end()) {
        const auto duplicate = *found;
        required.push_back(duplicate);
      }
    }
    if (std::getenv("MOSS_TEST_CONSUMER_DROP_LOCAL_TRANSFER_FACT")) {
      auto found = std::find_if(facts.begin(), facts.end(), same_query);
      if (found != facts.end())
        found->availability = TypedLocalTransferFactV1::Availability::Contradictory;
    }
    if (std::getenv("MOSS_TEST_CONSUMER_PERTURB_LOCAL_TRANSFER_FACT")) {
      auto found = std::find_if(facts.begin(), facts.end(), same_query);
      if (found != facts.end()) found->source_dead_after_assignment = false;
    }
    if (std::getenv("MOSS_TEST_CONSUMER_DUPLICATE_LOCAL_TRANSFER_FACT")) {
      auto found = std::find_if(facts.begin(), facts.end(), same_query);
      if (found != facts.end()) {
        const auto duplicate = *found;
        facts.push_back(duplicate);
      }
    }
#else
    constexpr bool require_fact = false;
#endif
    const auto requirement_count = static_cast<size_t>(std::count_if(
        required.begin(), required.end(), same_query));
    if (!requirement_count) {
      if (require_fact)
        throw std::runtime_error(
            "MOSS_INTERNAL_INVARIANT required local transfer fact missing");
      return nullptr;
    }
    if (requirement_count != 1)
      throw std::runtime_error(
          "MOSS_INTERNAL_INVARIANT duplicate local transfer requirement");

    const TypedLocalTransferFactV1* fact = nullptr;
    try {
      fact = checked_program.typed_semantics->find_local_transfer_fact(
          rhs_handle->arena_index, rhs_handle->expr,
          statement.checked_statement_occurrence_id,
          codegen_body_spec_id_);
    } catch (const std::exception& error) {
      throw std::runtime_error(
          "MOSS_INTERNAL_INVARIANT local transfer fact " +
          std::string(error.what()));
    }
    if (!fact || !fact->is_authoritative() ||
        fact->action != TypedLocalTransferFactV1::Action::TransferOwnedValue ||
        fact->est_arena_index != rhs_handle->arena_index ||
        fact->rhs_expression_occurrence_id != rhs_handle->expr ||
        fact->statement_occurrence_id !=
            statement.checked_statement_occurrence_id ||
        fact->specialization_id != codegen_body_spec_id_ ||
        fact->specialization_id != codegen_body_spec_id_)
      throw std::runtime_error(
          "MOSS_INTERNAL_INVARIANT local transfer fact missing or contradictory");

    const auto& arena = *checked_program.expression_arenas[rhs_handle->arena_index];
    const auto span = arena.span(rhs_handle->expr);
    if (span.line != fact->rhs_occurrence_span.line ||
        span.column != fact->rhs_occurrence_span.column ||
        span.origin_id != fact->rhs_occurrence_span.origin_id ||
        span.source_begin != fact->rhs_occurrence_span.source_begin ||
        span.source_end != fact->rhs_occurrence_span.source_end)
      throw std::runtime_error(
          "MOSS_INTERNAL_INVARIANT local transfer RHS span mismatch");
    const auto* expression_facts =
        arena.facts(rhs_handle->expr, fact->specialization_id);
    if (!expression_facts || !expression_facts->type ||
        !fact->source_type->structurally_equal(*expression_facts->type) ||
        !fact->target_type->structurally_equal(*expression_facts->type))
      throw std::runtime_error(
          "MOSS_INTERNAL_INVARIANT local transfer type contradicts exact EST facts");
    const auto* ownership = resolve_local_ownership_fact(statement,
                                                          "");
    if (ownership &&
        (ownership->source_binding_identity != fact->source_binding_identity ||
         ownership->definition_identity != fact->target_definition_identity ||
         ownership->binding_identity != fact->target_binding_identity))
      throw std::runtime_error(
          "MOSS_INTERNAL_INVARIANT local transfer disagrees with local ownership fact");
    return fact;
  }

  const TypedCopyFactV1& resolve_copy_fact(
      std::string_view expression) const {
    const Program& checked_program = semantic_program_ ? *semantic_program_ : p_;
    if (root_ingress_copy_fact_) {
      if (!root_ingress_copy_fact_->is_authoritative())
        throw std::runtime_error(
            "MOSS_INTERNAL_INVARIANT Root-ingress explicit copy fact missing or contradictory");
      (void)expression;
      return *root_ingress_copy_fact_;
    }
    if (!checked_program.typed_semantics || !active_functional_expression_handles_)
      throw std::runtime_error(
          "MOSS_INTERNAL_INVARIANT explicit copy lacks checked EST context");
    const auto fingerprint = est::fingerprint(string(expression));
    const est::Handle* matched = nullptr;
    for (const auto& handle : *active_functional_expression_handles_) {
      if (handle.source_fingerprint != fingerprint ||
          handle.arena_index >= checked_program.expression_arenas.size() ||
          !checked_program.expression_arenas[handle.arena_index])
        continue;
      const auto span = checked_program.expression_arenas[handle.arena_index]->span(handle.expr);
      if (span.source_begin != 0 || span.source_end != expression.size())
        continue;
      if (matched)
        throw std::runtime_error(
            "MOSS_INTERNAL_INVARIANT ambiguous checked explicit copy occurrence");
      matched = &handle;
    }
    if (!matched)
      throw std::runtime_error(
          "MOSS_INTERNAL_INVARIANT explicit copy occurrence is not an exact checked EST handle");
    try {
      const auto& requirement = checked_program.typed_semantics->find_copy_requirement(
          matched->arena_index, matched->expr, codegen_body_spec_id_);
      const auto* fact = checked_program.typed_semantics->find_copy_fact(requirement);
      if (!fact)
        throw std::runtime_error("required checked copy fact missing");
      return *fact;
    } catch (const std::exception& error) {
      throw std::runtime_error(
          "MOSS_INTERNAL_INVARIANT " + string(error.what()));
    }
  }

  const TypedCopyFactV1& resolve_copy_fact(
      std::uint32_t arena_index, est::ExprId occurrence) const {
    const Program& checked_program = semantic_program_ ? *semantic_program_ : p_;
    if (!checked_program.typed_semantics)
      throw std::runtime_error(
          "MOSS_INTERNAL_INVARIANT explicit copy lacks checked semantics");
    try {
      const auto& requirement = checked_program.typed_semantics->find_copy_requirement(
          arena_index, occurrence, codegen_body_spec_id_);
      const auto* fact = checked_program.typed_semantics->find_copy_fact(requirement);
      if (!fact)
        throw std::runtime_error("required checked copy fact missing");
      return *fact;
    } catch (const std::exception& error) {
      throw std::runtime_error(
          "MOSS_INTERNAL_INVARIANT " + string(error.what()));
    }
  }

  std::pair<NativeCheckedCopyFactV1, string> native_copy_provenance_identity(
      const TypedCopyFactV1& fact) const {
    std::error_code error;
    const auto source_path = std::filesystem::absolute(
        std::filesystem::path(fact.provenance.source_file), error).lexically_normal();
    if (error)
      throw std::runtime_error(
          "MOSS_INTERNAL_INVARIANT cannot normalize explicit-copy source path");
    std::ifstream source(source_path, std::ios::binary);
    if (!source)
      throw std::runtime_error(
          "MOSS_INTERNAL_INVARIANT cannot read exact explicit-copy source bytes");
    const string source_bytes{std::istreambuf_iterator<char>(source),
                              std::istreambuf_iterator<char>()};
    NativeCheckedCopyFactV1 row = native_checked_copy_fact_from_typed(
        fact, source_bytes);
    const string identity = native_checked_copy_identity(row);
    return {std::move(row), identity};
  }

  string lower_fact_authorized_copy(
      const TypedCopyFactV1& fact, const string& source_expression,
      const string& source_type, const Domain* domain,
      const std::set<string>& locals,
      const std::unordered_map<string, string>* types) const {
    auto [row, identity] = native_copy_provenance_identity(fact);
    (void)row;
    if (identity.rfind("typed-copy-fact-v1:", 0) != 0 || identity.size() != 83)
      throw std::runtime_error(
          "MOSS_INTERNAL_INVARIANT invalid explicit-copy fact identity");
    const string digest = identity.substr(19);
    const string prefix = digest.substr(0, 16);
    auto prior = generated_copy_helper_fact_ids_.find(prefix);
    if (prior != generated_copy_helper_fact_ids_.end() && prior->second != identity)
      throw std::runtime_error(
          "MOSS_INTERNAL_INVARIANT explicit-copy helper identity prefix collision");
    generated_copy_helper_fact_ids_[prefix] = identity;
    const string helper = "__moss_copy_value_" + prefix;
    const string concrete = canonical_type_name(source_type);
    const string result_type = rust_type(source_type);
    const bool view_copy = is_object_view(source_expression, domain, locals, types);
    std::ostringstream definition;
    if (view_copy) {
      auto object = objects_.find(concrete);
      const bool generic_object = object != objects_.end() &&
          std::any_of(object->second->fields.begin(), object->second->fields.end(),
              [](const Field& field) { return field.inferred || field.type.empty(); });
      if (object == objects_.end() || !view_object_type(concrete) || generic_object)
        throw std::runtime_error(
            "MOSS_INTERNAL_INVARIANT per-fact view copy helper lacks a concrete object layout");
      std::function<string(const string&, const string&)> build =
          [&](const string& type, const string& view) -> string {
        const string canonical = canonical_type_name(type);
        auto found = objects_.find(canonical);
        const bool generic = found != objects_.end() &&
            std::any_of(found->second->fields.begin(), found->second->fields.end(),
                [](const Field& field) { return field.inferred || field.type.empty(); });
        if (found == objects_.end() || !view_object_type(canonical) ||
            generic)
          throw std::runtime_error(
              "MOSS_INTERNAL_INVARIANT nested per-fact copy layout is not closed");
        std::ostringstream value;
        value << rust_type(canonical) << " { ";
        for (const auto& field : found->second->fields) {
          const string field_view = view + ".__moss_field_" +
              rust_identifier(field.name) + "()";
          value << rust_identifier(field.name) << ": ";
          if (view_object_type(field.type))
            value << build(field.type, field_view);
          else
            value << field_view << ".clone()";
          value << ", ";
        }
        value << "}";
        return value.str();
      };
      definition << "fn " << helper << "(value: &impl "
          << access_trait(concrete) << ") -> " << result_type << " { "
          << build(concrete, "value") << " } ";
    } else {
      definition << "fn " << helper << "(value: &" << result_type << ") -> "
          << result_type << " { (*value).clone() } ";
    }
    const string source_place = read_call_place(
        source_expression, domain, locals, types);
    return "{ " + definition.str() + "/* Moss copy fact call: " +
        identity + " */ " + helper + "(&(" +
        source_place + ")) }";
  }

  std::vector<const TypedLoopOwnershipFactV1*> resolve_loop_ownership_facts(
      const Stmt& s, const string& functional_context) const {
    (void)functional_context;
    if (!p_.typed_semantics) return {};

    const est::ExprSlot wanted = (s.kind == Stmt::Kind::For)
        ? est::ExprSlot::Iterable : est::ExprSlot::Condition;
    const est::Handle* matched_handle = nullptr;
    for (const auto& handle : s.expression_handles) {
      if (handle.slot == wanted && handle.argument_index == 0) {
        matched_handle = &handle;
        break;
      }
    }
    if (!matched_handle) return {};

    std::vector<const TypedLoopOwnershipFactV1*> facts;
    for (const auto& req : p_.typed_semantics->required_loop_ownership_occurrences) {
      if (req.loop_est_arena_index == matched_handle->arena_index &&
          req.loop_expression_occurrence_id == matched_handle->expr &&
          req.specialization_id == codegen_body_spec_id_ &&
          req.specialization_id == codegen_body_spec_id_) {
        if (req.availability != TypedLoopOwnershipFactV1::Availability::Present)
          continue;
        try {
          const auto* fact = p_.typed_semantics->find_loop_ownership_fact(req);
          if (!fact) {
            throw std::runtime_error("MOSS_INTERNAL_INVARIANT required loop ownership fact missing");
          }
          facts.push_back(fact);
        } catch (const std::exception& e) {
          throw std::runtime_error("MOSS_INTERNAL_INVARIANT " + std::string(e.what()));
        }
      }
    }
    for (const auto& fact : p_.typed_semantics->loop_ownership_facts) {
      if (fact.loop_est_arena_index == matched_handle->arena_index &&
          fact.loop_expression_occurrence_id == matched_handle->expr &&
          fact.specialization_id == codegen_body_spec_id_ &&
          fact.specialization_id == codegen_body_spec_id_) {
        if (fact.availability != TypedLoopOwnershipFactV1::Availability::Present)
          continue;
        bool found_req = false;
        for (const auto& req : p_.typed_semantics->required_loop_ownership_occurrences) {
          if (req.loop_est_arena_index == fact.loop_est_arena_index &&
              req.loop_expression_occurrence_id == fact.loop_expression_occurrence_id &&
              req.specialization_id == fact.specialization_id &&
              req.specialization_id == fact.specialization_id &&
              req.definition_identity == fact.definition_identity &&
              req.edge_kind == fact.edge_kind) {
            found_req = true;
            break;
          }
        }
        if (!found_req) {
          throw std::runtime_error("MOSS_INTERNAL_INVARIANT loop ownership requirement missing for fact");
        }
      }
    }
    return facts;
  }

  std::vector<const TypedExceptionalOwnershipFactV1*> resolve_exceptional_ownership_facts(
      const Stmt& s, const string& functional_context) const {
    (void)functional_context;
    if (!p_.typed_semantics) return {};
    (void)s;

    std::vector<const TypedExceptionalOwnershipFactV1*> facts;
    for (const auto& req : p_.typed_semantics->required_exceptional_ownership_occurrences) {
      if (req.specialization_id == codegen_body_spec_id_ &&
          req.specialization_id == codegen_body_spec_id_) {
        if (req.availability != TypedExceptionalOwnershipFactV1::Availability::Present)
          continue;
        try {
          const auto& fact = p_.typed_semantics->find_exceptional_ownership_fact(req);
          facts.push_back(&fact);
        } catch (const std::exception& e) {
          throw std::runtime_error("MOSS_INTERNAL_INVARIANT " + std::string(e.what()));
        }
      }
    }
    for (const auto& fact : p_.typed_semantics->exceptional_ownership_facts) {
      if (fact.specialization_id == codegen_body_spec_id_ &&
          fact.specialization_id == codegen_body_spec_id_) {
        if (fact.availability != TypedExceptionalOwnershipFactV1::Availability::Present)
          continue;
        bool found_req = false;
        for (const auto& req : p_.typed_semantics->required_exceptional_ownership_occurrences) {
          if (req.source_statement_occurrence_id == fact.source_statement_occurrence_id &&
              req.specialization_id == fact.specialization_id &&
              req.specialization_id == fact.specialization_id &&
              req.recovery_statement_occurrence_id == fact.recovery_statement_occurrence_id &&
              req.raised_variant_identity == fact.raised_variant_identity) {
            found_req = true;
            break;
          }
        }
        if (!found_req) {
          throw std::runtime_error("MOSS_INTERNAL_INVARIANT exceptional ownership requirement missing for fact");
        }
      }
    }
    return facts;
  }

  void require_bare_raise_transfer(
      const Stmt& raise, const Stmt& recovery,
      const TypedExceptionalOwnershipFactV1& source_edge,
      const RaisedIdentity& identity,
      const string& functional_context) const {
    (void)functional_context;
    if (!p_.typed_semantics ||
        raise.checked_statement_occurrence_id == UINT32_MAX ||
        recovery.checked_statement_occurrence_id == UINT32_MAX)
      throw std::runtime_error(
          "MOSS_INTERNAL_INVARIANT bare raise transfer identity unavailable");
    try {
      const auto& requirement =
          p_.typed_semantics->find_bare_raise_transfer_requirement(
              raise.checked_statement_occurrence_id,
              recovery.checked_statement_occurrence_id, source_edge,
              identity.type);
      if (requirement.specialization_id != codegen_body_spec_id_ ||
          requirement.raised_variant_identity != identity.canonical())
        throw std::invalid_argument(
            "bare raise transfer edge/context mismatch");
      (void)p_.typed_semantics->find_bare_raise_transfer_fact(requirement);
    } catch (const std::exception& e) {
      throw std::runtime_error("MOSS_INTERNAL_INVARIANT " + std::string(e.what()));
    }
  }

  const TypedTakeFactV1* resolve_take_fact(
      const Stmt& s, const string& functional_context,
      std::uint32_t* out_arena_index = nullptr,
      est::ExprId* out_expr_id = nullptr) const {
    (void)functional_context;
    if (!p_.typed_semantics) return nullptr;

    est::ExprSlot slot = est::ExprSlot::Expression;
    switch (s.kind) {
      case Stmt::Kind::Let:
      case Stmt::Kind::Var: slot = est::ExprSlot::Initializer; break;
      case Stmt::Kind::Assign: slot = est::ExprSlot::RhsValue; break;
      case Stmt::Kind::Return: slot = est::ExprSlot::ReturnValue; break;
      case Stmt::Kind::Reply: slot = est::ExprSlot::ReturnValue; break;
      case Stmt::Kind::Raise: slot = est::ExprSlot::RaisedValue; break;
      default: return nullptr;
    }

    const est::Handle* matched_handle = nullptr;
    for (const auto& handle : s.expression_handles) {
      if (handle.slot == slot && handle.argument_index == 0) {
        if (handle.arena_index < p_.expression_arenas.size() &&
            p_.expression_arenas[handle.arena_index]) {
          const auto& arena = *p_.expression_arenas[handle.arena_index];
          if (std::holds_alternative<est::Take>(arena.at(handle.expr))) {
            matched_handle = &handle;
            break;
          }
        }
      }
    }
    if (!matched_handle) return nullptr;

    if (out_arena_index) *out_arena_index = matched_handle->arena_index;
    if (out_expr_id) *out_expr_id = matched_handle->expr;

    const TypedTakeRequirementV1* requirement = nullptr;
    const TypedTakeFactV1* found_fact = nullptr;
    try {
      requirement = &p_.typed_semantics->find_take_requirement(
          matched_handle->arena_index, matched_handle->expr,
          codegen_body_spec_id_);
      found_fact = p_.typed_semantics->find_take_fact(*requirement);
    } catch (const std::exception& e) {
      throw std::runtime_error(
          "MOSS_INTERNAL_INVARIANT " + std::string(e.what()));
    }

    if (!requirement || !found_fact) {
      throw std::runtime_error(
          "MOSS_INTERNAL_INVARIANT required take fact record missing");
    }

    if (!found_fact->is_authoritative()) {
      throw std::runtime_error(
          "MOSS_INTERNAL_INVARIANT take fact unauthoritative availability=" +
          std::to_string(static_cast<int>(found_fact->availability)));
    }

    return found_fact;
  }

  const TypedMatchOwnershipFactV1* resolve_match_ownership_fact(
      const Stmt& s, const string& functional_context) const {
    const Program& checked_program = semantic_program_ ? *semantic_program_ : p_;
    if (!checked_program.typed_semantics)
      throw std::runtime_error(
          "MOSS_INTERNAL_INVARIANT typed match semantics are unavailable");
    const est::Handle* matched_handle = nullptr;
    for (const auto& handle : s.expression_handles) {
      if (handle.slot == est::ExprSlot::Scrutinee && handle.argument_index == 0) {
        matched_handle = &handle;
        break;
      }
    }
    if (!matched_handle)
      throw std::runtime_error(
          "MOSS_INTERNAL_INVARIANT checked match scrutinee occurrence is missing");
    (void)functional_context;  // Diagnostic label only.
    const TypedMatchOwnershipRequirementV1* requirement = nullptr;
#ifdef MOSS_TEST_BUILD
    const bool drop_match_requirement =
        std::getenv("MOSS_TEST_CONSUMER_DROP_MATCH_REQUIREMENT") != nullptr;
    const bool perturb_match_spec_id = std::getenv(
        "MOSS_TEST_CONSUMER_PERTURB_MATCH_SPEC_ID") != nullptr;
#else
    constexpr bool drop_match_requirement = false;
    constexpr bool perturb_match_spec_id = false;
#endif
    size_t requirement_count = 0;
    for (const auto& req : checked_program.typed_semantics->required_match_occurrences) {
      if (!drop_match_requirement &&
          req.est_arena_index == matched_handle->arena_index &&
          req.scrutinee_occurrence_id == matched_handle->expr &&
          req.specialization_id == (perturb_match_spec_id
              ? InvalidSpecId : codegen_body_spec_id_)) {
        requirement = &req;
        ++requirement_count;
      }
    }
    if (requirement_count != 1 || !requirement)
      throw std::runtime_error(
          "MOSS_INTERNAL_INVARIANT required checked match requirement missing or ambiguous");
#ifdef MOSS_TEST_BUILD
    if (std::getenv("MOSS_TEST_CONSUMER_PERTURB_MATCH_ARM_ACCESS")) {
      auto corrupted = *requirement;
      if (corrupted.arms.empty())
        throw std::runtime_error("MOSS_INTERNAL_INVARIANT cannot perturb empty checked match arms");
      auto& arm = corrupted.arms.front();
      arm.payload_access = arm.payload_access == Effect::Write
          ? Effect::Read : Effect::Write;
      try {
      (void)checked_program.typed_semantics->find_match_ownership_fact(corrupted);
      } catch (const std::exception& e) {
        throw std::runtime_error("MOSS_INTERNAL_INVARIANT " + std::string(e.what()));
      }
      throw std::runtime_error("MOSS_INTERNAL_INVARIANT perturbed match access was accepted");
    }
    if (std::getenv("MOSS_TEST_CONSUMER_DROP_MATCH_FACT")) {
      auto corrupted = *checked_program.typed_semantics;
      auto fact = std::find_if(corrupted.match_ownership_facts.begin(),
          corrupted.match_ownership_facts.end(), [&](const auto& candidate) {
            return candidate.est_arena_index == requirement->est_arena_index &&
                candidate.scrutinee_occurrence_id == requirement->scrutinee_occurrence_id &&
                candidate.statement_occurrence_id == requirement->statement_occurrence_id &&
                candidate.specialization_id == requirement->specialization_id;
          });
      if (fact == corrupted.match_ownership_facts.end())
        throw std::runtime_error("MOSS_INTERNAL_INVARIANT cannot drop absent checked match fact");
      corrupted.match_ownership_facts.erase(fact);
      try {
        (void)corrupted.find_match_ownership_fact(*requirement);
      } catch (const std::exception& e) {
        throw std::runtime_error("MOSS_INTERNAL_INVARIANT " + std::string(e.what()));
      }
      throw std::runtime_error("MOSS_INTERNAL_INVARIANT missing checked match fact was accepted");
    }
#endif
    try {
      return checked_program.typed_semantics->find_match_ownership_fact(*requirement);
    } catch (const std::exception& e) {
      throw std::runtime_error("MOSS_INTERNAL_INVARIANT " + std::string(e.what()));
    }
  }

  const TypedLocalOwnershipFactV1* resolve_vector_element_ownership_fact(
      const Stmt& s, const string& functional_context) const {
    (void)functional_context;
    if (!p_.typed_semantics) return nullptr;
    const est::Handle* matched_handle = nullptr;
    for (const auto& handle : s.expression_handles) {
      if (handle.slot == est::ExprSlot::Iterable && handle.argument_index == 0) {
        matched_handle = &handle;
        break;
      }
    }
    if (!matched_handle) return nullptr;
    try {
      const auto* requirement = p_.typed_semantics->find_local_ownership_requirement(
          matched_handle->arena_index, matched_handle->expr,
          codegen_body_spec_id_);
      if (!requirement) return nullptr;
      const auto& fact = p_.typed_semantics->find_local_ownership_fact(*requirement);
      if (fact.source_kind != TypedLocalOwnershipFactV1::SourceKind::VectorElement)
        return nullptr;
      if (!fact.is_authoritative()) {
        throw std::runtime_error("MOSS_INTERNAL_INVARIANT unauthoritative vector element fact");
      }
      return &fact;
    } catch (const std::exception& e) {
      throw std::runtime_error("MOSS_INTERNAL_INVARIANT " + std::string(e.what()));
    }
  }

  struct SelectorNodeFacts {
    TypeRef type;
    SpecId callee_spec_id = InvalidSpecId;
    std::optional<Effect> inferred_effect;
  };

  std::optional<SelectorNodeFacts> lookup_selector_facts(
      const est::Arena& arena, est::ExprId id,
      SpecId specialization_id) const {
    if (specialization_id == InvalidSpecId) {
      throw std::runtime_error("MOSS_INTERNAL_INVARIANT selector fact lookup missing primary checker context");
    }
    const auto* facts = arena.facts(id, specialization_id);
    SelectorNodeFacts result;
    if (facts) {
      if (facts->type) result.type = facts->type;
      if (facts->inferred_effect) result.inferred_effect = facts->inferred_effect;
    }
    if (std::holds_alternative<est::Call>(arena.at(id))) {
      const Program& checked_program = semantic_program_ ? *semantic_program_ : p_;
      std::uint32_t arena_index = UINT32_MAX;
      for (std::uint32_t index = 0;
           index < checked_program.expression_arenas.size(); ++index)
        if (checked_program.expression_arenas[index].get() == &arena) {
          if (arena_index != UINT32_MAX)
            throw std::runtime_error(
                "MOSS_INTERNAL_INVARIANT selector arena identity is ambiguous");
          arena_index = index;
        }
      if (arena_index == UINT32_MAX)
        throw std::runtime_error(
            "MOSS_INTERNAL_INVARIANT selector Call arena is not checker-owned");
      const auto lookup = select_exact_semantic_call_edge(
          checked_program, specialization_id, arena_index, id);
      if (lookup.kind != ExactSemanticCallEdgeLookup::Kind::Unique ||
          !lookup.edge ||
          lookup.edge->target_kind != SemanticCallTargetKind::Static ||
          lookup.edge->callee_spec_id == InvalidSpecId ||
          lookup.edge->callee_spec_id >=
              checked_program.specialization_table.size())
        throw std::runtime_error(
            "MOSS_INTERNAL_INVARIANT selector exact checked call edge is missing or contradictory");
      result.callee_spec_id = lookup.edge->callee_spec_id;
    }
    return result;
  }

  string lower_est_selector(
      const est::Arena& arena,
      est::ExprId node_id,
      const Domain* d,
      const std::set<string>& locals,
      const std::unordered_map<string,string>* types,
      SpecId specialization_id) const {
    const auto facts = lookup_selector_facts(arena, node_id, specialization_id);
    const auto& node = arena.at(node_id);
    return std::visit([&](const auto& value) -> string {
      using T = std::decay_t<decltype(value)>;
      if constexpr (std::is_same_v<T, est::Identifier>) {
        if (value.name == "true" || value.name == "false") return value.name;
        if (view_parameters_.count(value.name)) {
          if (!types || !types->count(value.name))
            throw std::runtime_error("MOSS_INTERNAL_INVARIANT access view lacks checked type");
          return value.name + "." + access_value_method(types->at(value.name)) + "()";
        }
        if (borrowed_function_parameters_.count(value.name)) {
          return "(*" + value.name + ")";
        }
        bool is_state_field = d && std::any_of(d->state.begin(), d->state.end(),
            [&](const Field& f) { return f.name == value.name; });
        if (d && (value.name == "self" || (!locals.count(value.name) && is_state_field))) {
          return "self." + rust_identifier(value.name);
        }
        return rust_identifier(value.name);
      } else if constexpr (std::is_same_v<T, est::Literal>) {
        if (!facts || !facts->type) {
          throw std::runtime_error("MOSS_INTERNAL_INVARIANT selector literal missing checker-issued type facts");
        }
        if (facts->type->kind == TypeKind::Primitive && facts->type->name == "string") {
          return "String::from(" + value.spelling + ")";
        }
        if (facts->type->kind == TypeKind::Primitive && facts->type->name == "int") {
          if (value.spelling.find('_') != string::npos) return value.spelling;
          return value.spelling + "_i64";
        }
        return value.spelling;
      } else if constexpr (std::is_same_v<T, est::Parenthesized>) {
        return "(" + lower_est_selector(arena, value.inner, d, locals, types, specialization_id) + ")";
      } else if constexpr (std::is_same_v<T, est::Unary>) {
        const auto operand_facts = lookup_selector_facts(arena, value.operand, specialization_id);
        if (!facts || !facts->type || !operand_facts || !operand_facts->type) {
          throw std::runtime_error("MOSS_INTERNAL_INVARIANT selector unary operator missing checker-issued type facts");
        }
        string operand = lower_est_selector(arena, value.operand, d, locals, types, specialization_id);
        if (value.op == "-") {
          if (operand_facts->type->kind == TypeKind::Primitive && operand_facts->type->name == "int") {
            return "(" + operand + ").wrapping_neg()";
          }
          if (operand_facts->type->kind == TypeKind::Primitive && operand_facts->type->name == "float") {
            return "-(" + operand + ")";
          }
        } else if (value.op == "!" || value.op == "not") {
          if (operand_facts->type->kind == TypeKind::Primitive && operand_facts->type->name == "bool") {
            return "!(" + operand + ")";
          }
        }
        throw std::runtime_error("MOSS_INTERNAL_INVARIANT unsupported selector unary operator '" + value.op +
                                 "' for type " + operand_facts->type->to_canonical_string());
      } else if constexpr (std::is_same_v<T, est::Binary>) {
        const auto left_facts = lookup_selector_facts(arena, value.left, specialization_id);
        const auto right_facts = lookup_selector_facts(arena, value.right, specialization_id);
        if (!facts || !facts->type || !left_facts || !left_facts->type || !right_facts || !right_facts->type) {
          throw std::runtime_error("MOSS_INTERNAL_INVARIANT selector binary operator missing checker-issued type facts");
        }
        string left = lower_est_selector(arena, value.left, d, locals, types, specialization_id);
        string right = lower_est_selector(arena, value.right, d, locals, types, specialization_id);

        // Respect checker-issued operator and operand types:
        if (left_facts->type->kind == TypeKind::Primitive && left_facts->type->name == "int") {
          if (value.op == "+") return "(" + left + ").wrapping_add(" + right + ")";
          if (value.op == "-") return "(" + left + ").wrapping_sub(" + right + ")";
          if (value.op == "*") return "(" + left + ").wrapping_mul(" + right + ")";
          if (value.op == "/") return "(" + left + ") / (" + right + ")";
          if (value.op == "%") return "(" + left + ") % (" + right + ")";
          if (value.op == "==") return "(" + left + ") == (" + right + ")";
          if (value.op == "!=") return "(" + left + ") != (" + right + ")";
          if (value.op == "<") return "(" + left + ") < (" + right + ")";
          if (value.op == "<=") return "(" + left + ") <= (" + right + ")";
          if (value.op == ">") return "(" + left + ") > (" + right + ")";
          if (value.op == ">=") return "(" + left + ") >= (" + right + ")";
        } else if (left_facts->type->kind == TypeKind::Primitive && left_facts->type->name == "float") {
          if (value.op == "+") return "(" + left + " + " + right + ")";
          if (value.op == "-") return "(" + left + " - " + right + ")";
          if (value.op == "*") return "(" + left + " * " + right + ")";
          if (value.op == "/") return "(" + left + " / " + right + ")";
          if (value.op == "==") return "(" + left + ") == (" + right + ")";
          if (value.op == "!=") return "(" + left + ") != (" + right + ")";
          if (value.op == "<") return "(" + left + ") < (" + right + ")";
          if (value.op == "<=") return "(" + left + ") <= (" + right + ")";
          if (value.op == ">") return "(" + left + ") > (" + right + ")";
          if (value.op == ">=") return "(" + left + ") >= (" + right + ")";
        } else if (left_facts->type->kind == TypeKind::Primitive && left_facts->type->name == "string") {
          if (value.op == "+") return "format!(\"{}{}\", " + left + ", " + right + ")";
          if (value.op == "==") return "(" + left + ") == (" + right + ")";
          if (value.op == "!=") return "(" + left + ") != (" + right + ")";
        } else if (left_facts->type->kind == TypeKind::Primitive && left_facts->type->name == "bool") {
          if (value.op == "and") return "(" + left + ") && (" + right + ")";
          if (value.op == "or") return "(" + left + ") || (" + right + ")";
          if (value.op == "==") return "(" + left + ") == (" + right + ")";
          if (value.op == "!=") return "(" + left + ") != (" + right + ")";
        }
        throw std::runtime_error("MOSS_INTERNAL_INVARIANT unsupported selector binary operator '" + value.op +
                                 "' for type " + left_facts->type->to_canonical_string());
      } else if constexpr (std::is_same_v<T, est::Member>) {
        string base = lower_est_selector(arena, value.base, d, locals, types, specialization_id);
        return base + "." + rust_identifier(value.name);
      } else if constexpr (std::is_same_v<T, est::Index>) {
        string base = lower_est_selector(arena, value.base, d, locals, types, specialization_id);
        string idx = lower_est_selector(arena, value.index, d, locals, types, specialization_id);
        const auto base_facts = lookup_selector_facts(arena, value.base, specialization_id);
        if (base_facts && base_facts->type && base_facts->type->kind == TypeKind::Builtin && base_facts->type->name == "map") {
          return "(" + base + ").get(&(" + idx + ")).expect(\"Moss missing map key\")";
        }
        return "(" + base + ")[(" + idx + ") as usize]";
      } else if constexpr (std::is_same_v<T, est::Call>) {
        const Program& checked_program = semantic_program_ ? *semantic_program_ : p_;
        if (!facts || facts->callee_spec_id == InvalidSpecId ||
            facts->callee_spec_id >= checked_program.specialization_table.size())
          throw std::runtime_error(
              "MOSS_INTERNAL_INVARIANT selector Call lacks checked callee SpecId");
        const auto& target_identity =
            checked_program.specialization_table[facts->callee_spec_id];
        if (target_identity.id != facts->callee_spec_id)
          throw std::runtime_error(
              "MOSS_INTERNAL_INVARIANT selector callee SpecId table row is contradictory");
        const Function* fn = find_function_by_callable_id(
            target_identity.semantic_callable_identity);
        if (!fn)
          throw std::runtime_error(
              "MOSS_INTERNAL_INVARIANT checked selector callee has no function body");
        string fn_name;
        const std::vector<Effect>* callee_parameter_effects = nullptr;
        if (facts->callee_spec_id != fn->spec_id) {
          auto exact_fn_spec = std::find_if(
              fn->specializations.begin(), fn->specializations.end(),
              [&](const FunctionSpecialization& cand) {
                return cand.spec_id == facts->callee_spec_id;
              });
          if (exact_fn_spec == fn->specializations.end() || exact_fn_spec->generated_name.empty()) {
            throw std::runtime_error(
                "MOSS_INTERNAL_INVARIANT selector call specialization SpecId has no generated function");
          }
          callee_parameter_effects = &exact_fn_spec->parameter_effects;
          bool is_view_fn = false;
          for (const auto& p : fn->params) {
            if (view_object_type(p.type)) { is_view_fn = true; break; }
          }
          fn_name = is_view_fn ? "__moss_view_" + exact_fn_spec->generated_name : exact_fn_spec->generated_name;
        } else {
          if (fn->generic || fn->static_dispatch) {
            throw std::runtime_error(
                "MOSS_INTERNAL_INVARIANT selector call to generic function has no checked specialization SpecId");
          }
          callee_parameter_effects = &fn->parameter_effects;
          bool is_view_fn = false;
          for (const auto& p : fn->params) {
            if (view_object_type(p.type)) { is_view_fn = true; break; }
          }
          fn_name = is_view_fn ? "__moss_view_" + fn->name : rust_identifier(fn->name);
        }

        string call_str = fn_name + "(";
        for (size_t i = 0; i < value.arguments.size(); ++i) {
          if (i) call_str += ", ";
          const auto arg_facts = lookup_selector_facts(arena, value.arguments[i], specialization_id);
          if (!arg_facts || !arg_facts->inferred_effect) {
            throw std::runtime_error("MOSS_INTERNAL_INVARIANT selector call argument missing checker-issued effect");
          }
          Effect eff = *arg_facts->inferred_effect;
          if (!callee_parameter_effects || i >= callee_parameter_effects->size() ||
              eff != (*callee_parameter_effects)[i]) {
            throw std::runtime_error("MOSS_INTERNAL_INVARIANT selector call argument effect mismatch with callee parameter effect");
          }
          const auto* arg_id = std::get_if<est::Identifier>(&arena.at(value.arguments[i]));
          string arg_rendered = lower_est_selector(arena, value.arguments[i], d, locals, types, specialization_id);

          if (eff == Effect::Write) {
            if (arg_id && view_parameters_.count(arg_id->name)) {
              call_str += "&mut (*" + arg_id->name + ")";
            } else if (arg_id && borrowed_function_parameters_.count(arg_id->name)) {
              call_str += rust_identifier(arg_id->name);
            } else {
              call_str += "&mut (" + arg_rendered + ")";
            }
          } else if (eff == Effect::Read) {
            if (arg_id && view_parameters_.count(arg_id->name)) {
              call_str += "&(*" + arg_id->name + ")";
            } else if (arg_id && borrowed_function_parameters_.count(arg_id->name)) {
              call_str += rust_identifier(arg_id->name);
            } else if (arg_facts->type && (arg_facts->type->kind == TypeKind::NominalInstance ||
                                           !is_primitive_copy_type(arg_facts->type))) {
              call_str += "&(" + arg_rendered + ")";
            } else {
              call_str += arg_rendered;
            }
          } else if (eff == Effect::Consume) {
            call_str += arg_rendered;
          }
        }
        call_str += ")";
        return call_str;
      } else {
        throw std::runtime_error("MOSS_INTERNAL_INVARIANT unhandled EST node in selector");
      }
    }, node);
  }

  string emit_take_expression(
      const TypedTakeFactV1& fact,
      const Stmt& s,
      const Domain* d,
      const std::set<string>& locals,
      const std::unordered_map<string,string>* types) const {
    if (fact.est_arena_index >= p_.expression_arenas.size() ||
        !p_.expression_arenas[fact.est_arena_index]) {
      throw std::runtime_error("MOSS_INTERNAL_INVARIANT missing take EST arena");
    }
    const auto& arena = *p_.expression_arenas[fact.est_arena_index];
    (void)s;
    const SpecId specialization_id = fact.specialization_id;

    string current_place;
    bool is_view_root = false;
    if (view_parameters_.count(fact.root_name)) {
      current_place = fact.root_name;
      is_view_root = true;
    } else if (view_method_ && d && (fact.root_name == "self" || !locals.count(fact.root_name))) {
      if (fact.root_name == "self") {
        current_place = "self";
      } else {
        current_place = "self.__moss_field_" + fact.root_name + "_mut()";
      }
      is_view_root = true;
    } else if (view_domain_ && d && !locals.count(fact.root_name)) {
      current_place = "state." + fact.root_name;
      is_view_root = view_domain_access_;
    } else if (borrowed_function_parameters_.count(fact.root_name)) {
      current_place = "(*" + fact.root_name + ")";
    } else {
      current_place = fact.root_name;
    }

    bool ends_with_mut_accessor = current_place.rfind("_mut()") != string::npos;
    bool current_is_view = is_view_root;

    for (const auto& proj : fact.projection_path) {
      switch (proj.kind) {
        case TypedTakeProjectionKindV1::RecordField: {
          const auto* member = std::get_if<est::Member>(&arena.at(proj.expression_occurrence_id));
          if (!member) {
            throw std::runtime_error("MOSS_INTERNAL_INVARIANT take projection member missing");
          }
          if (current_is_view) {
            current_place += ".__moss_field_" + member->name + "_mut()";
            ends_with_mut_accessor = true;
            current_is_view = proj.output_type && view_object_type(canonical_type_name(proj.output_type));
          } else {
            current_place += "." + rust_identifier(member->name);
            ends_with_mut_accessor = false;
            current_is_view = false;
          }
          break;
        }
        case TypedTakeProjectionKindV1::VectorIndex:
        case TypedTakeProjectionKindV1::QueueIndex: {
          string rendered_selector = lower_est_selector(
              arena, proj.selector_occurrence_id, d, locals, types,
              specialization_id);
          current_place = "(" + current_place + ")[" + "(" + rendered_selector + ") as usize]";
          ends_with_mut_accessor = false;
          current_is_view = false;
          break;
        }
        case TypedTakeProjectionKindV1::MapIndex: {
          string rendered_selector = lower_est_selector(
              arena, proj.selector_occurrence_id, d, locals, types,
              specialization_id);
          current_place = "(" + current_place + ").get_mut(&(" + rendered_selector + ")).expect(\"Moss missing map key\")";
          ends_with_mut_accessor = false;
          current_is_view = false;
          break;
        }
      }
    }

    if (ends_with_mut_accessor) {
      return "std::mem::take(" + current_place + ")";
    } else {
      return "std::mem::take(&mut " + current_place + ")";
    }
  }

  void gen_stmts(std::ostringstream& o, const vector<Stmt>& ss, const Domain* d,
                 const Handler* current_handler, const string& reply_slot,
                 std::set<string>& locals, std::unordered_map<string,string>& types,
                 int base, bool in_handler,
                 bool in_function = false,
                 const string& functional_context = "",
                 SpecId body_spec_id = InvalidSpecId) {
    (void)functional_context;
    struct BodySpecScope {
      SpecId& slot;
      SpecId prior;
      BodySpecScope(SpecId& target, SpecId current)
          : slot(target), prior(target) { slot = current; }
      ~BodySpecScope() { slot = prior; }
    } body_spec_scope(codegen_body_spec_id_, body_spec_id);
    // Binding metadata is lexical to this generated Moss body.  Keeping it
    // separate from Rust type strings prevents a local/backend name collision
    // from being mistaken for a specialization relationship.
    domain_instance_bindings_.clear();
    ambiguous_domain_instance_bindings_.clear();
    size_t i = 0;
    gen_block(o, ss, i, 0, d, current_handler, reply_slot, locals, types, base,
              in_handler, in_function, {},
              "");
    if (i != ss.size()) throw std::runtime_error("internal error: statement indentation tree not fully consumed");
  }

  void gen_block(std::ostringstream& o, const vector<Stmt>& ss, size_t& i, int level,
                 const Domain* d, const Handler* current_handler, const string& reply_slot,
                 std::set<string>& locals, std::unordered_map<string,string>& types,
                 int base, bool in_handler, bool in_function,
                 std::set<string> join_assignments = {},
                 const string& unused_context = "") {
    (void)unused_context;
    struct RecoverySlotScope {
      std::set<string>& slot;
      std::set<string> prior;
      explicit RecoverySlotScope(std::set<string>& current)
          : slot(current), prior(current) {}
      ~RecoverySlotScope() { slot = std::move(prior); }
    } recovery_slot_scope(phase21_option_slots_);
    auto indent = [&](int lev){ return string((base + lev) * 4, ' '); };
    while (i < ss.size()) {
      const auto& s = ss[i];
      auto concrete_binding = s.semantic_types_by_spec_id.find(codegen_body_spec_id_);
      string statement_type = concrete_binding == s.semantic_types_by_spec_id.end()
          ? s.semantic_type : concrete_binding->second;
      if (const char* dropped = std::getenv("MOSS_TEST_DROP_FACT");
          dropped && string(dropped) == "g2")
        statement_type.clear();
      if (s.indent < level) return;
      if (s.indent > level) throw std::runtime_error("internal error: unexpected statement indentation");
      if (s.kind == Stmt::Kind::Else) return; // consumed by the preceding if

      struct CodegenStatementScope {
        const Stmt*& slot;
        const Stmt* prior;
        CodegenStatementScope(const Stmt*& target, const Stmt* current)
            : slot(target), prior(target) { slot = current; }
        ~CodegenStatementScope() { slot = prior; }
      } codegen_statement_scope(phase21_codegen_statement_, &s);
      FunctionalExpressionHandlesScope expression_handles_scope(
          active_functional_expression_handles_, &s.expression_handles);
      std::optional<NativeEstCursor> statement_est_cursor;
      std::optional<est::ExprSlot> statement_est_slot;
      switch (s.kind) {
        case Stmt::Kind::Let:
        case Stmt::Kind::Var: statement_est_slot = est::ExprSlot::Initializer; break;
        case Stmt::Kind::Assign: statement_est_slot = est::ExprSlot::RhsValue; break;
        case Stmt::Kind::Return:
        case Stmt::Kind::Reply: statement_est_slot = est::ExprSlot::ReturnValue; break;
        case Stmt::Kind::If:
        case Stmt::Kind::While: statement_est_slot = est::ExprSlot::Condition; break;
        case Stmt::Kind::For: statement_est_slot = est::ExprSlot::Iterable; break;
        case Stmt::Kind::Match: statement_est_slot = est::ExprSlot::Scrutinee; break;
        case Stmt::Kind::Raise: statement_est_slot = est::ExprSlot::RaisedValue; break;
        case Stmt::Kind::Call: statement_est_slot = est::ExprSlot::Expression; break;
        case Stmt::Kind::Message: statement_est_slot = est::ExprSlot::Receiver; break;
        default: break;
      }
      if (statement_est_slot) {
        const est::Handle* selected = nullptr;
        for (const auto& handle : s.expression_handles) {
          if (handle.slot != *statement_est_slot || handle.argument_index != 0)
            continue;
          if (selected)
            throw std::runtime_error(
                "MOSS_INTERNAL_INVARIANT duplicate Native statement root EST handle");
          selected = &handle;
        }
        const Program& checked_program = semantic_program_ ? *semantic_program_ : p_;
        if (selected && selected->arena_index <
                checked_program.expression_arenas.size() &&
            checked_program.expression_arenas[selected->arena_index] &&
            selected->expr < checked_program.expression_arenas[
                selected->arena_index]->size())
          statement_est_cursor = NativeEstCursor{
              checked_program.expression_arenas[selected->arena_index].get(),
              selected->arena_index, selected->expr};
      }
      NativeEstCursorScope statement_est_scope(
          active_native_est_cursor_,
          statement_est_cursor ? &*statement_est_cursor : nullptr);
      source_comment(o, (base + level) * 4, s.line, s.text);

      struct RootIngressScope {
        bool& flag; bool saved;
        RootIngressScope(bool& f, bool value) : flag(f), saved(f) { flag = value; }
        ~RootIngressScope() { flag = saved; }
      } root_ingress_scope(root_ingress_statement_, s.message_root_ingress);
      struct CaptureLocalsScope {
        const std::set<string>*& slot;
        const std::set<string>* prior;
        CaptureLocalsScope(const std::set<string>*& target,
                           const std::set<string>& current)
            : slot(target), prior(target) { slot = &current; }
        ~CaptureLocalsScope() { slot = prior; }
      } capture_locals_scope(phase21_capture_locals_, locals);
      struct CaptureAlternativesScope {
        const RaiseSet*& slot;
        const RaiseSet* prior;
        CaptureAlternativesScope(const RaiseSet*& target,
                                 const RaiseSet& current)
            : slot(target), prior(target) { slot = &current; }
        ~CaptureAlternativesScope() { slot = prior; }
      };
      RaiseSet checked_exceptional_alternatives = s.exceptional_alternatives;
      if (std::getenv("MOSS_TEST_CONSUMER_DROP_COLLECTION_ERROR_FACT"))
        for (auto it = checked_exceptional_alternatives.begin();
             it != checked_exceptional_alternatives.end();) {
          if (it->kind == RaisedIdentityKind::EnumVariant &&
              it->type == "std__CollectionError")
            it = checked_exceptional_alternatives.erase(it);
          else
            ++it;
        }
      if (std::getenv("MOSS_TEST_CONSUMER_PERTURB_COLLECTION_ERROR_FACT"))
      {
        RaiseSet perturbed;
        for (const auto& checked : checked_exceptional_alternatives)
          perturbed.insert(
              checked.kind == RaisedIdentityKind::EnumVariant &&
                      checked.type == "std__CollectionError"
                  ? RaisedIdentity::enum_variant(checked.type, "perturbed")
                  : checked);
        checked_exceptional_alternatives = std::move(perturbed);
      }
      CaptureAlternativesScope capture_alternatives_scope(
          phase21_capture_alternatives_, checked_exceptional_alternatives);

      size_t direct_functional_id = statement_functional_pipeline_id(
          s, "", 0);
      const FunctionalPipeline* direct_functional =
          planned_functional_pipeline(direct_functional_id);
      if (direct_functional &&
          direct_functional->virtualized_into_pipeline_id &&
          (s.kind == Stmt::Kind::Assign || s.kind == Stmt::Kind::Let ||
           s.kind == Stmt::Kind::Var)) {
        backend_comment(
            o, (base + level) * 4,
            "FUNCTIONAL INTERMEDIATE '" + direct_functional->binding_name +
                "' VIRTUALIZED across one immutable binding; no collection allocated");
        locals.insert(s.a);
        types[s.a] = direct_functional->output_type;
        ++i;
        continue;
      }
      if (direct_functional && direct_functional->traversal_group_id) {
        const FunctionalTraversalGroup* group = functional_traversal_group(
            direct_functional->traversal_group_id);
        if (!group || group->consumers.empty() ||
            group->consumers.front().pipeline_id != direct_functional_id)
          throw std::runtime_error(
              "internal error: shared functional traversal did not start at "
              "its first consumer");
        gen_shared_functional_traversal(
            o, *group, ss, i, d, locals, types, (base + level) * 4,
            "");
        i += group->consumers.size();
        continue;
      }
      if (direct_functional && !direct_functional->binding_name.empty() &&
          !direct_functional->virtualized_into_pipeline_id &&
          !direct_functional->binding_materialization_reason.empty())
        backend_comment(
            o, (base + level) * 4,
            "FUNCTIONAL INTERMEDIATE '" + direct_functional->binding_name +
                "' MATERIALIZED; reason: " +
                direct_functional->binding_materialization_reason);

      if (gen_executor_or_fileio_statement(o, s, (base + level) * 4, d, locals, types)) {
        ++i;
        continue;
      }

      switch (s.kind) {
        case Stmt::Kind::Try: {
          const auto& try_continuation =
              resolve_try_continuation_fact(s, "");
          auto ex_facts = resolve_exceptional_ownership_facts(s, "");
          (void)ex_facts;
          // Rust cannot infer that a move on one exceptional variant leaves
          // an owned local available on another. The checker has already
          // proved the recovery path; retain only affected locals in slots.
          size_t attempt_end = i + 1;
          while (attempt_end < ss.size() && ss[attempt_end].indent > level)
            ++attempt_end;
          size_t recover_end = attempt_end;
          while (recover_end < ss.size() && ss[recover_end].indent == level &&
                 ss[recover_end].kind == Stmt::Kind::Recover) {
            ++recover_end;
            while (recover_end < ss.size() && ss[recover_end].indent > level)
              ++recover_end;
          }
          std::set<string> recovery_slots;
          for (size_t candidate = i + 1; candidate < attempt_end; ++candidate) {
            const Stmt& consumed = ss[candidate];
            for (const auto& binding : consumed.ownership_consumed_bindings) {
              if (!types.count(binding) || copy_type(types.at(binding)) ||
                  phase21_option_slots_.count(binding))
                continue;
              for (size_t read = attempt_end; read < recover_end; ++read) {
                const Stmt& recovered = ss[read];
                bool mentions = tree_expression_mentions_identifier(
                    recovered.b, binding);
                if (recovered.kind != Stmt::Kind::Assign)
                  mentions |= tree_expression_mentions_identifier(
                      recovered.a, binding);
                for (const auto& argument : recovered.args)
                  mentions |= tree_expression_mentions_identifier(
                      argument, binding);
                if (mentions) {
                  recovery_slots.insert(binding);
                  break;
                }
              }
            }
          }
          for (const auto& binding : recovery_slots) {
            o << indent(level) << "let mut __moss_recovery_slot_" << binding
              << " = Some(" << rust_identifier(binding) << ");\n";
            phase21_option_slots_.insert(binding);
          }
          if (moss_test_drop_fact("joins") || moss_test_perturb_fact("joins"))
            throw std::runtime_error(
                "MOSS_INTERNAL_INVARIANT checked join fact was dropped or perturbed");
          const auto exact_join =
              s.joined_types_by_spec_id.find(codegen_body_spec_id_);
          const auto& joined_types = exact_join ==
                  s.joined_types_by_spec_id.end()
              ? s.joined_types
              : exact_join->second;
          vector<string> joined_bindings;
          for (const auto& entry : joined_types)
            if (!types.count(entry.first) &&
                !contains_internal_placeholder(entry.second))
              joined_bindings.push_back(entry.first);
          std::sort(joined_bindings.begin(), joined_bindings.end());
          for (const auto& binding : joined_bindings) {
            bool is_joined_borrow = false;
            if (p_.typed_semantics) {
              for (const auto& fact : p_.typed_semantics->local_ownership_facts) {
                const std::string& bind_id = fact.binding_identity;
                if (!fact.join_definition_identity.empty() &&
                    (ends_with(fact.definition_identity, ":" + binding) ||
                     ends_with(fact.join_definition_identity, ":" + binding) ||
                     bind_id == binding ||
                     ends_with(bind_id, ":" + binding))) {
                  if (fact.access == Effect::Read || fact.access == Effect::Write) {
                    is_joined_borrow = true;
                    break;
                  }
                }
              }
            }
            if (is_joined_borrow || !view_parameters_.empty()) {
              o << indent(level) << "let mut " << rust_identifier(binding) << ";\n";
            } else {
              o << indent(level) << "let mut " << rust_identifier(binding) << ": "
                << rust_type(joined_types.at(binding)) << ";\n";
            }
            locals.insert(binding);
            types[binding] = joined_types.at(binding);
            if (!view_parameters_.empty() && view_object_type(joined_types.at(binding)))
              view_parameters_.insert(binding);
          }

          if (s.exceptional_alternatives.empty()) {
            ++i;
            auto attempt_locals = locals;
            auto attempt_types = types;
            auto attempt_join_assignments = join_assignments;
            attempt_join_assignments.insert(joined_bindings.begin(),
                                            joined_bindings.end());
            gen_block(o, ss, i, level + 1, d, current_handler, reply_slot,
                      attempt_locals, attempt_types, base, in_handler,
                      in_function, attempt_join_assignments,
                      "");
            while (i < ss.size() && ss[i].indent == level &&
                   ss[i].kind == Stmt::Kind::Recover) {
              ++i;
              while (i < ss.size() && ss[i].indent > level) ++i;
            }
            if (!try_continuation.has_normal_successor)
              while (i < ss.size() && ss[i].indent >= level) ++i;
            break;
          }

          const string outcome = "__moss_try_outcome_" +
              std::to_string(phase21_try_temp_++);
          // The attempt's normal completion leaves this outer block
          // directly, so rustc sees every join binding initialized on the
          // normal path even when a raise precedes its first assignment.
          const string done_label = "__moss_try_done_" +
              std::to_string(phase21_try_temp_++);
          o << indent(level) << "'" << done_label << ": {\n";
          o << indent(level) << "#[allow(unreachable_code)]\n";
          const string try_label = "__moss_try_" +
              std::to_string(phase21_try_temp_++);
          const bool captured_try = phase21_capture_handler_ != nullptr;
          const string carrier = captured_try
              ? handler_capture_carrier_type(*phase21_capture_handler_) : "";
          o << indent(level) << "let " << outcome
            << ": Result<(), " << (captured_try ? carrier : "__MossRaised")
            << "> = '" << try_label << ": {\n";
          ++i;
          auto attempt_locals = locals;
          auto attempt_types = types;
          auto attempt_join_assignments = join_assignments;
          attempt_join_assignments.insert(joined_bindings.begin(),
                                          joined_bindings.end());
          const bool saved_result_context = phase21_result_context_;
          const string saved_try_label = phase21_try_label_;
          const auto saved_return_result_context = phase21_return_result_context_;
          if (!phase21_return_result_context_)
            phase21_return_result_context_ = saved_result_context;
          phase21_result_context_ = true;
          phase21_try_label_ = try_label;
          gen_block(o, ss, i, level + 1, d, current_handler, reply_slot,
                    attempt_locals, attempt_types, base, in_handler,
                    in_function, attempt_join_assignments,
                    "");
          phase21_result_context_ = saved_result_context;
          phase21_try_label_ = saved_try_label;
          o << indent(level + 1) << "break '" << done_label << ";\n";
          o << indent(level) << "};\n";
          o << indent(level) << "#[allow(unreachable_patterns)]\n";
          o << indent(level) << "match " << outcome << " {\n";
          o << indent(level + 1) << "Ok(()) => std::process::abort(),\n";

          RaiseSet handled;
          while (i < ss.size() && ss[i].indent == level &&
                 ss[i].kind == Stmt::Kind::Recover) {
            const Stmt& arm = ss[i++];
            const size_t body_begin = i;
            size_t body_end = i;
            while (body_end < ss.size() && ss[body_end].indent > level)
              ++body_end;

            const bool arm_has_bare_raise = std::any_of(
                ss.begin() + static_cast<std::ptrdiff_t>(body_begin),
                ss.begin() + static_cast<std::ptrdiff_t>(body_end),
                [](const Stmt& nested) {
                  return nested.kind == Stmt::Kind::Raise && nested.a.empty();
                });

            if (arm_has_bare_raise) {
              for (size_t nested_index = body_begin;
                   nested_index < body_end; ++nested_index) {
                const auto& nested = ss[nested_index];
                if (nested.kind != Stmt::Kind::Raise || !nested.a.empty())
                  continue;
                if (arm.exceptional_alternatives.empty())
                  throw std::runtime_error(
                      "MOSS_INTERNAL_INVARIANT bare raise has no checked selected variant");
                for (const auto& identity : arm.exceptional_alternatives) {
                  std::size_t edge_count = 0;
                  for (const auto* edge : ex_facts) {
                    if (edge->recovery_statement_occurrence_id !=
                            arm.checked_statement_occurrence_id ||
                        edge->raised_variant_identity != identity.canonical())
                      continue;
                    ++edge_count;
                    require_bare_raise_transfer(
                        nested, arm, *edge, identity, "");
                  }
                  if (!edge_count)
                    throw std::runtime_error(
                        "MOSS_INTERNAL_INVARIANT bare raise transfer source edge missing");
                }
              }
            }
            for (const auto& identity : arm.exceptional_alternatives) {
              handled.insert(identity);
              const string variant = phase21_rust_variant(identity);
              const string inner = "__moss_selected_value";
              if (captured_try) {
                o << indent(level + 1) << "Err(__moss_selected_carrier) "
                  << "if matches!(&__moss_selected_carrier.0.error, "
                  << "__MossRaised::" << variant << "(";
              } else {
                o << indent(level + 1) << "Err(__MossRaised::" << variant
                  << "(" << inner << " @ ";
              }
              if (identity.kind == RaisedIdentityKind::EnumVariant) {
                o << phase21_rust_error_type(identity) << "::"
                  << rust_identifier(identity.variant);
                string enum_key = canonical_type_name(identity.type);
                if (!enums_.count(enum_key)) {
                  for (size_t separator = enum_key.find("::");
                       separator != string::npos;
                       separator = enum_key.find("::"))
                    enum_key.replace(separator, 2, "__");
                }
                auto declaration = enums_.find(enum_key);
                auto item = declaration == enums_.end()
                    ? vector<EnumCase>::const_iterator{}
                    : std::find_if(
                          declaration->second->cases.begin(),
                          declaration->second->cases.end(),
                          [&](const EnumCase& candidate) {
                            return candidate.name == identity.variant;
                          });
                if (declaration != enums_.end() &&
                    item != declaration->second->cases.end() &&
                    !item->fields.empty())
                  o << " { .. }";
              } else {
                o << "_";
              }
              o << ")";
              if (captured_try) {
                o << ") => {\n";
                o << indent(level + 2) << "let " << inner
                  << " = match &__moss_selected_carrier.0.error { "
                  << "__MossRaised::" << variant
                  << "(__moss_value) => __moss_value, "
                  << "_ => std::process::abort() };\n";
              } else {
                o << ") => {\n";
              }
              string selected_error_expression;
              if (arm.est_pattern && arm.est_pattern->kind ==
                      est::PatternNode::Kind::RecoveryBinding &&
                  !arm.est_pattern->binding.empty())
                selected_error_expression = "__MossRaised::" + variant +
                    "(" + rust_identifier(arm.est_pattern->binding) + ")";
              else
                selected_error_expression = "__MossRaised::" + variant +
                    "(" + inner + ")";

              auto arm_locals = locals;
              auto arm_types = types;
              if (moss_test_drop_fact("patterns") ||
                  moss_test_perturb_fact("patterns"))
                throw std::runtime_error(
                    "MOSS_INTERNAL_INVARIANT checked recovery pattern was dropped or perturbed");
              if (arm.est_pattern && arm.est_pattern->kind ==
                  est::PatternNode::Kind::RecoveryBinding) {
                const auto& binding = arm.est_pattern->binding;
                if (std::find(joined_bindings.begin(), joined_bindings.end(),
                              binding) != joined_bindings.end()) {
                  o << indent(level + 2) << binding << " = " << inner
                    << ";\n";
                } else {
                  o << indent(level + 2) << "let " << binding << " = " << inner
                    << ";\n";
                  arm_locals.insert(binding);
                  arm_types[binding] = identity.type;
                }
              } else if (arm.est_pattern &&
                  (arm.est_pattern->kind == est::PatternNode::Kind::QualifiedRecoveryCase ||
                   arm.est_pattern->kind == est::PatternNode::Kind::GroupedAlternatives)) {
                const auto alternatives = arm.est_pattern->kind ==
                        est::PatternNode::Kind::GroupedAlternatives
                    ? arm.est_pattern->alternatives
                    : std::vector<est::PatternNode>{*arm.est_pattern};
                for (const auto& pattern : alternatives) {
                  if (!(generated_failure_pattern_identity(pattern) == identity))
                    continue;
                  emit_generated_error_payload_bindings(
                      o, (base + level + 2) * 4, inner, identity,
                      generated_error_pattern_bindings(pattern, identity),
                      arm_locals, arm_types, true);
                  break;
                }
              }
              auto arm_join_assignments = join_assignments;
              arm_join_assignments.insert(joined_bindings.begin(),
                                          joined_bindings.end());
              const string saved_selected_error = phase21_selected_error_;
              const string saved_selected_carrier = phase21_selected_carrier_;
              phase21_selected_error_ = arm_has_bare_raise
                  ? selected_error_expression : string();
              phase21_selected_carrier_ = captured_try
                  ? "__moss_selected_carrier" : string();
              size_t rendered_end = body_begin;
              gen_block(o, ss, rendered_end, level + 1, d, current_handler,
                        reply_slot, arm_locals, arm_types, base + 1,
                        in_handler, in_function, arm_join_assignments,
                        "");
              phase21_selected_error_ = saved_selected_error;
              phase21_selected_carrier_ = saved_selected_carrier;
              if (rendered_end != body_end)
                throw std::runtime_error(
                    "internal error: recover body was not fully lowered");
              o << indent(level + 1) << "},\n";
            }
            i = body_end;
          }
          RaiseSet remaining = raise_set_without(
              s.exceptional_alternatives, handled);
          if (!remaining.empty()) {
            o << indent(level + 1)
              << "Err(__moss_unhandled_error) => ";
            if (!saved_try_label.empty())
              o << "break '" << saved_try_label
                << " Err(__moss_unhandled_error),\n";
            else if (saved_result_context)
              o << "return Err(" <<
                  "__moss_unhandled_error" << "),\n";
            else
              o << "std::process::abort(),\n";
          } else {
            o << indent(level + 1)
              << "Err(_) => std::process::abort(),\n";
          }
          o << indent(level) << "}\n";
          o << indent(level) << "}\n";
          phase21_return_result_context_ = saved_return_result_context;
          if (!try_continuation.has_normal_successor)
            while (i < ss.size() && ss[i].indent >= level) ++i;
          break;
        }
        case Stmt::Kind::Recover:
          throw std::runtime_error(
              "internal error: recover arm escaped checked try lowering");
        case Stmt::Kind::Raise: {
          if (s.a.empty()) {
            if (phase21_selected_error_.empty())
              throw std::runtime_error(
                  "internal error: bare raise lacks a selected error");
            if (phase21_selected_carrier_.empty()) {
              o << indent(level)
                << phase21_error_exit(phase21_selected_error_) << ";\n";
            } else if (phase21_try_label_.empty()) {
              o << indent(level) << "return Err("
                << phase21_selected_carrier_ << ");\n";
            } else {
              o << indent(level) << "break '" << phase21_try_label_
                << " Err(" << phase21_selected_carrier_ << ");\n";
            }
          } else {
            auto type = generated_expr_type(s.a, &types);
            if (!type)
              throw std::runtime_error(
                  "internal error: raise lacks a checked error value type");
            const string concrete = canonical_type_name(*type);
            const auto* take_fact = resolve_take_fact(s, "");
            const string value = take_fact
                ? emit_take_expression(*take_fact, s, d, locals, &types)
                : expr(s.a, d, locals, &types,
                    statement_functional_pipeline_id(
                        s, "", 0));
            if (enums_.count(concrete)) {
              const string raised_value = "__moss_error_value_" +
                  std::to_string(phase21_try_temp_++);
              const string temporary = "__moss_raised_value_" +
                  std::to_string(phase21_try_temp_++);
              o << indent(level) << "let " << raised_value << " = "
                << value << ";\n"
                << indent(level) << "#[allow(unreachable_patterns)]\n"
                << indent(level) << "let " << temporary << " = match "
                << raised_value << " {\n";
              for (const auto& identity : s.exceptional_alternatives) {
                if (identity.kind != RaisedIdentityKind::EnumVariant ||
                    canonical_type_name(identity.type) != concrete)
                  continue;
                const auto& declaration = *enums_.at(concrete);
                auto item = std::find_if(declaration.cases.begin(),
                    declaration.cases.end(), [&](const EnumCase& candidate) {
                      return candidate.name == identity.variant;
                    });
                if (item == declaration.cases.end()) continue;
                o << indent(level + 1) << "value @ "
                  << phase21_rust_error_type(identity) << "::"
                  << rust_identifier(identity.variant);
                if (!item->fields.empty()) o << " { .. }";
                o << " => __MossRaised::"
                  << phase21_rust_variant(identity) << "(value),\n";
              }
              o << indent(level + 1)
                << "_ => std::process::abort(),\n"
                << indent(level) << "};\n"
                << indent(level)
                << phase21_error_exit(temporary) << ";\n";
            } else {
              const RaisedIdentity identity{
                  RaisedIdentityKind::ScalarType, concrete, {}};
              if (!s.exceptional_alternatives.count(identity))
                throw std::runtime_error(
                    "internal error: raise lacks its checked scalar identity");
              o << indent(level) << phase21_error_exit(
                  "__MossRaised::" + phase21_rust_variant(identity) +
                  "(" + value + ")") << ";\n";
            }
          }
          ++i;
          while (i < ss.size() && ss[i].indent >= level) ++i;
          break;
        }
        case Stmt::Kind::Match: {
          const auto* match_fact = resolve_match_ownership_fact(s, "");
          if (!match_fact || !match_fact->scrutinee_type)
            throw std::runtime_error(
                "MOSS_INTERNAL_INVARIANT checked match fact lacks its scrutinee type");
          if (!match_fact->is_consuming &&
              has_nested_index_member_projection(s.a))
            throw std::runtime_error(
                "MOSS_INTERNAL_INVARIANT checked READ match scrutinee has unsupported nested indexed projection");
          const bool is_option =
              match_fact->scrutinee_kind == TypedMatchScrutineeKindV1::Option;
          if (is_option &&
              (match_fact->scrutinee_type->kind != TypeKind::Builtin ||
               match_fact->scrutinee_type->name != "option"))
            throw std::runtime_error(
                "MOSS_INTERNAL_INVARIANT checked Option match fact has a non-Option type");
          const string checked_match_type =
              match_fact->scrutinee_type->to_canonical_string();
          static const EnumType option_enum = [] {
            EnumType opt;
            opt.name = "option";
            EnumCase some_c;
            some_c.name = "Some";
            Field f;
            f.name = "value";
            some_c.fields.push_back(f);
            opt.cases.push_back(some_c);
            EnumCase none_c;
            none_c.name = "None";
            opt.cases.push_back(none_c);
            return opt;
          }();
          const EnumType* declaration_ptr = is_option
              ? &option_enum
              : enums_.count(checked_match_type)
                  ? enums_.at(checked_match_type) : nullptr;
          if (!declaration_ptr)
            throw std::runtime_error("unchecked enum match");
          const EnumType& declaration = *declaration_ptr;
          if (moss_test_drop_fact("joins") || moss_test_perturb_fact("joins"))
            throw std::runtime_error(
                "MOSS_INTERNAL_INVARIANT checked join fact was dropped or perturbed");
          const auto exact_join =
              s.joined_types_by_spec_id.find(codegen_body_spec_id_);
          const auto& joined_types = exact_join == s.joined_types_by_spec_id.end()
              ? s.joined_types : exact_join->second;
          vector<string> joined_bindings;
          for (const auto& entry : joined_types)
            if (!types.count(entry.first) && !contains_internal_placeholder(entry.second))
              joined_bindings.push_back(entry.first);
          std::sort(joined_bindings.begin(), joined_bindings.end());
          for (const auto& binding : joined_bindings) {
            bool is_joined_borrow = false;
            if (p_.typed_semantics) {
              for (const auto& fact : p_.typed_semantics->local_ownership_facts) {
                const std::string& bind_id = fact.binding_identity;
                if (!fact.join_definition_identity.empty() &&
                    (ends_with(fact.definition_identity, ":" + binding) ||
                     ends_with(fact.join_definition_identity, ":" + binding) ||
                     bind_id == binding ||
                     ends_with(bind_id, ":" + binding))) {
                  if (fact.access == Effect::Read || fact.access == Effect::Write) {
                    is_joined_borrow = true;
                    break;
                  }
                }
              }
            }
            if (is_joined_borrow || !view_parameters_.empty()) {
              o << indent(level) << "let mut " << rust_identifier(binding) << ";\n";
            } else {
              o << indent(level) << "let mut " << rust_identifier(binding) << ": "
                << rust_type(joined_types.at(binding)) << ";\n";
            }
            locals.insert(binding);
            types[binding] = joined_types.at(binding);
            if (!view_parameters_.empty() && view_object_type(joined_types.at(binding)))
              view_parameters_.insert(binding);
          }
          string match_callee;
          vector<string> match_arguments;
          bool struct_literal = false;
          // A Rust struct literal is not allowed as a bare scrutinee.
          if (tree_parse_simple_call(s.a, match_callee, match_arguments)) {
            for (const auto& candidate : declaration.cases) {
              const string suffix = declaration.name + "." + candidate.name;
              if (!candidate.fields.empty() &&
                  (match_callee == suffix ||
                   (match_callee.size() > suffix.size() &&
                    match_callee.compare(match_callee.size() - suffix.size(), suffix.size(), suffix) == 0 &&
                    match_callee[match_callee.size() - suffix.size() - 1] == '.'))) {
                struct_literal = true;
                break;
              }
            }
          }
          o << indent(level) << "match ";
          const bool recovery_slot = phase21_option_slots_.count(s.a) != 0;
          const bool mutable_match_payload = std::any_of(
              match_fact->arms.begin(), match_fact->arms.end(),
              [](const TypedMatchArmFactV1& arm) {
                return arm.payload_access == Effect::Write;
              });
          // The checked match fact, not the source spelling, selects whether
          // the scrutinee is borrowed or transferred.  In particular, lower
          // a READ match over an indexed member as an addressable place so
          // expr() cannot materialize it with a hidden non-Copy clone.
          const bool borrow_match_scrutinee =
              !match_fact->is_consuming && !recovery_slot;
          if (borrow_match_scrutinee)
            o << (mutable_match_payload ? "&mut " : "&");
          if (struct_literal) o << "(";
          if (recovery_slot && match_fact->is_consuming)
            o << "__moss_recovery_slot_" << s.a
              << ".take().unwrap_or_else(|| std::process::abort())";
          else if (borrow_match_scrutinee) {
            const string borrowed_scrutinee =
                read_call_place(s.a, d, locals, &types);
            if (borrowed_scrutinee.find(".clone(") != string::npos ||
                borrowed_scrutinee.find(".cloned(") != string::npos ||
                borrowed_scrutinee.find(".to_owned(") != string::npos)
              throw std::runtime_error(
                  "MOSS_INTERNAL_INVARIANT checked READ match scrutinee "
                  "could not be lowered without materializing a copy");
            o << borrowed_scrutinee;
          }
          else
            o << expr(s.a, d, locals, &types);
          if (struct_literal) o << ")";
          o << " {\n";
          ++i;
          while (i < ss.size() && ss[i].indent == level + 1) {
            const Stmt& arm = ss[i];
            if (!arm.est_pattern ||
                (arm.est_pattern->kind != est::PatternNode::Kind::EnumTag &&
                 arm.est_pattern->kind != est::PatternNode::Kind::EnumCase))
              throw std::runtime_error(
                  "MOSS_INTERNAL_INVARIANT checked enum pattern is absent");
            if (moss_test_drop_fact("patterns") ||
                moss_test_perturb_fact("patterns"))
              throw std::runtime_error(
                  "MOSS_INTERNAL_INVARIANT checked enum pattern was dropped or perturbed");
            const auto& pattern = *arm.est_pattern;
            auto item = std::find_if(declaration.cases.begin(), declaration.cases.end(),
                [&](const EnumCase& candidate) { return candidate.name == pattern.case_name; });
            if (item == declaration.cases.end())
              throw std::runtime_error("unchecked enum case");
            const auto checked_arm = std::find_if(
                match_fact->arms.begin(), match_fact->arms.end(),
                [&](const TypedMatchArmFactV1& candidate) {
                  return candidate.case_name == pattern.case_name;
                });
            if (!pattern.payload_bindings.empty() &&
                (checked_arm == match_fact->arms.end() ||
                 checked_arm->payload_binding_names != pattern.payload_bindings ||
                 checked_arm->payload_types.size() != pattern.payload_bindings.size()))
              throw std::runtime_error(
                  "MOSS_INTERNAL_INVARIANT checked match arm fact is missing or contradictory");
            if (is_option) {
              if (pattern.case_name == "Some") {
                o << indent(level + 1) << "Some("
                  << (pattern.payload_bindings.empty() ? "_" : pattern.payload_bindings.front())
                  << ")";
              } else {
                o << indent(level + 1) << "None";
              }
            } else {
              o << indent(level + 1) << declaration.name << "::"
                << rust_identifier(pattern.case_name);
              if (!item->fields.empty()) {
                o << " { ";
                for (size_t field_index = 0;
                     field_index < pattern.payload_bindings.size(); ++field_index) {
                  if (field_index) o << ", ";
                  string field_name = rust_identifier(item->fields[field_index].name);
                  if (field_name == pattern.payload_bindings[field_index])
                    o << field_name;
                  else
                    o << field_name << ": " << pattern.payload_bindings[field_index];
                }
                o << " }";
              }
            }
            o << " => {\n";
            auto branch_locals = locals;
            auto branch_types = types;
            auto saved_borrowed = borrowed_function_parameters_;
            auto saved_match_primitive_values = mutable_match_primitive_values_;
            for (size_t field_index = 0;
                 field_index < pattern.payload_bindings.size(); ++field_index) {
              const string& name = pattern.payload_bindings[field_index];
              string type;
              if (field_index >= checked_arm->payload_types.size() ||
                  !checked_arm->payload_types[field_index])
                throw std::runtime_error(
                    "MOSS_INTERNAL_INVARIANT checked match payload type is missing");
              type = checked_arm->payload_types[field_index]->to_canonical_string();
              branch_locals.insert(name);
              branch_types[name] = type;
              if (s.b != "consume") {
                if (checked_arm->payload_access == Effect::Write) {
                  borrowed_function_parameters_.insert(name);
                  if (copy_type_name(type))
                    mutable_match_primitive_values_.insert(name);
                } else if (copy_type_name(type))
                  o << indent(level + 2) << "let " << name << " = *" << name << ";\n";
                else borrowed_function_parameters_.insert(name);
              }
            }
            ++i;
            auto branch_join = join_assignments;
            branch_join.insert(joined_bindings.begin(), joined_bindings.end());
            gen_block(o, ss, i, level + 2, d, current_handler, reply_slot,
                      branch_locals, branch_types, base, in_handler, in_function,
                      branch_join, "");
            borrowed_function_parameters_ = std::move(saved_borrowed);
            mutable_match_primitive_values_ =
                std::move(saved_match_primitive_values);
            o << indent(level + 1) << "},\n";
          }
          o << indent(level) << "}\n";
          break;
        }
        case Stmt::Kind::Case:
          throw std::runtime_error("case outside checked match");
        case Stmt::Kind::If: {
          if (moss_test_drop_fact("joins") || moss_test_perturb_fact("joins"))
            throw std::runtime_error(
                "MOSS_INTERNAL_INVARIANT checked join fact was dropped or perturbed");
          const auto exact_join =
              s.joined_types_by_spec_id.find(codegen_body_spec_id_);
          const auto& joined_types = exact_join == s.joined_types_by_spec_id.end()
              ? s.joined_types : exact_join->second;
          vector<string> joined_bindings;
          for (const auto& entry : joined_types)
            if (!types.count(entry.first) && !contains_internal_placeholder(entry.second))
              joined_bindings.push_back(entry.first);
          std::sort(joined_bindings.begin(), joined_bindings.end());
          for (const auto& binding : joined_bindings) {
            const string& type = joined_types.at(binding);
            backend_comment(o, (base + level) * 4,
                            "CONTROL-FLOW JOIN: binding has one definite static type on every path");
            bool is_joined_borrow = false;
            if (p_.typed_semantics) {
              for (const auto& fact : p_.typed_semantics->local_ownership_facts) {
                const std::string& bind_id = fact.binding_identity;
                if (!fact.join_definition_identity.empty() &&
                    (ends_with(fact.definition_identity, ":" + binding) ||
                     ends_with(fact.join_definition_identity, ":" + binding) ||
                     bind_id == binding ||
                     ends_with(bind_id, ":" + binding))) {
                  if (fact.access == Effect::Read || fact.access == Effect::Write) {
                    is_joined_borrow = true;
                    break;
                  }
                }
              }
            }
            if (is_joined_borrow || !view_parameters_.empty()) {
              o << indent(level) << "let mut " << rust_identifier(binding) << ";\n";
            } else {
              o << indent(level) << "let mut " << rust_identifier(binding) << ": "
                << rust_type(type)
                << ";\n";
            }
            locals.insert(binding);
            types[binding] = type;
            if (!view_parameters_.empty() && view_object_type(type))
              view_parameters_.insert(binding);
          }
          o << indent(level) << "if "
            << expr(s.a, d, locals, &types,
                    statement_functional_pipeline_id(
                        s, "", 0), nullptr,
                    root_ingress_expression_occurrence(
                        s, est::ExprSlot::Condition, 0))
            << " {\n";
          ++i;
          auto child_locals = locals;
          auto child_types = types;
          auto child_join_assignments = join_assignments;
          child_join_assignments.insert(joined_bindings.begin(), joined_bindings.end());
          gen_block(o, ss, i, level + 1, d, current_handler, reply_slot,
                    child_locals, child_types, base, in_handler, in_function, child_join_assignments,
                    "");
          o << indent(level) << "}";
          if (i < ss.size() && ss[i].indent == level && ss[i].kind == Stmt::Kind::Else) {
            o << " else {\n";
            source_comment(o, (base + level + 1) * 4, ss[i].line, ss[i].text);
            ++i;
            auto else_locals = locals;
            auto else_types = types;
            gen_block(o, ss, i, level + 1, d, current_handler, reply_slot,
                      else_locals, else_types, base, in_handler, in_function, child_join_assignments,
                      "");
            o << indent(level) << "}\n";
          } else {
            o << "\n";
          }
          // Existing bindings keep their already established type in this
          // instance; only new joined locals use this context's checked type.
          break;
        }
        case Stmt::Kind::While: {
          auto loop_facts = resolve_loop_ownership_facts(s, "");
          (void)loop_facts;
          o << indent(level) << "while "
            << expr(s.a, d, locals, &types,
                    statement_functional_pipeline_id(
                        s, "", 0), nullptr,
                    root_ingress_expression_occurrence(
                        s, est::ExprSlot::Condition, 0))
            << " {\n";
          ++i;
          auto child_locals = locals;
          auto child_types = types;
          gen_block(o, ss, i, level + 1, d, current_handler, reply_slot,
                    child_locals, child_types, base, in_handler, in_function, join_assignments,
                    "");
          o << indent(level) << "}\n";
          break;
        }
        case Stmt::Kind::For: {
          auto loop_facts = resolve_loop_ownership_facts(s, "");
          string source = trim(s.b);
          string range_name;
          vector<string> range_args;
          bool is_range = tree_parse_simple_call(source, range_name, range_args) &&
              range_name == "range";
          auto source_type = generated_expr_type(source, &types);
          string element_type = source_type ? canonical_type_name(*source_type) : "";
          if (is_range) {
            // Materialize each range bound into a typed local before the
            // for-loop.  An expression such as `values |> count` expands to a
            // Rust block `{ … __moss_result }`.  When placed after `..` in
            //   for index in start..{ block } { body }
            // rustc parses `{ block }` as the for-body, not the upper bound,
            // silently dropping the induction binding and the intended body.
            // Binding the result first avoids this precedence ambiguity entirely.
            string start_var = "__moss_range_start_" + std::to_string(s.line);
            string end_var   = "__moss_range_end_"   + std::to_string(s.line);
            o << indent(level) << "let " << start_var << ": i64 = "
              << expr(range_args[0], d, locals, &types) << ";\n";
            o << indent(level) << "let " << end_var << ": i64 = "
              << expr(range_args[1], d, locals, &types) << ";\n";
            if (range_args.size() == 3) {
              string step_var = "__moss_range_step_" + std::to_string(s.line);
              o << indent(level) << "let " << step_var << ": i64 = "
                << expr(range_args[2], d, locals, &types) << ";\n";
              o << indent(level) << "for " << s.a << " in ("
                << start_var << ".." << end_var
                << ").step_by(" << step_var << " as usize) {\n";
            } else {
              o << indent(level) << "for " << s.a << " in "
                << start_var << ".." << end_var << " {\n";
            }
            auto child_locals = locals;
            auto child_types = types;
            child_locals.insert(s.a);
            child_types[s.a] = "int";
            ++i;
            gen_block(o, ss, i, level + 1, d, current_handler, reply_slot,
                      child_locals, child_types, base, in_handler, in_function, join_assignments,
                      "");
            o << indent(level) << "}\n";
            break;
          }

          bool vector_source = source_type &&
              (starts_with(element_type, "vector[") ||
               starts_with(element_type, "seq["));
          if (source_type &&
              canonical_type_name(*source_type) == "RangeBatch") {
            string collection_expression = expr(source, d, locals, &types);
            o << indent(level) << "for " << s.a << " in ("
              << collection_expression << ").ranges() {\n";
            auto child_locals = locals;
            auto child_types = types;
            child_locals.insert(s.a);
            child_types[s.a] = "Range";
            ++i;
            gen_block(o, ss, i, level + 1, d, current_handler, reply_slot,
                      child_locals, child_types, base, in_handler, in_function,
                      join_assignments, "");
            o << indent(level) << "}\n";
            break;
          }
          if (source_type && canonical_type_name(*source_type) == "Range") {
            string collection_expression = expr(source, d, locals, &types);
            o << indent(level) << "for " << s.a << " in ("
              << collection_expression << ").bytes() {\n";
            auto child_locals = locals;
            auto child_types = types;
            child_locals.insert(s.a);
            child_types[s.a] = "int";
            ++i;
            gen_block(o, ss, i, level + 1, d, current_handler, reply_slot,
                      child_locals, child_types, base, in_handler, in_function,
                      join_assignments, "");
            o << indent(level) << "}\n";
            break;
          }
          if (vector_source) {
            string element = starts_with(element_type, "vector[")
                ? trim(element_type.substr(7, element_type.size() - 8))
                : trim(element_type.substr(4, element_type.size() - 5));
            string collection_expression = expr(source, d, locals, &types);
            string traversal = "(" + collection_expression + ").iter()";
            bool write_iteration = false;
            // FileIO.chunks yields Agent B's MossChunks, already an
            // Iterator<Item = Range> borrowing the FileIO.
            if (canonical_type_name(element_type) == "seq[Range]") {
              traversal = collection_expression;
            } else {
              const auto* vector_element_fact = resolve_vector_element_ownership_fact(s, "");
              if (vector_element_fact && vector_element_fact->access == Effect::Write) {
                write_iteration = true;
              }
              if (!write_iteration) {
                for (const auto* fact : loop_facts) {
                  if ((fact->binding_identity == s.a || ends_with(fact->binding_identity, ":" + s.a)) &&
                      fact->converged_access == Effect::Write) {
                    write_iteration = true;
                    break;
                  }
                }
              }
              if (write_iteration) {
                traversal = "(" + collection_expression + ").iter_mut()";
              } else if (copy_type(element)) {
                traversal += ".copied()";
              }
            }
            o << indent(level) << "for " << s.a << " in " << traversal
              << " {\n";
            auto child_locals = locals;
            auto child_types = types;
            child_locals.insert(s.a);
            child_types[s.a] = element;
            auto saved_write_through = write_through_parameters_;
            if (write_iteration) {
              write_through_parameters_.insert(s.a);
            }
            ++i;
            gen_block(o, ss, i, level + 1, d, current_handler, reply_slot,
                      child_locals, child_types, base, in_handler, in_function, join_assignments,
                      "");
            write_through_parameters_ = std::move(saved_write_through);
            o << indent(level) << "}\n";
            break;
          }

          if (!source_type)
            throw std::runtime_error("internal error: unresolved static iterator source");
          string concrete = canonical_type_name(*source_type);
          const auto iterator_ids = s.iterator_method_spec_ids_by_spec_id.find(
              codegen_body_spec_id_);
          const vector<SpecId>* checked_iterator_ids =
              iterator_ids == s.iterator_method_spec_ids_by_spec_id.end()
                  ? nullptr : &iterator_ids->second;
          const bool custom_iterator = objects_.count(concrete) != 0;
          if (custom_iterator && !checked_iterator_ids)
            throw std::runtime_error(
                "MOSS_INTERNAL_INVARIANT custom iterator lacks checked per-For method SpecIds");
          if (!custom_iterator && checked_iterator_ids)
            throw std::runtime_error(
                "MOSS_INTERNAL_INVARIANT built-in iterator has contradictory custom method SpecIds");
          if (checked_iterator_ids &&
              checked_iterator_ids->size() != 1 &&
              checked_iterator_ids->size() != 2)
            throw std::runtime_error(
                "MOSS_INTERNAL_INVARIANT custom iterator method SpecId sequence has invalid arity");
          const Method* iterator_factory = nullptr;
          const Method* next = nullptr;
          bool direct_iterator = false;
          if (checked_iterator_ids) {
            if (checked_iterator_ids->size() == 1) {
              next = checked_method_body_for_spec_id(
                  checked_iterator_ids->front());
              direct_iterator = true;
              if (next->name != "next" ||
                  canonical_type_name(next->owner) != concrete)
                throw std::runtime_error(
                    "MOSS_INTERNAL_INVARIANT direct iterator SpecId owner/name is contradictory");
            } else {
              iterator_factory = checked_method_body_for_spec_id(
                  checked_iterator_ids->at(0));
              next = checked_method_body_for_spec_id(
                  checked_iterator_ids->at(1));
              if (iterator_factory->name != "iter" || next->name != "next" ||
                  canonical_type_name(iterator_factory->owner) != concrete ||
                  !iterator_factory->return_type ||
                  canonical_type_name(next->owner) !=
                      canonical_type_name(*iterator_factory->return_type))
                throw std::runtime_error(
                    "MOSS_INTERNAL_INVARIANT iter()/next() SpecId sequence owner/name is contradictory");
            }
          }
          string iterator_expression;
          if (direct_iterator && next->receiver_effect == Effect::Consume)
            iterator_expression = expr(source, d, locals, &types);
          else if (direct_iterator)
            iterator_expression = "&mut " + expr(source, d, locals, &types);
          else
            iterator_expression = expr(source, d, locals, &types) + ".iter()";
          o << indent(level) << "let mut __moss_iterator = "
            << iterator_expression << ";\n";
          o << indent(level) << "loop {\n";
          o << indent(level + 1) << "match __moss_iterator.next() {\n";
          o << indent(level + 2) << "Some(" << s.a << ") => {\n";
          auto child_locals = locals;
          auto child_types = types;
          child_locals.insert(s.a);
          string next_type = next && next->return_type
              ? canonical_type_name(*next->return_type) : "option[_]";
          string element = starts_with(next_type, "option[") && ends_with(next_type, "]")
              ? trim(next_type.substr(7, next_type.size() - 8)) : "_value";
          child_types[s.a] = element;
          ++i;
          gen_block(o, ss, i, level + 1, d, current_handler, reply_slot,
                    child_locals, child_types, base, in_handler, in_function, join_assignments,
                    "");
          o << indent(level + 2) << "}\n";
          o << indent(level + 2) << "None => break,\n";
          o << indent(level + 1) << "}\n";
          o << indent(level) << "}\n";
          break;
        }
        case Stmt::Kind::Echo: {
          o << indent(level);
          if (s.args.empty()) o << "println!();\n";
          else {
            o << "println!(\"";
            for (size_t k = 0; k < s.args.size(); ++k) { if (k) o << " "; o << "{}"; }
            o << "\"";
            for (size_t argument = 0; argument < s.args.size(); ++argument) {
              const auto argument_cursor = checked_statement_cursor(
                  s, est::ExprSlot::Argument,
                  static_cast<std::uint32_t>(argument));
              NativeEstCursorScope argument_cursor_scope(
                  active_native_est_cursor_, &argument_cursor);
              const auto* echo_fact = resolve_echo_argument_fact(
                  s, static_cast<std::uint32_t>(argument), "");
              string proj_base, proj_field, idx_b, idx_i;
              const bool is_indexed_member =
                  statement_functional_pipeline_id(s, "", argument) == 0 &&
                  tree_parse_member_access(trim(s.args[argument]), proj_base, proj_field) &&
                  tree_parse_index(proj_base, idx_b, idx_i);
              const bool checked_vector_borrow = echo_fact &&
                  echo_fact->mode ==
                      TypedEchoArgumentModeV1::BorrowedVectorElement;
              string rendered = checked_vector_borrow || is_indexed_member
                  ? read_call_place(s.args[argument], d, locals, &types)
                  : expr(
                      s.args[argument], d, locals, &types,
                      statement_functional_pipeline_id(s, "",
                                                       argument),
                      nullptr,
                      root_ingress_expression_occurrence(
                          s, est::ExprSlot::Argument,
                          static_cast<std::uint32_t>(argument)),
                      &argument_cursor);
              auto type = generated_expr_type(s.args[argument], &types);
              auto resolved = [](const std::optional<string>& t) {
                if (!t) return false;
                const string c = canonical_type_name(*t);
                return !c.empty() && c != "vector" && !internal_semantic_placeholder(c);
              };
              if (!resolved(type)) {
                // e.g. `[[], [1]]`: the backend's local typing cannot see the
                // element type; use the type the checker accepted instead.
                auto checked = s.echo_types_by_spec_id.find(codegen_body_spec_id_);
                if (checked != s.echo_types_by_spec_id.end() &&
                    argument < checked->second.size() &&
                    resolved(checked->second[argument]))
                  type = checked->second[argument];
              }
              // The checker owns the echo display contract (S-B); a Vector
              // formats through the Moss display runtime, never Rust Debug.
              // Anything else without a contract is an internal error here,
              // reported with the checker's code rather than as rustc E0277.
              const string shown = type ? canonical_type_name(*type) : string();
              if (shown == "map" || shown == "queue" || starts_with(shown, "map[") ||
                  starts_with(shown, "queue[") || objects_.count(shown) ||
                  enums_.count(shown) ||
                  (starts_with(shown, "vector[") &&
                   !native_echo_vector_displayable(shown)))
                throw CompileError(s.line, "echo cannot display a value of type '" +
                    shown + "'", "ECHO_DISPLAY_UNSUPPORTED");
              if (shown == "float") {
                rendered = "__moss_ryu::canonical_float_text(" + rendered + ")";
              } else if (starts_with(shown, "vector[")) {
                auto element = native_echo_element(shown.substr(7, shown.size() - 8));
                if (!element) {
                  // The backend saw a partially open element type; use the
                  // complete type the checker accepted for this context.
                auto checked = s.echo_types_by_spec_id.find(codegen_body_spec_id_);
                if (checked != s.echo_types_by_spec_id.end() &&
                      argument < checked->second.size() &&
                      starts_with(checked->second[argument], "vector[")) {
                    const string& full = checked->second[argument];
                    element = native_echo_element(full.substr(7, full.size() - 8));
                  }
                }
                if (!element)
                  throw CompileError(s.line, "internal error: echo of '" + shown +
                      "' has no resolved element type for native display",
                      "ECHO_DISPLAY_UNSUPPORTED");
                rendered = "__moss_display_vector(&(" + rendered + "), &" + *element + ")";
              }
              o << ", " << rendered;
            }
            o << ");\n";
          }
          ++i;
          break;
        }
        case Stmt::Kind::Assign: {
          const auto rhs_cursor = checked_statement_cursor(
              s, est::ExprSlot::RhsValue);
          NativeEstCursorScope rhs_cursor_scope(
              active_native_est_cursor_, &rhs_cursor);
          bool joined_assignment = join_assignments.erase(s.a) != 0;
          if (s.a == "_") {
            o << indent(level) << "let _ = "
              << expr(s.b, d, locals, &types,
                      statement_functional_pipeline_id(
                          s, "", 0), nullptr,
                      root_ingress_expression_occurrence(
                          s, est::ExprSlot::RhsValue, 0))
              << ";\n";
            ++i;
            break;
          }
          if (phase21_option_slots_.count(s.a)) {
            o << indent(level) << "__moss_recovery_slot_" << s.a
              << " = Some(" << expr(s.b, d, locals, &types,
                     statement_functional_pipeline_id(
                         s, "", 0), nullptr,
                     root_ingress_expression_occurrence(
                         s, est::ExprSlot::RhsValue, 0)) << ");\n";
            ++i;
            break;
          }
          if (auto sd = domain_construction(s.b)) {
            const string source_domain = *sd;
            construction_binding_ = s.a;
            remember_domain_instance_binding(s.a, source_domain, s.a);
            auto specialized = specialization_names_.find(*sd + "\n" + s.a);
            if (specialized != specialization_names_.end()) *sd = specialized->second;
            bool existing_binding = locals.count(s.a);
            o << indent(level) << (existing_binding ? "" : "let ") << s.a << " = ";
            emit_domain_construction(o, *sd, s.b, d, locals, &types);
            locals.insert(s.a);
            types[s.a] = *sd;
            ++i;
            break;
          }
          if (auto map_write = resolve_map_write_through(s, "")) {
            o << indent(level) << emit_checked_map_write_through(
                *map_write, s, d, locals, &types,
                statement_functional_pipeline_id(s, "", 0))
              << ";\n";
            ++i;
            break;
          }
          string lhs;
          string lhs_base, lhs_index;
          string index_temporary;
          if (tree_parse_index(s.a, lhs_base, lhs_index)) {
            auto indexed_base_type = generated_expr_type(lhs_base, &types);
            const bool indexed_map = indexed_base_type &&
                (canonical_type_name(*indexed_base_type) == "map" ||
                 starts_with(canonical_type_name(*indexed_base_type), "map["));
            string root_id = lhs_base;
            auto dot_pos = root_id.find('.');
            if (dot_pos != string::npos) root_id = root_id.substr(0, dot_pos);
            bool conflicts_with_base = !root_id.empty() && tree_expression_mentions_identifier(lhs_index, root_id);
            bool reads_sibling = false;
            if (view_method_ && d && is_pure_state_read_expression(lhs_index)) {
              for (const auto& field : d->state) {
                if (field.name != lhs_base &&
                    tree_expression_mentions_identifier(lhs_index, field.name)) {
                  reads_sibling = true;
                  break;
                }
              }
            }
            if (!indexed_map && (conflicts_with_base || reads_sibling)) {
              index_temporary = "__moss_index_" +
                  std::to_string(call_argument_temp_++);
              o << indent(level) << "let " << index_temporary << " = "
                << expr(lhs_index, d, locals, &types) << ";\n";
            }
          }
          if (tree_parse_index(s.a, lhs_base, lhs_index)) {
            bool string_index = tree_literal_class(lhs_index) ==
                est::LiteralClass::String;
            auto base_type = generated_expr_type(lhs_base, &types);
            bool map_index = base_type &&
                (canonical_type_name(*base_type) == "map" ||
                 starts_with(canonical_type_name(*base_type), "map["));
            string ir = !index_temporary.empty() ? index_temporary :
                (string_index || map_index ? lhs_index :
                 expr(lhs_index, d, locals, &types));
            if (!string_index && !map_index) ir = "(" + ir + ") as usize";
            lhs = "(" + write_call_place(lhs_base, d, locals, &types) + ")[" + ir + "]";
          }
          else if (joined_assignment && tree_plain_identifier(s.a))
            // A checker-proved first definition at a control-flow join is an
            // assignment to the predeclared join slot in each branch. It is
            // not a write through an alias established by a sibling branch.
            lhs = rust_identifier(s.a);
          else lhs = write_call_place(s.a, d, locals, &types);
          bool state_field = d && std::any_of(d->state.begin(), d->state.end(),
                                              [&](const Field& field) { return field.name == s.a; });
          if (tree_plain_identifier(s.a) && !locals.count(s.a) && !state_field) {
            if (statement_type.empty())
              throw std::runtime_error(
                  "MOSS_INTERNAL_INVARIANT missing checked statement type line=" +
                  std::to_string(s.line) + " SpecId=" + std::to_string(codegen_body_spec_id_));
            backend_comment(o, (base + level) * 4,
                            "LOCAL assignment: introduce an inferred Moss binding");
            string annotation;
            string inferred_map_type;
            if (staged_abi_v10() && tree_empty_constructor_named(s.b, "Map") &&
                !starts_with(statement_type, "map[")) {
              // A bare Map constructor has no intrinsic element types. The
              // checked body may close it at a later indexed assignment;
              // preserve that evidence before emitting its Rust declaration.
              for (size_t next = i + 1; next < ss.size(); ++next) {
                string map_base, index_expression;
                if (ss[next].kind != Stmt::Kind::Assign ||
                    !tree_parse_index(ss[next].a, map_base, index_expression) ||
                    map_base != s.a) continue;
                auto key_type = generated_expr_type(index_expression, &types);
                auto value_type = generated_expr_type(ss[next].b, &types);
                if (key_type && value_type && shared_protocol_map_key(*key_type)) {
                  inferred_map_type = "map[" + canonical_type_name(*key_type) +
                      "," + canonical_type_name(*value_type) + "]";
                  break;
                }
              }
            }
            if (starts_with(statement_type, "map[") && ends_with(statement_type, "]"))
              annotation = rust_type(statement_type);
            else if (!inferred_map_type.empty()) annotation = rust_type(inferred_map_type);
            else if (starts_with(canonical_type_name(statement_type), "queue[") &&
                     ends_with(canonical_type_name(statement_type), "]") &&
                     canonical_type_name(statement_type).find("never") != string::npos)
              annotation = rust_type(statement_type);
            else if (canonical_type_name(statement_type) == "float")
              annotation = "f64";
            const auto* take_fact = resolve_take_fact(s, "");
            const auto* ownership_fact = take_fact ? nullptr : resolve_local_ownership_fact(s, "");
            if (take_fact) {
              string rhs_val = emit_take_expression(*take_fact, s, d, locals, &types);
              o << indent(level) << "let mut " << rust_identifier(s.a) << " = " << rhs_val << ";\n";
            } else if (ownership_fact) {
              string p_b, p_i;
              bool is_index_source = tree_parse_index(s.b, p_b, p_i);
              string rhs_val = (is_index_source && ownership_fact->access == Effect::Write)
                  ? write_call_place(s.b, d, locals, &types)
                  : (is_index_source && ownership_fact->access == Effect::Read)
                      ? read_call_place(s.b, d, locals, &types)
                      : [&] {
                          const bool prior = checked_map_get_write_through_;
                          checked_map_get_write_through_ =
                              ownership_fact->access == Effect::Write;
                          const auto rhs_cursor = checked_statement_cursor(
                              s, est::ExprSlot::RhsValue);
                          string rendered = expr(s.b, d, locals, &types,
                              statement_functional_pipeline_id(
                                  s, "", 0), nullptr,
                              root_ingress_expression_occurrence(
                                  s, est::ExprSlot::RhsValue, 0),
                              &rhs_cursor);
                          checked_map_get_write_through_ = prior;
                          return rendered;
                        }();
              const auto rhs_name = tree_identifier_name(s.b);
              bool rhs_is_borrow = rhs_name && borrowed_function_parameters_.count(*rhs_name) && !write_through_parameters_.count(*rhs_name);
              bool mutable_decl = (s.kind == Stmt::Kind::Var) || (ownership_fact->access != Effect::Read);
              if (ownership_fact->access == Effect::Read) {
                if (rhs_is_borrow) {
                  o << indent(level) << "let " << (mutable_decl ? "mut " : "") << rust_identifier(s.a) << " = " << rhs_val << ";\n";
                } else {
                  o << indent(level) << "let " << (mutable_decl ? "mut " : "") << rust_identifier(s.a) << " = &(" << rhs_val << ");\n";
                }
              } else if (ownership_fact->access == Effect::Write) {
                if (rhs_is_borrow) {
                  o << indent(level) << "let mut " << rust_identifier(s.a) << " = " << rhs_val << ";\n";
                } else {
                  o << indent(level) << "let mut " << rust_identifier(s.a) << " = &mut (" << rhs_val << ");\n";
                }
              } else {
                o << indent(level) << "let mut " << rust_identifier(s.a) << " = " << rhs_val << ";\n";
              }
            } else {
              const auto rhs_cursor = checked_statement_cursor(
                  s, est::ExprSlot::RhsValue);
              o << indent(level) << "let mut " << rust_identifier(s.a);
              if (!annotation.empty()) o << ": " << annotation;
              o << " = "
                << expr(s.b, d, locals, &types,
                        statement_functional_pipeline_id(
                            s, "", 0), nullptr,
                        root_ingress_expression_occurrence(
                            s, est::ExprSlot::RhsValue, 0),
                        &rhs_cursor)
                << ";\n";
            }
            locals.insert(s.a);
            if (ownership_fact && ownership_fact->access == Effect::Write)
              write_through_parameters_.insert(s.a);
            types[s.a] = take_fact ? canonical_type_name(take_fact->terminal_type)
                : !inferred_map_type.empty() ? inferred_map_type
                : statement_type;
            const auto source_name = tree_identifier_name(s.b);
            auto source_binding = source_name
                ? domain_instance_bindings_.find(*source_name)
                : domain_instance_bindings_.end();
            if (source_binding != domain_instance_bindings_.end())
              remember_domain_instance_binding(
                  s.a, source_binding->second.source_domain,
                  source_binding->second.instance);
            else
              forget_domain_instance_binding(s.a);
            if (source_name && view_parameters_.count(*source_name))
              view_parameters_.insert(s.a);
          } else {
            string mb, mi;
            bool map_assignment = tree_parse_index(s.a, mb, mi);
            auto map_assignment_type = map_assignment
                ? generated_expr_type(mb, &types) : std::optional<string>{};
            if (map_assignment && map_assignment_type &&
                (canonical_type_name(*map_assignment_type) == "map" ||
                 starts_with(canonical_type_name(*map_assignment_type), "map["))) {
              const bool string_key = tree_literal_class(mi) ==
                  est::LiteralClass::String;
              const TypedCopyFactV1* explicit_key_copy = nullptr;
              string explicit_key_copy_argument;
              if (phase21_codegen_statement_ &&
                  active_functional_expression_handles_) {
                const Program& checked_program = semantic_program_
                    ? *semantic_program_ : p_;
                for (const auto& handle : *active_functional_expression_handles_) {
                  if (handle.slot != est::ExprSlot::LhsPlace ||
                      handle.arena_index >= checked_program.expression_arenas.size() ||
                      !checked_program.expression_arenas[handle.arena_index])
                    continue;
                  est::assert_fresh(handle, phase21_codegen_statement_->a);
                  const auto& arena = *checked_program.expression_arenas[handle.arena_index];
                  if (handle.expr >= arena.size()) continue;
                  auto place_id = est::unparenthesized(arena, handle.expr);
                  if (place_id >= arena.size()) continue;
                  const auto* place = std::get_if<est::Index>(&arena.at(place_id));
                  if (!place) continue;
                  auto key_id = est::unparenthesized(arena, place->index);
                  if (key_id >= arena.size()) continue;
                  const auto* call = std::get_if<est::Call>(&arena.at(key_id));
                  if (!call || call->arguments.size() != 1) continue;
                  const auto* callee = std::get_if<est::Identifier>(
                      &arena.at(call->callee));
                  if (!callee || callee->name != "copy") continue;
                  if (explicit_key_copy)
                    throw std::runtime_error(
                        "MOSS_INTERNAL_INVARIANT multiple checked copies in Map key place");
                  explicit_key_copy = &resolve_copy_fact(
                      handle.arena_index, key_id);
                  const auto argument_id = call->arguments.front();
                  if (argument_id >= arena.size())
                    throw std::runtime_error(
                        "MOSS_INTERNAL_INVARIANT checked Map key copy argument is invalid");
                  const auto argument_span = arena.span(argument_id);
                  if (argument_span.source_begin > argument_span.source_end ||
                      argument_span.source_end > phase21_codegen_statement_->a.size())
                    throw std::runtime_error(
                        "MOSS_INTERNAL_INVARIANT checked Map key copy span is invalid");
                  explicit_key_copy_argument = trim(
                      phase21_codegen_statement_->a.substr(
                          argument_span.source_begin,
                          argument_span.source_end - argument_span.source_begin));
                }
              }
              string key;
              if (explicit_key_copy) {
                const auto source_type = generated_expr_type(
                    explicit_key_copy_argument, &types);
                if (!source_type)
                  throw std::runtime_error(
                      "MOSS_INTERNAL_INVARIANT checked Map key copy source type is unavailable");
                key = lower_fact_authorized_copy(
                    *explicit_key_copy, explicit_key_copy_argument,
                    *source_type, d, locals, &types);
              } else {
                key = string_key
                    ? "MossStr::from(" + mi + ")" : expr(mi, d, locals, &types);
              }
              auto key_type = generated_expr_type(mi, &types);
              if (!explicit_key_copy && key_type &&
                  shared_nominal_map_key(*key_type) &&
                  is_object_view(mi, d, locals, &types))
                key = "(" + key + ")." + access_value_method(*key_type) + "()";
              else if (key_type && !copy_type(*key_type) &&
                  !string_key && !explicit_key_copy)
                key = "(" + key + ").clone()";
              string inserted = expr(s.b, d, locals, &types,
                  statement_functional_pipeline_id(s, "", 0),
                  nullptr, root_ingress_expression_occurrence(
                      s, est::ExprSlot::RhsValue, 0));
              const auto source_binding = tree_identifier_name(s.b);
              if (source_binding &&
                  borrowed_function_parameters_.count(*source_binding))
                inserted = "(" + inserted + ").clone()";
              if (auto shared_key =
                      shared_protocol_map_key_from_type(*map_assignment_type)) {
                o << indent(level) << "{ let __moss_key = " << key
                  << "; (" << write_call_place(mb, d, locals, &types)
                  << ").insert_with(" << shared_map_hash(*shared_key, "__moss_key")
                  << ", __moss_key, " << inserted << ", |a, b| "
                  << shared_map_equal(*shared_key, "a", "b") << "); }\n";
              } else {
                o << indent(level) << "(" << write_call_place(mb, d, locals, &types)
                  << ").insert(" << key << ", "
                  << inserted
                  << ");\n";
              }
            }
            else if (is_object_view(s.a, d, locals, &types) && (!tree_plain_identifier(s.a) || !locals.count(s.a))) {
              string assigned = expr(s.b, d, locals, &types,
                  statement_functional_pipeline_id(s, "", 0),
                  nullptr, root_ingress_expression_occurrence(
                      s, est::ExprSlot::RhsValue, 0));
              o << indent(level) << "{ let __moss_assigned = " << assigned << "; "
                << "(" << lhs << ").__moss_replace(__moss_assigned); }\n";
            }
            else {
              const auto* transfer_fact =
                  resolve_local_transfer_fact(s, "");
              const auto* take_fact = transfer_fact
                  ? nullptr : resolve_take_fact(s, "");
              const auto* ownership_fact = (take_fact || transfer_fact)
                  ? nullptr : resolve_local_ownership_fact(s, "");
              string assigned;
              vector<string> concat_operands;
              std::function<void(string)> collect_concat = [&](string source) {
                source = tree_strip_expression_grouping(std::move(source));
                auto binary = tree_generated_binary(source, {"+"});
                auto left_type = binary
                    ? generated_expr_type(binary->left, &types) : std::nullopt;
                auto right_type = binary
                    ? generated_expr_type(binary->right, &types) : std::nullopt;
                if (binary && left_type && right_type &&
                    canonical_type_name(*left_type) == "string" &&
                    canonical_type_name(*right_type) == "string") {
                  collect_concat(binary->left);
                  collect_concat(binary->right);
                } else {
                  concat_operands.push_back(std::move(source));
                }
              };
              collect_concat(s.b);
              bool reuse_string_buffer = tree_plain_identifier(s.a) &&
                  locals.count(s.a) && concat_operands.size() > 1 &&
                  tree_expressions_equal(concat_operands.front(), s.a) &&
                  canonical_type_name(types.count(s.a) ? types.at(s.a) : "") ==
                      "string";
              for (size_t operand = 1;
                   reuse_string_buffer && operand < concat_operands.size();
                   ++operand)
                if (tree_expression_mentions_identifier(concat_operands[operand], s.a))
                  reuse_string_buffer = false;
              if (reuse_string_buffer) {
                std::ostringstream reused;
                reused << "{ ";
                for (size_t operand = 1; operand < concat_operands.size(); ++operand) {
                  const string part = trim(concat_operands[operand]);
                  const bool literal = tree_literal_class(part) ==
                      est::LiteralClass::String;
                  reused << "let __moss_append_" << operand << " = ";
                  if (literal) reused << part << "; ";
                  else
                    reused << "&(" << expr(concat_operands[operand], d, locals, &types)
                           << "); ";
                }
                string place = write_call_place(s.a, d, locals, &types);
                reused << "(" << place << ").reserve(";
                for (size_t operand = 1; operand < concat_operands.size(); ++operand) {
                  if (operand > 1) reused << ".saturating_add(";
                  reused << "__moss_append_" << operand << ".len()";
                }
                for (size_t operand = 2; operand < concat_operands.size(); ++operand)
                  reused << ")";
                reused << "); ";
                for (size_t operand = 1; operand < concat_operands.size(); ++operand)
                  reused << "(" << place << ").push_str(__moss_append_"
                         << operand
                         << (tree_literal_class(concat_operands[operand]) ==
                                 est::LiteralClass::String
                                 ? "); " : ".as_str()); ");
                reused << "}";
                assigned = reused.str();
              } else {
                const bool prior = checked_map_get_write_through_;
                checked_map_get_write_through_ =
                    ownership_fact && ownership_fact->access == Effect::Write;
                assigned = expr(s.b, d, locals, &types,
                    statement_functional_pipeline_id(s, "", 0),
                    nullptr, root_ingress_expression_occurrence(
                        s, est::ExprSlot::RhsValue, 0));
                checked_map_get_write_through_ = prior;
              }
              const auto source_binding = tree_identifier_name(s.b);
              if (map_assignment && source_binding &&
                  borrowed_function_parameters_.count(*source_binding))
                assigned = "(" + assigned + ").clone()";
              if (take_fact) {
                assigned = emit_take_expression(*take_fact, s, d, locals, &types);
              } else if (ownership_fact) {
                const auto rhs_name = tree_identifier_name(s.b);
                bool rhs_is_borrow = rhs_name && borrowed_function_parameters_.count(*rhs_name) && !write_through_parameters_.count(*rhs_name);
                string p_b, p_i;
                bool is_index_source = tree_parse_index(s.b, p_b, p_i);
                if (ownership_fact->access == Effect::Read) {
                  if (!rhs_is_borrow) assigned = "&(" + (is_index_source ? read_call_place(s.b, d, locals, &types) : assigned) + ")";
                } else if (ownership_fact->access == Effect::Write) {
                  if (!rhs_is_borrow) assigned = "&mut (" + (is_index_source ? write_call_place(s.b, d, locals, &types) : assigned) + ")";
                  write_through_parameters_.insert(s.a);
                }
              }
              if (reuse_string_buffer)
                o << indent(level) << assigned << ";\n";
              else {
                bool shadow_killed_definition = ownership_fact &&
                    !ownership_fact->killed_definition_identity.empty() &&
                    tree_plain_identifier(s.a) && !joined_assignment && level == 0;
                if (shadow_killed_definition) {
                  bool mutable_decl = (ownership_fact->access != Effect::Read);
                  o << indent(level) << "let " << (mutable_decl ? "mut " : "")
                    << rust_identifier(s.a) << " = " << assigned << ";\n";
                } else {
                  string target_lhs = (tree_plain_identifier(s.a) && locals.count(s.a) &&
                      !write_through_parameters_.count(s.a) &&
                      !mutable_match_primitive_values_.count(s.a))
                      ? rust_identifier(s.a) : lhs;
                  o << indent(level) << target_lhs << " = " << assigned << ";\n";
                }
              }
              if (source_binding && view_parameters_.count(*source_binding))
                view_parameters_.insert(s.a);
            }
            if (tree_plain_identifier(s.a)) forget_domain_instance_binding(s.a);
          }
          ++i;
          break;
        }
        case Stmt::Kind::Call: {
          auto checked_receiver = s.receiver_types_by_spec_id.find(codegen_body_spec_id_);
          if (checked_receiver != s.receiver_types_by_spec_id.end() && types.count(s.a))
            types[s.a] = checked_receiver->second;
          string statement_receiver_type = !s.b.empty()
              ? canonical_type_name(generated_expr_type(s.a, &types).value_or("")) : "";
          const string& method_name = s.b;
          if (!method_name.empty() &&
              (statement_receiver_type == "string" ||
               statement_receiver_type == "vector" ||
               starts_with(statement_receiver_type, "vector[") ||
               statement_receiver_type == "queue" ||
               starts_with(statement_receiver_type, "queue[") ||
               (staged_abi_v10() &&
                (statement_receiver_type == "set" ||
                 starts_with(statement_receiver_type, "set["))) ||
               (s.b == "delete" && (statement_receiver_type == "map" ||
                                    starts_with(statement_receiver_type, "map["))))) {
            if (method_name == "push" && s.args.size() == 1) {
              const est::Handle* arg_handle = nullptr;
              for (const auto& handle : s.expression_handles) {
                if (handle.slot == est::ExprSlot::Argument && handle.argument_index == 0) {
                  arg_handle = &handle;
                  break;
                }
              }
              const est::Arena* arena = nullptr;
              if (arg_handle && arg_handle->arena_index < p_.expression_arenas.size() &&
                  p_.expression_arenas[arg_handle->arena_index]) {
                arena = p_.expression_arenas[arg_handle->arena_index].get();
              } else if (arg_handle && arg_handle->arena_index == 0 && p_.expression_arena) {
                arena = p_.expression_arena.get();
              }
              const est::SemanticFacts* arg_facts = nullptr;
              if (arg_handle && arena) {
                arg_facts = arena->facts(arg_handle->expr,
                                         codegen_body_spec_id_);
              }
              if (arg_facts && is_primitive_copy_type(arg_facts->type)) {
                string arg_val = expr(s.args[0], d, locals, &types);
                string temp_var = "__moss_push_arg_" + std::to_string(call_argument_temp_++);
                auto recv_type = generated_expr_type(s.a, &types);
                string push_method = (recv_type && starts_with(canonical_type_name(*recv_type), "queue"))
                    ? "push_back" : "push";
                o << indent(level) << "{ let " << temp_var << " = " << arg_val << "; ("
                  << write_call_place(s.a, d, locals, &types) << ")." << push_method << "(" << temp_var << "); }\n";
                ++i;
                break;
              }
            }
            string call = s.a + "." + s.b + "(";
            for (size_t argument = 0; argument < s.args.size(); ++argument) {
              if (argument) call += ", ";
              call += s.args[argument];
            }
            call += ")";
            o << indent(level) << expr(call, d, locals, &types) << ";\n";
            ++i;
            break;
          }
          if (s.b.empty() && s.a == "assert") {
            o << indent(level) << "if !("
              << expr(s.args.front(), d, locals, &types,
                      statement_functional_pipeline_id(
                          s, "", 0))
              << ") { panic!(\"{}\", "
              << rust_string_literal(
                     "Moss assertion failed at line " +
                     std::to_string(s.line) + ": " + s.text)
              << "); }\n";
            ++i;
            break;
          }
          if (s.b.empty() && s.a == "assertEqual") {
            size_t id = assertion_temp_++;
            string actual = "__moss_assert_actual_" + std::to_string(id);
            string expected = "__moss_assert_expected_" + std::to_string(id);
            const auto* actual_indexed_read =
                resolve_indexed_call_argument_read_fact(
                    s, 0, "");
            const auto* actual_projected_read =
                resolve_projected_call_argument_read_fact(
                    s, 0, "");
            const auto* expected_indexed_read = s.args.size() > 1
                ? resolve_indexed_call_argument_read_fact(
                      s, 1, "")
                : nullptr;
            const auto* expected_projected_read = s.args.size() > 1
                ? resolve_projected_call_argument_read_fact(
                      s, 1, "")
                : nullptr;
            o << indent(level) << "let " << actual << " = &("
              << (actual_indexed_read || actual_projected_read
                      ? read_call_place(s.args[0], d, locals, &types)
                      : expr(s.args[0], d, locals, &types,
                             statement_functional_pipeline_id(
                                 s, "", 0)))
              << ");\n";
            o << indent(level) << "let " << expected << " = &("
              << (expected_indexed_read || expected_projected_read
                      ? read_call_place(s.args[1], d, locals, &types)
                      : expr(s.args[1], d, locals, &types,
                             statement_functional_pipeline_id(
                                 s, "", 1)))
              << ");\n";
            string escaped_text;
            for (char ch : s.text) {
              if (ch == '{') escaped_text += "{{";
              else if (ch == '}') escaped_text += "}}";
              else escaped_text += ch;
            }
            string message = "Moss assertion failed at line " +
                std::to_string(s.line) + ": " + escaped_text +
                "; actual={:?}, expected={:?}";
            string cond = actual + " != " + expected;
            o << indent(level) << "if " << cond
              << " { panic!(" << rust_string_literal(message) << ", "
              << actual << ", " << expected << "); }\n";
            ++i;
            break;
          }
          if (s.b.empty() && s.a == "sort" && !functions_.count(s.a) &&
              (s.args.size() == 1 || s.args.size() == 2)) {
            o << indent(level) << sort_call_rust(s.args, d, locals, &types) << ";\n";
            ++i;
            break;
          }
          string function_name = resolved_callable_identity(s.a, &types);
          const Function* function_target = nullptr;
          if (starts_with(function_name, "callable-v1:")) {
            function_target = find_function_by_callable_id(function_name);
          } else {
            auto it = functions_.find(function_name);
            if (it != functions_.end()) function_target = it->second;
          }
          bool known_function = (function_target != nullptr);
          if (s.b.empty() && known_function && !benchmark_body_) {
            NativeEstCursor call_cursor;
            if (function_target->static_dispatch)
              call_cursor = checked_statement_cursor(
                  s, est::ExprSlot::Expression);
            o << indent(level)
              << render_known_function_call(*function_target, function_target->name, s.args,
                                           d, locals, &types,
                                           function_target->static_dispatch
                                               ? &call_cursor
                                               : nullptr)
              << ";\n";
            ++i;
            break;
          }
          bool implicit_method = in_function && d && objects_.count(d->name) &&
              s.b.empty() && !known_function;
          auto call_receiver_type = implicit_method
              ? std::optional<string>(d->name)
              : !s.b.empty() ? generated_expr_type(s.a, &types)
                             : std::optional<string>{};
          const Method* call_method = nullptr;
          if (call_receiver_type && (!s.b.empty() || implicit_method))
            call_method = checked_method_for_occurrence(
                implicit_method ? s.a : s.b, active_native_est_cursor_);
          const auto external_method_module = call_method && call_receiver_type
              ? phase21_external_module_for_symbol(
                    canonical_type_name(*call_receiver_type))
              : std::optional<string>{};
          const bool standard_write_bridge = external_method_module &&
              std::any_of(call_method->params.begin(), call_method->params.end(),
                  [&](const Param& parameter) {
                    const size_t index = static_cast<size_t>(
                        &parameter - call_method->params.data());
                    return !standard_error_variants(canonical_type_name(
                        parameter.type)).empty() &&
                        method_effect(*call_method, index) == Effect::Write;
                  });
          vector<string> argument_temporaries(s.args.size());
          if (call_method && call_method->receiver_effect != Effect::Read) {
            for (size_t k = 0; k < s.args.size(); ++k) {
              bool same_receiver_mutating_argument = false;
              if (!s.b.empty()) {
                string nested_receiver, nested_method;
                vector<string> nested_args;
                if (tree_parse_member_call(s.args[k], nested_receiver,
                                           nested_method, nested_args) &&
                    tree_expressions_equal(nested_receiver, s.a)) {
                  auto nested_cursor = native_call_argument_cursor(
                      active_native_est_cursor_, k);
                  const auto* nested_target = nested_cursor
                      ? checked_method_for_occurrence(nested_method,
                                                      &*nested_cursor)
                      : nullptr;
                  same_receiver_mutating_argument = nested_target &&
                      nested_target->receiver_effect != Effect::Read;
                }
              } else if (implicit_method) {
                string nested_name;
                vector<string> nested_args;
                if (tree_parse_simple_call(s.args[k], nested_name, nested_args)) {
                  auto nested_cursor = native_call_argument_cursor(
                      active_native_est_cursor_, k);
                  const auto* nested_target = nested_cursor
                      ? checked_method_for_occurrence(nested_name,
                                                      &*nested_cursor)
                      : nullptr;
                  same_receiver_mutating_argument = nested_target &&
                      nested_target->receiver_effect != Effect::Read;
                }
              }
              if (!same_receiver_mutating_argument) continue;
              argument_temporaries[k] = "__moss_call_argument_" +
                  std::to_string(call_argument_temp_++);
              string temporary_type =
                  generated_expr_type(s.args[k], &types).value_or("_value");
              o << indent(level) << "let " << argument_temporaries[k] << " = "
                << expr(s.args[k], d, locals, &types,
                        statement_functional_pipeline_id(
                            s, "", k))
                << ";\n";
              locals.insert(argument_temporaries[k]);
              types[argument_temporaries[k]] = temporary_type;
            }
          }
          string method_receiver_expression = (implicit_method ? "self." : "") +
            (s.b.empty() && known_function
                    ? viewed_function_name(function_target->name, s.args, d, locals, &types)
                    : !s.b.empty() ? method_receiver_place(s.a, s.b, s.args, d, locals, &types)
                    : expr(s.a, d, locals, &types));
          std::ostringstream bridge_before, bridge_after, call_output;
          call_output << (standard_write_bridge ? "__moss_bridge_receiver" :
              method_receiver_expression);
          if (!s.b.empty()) {
            string method = s.b;
            auto it = types.find(s.a);
            if (it != types.end() && collection_named(parsed_type(it->second), {"queue"}))
              method = method == "push" ? "push_back" : method == "pop" ? "pop_front" : method;
            call_output << "." << viewed_method_name(s.a, method, s.args, d, locals, &types);
          }
          call_output << "(";
          size_t emitted_arguments = 0;
          for (size_t k = 0; k < s.args.size(); ++k) {
            bool compile_time_callable = known_function &&
                is_compile_time_callable(generated_expr_type(s.args[k], &types).value_or(""));
            if (compile_time_callable) continue;
            if (emitted_arguments++) call_output << ", ";
            if (benchmark_body_) call_output << "std::hint::black_box(";
            string argument_expression = argument_temporaries[k].empty()
                ? s.args[k] : argument_temporaries[k];
            if (known_function) {
              call_output << function_call_argument(*function_target, k, argument_expression, d,
                                          locals, &types,
                                          statement_functional_pipeline_id(
                                              s, "", k),
                                          call_specialization(*function_target,
                                                              s.args, &types,
                                                              active_native_est_cursor_));
            } else if (!s.b.empty() || implicit_method) {
              if (call_method) {
                string rendered = method_call_argument(
                    *call_method, k, argument_expression, d, locals, &types,
                    statement_functional_pipeline_id(
                        s, "", k));
                bool standard_write_argument = false;
                if (external_method_module && k < call_method->params.size() &&
                    !standard_error_variants(canonical_type_name(
                        call_method->params[k].type)).empty()) {
                  const string local_type =
                      canonical_type_name(call_method->params[k].type);
                  const string external_type = tooling_name("moss_" +
                      *external_method_module) + "::" + local_type;
                  const Effect effect = method_effect(*call_method, k);
                  if (effect == Effect::Write) {
                    standard_write_argument = true;
                    const string prefix = "__moss_standard_write_" +
                        std::to_string(call_argument_temp_++);
                    const string local_ref = prefix + "_local";
                    const string provider_value = prefix + "_provider";
                    bridge_before << "let " << local_ref << " = " << rendered
                      << "; let mut " << provider_value << " = "
                      << external_type << "::from((*" << local_ref
                      << ").clone()); ";
                    bridge_after << "*" << local_ref << " = " << local_type
                      << "::from(" << provider_value << "); ";
                    rendered = "&mut " + provider_value;
                  } else
                    rendered = effect == Effect::Read
                        ? "&" + external_type + "::from((*(" + rendered +
                            ")).clone())"
                        : external_type + "::from(" + rendered + ")";
                }
                if (standard_write_bridge && !standard_write_argument) {
                  const string staged = "__moss_call_argument_" +
                      std::to_string(call_argument_temp_++);
                  bridge_before << "let " << staged << " = " << rendered
                    << "; ";
                  rendered = staged;
                }
                call_output << rendered;
              } else
                call_output << expr(argument_expression, d, locals, &types,
                          statement_functional_pipeline_id(
                              s, "", k));
            } else {
              call_output << expr(s.args[k], d, locals, &types,
                        statement_functional_pipeline_id(
                            s, "", k));
            }
            if (benchmark_body_) call_output << ")";
          }
          call_output << ")";
          string lowered_call = call_output.str();
          if (standard_write_bridge) {
            string receiver_binding = call_method->receiver_effect == Effect::Consume
                ? method_receiver_expression
                : (call_method->receiver_effect == Effect::Write ? "&mut (" :
                    "&(") + method_receiver_expression + ")";
            lowered_call = "{ let __moss_bridge_receiver = " +
                receiver_binding + "; " + bridge_before.str() +
                "let __moss_bridge_result = " + lowered_call + "; " +
                bridge_after.str() + "__moss_bridge_result }";
          }
          if (call_method && !call_method->observable_effects.raise_set.empty()) {
            if (!phase21_result_context_)
              throw std::runtime_error(
                  "internal error: checked raising method statement lacks a Phase 21 outcome context");
            lowered_call = phase21_propagate(lowered_call);
          }
          o << indent(level);
          if (benchmark_body_) o << "std::hint::black_box(";
          o << lowered_call;
          auto receiver_type = !s.b.empty()
              ? generated_expr_type(s.a, &types) : std::optional<string>{};
          if (!s.b.empty() && s.b == "pop" && receiver_type &&
              (starts_with(canonical_type_name(*receiver_type), "vector[") ||
               starts_with(canonical_type_name(*receiver_type), "queue[")))
            o << ".unwrap_or_else(|| std::process::abort())";
          if (benchmark_body_) o << ")";
          o << ";\n";
          ++i;
          break;
        }
        case Stmt::Kind::Let:
        case Stmt::Kind::Var: {
          if (statement_type.empty())
            throw std::runtime_error(
                "MOSS_INTERNAL_INVARIANT missing checked statement type line=" +
                std::to_string(s.line) + " SpecId=" + std::to_string(codegen_body_spec_id_));
          bool joined_assignment = join_assignments.erase(s.a) != 0;
          auto sd = domain_construction(s.b);
          if (sd) {
            const string source_domain = *sd;
            construction_binding_ = s.a;
            remember_domain_instance_binding(s.a, source_domain, s.a);
            auto specialized = specialization_names_.find(*sd + "\n" + s.a);
            if (specialized != specialization_names_.end()) *sd = specialized->second;
            o << indent(level);
            if (!joined_assignment)
              o << "let " << (s.kind == Stmt::Kind::Var ? "mut " : "");
            o << rust_identifier(s.a) << " = ";
            emit_domain_construction(o, *sd, s.b, d, locals, &types);
            types[s.a] = *sd;
          } else {
            const auto source_name = tree_identifier_name(s.b);
            auto source = source_name ? types.find(*source_name) : types.end();
            bool domain_capability = source != types.end() && domains_.count(source->second);
            string inferred_map_type;
            if (staged_abi_v10() && tree_empty_constructor_named(s.b, "Map") &&
                !starts_with(statement_type, "map[")) {
              for (size_t next = i + 1; next < ss.size(); ++next) {
                string map_base, index_expression;
                if (ss[next].kind != Stmt::Kind::Assign ||
                    !tree_parse_index(ss[next].a, map_base, index_expression) ||
                    map_base != s.a) continue;
                auto key_type = generated_expr_type(index_expression, &types);
                auto value_type = generated_expr_type(ss[next].b, &types);
                if (key_type && value_type && shared_protocol_map_key(*key_type)) {
                  inferred_map_type = "map[" + canonical_type_name(*key_type) +
                      "," + canonical_type_name(*value_type) + "]";
                  break;
                }
              }
            }
            const auto* take_fact = resolve_take_fact(s, "");
            const auto* ownership_fact = take_fact ? nullptr : resolve_local_ownership_fact(s, "");
            o << indent(level);
            if (!joined_assignment) {
              bool mutable_decl = (s.kind == Stmt::Kind::Var) || (take_fact != nullptr);
              if (ownership_fact && ownership_fact->access == Effect::Write) {
                mutable_decl = true;
              }
              o << "let " << (mutable_decl ? "mut " : "");
            }
            bool is_killed_by_borrow = false;
            if (p_.typed_semantics && s.kind == Stmt::Kind::Var && !ownership_fact) {
              for (const auto& fact : p_.typed_semantics->local_ownership_facts) {
                const std::string& killed_id = fact.killed_definition_identity;
                if (!killed_id.empty() && ends_with(killed_id, ":" + s.a) &&
                    (fact.access == Effect::Read || fact.access == Effect::Write)) {
                  is_killed_by_borrow = true;
                  break;
                }
              }
              if (!is_killed_by_borrow) {
                for (const auto& ex_fact : p_.typed_semantics->exceptional_ownership_facts) {
                  for (const auto& bs : ex_fact.state_at_raise) {
                    if (bs.name == s.a && (!bs.borrow_source_binding_identities.empty() || !bs.borrow_storage_paths.empty())) {
                      is_killed_by_borrow = true;
                      break;
                    }
                  }
                  if (is_killed_by_borrow) break;
                }
              }
            }
            o << rust_identifier(s.a);
            if (is_killed_by_borrow) {
              o << ";\n";
            } else if (take_fact) {
              o << " = " << emit_take_expression(*take_fact, s, d, locals, &types) << ";\n";
            } else {
              if (!joined_assignment && !domain_capability && !ownership_fact &&
                  (canonical_type_name(statement_type) == "float" ||
                   canonical_type_name(generated_expr_type(s.b, &types).value_or("")) == "float"))
                o << ": f64";
              else if (!joined_assignment && !domain_capability && !ownership_fact &&
                       (starts_with(statement_type, "map[") || !inferred_map_type.empty()))
                o << ": " << rust_type(!inferred_map_type.empty()
                    ? inferred_map_type : statement_type);
              else if (!joined_assignment && !domain_capability && !ownership_fact &&
                       starts_with(canonical_type_name(statement_type), "queue[") &&
                       ends_with(canonical_type_name(statement_type), "]") &&
                       canonical_type_name(statement_type).find("never") != string::npos)
                o << ": " << rust_type(statement_type);
              string rhs_val = expr(s.b, d, locals, &types,
                                    statement_functional_pipeline_id(
                                        s, "", 0), nullptr,
                                    root_ingress_expression_occurrence(
                                        s, est::ExprSlot::RhsValue, 0));
              if (ownership_fact) {
                const auto rhs_name = tree_identifier_name(s.b);
                bool rhs_is_borrow = rhs_name && borrowed_function_parameters_.count(*rhs_name) && !write_through_parameters_.count(*rhs_name);
                string p_b, p_i;
                bool is_index_source = tree_parse_index(s.b, p_b, p_i);
                if (ownership_fact->access == Effect::Read) {
                  if (!rhs_is_borrow) rhs_val = "&(" + (is_index_source ? read_call_place(s.b, d, locals, &types) : rhs_val) + ")";
                } else if (ownership_fact->access == Effect::Write) {
                  if (!rhs_is_borrow) rhs_val = "&mut (" + (is_index_source ? write_call_place(s.b, d, locals, &types) : rhs_val) + ")";
                  write_through_parameters_.insert(s.a);
                }
              }
              if (domain_capability)
                rhs_val = "moss_rt::share_domain(&(" + rhs_val + "))";
              o << " = " << rhs_val;
              o << ";\n";
            }
            types[s.a] = take_fact ? canonical_type_name(take_fact->terminal_type)
                : domain_capability ? source->second
                : !inferred_map_type.empty() ? inferred_map_type
                : statement_type;
            if (domain_capability) {
              auto source_binding = source_name
                  ? domain_instance_bindings_.find(*source_name)
                  : domain_instance_bindings_.end();
              if (source_binding != domain_instance_bindings_.end())
                remember_domain_instance_binding(
                    s.a, source_binding->second.source_domain,
                    source_binding->second.instance);
              else
                forget_domain_instance_binding(s.a);
            } else {
              forget_domain_instance_binding(s.a);
            }
            if (source_name && view_parameters_.count(*source_name))
              view_parameters_.insert(s.a);
          }
          locals.insert(s.a);
          ++i;
          break;
        }
        case Stmt::Kind::Message: {
          string call = "message " + s.a + "." + s.b + "(";
          for (size_t argument = 0; argument < s.args.size(); ++argument) {
            if (argument) call += ", ";
            call += s.args[argument];
          }
          call += ")";
          o << indent(level);
          // `field = message ...` in a handler writes the domain state field
          // unless a local binding of that name is in scope.
          const bool state_result = d && !s.message_result.empty() &&
              !locals.count(s.message_result) &&
              std::any_of(d->state.begin(), d->state.end(), [&](const Field& field) {
                return field.name == s.message_result;
              });
          const bool state_view = state_result &&
              is_object_view(s.message_result, d, locals, &types);
          if (state_view) {
            o << "(" << write_call_place(s.message_result, d, locals, &types)
              << ").__moss_replace(";
          } else if (state_result) {
            o << write_call_place(s.message_result, d, locals, &types) << " = ";
          } else if (!s.message_result.empty()) {
            if (!locals.count(s.message_result)) o << "let mut ";
            o << s.message_result << " = ";
          }
          vector<size_t> argument_plans;
          for (size_t argument = 0; argument < s.args.size(); ++argument)
            argument_plans.push_back(statement_functional_pipeline_id(s, "", argument));
          o << expr(call, d, locals, &types, 0, &argument_plans,
                    root_ingress_expression_occurrence(
                        s, est::ExprSlot::Receiver, 0))
            << (state_view ? ")" : "") << ";\n";
          if (!s.message_result.empty() && !state_result) {
            const Domain& target = *domains_.at(canonical_type_name(types.at(s.a)));
            locals.insert(s.message_result);
            types[s.message_result] = find_handler(target, s.b)->reply_type.value_or("unit");
          }
          ++i;
          break;
        }
        case Stmt::Kind::Reply: {
          if (!current_handler || !current_handler->reply_type || reply_slot.empty())
            throw std::runtime_error("internal error: unchecked reply reached code generation");
          const auto* take_fact = resolve_take_fact(s, "");
          const auto* return_fact = resolve_return_value_fact(
              s, "");
          if (take_fact) {
            o << indent(level) << reply_slot << " = Some("
              << emit_take_expression(*take_fact, s, d, locals, &types) << ");\n";
          } else {
            o << indent(level) << reply_slot << " = Some("
              << message_arg(s.a, *current_handler->reply_type, d, locals, &types,
                  statement_functional_pipeline_id(s, "", 0),
                  return_fact) << ");\n";
          }
          o << indent(level) << "break 'handler;\n";
          ++i;
          // `reply` terminates the handler.  Do not emit statements that are
          // lexically after an unconditional reply: rustc would otherwise
          // diagnose the generated code as unreachable, even though Moss's
          // control-flow checker has already established the terminating
          // semantics.
          while (i < ss.size() && ss[i].indent >= level) ++i;
          break;
        }
        case Stmt::Kind::Return:
          if (in_function && !s.a.empty()) {
            const auto* take_fact = resolve_take_fact(s, "");
            const auto* return_fact = resolve_return_value_fact(
                s, "");
            string rendered;
            if (take_fact) {
              rendered = emit_take_expression(*take_fact, s, d, locals, &types);
            } else {
              rendered = expr(s.a, d, locals, &types,
                        statement_functional_pipeline_id(
                            s, "", 0));
              const auto returned_binding = tree_identifier_name(s.a);
              if (return_fact->action ==
                  TypedReturnValueActionV1::ReadCopyPrimitive) {
                if (returned_binding && types.count(*returned_binding) &&
                    view_object_type(types.at(*returned_binding)))
                  throw std::runtime_error(
                      "MOSS_INTERNAL_INVARIANT primitive return-copy fact conflicts with view type");
              } else if (return_fact->action ==
                         TypedReturnValueActionV1::TransferOwnedValue) {
                if (returned_binding && view_parameters_.count(*returned_binding))
                  throw std::runtime_error(
                      "MOSS_INTERNAL_INVARIANT checked return transfer cannot move from a borrowed view");
                // A checker-selected transfer is a Rust move. Returning a
                // MossAccess projection by cloning it here would invent a
                // semantic copy at the backend boundary.
              } else {
                throw std::runtime_error(
                    "MOSS_INTERNAL_INVARIANT unsupported checked return action");
              }
            }
            o << indent(level) << "return ";
            const bool returns_result = phase21_return_result_context_.value_or(
                phase21_result_context_);
            if (returns_result) o << "Ok(";
            o << rendered;
            if (returns_result) o << ")";
            o << ";\n";
          }
          else
            o << indent(level) << (in_handler ? "break 'handler;" : "return;") << "\n";
          ++i;
          break;
        case Stmt::Kind::Pass:
          o << indent(level) << "let _ = ();\n";
          ++i;
          break;
        case Stmt::Kind::Raw:
          o << indent(level);
          // A bare expression is a valid benchmark body.  Protect its value
          // just like an ordinary call result; otherwise rustc may erase the
          // very arithmetic or functional pipeline being measured.
          if (benchmark_body_) o << "std::hint::black_box(";
          o << expr(s.text, d, locals, &types,
                    statement_functional_pipeline_id(
                        s, "", 0));
          if (benchmark_body_) o << ")";
          o << ";\n";
          ++i;
          break;
        case Stmt::Kind::Else:
          return;
      }
    }
  }

  std::optional<string> domain_construction(const string& expr) const {
    string e = trim(expr);
    string callee; vector<string> args;
    if (!tree_parse_simple_call(e, callee, args)) return std::nullopt;
    return domains_.count(callee) ? std::optional<string>(callee) : std::nullopt;
  }

  void emit_domain_construction(std::ostringstream& o, const string& domain,
                       const string& expression, const Domain* context,
                       const std::set<string>& locals,
                       const std::unordered_map<string,string>* types) const {
    auto found = domains_.find(domain);
    if (found == domains_.end()) {
      throw std::runtime_error("internal error: unknown concrete domain constructor");
    }
    const Domain& definition = *found->second;
    o << "construct_" << snake_case(domain) << "(";
    string constructor = trim(expression);
    string callee; vector<string> arguments;
    std::unordered_map<string,string> named;
    if (tree_parse_simple_call(constructor, callee, arguments)) {
      for (const auto& argument : arguments) {
        string name, value;
        if (tree_parse_named_argument(argument, name, value)) named[name] = value;
      }
    }
    bool constructor_argument = false;
    {
      for (const auto& route : definition.routes) {
        auto value = named.find(route.name);
        if (value == named.end())
          throw std::runtime_error("internal error: missing route binding during lowering");
        // The checked graph has already proved nominal route compatibility.
        // Adapt the concrete storage reference to the existing route handle;
        // never use a generated type spelling to establish compatibility.
        if (constructor_argument) o << ", ";
        constructor_argument = true;
        o << "moss_rt::share_domain(&(" << expr(value->second, context, locals, types) << ")).into()";
      }
      for (const auto& field : definition.state) {
        auto value = named.find(field.name);
        if (constructor_argument) o << ", ";
        constructor_argument = true;
        o << (value == named.end()
            ? (field.init.empty() ? default_value(field.type)
                                  : expr(field.init, context, locals, types))
            : expr(value->second, context, locals, types));
      }
    }
    if (constructor_argument) o << ", ";
    const auto& plan = physical_domain_plan(construction_binding_);
    o << rust_string_literal(plan.concrete_instance_id) << ", " << plan.domain_rank;
    for (const auto& h : definition.handlers) {
      auto found_handler = std::find_if(plan.handlers.begin(), plan.handlers.end(),
          [&](const auto& candidate) { return candidate.name == h.name; });
      synchronization_require(found_handler != plan.handlers.end(), "missing typed handler identity");
      o << ", " << rust_string_literal(found_handler->handler_identity);
    }
    o << ");\n";
  }

  const Handler* find_handler(const Domain& d, const string& name) const {
    for (const auto& h : d.handlers) if (h.name == name) return &h;
    return nullptr;
  }

};

std::string generate_native(
    const Program& program, const OptimizationPlan& plan, bool debug_build,
    ProgramGenerationMode mode, vector<string> rust_dependencies,
    const Program* resolution_program, bool project_module_artifact,
    bool emit_static_specializations, bool public_specializations) {
#ifdef MOSS_TEST_BUILD
  est::ExpressionParseSiteScope parse_site(est::ExpressionParseSite::Consumer);
#endif
  return Generator(program, plan, debug_build, mode,
                   std::move(rust_dependencies), resolution_program,
                   project_module_artifact, emit_static_specializations,
                   public_specializations).generate();
}

}  // namespace moss
