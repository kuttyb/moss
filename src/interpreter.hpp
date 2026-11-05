#pragma once

#include <cmath>
#include <exception>
#include <charconv>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <functional>
#include <limits>
#include <memory>
#include <map>
#include <optional>
#include <ostream>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include "ast.hpp"
#include "numeric_literal.hpp"
#include "numeric_runtime.hpp"
#include "rust_float_display.hpp"
#include "type_placeholders.hpp"
#include "semantic_checker_support.hpp"

namespace moss {

bool staged_abi_v10();

// Fast Debug is deliberately an execution backend over the checked Moss AST.
// It does not parse, resolve, or type-check anything itself.  The checker has
// already rejected unsupported ownership/concurrency patterns before this
// class is constructed.
inline std::string moss_canonical_float_text(double value);
inline std::string rust_f64_to_string(double value);

inline std::string utf8_from_codepoint(uint32_t cp) {
  std::string s;
  if (cp <= 0x7F) {
    s.push_back(static_cast<char>(cp));
  } else if (cp <= 0x7FF) {
    s.push_back(static_cast<char>(0xC0 | ((cp >> 6) & 0x1F)));
    s.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
  } else if (cp <= 0xFFFF) {
    s.push_back(static_cast<char>(0xE0 | ((cp >> 12) & 0x0F)));
    s.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
    s.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
  } else if (cp <= 0x10FFFF) {
    s.push_back(static_cast<char>(0xF0 | ((cp >> 18) & 0x07)));
    s.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
    s.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
    s.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
  }
  return s;
}

inline uint32_t decode_char(const std::string& raw) {
  if (raw.empty()) return 0;
  if (raw.front() == '\\' && raw.size() >= 2) {
    if (raw[1] == 'n') return '\n';
    if (raw[1] == 'r') return '\r';
    if (raw[1] == 't') return '\t';
    if (raw[1] == '0') return '\0';
    if (raw[1] == '\'') return '\'';
    if (raw[1] == '\\') return '\\';
    if (raw[1] == 'u' && raw.size() >= 4 && raw[2] == '{' && raw.back() == '}') {
      std::string hex = raw.substr(3, raw.size() - 4);
      return static_cast<uint32_t>(std::stoul(hex, nullptr, 16));
    }
  }
  unsigned char b0 = static_cast<unsigned char>(raw[0]);
  if (b0 < 0x80) return b0;
  if ((b0 & 0xE0) == 0xC0 && raw.size() >= 2) {
    return ((b0 & 0x1F) << 6) | (static_cast<unsigned char>(raw[1]) & 0x3F);
  }
  if ((b0 & 0xF0) == 0xE0 && raw.size() >= 3) {
    return ((b0 & 0x0F) << 12) | ((static_cast<unsigned char>(raw[1]) & 0x3F) << 6) |
           (static_cast<unsigned char>(raw[2]) & 0x3F);
  }
  if ((b0 & 0xF8) == 0xF0 && raw.size() >= 4) {
    return ((b0 & 0x07) << 18) | ((static_cast<unsigned char>(raw[1]) & 0x3F) << 12) |
           ((static_cast<unsigned char>(raw[2]) & 0x3F) << 6) | (static_cast<unsigned char>(raw[3]) & 0x3F);
  }
  return b0;
}

class FastInterpreter {
 public:
  struct TraceEvent {
    int event_id = 0;
    int parent_event_id = 0;
    int call_event_id = 0;
    int message_event_id = 0;
    int handler_event_id = 0;
    int control_event_id = 0;
    int depth = 0;
    std::string kind;
    std::string function;
    std::string source_file;
    std::string semantic_identity;
    int line = 0;
    std::string detail;
    std::string instance, specialization, handler, path, before, after;
  };

  struct Options {
    bool trace = false;
    std::string source_file;
    // Test semantic identity -> FAIL detail for a test that the static
    // target-capability preflight rejected; such a test is never executed.
    std::map<std::string, std::string> unsupported_tests;
  };

  class RuntimeError : public std::runtime_error {
   public:
    RuntimeError(int line, std::string message)
        : std::runtime_error(std::move(message)), line_(line) {}
    int line() const { return line_; }

   private:
    int line_;
  };

  explicit FastInterpreter(const Program& program)
      : program_(program) {
#ifdef MOSS_TEST_BUILD
    init_test_hooks();
#endif
  }
  FastInterpreter(const Program& program, Options options)
      : program_(program), options_(options) {
#ifdef MOSS_TEST_BUILD
    init_test_hooks();
#endif
  }

#ifdef MOSS_TEST_BUILD
  void init_test_hooks() {
    auto* semantics = program_.typed_semantics.get();
    if (!semantics) return;

    const char* target_spec = std::getenv("MOSS_TEST_TARGET_SPECIALIZATION");
    auto find_target_fact = [&](std::vector<TypedLocalOwnershipFactV1>& facts) -> TypedLocalOwnershipFactV1* {
      for (auto& f : facts) {
        if (!f.specialization_identity.empty()) {
          if (!target_spec || f.specialization_context.find(target_spec) != std::string::npos)
            return &f;
        }
      }
      return facts.empty() ? nullptr : &facts.front();
    };
    auto find_target_req = [&](std::vector<TypedLocalOwnershipRequirementV1>& reqs) -> TypedLocalOwnershipRequirementV1* {
      for (auto& r : reqs) {
        if (!r.specialization_identity.empty()) {
          if (!target_spec || r.specialization_context.find(target_spec) != std::string::npos)
            return &r;
        }
      }
      return reqs.empty() ? nullptr : &reqs.front();
    };

    if (std::getenv("MOSS_TEST_CONSUMER_DROP_OWNERSHIP_FACT")) {
      if (!semantics->local_ownership_facts.empty()) {
        if (auto* fact = find_target_fact(semantics->local_ownership_facts)) {
          fact->availability = TypedLocalOwnershipFactV1::Availability::MissingEstOccurrence;
        }
      }
    }
    if (std::getenv("MOSS_TEST_CONSUMER_PERTURB_OWNERSHIP_FACT")) {
      if (!semantics->local_ownership_facts.empty()) {
        if (auto* fact = find_target_fact(semantics->local_ownership_facts)) {
          fact->access = (fact->access == Effect::Read) ? Effect::Write : Effect::Read;
        }
      }
    }
    if (std::getenv("MOSS_TEST_CONSUMER_DELETE_OWNERSHIP_RECORD")) {
      if (!semantics->local_ownership_facts.empty()) {
        if (target_spec) {
          for (auto it = semantics->local_ownership_facts.begin();
               it != semantics->local_ownership_facts.end(); ++it) {
            if (it->specialization_context.find(target_spec) != std::string::npos) {
              semantics->local_ownership_facts.erase(it);
              break;
            }
          }
        } else {
          semantics->local_ownership_facts.erase(
              semantics->local_ownership_facts.begin());
        }
      }
    }
    if (std::getenv("MOSS_TEST_CONSUMER_PERTURB_SPECIALIZATION_ID")) {
      if (!semantics->local_ownership_facts.empty()) {
        if (auto* fact = find_target_fact(semantics->local_ownership_facts)) {
          fact->specialization_identity = "perturbed-specialization-id";
        }
      }
    }
    if (std::getenv("MOSS_TEST_CONSUMER_PERTURB_SOURCE_ID")) {
      if (!semantics->local_ownership_facts.empty()) {
        if (auto* fact = find_target_fact(semantics->local_ownership_facts)) {
          fact->source_binding_identity = "perturbed-source-id";
        }
      }
    }
    if (std::getenv("MOSS_TEST_CONSUMER_DUPLICATE_OWNERSHIP_FACT")) {
      if (!semantics->local_ownership_facts.empty()) {
        if (auto* fact = find_target_fact(semantics->local_ownership_facts)) {
          semantics->local_ownership_facts.push_back(*fact);
        }
      }
    }
    if (std::getenv("MOSS_TEST_CONSUMER_DUPLICATE_OWNERSHIP_REQUIREMENT")) {
      if (!semantics->required_local_ownership_occurrences.empty()) {
        if (auto* req = find_target_req(semantics->required_local_ownership_occurrences)) {
          semantics->required_local_ownership_occurrences.push_back(*req);
        }
      }
    }

    auto find_target_loop_fact = [&](std::vector<TypedLoopOwnershipFactV1>& facts) -> TypedLoopOwnershipFactV1* {
      for (auto& f : facts) {
        if (!f.specialization_identity.empty()) {
          if (!target_spec || f.specialization_context.find(target_spec) != std::string::npos)
            return &f;
        }
      }
      return facts.empty() ? nullptr : &facts.front();
    };
    auto find_target_loop_req = [&](std::vector<TypedLoopOwnershipRequirementV1>& reqs) -> TypedLoopOwnershipRequirementV1* {
      for (auto& r : reqs) {
        if (!r.specialization_identity.empty()) {
          if (!target_spec || r.specialization_context.find(target_spec) != std::string::npos)
            return &r;
        }
      }
      return reqs.empty() ? nullptr : &reqs.front();
    };

    if (std::getenv("MOSS_TEST_CONSUMER_DROP_LOOP_FACT")) {
      if (!semantics->loop_ownership_facts.empty()) {
        if (auto* fact = find_target_loop_fact(semantics->loop_ownership_facts)) {
          fact->availability = TypedLoopOwnershipFactV1::Availability::MissingLoopOccurrence;
        }
      }
    }
    if (std::getenv("MOSS_TEST_CONSUMER_PERTURB_LOOP_FACT")) {
      if (!semantics->loop_ownership_facts.empty()) {
        if (auto* fact = find_target_loop_fact(semantics->loop_ownership_facts)) {
          fact->converged_access = (fact->converged_access == Effect::Read) ? Effect::Write : Effect::Read;
        }
      }
    }
    if (std::getenv("MOSS_TEST_CONSUMER_DELETE_LOOP_RECORD")) {
      if (!semantics->loop_ownership_facts.empty()) {
        if (target_spec) {
          for (auto it = semantics->loop_ownership_facts.begin();
               it != semantics->loop_ownership_facts.end(); ++it) {
            if (it->specialization_context.find(target_spec) != std::string::npos) {
              semantics->loop_ownership_facts.erase(it);
              break;
            }
          }
        } else {
          semantics->loop_ownership_facts.erase(
              semantics->loop_ownership_facts.begin());
        }
      }
    }
    if (std::getenv("MOSS_TEST_CONSUMER_DELETE_LOOP_REQUIREMENT")) {
      if (!semantics->required_loop_ownership_occurrences.empty()) {
        semantics->required_loop_ownership_occurrences.erase(
            semantics->required_loop_ownership_occurrences.begin());
      }
    }
    if (std::getenv("MOSS_TEST_CONSUMER_DUPLICATE_LOOP_FACT")) {
      if (!semantics->loop_ownership_facts.empty()) {
        if (auto* fact = find_target_loop_fact(semantics->loop_ownership_facts)) {
          semantics->loop_ownership_facts.push_back(*fact);
        }
      }
    }
    if (std::getenv("MOSS_TEST_CONSUMER_DUPLICATE_LOOP_REQUIREMENT")) {
      if (!semantics->required_loop_ownership_occurrences.empty()) {
        if (auto* req = find_target_loop_req(semantics->required_loop_ownership_occurrences)) {
          semantics->required_loop_ownership_occurrences.push_back(*req);
        }
      }
    }
    if (std::getenv("MOSS_TEST_CONSUMER_PERTURB_LOOP_SPECIALIZATION_ID")) {
      if (!semantics->loop_ownership_facts.empty()) {
        if (auto* fact = find_target_loop_fact(semantics->loop_ownership_facts)) {
          fact->specialization_identity = "perturbed-loop-spec-id";
        }
      }
    }

    auto find_target_ex_fact = [&](std::vector<TypedExceptionalOwnershipFactV1>& facts) -> TypedExceptionalOwnershipFactV1* {
      for (auto& f : facts) {
        if (!f.specialization_identity.empty()) {
          if (!target_spec || f.specialization_context.find(target_spec) != std::string::npos)
            return &f;
        }
      }
      return facts.empty() ? nullptr : &facts.front();
    };
    auto find_target_ex_req = [&](std::vector<TypedExceptionalOwnershipRequirementV1>& reqs) -> TypedExceptionalOwnershipRequirementV1* {
      for (auto& r : reqs) {
        if (!r.specialization_identity.empty()) {
          if (!target_spec || r.specialization_context.find(target_spec) != std::string::npos)
            return &r;
        }
      }
      return reqs.empty() ? nullptr : &reqs.front();
    };

    if (std::getenv("MOSS_TEST_CONSUMER_DROP_EXCEPTIONAL_FACT")) {
      if (!semantics->exceptional_ownership_facts.empty()) {
        if (auto* fact = find_target_ex_fact(semantics->exceptional_ownership_facts)) {
          fact->availability = TypedExceptionalOwnershipFactV1::Availability::MissingOccurrence;
        }
      }
    }
    if (std::getenv("MOSS_TEST_CONSUMER_PERTURB_EXCEPTIONAL_FACT")) {
      if (!semantics->exceptional_ownership_facts.empty()) {
        if (auto* fact = find_target_ex_fact(semantics->exceptional_ownership_facts)) {
          fact->raised_variant_identity = "perturbed-variant-id";
        }
      }
    }
    if (std::getenv("MOSS_TEST_CONSUMER_DELETE_EXCEPTIONAL_RECORD")) {
      if (!semantics->exceptional_ownership_facts.empty()) {
        if (target_spec) {
          for (auto it = semantics->exceptional_ownership_facts.begin();
               it != semantics->exceptional_ownership_facts.end(); ++it) {
            if (it->specialization_context.find(target_spec) != std::string::npos) {
              semantics->exceptional_ownership_facts.erase(it);
              break;
            }
          }
        } else {
          semantics->exceptional_ownership_facts.erase(
              semantics->exceptional_ownership_facts.begin());
        }
      }
    }
    if (std::getenv("MOSS_TEST_CONSUMER_DELETE_EXCEPTIONAL_REQUIREMENT")) {
      if (!semantics->required_exceptional_ownership_occurrences.empty()) {
        semantics->required_exceptional_ownership_occurrences.erase(
            semantics->required_exceptional_ownership_occurrences.begin());
      }
    }
    if (std::getenv("MOSS_TEST_CONSUMER_DUPLICATE_EXCEPTIONAL_FACT")) {
      if (!semantics->exceptional_ownership_facts.empty()) {
        if (auto* fact = find_target_ex_fact(semantics->exceptional_ownership_facts)) {
          semantics->exceptional_ownership_facts.push_back(*fact);
        }
      }
    }
    if (std::getenv("MOSS_TEST_CONSUMER_DUPLICATE_EXCEPTIONAL_REQUIREMENT")) {
      if (!semantics->required_exceptional_ownership_occurrences.empty()) {
        if (auto* req = find_target_ex_req(semantics->required_exceptional_ownership_occurrences)) {
          semantics->required_exceptional_ownership_occurrences.push_back(*req);
        }
      }
    }

    auto find_target_take_fact = [&](std::vector<TypedTakeFactV1>& facts) -> TypedTakeFactV1* {
      for (auto& f : facts) {
        if (!f.specialization_identity.empty()) {
          if (!target_spec || f.specialization_context.find(target_spec) != std::string::npos)
            return &f;
        }
      }
      return facts.empty() ? nullptr : &facts.front();
    };
    auto find_target_take_req = [&](std::vector<TypedTakeRequirementV1>& reqs) -> TypedTakeRequirementV1* {
      for (auto& r : reqs) {
        if (!r.specialization_identity.empty()) {
          if (!target_spec || r.specialization_context.find(target_spec) != std::string::npos)
            return &r;
        }
      }
      return reqs.empty() ? nullptr : &reqs.front();
    };

    if (std::getenv("MOSS_TEST_CONSUMER_DROP_TAKE_FACT")) {
      if (!semantics->take_facts.empty()) {
        if (auto* fact = find_target_take_fact(semantics->take_facts)) {
          fact->availability = TypedTakeFactV1::Availability::MissingTakeOccurrence;
        }
      }
    }
    if (std::getenv("MOSS_TEST_CONSUMER_PERTURB_TAKE_FACT")) {
      if (!semantics->take_facts.empty()) {
        if (auto* fact = find_target_take_fact(semantics->take_facts)) {
          fact->required_access = Effect::Read;
        }
      }
    }
    if (std::getenv("MOSS_TEST_CONSUMER_DELETE_TAKE_RECORD")) {
      if (!semantics->take_facts.empty()) {
        semantics->take_facts.erase(semantics->take_facts.begin());
      }
    }
    if (std::getenv("MOSS_TEST_CONSUMER_DELETE_TAKE_REQUIREMENT")) {
      if (!semantics->required_take_occurrences.empty()) {
        semantics->required_take_occurrences.erase(
            semantics->required_take_occurrences.begin());
      }
    }
    if (std::getenv("MOSS_TEST_CONSUMER_DUPLICATE_TAKE_FACT")) {
      if (!semantics->take_facts.empty()) {
        if (auto* fact = find_target_take_fact(semantics->take_facts)) {
          semantics->take_facts.push_back(*fact);
        }
      }
    }
    if (std::getenv("MOSS_TEST_CONSUMER_DUPLICATE_TAKE_REQUIREMENT")) {
      if (!semantics->required_take_occurrences.empty()) {
        if (auto* req = find_target_take_req(semantics->required_take_occurrences)) {
          semantics->required_take_occurrences.push_back(*req);
        }
      }
    }
    if (std::getenv("MOSS_TEST_CONSUMER_PERTURB_TAKE_PLACE_ID")) {
      if (!semantics->take_facts.empty()) {
        if (auto* fact = find_target_take_fact(semantics->take_facts)) {
          fact->place_occurrence_id += 9999;
        }
      }
    }
    if (auto* target_arena_env = std::getenv("MOSS_TEST_TARGET_ARENA")) {
      auto* target_expr_env = std::getenv("MOSS_TEST_TARGET_EXPR");
      auto* target_context_env = std::getenv("MOSS_TEST_TARGET_CONTEXT");
      auto* mutation_env = std::getenv("MOSS_TEST_TARGET_MUTATION");
      if (target_expr_env && target_context_env && mutation_env) {
        size_t arena_idx = std::stoull(target_arena_env);
        est::ExprId expr_id = std::stoull(target_expr_env);
        string ctx = target_context_env;
        string mut = mutation_env;
        if (arena_idx < program_.expression_arenas.size() && program_.expression_arenas[arena_idx]) {
          auto& arena = *program_.expression_arenas[arena_idx];
          if (mut == "drop_fact") {
            arena.test_drop_occurrence_fact(expr_id, ctx);
          } else if (mut == "drop_effect") {
            arena.test_drop_occurrence_effect(expr_id, ctx);
          } else if (mut == "perturb_effect") {
            arena.test_perturb_occurrence_effect(expr_id, ctx);
          } else if (mut == "drop_target") {
            arena.test_mutate_occurrence_target(expr_id, ctx, "");
          } else if (mut == "perturb_target") {
            arena.test_mutate_occurrence_target(expr_id, ctx, "fn:__moss_test_perturbed_target__");
          } else if (mut == "corrupt_type") {
            arena.test_mutate_occurrence_type(expr_id, ctx, Type::primitive("bool"));
          } else if (mut == "corrupt_specialization") {
            arena.test_mutate_occurrence_specialization(expr_id, ctx, "fn:__moss_test_wrong_spec__");
          }
          if (auto* sibling_env = std::getenv("MOSS_TEST_ISOLATION_SIBLING_EXPR")) {
            est::ExprId sibling_id = std::stoull(sibling_env);
            const auto* sibling_facts = arena.facts(sibling_id, ctx);
            if (!sibling_facts || !sibling_facts->inferred_effect) {
              throw std::runtime_error("MOSS_INTERNAL_INVARIANT selector isolation violated: sibling facts corrupted");
            }
          }
        }
      }
    }
    if (std::getenv("MOSS_TEST_CONSUMER_CONTRADICT_SELECTOR_TYPE")) {
      for (const auto& arena : program_.expression_arenas) {
        if (arena) arena->test_contradict_selector_type(Type::primitive("bool"));
      }
    }
    if (std::getenv("MOSS_TEST_CONSUMER_CONTRADICT_SELECTOR_TARGET")) {
      for (const auto& arena : program_.expression_arenas) {
        if (arena) arena->test_contradict_selector_target("callable-v1:module-v1:corrupted::target");
      }
    }
    if (std::getenv("MOSS_TEST_CONSUMER_CONTRADICT_SELECTOR_EFFECT")) {
      for (const auto& arena : program_.expression_arenas) {
        if (arena) arena->test_contradict_selector_effect();
      }
    }
    if (std::getenv("MOSS_TEST_CONSUMER_DROP_PRIMARY_SELECTOR_FACT")) {
      for (const auto& arena : program_.expression_arenas) {
        if (arena) arena->test_drop_primary_selector_fact();
      }
    }
    if (std::getenv("MOSS_TEST_CONSUMER_DROP_SECONDARY_SELECTOR_FACT")) {
      for (const auto& arena : program_.expression_arenas) {
        if (arena) arena->test_drop_secondary_selector_fact();
      }
    }
    if (std::getenv("MOSS_TEST_CONSUMER_PERTURB_PRIMARY_EFFECT")) {
      for (const auto& arena : program_.expression_arenas) {
        if (arena) arena->test_perturb_primary_effect();
      }
    }
    if (std::getenv("MOSS_TEST_CONSUMER_DROP_SELECTOR_OP_TYPE")) {
      for (const auto& arena : program_.expression_arenas) {
        if (arena) arena->test_drop_selector_op_type();
      }
    }
    if (std::getenv("MOSS_TEST_CONSUMER_DROP_SELECTOR_TARGET")) {
      for (const auto& arena : program_.expression_arenas) {
        if (arena) arena->test_drop_selector_target();
      }
    }
    if (std::getenv("MOSS_TEST_CONSUMER_PERTURB_SELECTOR_TARGET")) {
      for (const auto& arena : program_.expression_arenas) {
        if (arena) arena->test_perturb_selector_target("callable-v1:module-v1:corrupted::unknown");
      }
    }
    if (std::getenv("MOSS_TEST_CONSUMER_DROP_SELECTOR_EFFECT")) {
      for (const auto& arena : program_.expression_arenas) {
        if (arena) arena->test_drop_selector_effect();
      }
    }
    if (std::getenv("MOSS_TEST_CONSUMER_WRONG_SELECTOR_SPECIALIZATION")) {
      for (const auto& arena : program_.expression_arenas) {
        if (arena) arena->test_perturb_specialization_identity("specialization-v2:sha256:corrupted_wrong_key");
      }
    }

    auto borrowed_echo_fact = std::find_if(
        semantics->echo_argument_facts.begin(),
        semantics->echo_argument_facts.end(), [](const auto& fact) {
          return fact.mode ==
              TypedEchoArgumentModeV1::BorrowedVectorElement;
        });
    auto borrowed_echo_requirement = std::find_if(
        semantics->required_echo_argument_occurrences.begin(),
        semantics->required_echo_argument_occurrences.end(),
        [](const auto& requirement) {
          return requirement.mode ==
              TypedEchoArgumentModeV1::BorrowedVectorElement;
        });
    if (borrowed_echo_fact != semantics->echo_argument_facts.end()) {
      if (std::getenv("MOSS_TEST_CONSUMER_DROP_ECHO_ARGUMENT_FACT"))
        semantics->echo_argument_facts.erase(borrowed_echo_fact);
      else if (std::getenv(
                   "MOSS_TEST_CONSUMER_PERTURB_ECHO_ARGUMENT_FACT"))
        borrowed_echo_fact->required_access = Effect::Consume;
      else if (std::getenv(
                   "MOSS_TEST_CONSUMER_DUPLICATE_ECHO_ARGUMENT_FACT"))
        {
          const auto duplicate = *borrowed_echo_fact;
          semantics->echo_argument_facts.push_back(duplicate);
        }
    }
    if (borrowed_echo_requirement !=
        semantics->required_echo_argument_occurrences.end()) {
      if (std::getenv(
              "MOSS_TEST_CONSUMER_DROP_ECHO_ARGUMENT_REQUIREMENT"))
        semantics->required_echo_argument_occurrences.erase(
            borrowed_echo_requirement);
      else if (std::getenv(
                   "MOSS_TEST_CONSUMER_DUPLICATE_ECHO_ARGUMENT_REQUIREMENT"))
        {
          const auto duplicate = *borrowed_echo_requirement;
          semantics->required_echo_argument_occurrences.push_back(duplicate);
        }
    }
  }
#endif

  const std::vector<TraceEvent>& trace() const { return trace_; }

  void write_trace(std::ostream& out) const {
    for (const auto& event : trace_) {
      out << "{\"event_id\":" << event.event_id
          << ",\"event\":\"" << escape(event.kind)
          << "\",\"function\":\"" << escape(event.function)
          << "\",\"source_file\":\"" << escape(event.source_file)
          << "\",\"semantic_identity\":\""
          << escape(event.semantic_identity)
          << "\",\"line\":" << event.line;
      if (event.parent_event_id)
        out << ",\"parent_event_id\":" << event.parent_event_id;
      if (event.call_event_id)
        out << ",\"call_event_id\":" << event.call_event_id;
      if (event.message_event_id)
        out << ",\"message_event_id\":" << event.message_event_id;
      if (event.handler_event_id)
        out << ",\"handler_event_id\":" << event.handler_event_id;
      if (event.control_event_id)
        out << ",\"control_event_id\":" << event.control_event_id;
      out << ",\"depth\":" << event.depth;
      if (!event.detail.empty())
        out << ",\"detail\":\"" << escape(event.detail) << "\"";
      for (const auto& field : {std::make_pair("instance", event.instance),
          std::make_pair("specialization", event.specialization), std::make_pair("handler", event.handler),
          std::make_pair("path", event.path), std::make_pair("before", event.before), std::make_pair("after", event.after)})
        if (!field.second.empty()) out << ",\"" << field.first << "\":\"" << escape(field.second) << "\"";
      out << "}\n";
    }
  }

  void run_main(std::ostream& output) {
#ifdef MOSS_TEST_BUILD
    est::ExpressionParseSiteScope parse_site(est::ExpressionParseSite::Consumer);
#endif
    if (!program_.main)
      throw RuntimeError(0, "Fast Debug requires a Moss main procedure");
    prepare_domains();
    Frame frame;
    frame.function = "main";
    frame.functional_context = "main";
    frame.source_file = program_.main->source_file;
    frame.semantic_identity = "main@" + std::to_string(program_.main->line);
    execute(program_.main->body, frame, output);
  }

  struct TestResult {
    std::string id;
    std::string name;
    std::string source_file;
    int line = 0;
    bool passed = false;
    std::string diagnostic;
    std::string output;
  };

  int run_tests(std::ostream& output, const std::string& filter = {},
                std::vector<TestResult>* results = nullptr) {
    size_t discovered = 0;
    size_t passed = 0;
    size_t failed = 0;
    for (const auto& test : program_.tests) {
      if (!filter.empty() && test.name.find(filter) == std::string::npos &&
          test.semantic_identity.find(filter) == std::string::npos)
        continue;
      ++discovered;
      if (auto unsupported = options_.unsupported_tests.find(test.semantic_identity);
          unsupported != options_.unsupported_tests.end()) {
        output << "FAIL " << test.name << ": " << unsupported->second << "\n";
        if (results) results->push_back({test.semantic_identity, test.name,
            test.source_file, test.line, false, unsupported->second, {}});
        ++failed;
        continue;
      }
      Frame frame;
      frame.function = "test:" + test.name;
      frame.functional_context = frame.function;
      frame.source_file = test.source_file;
      frame.semantic_identity = test.semantic_identity;
      std::ostringstream test_output;
      try {
        execute(test.body, frame, test_output);
        output << test_output.str();
        output << "PASS " << test.name << "\n";
        if (results) results->push_back({test.semantic_identity, test.name,
            test.source_file, test.line, true, {}, test_output.str()});
        ++passed;
      } catch (const RuntimeError& error) {
        output << test_output.str();
        output << "FAIL " << test.name << ": " << error.what() << "\n";
        if (results) results->push_back({test.semantic_identity, test.name,
            test.source_file, error.line(), false, error.what(), test_output.str()});
        ++failed;
      } catch (const RaisedSignal& raised) {
        const std::string detail = "unhandled typed raise " +
            raised.identity.canonical() + " at " +
            (!raised.source_file.empty() ? raised.source_file :
             !test.source_file.empty() ? test.source_file :
             options_.source_file) + ":" + std::to_string(raised.line);
        output << test_output.str();
        output << "FAIL " << test.name << ": unhandled typed raise "
               << raised.identity.canonical() << " at "
               << (!raised.source_file.empty() ? raised.source_file :
                   !test.source_file.empty() ? test.source_file :
                   options_.source_file)
               << ":" << raised.line << "\n";
        if (results) results->push_back({test.semantic_identity, test.name,
            test.source_file, raised.line, false, detail, test_output.str()});
        ++failed;
      }
    }
    if (!discovered)
      throw RuntimeError(0, filter.empty() ? "no Moss tests were found"
                                          : "no Moss tests matched filter '" + filter + "'");
    if (failed > 0) {
      output << passed << " passed, " << failed << " failed\n";
      return 1;
    }
    output << passed << " passed\n";
    return 0;
  }

 private:
  struct StructValue;
  struct DomainValue;

  struct Value {
    using HashIndex = std::unordered_map<std::int64_t, std::vector<size_t>>;
    enum class Kind { Unit, Bool, Int, Float, String, Callable, Struct, Vector, Queue, Map, Set, DomainHandle, Char } kind = Kind::Unit;
    bool boolean = false;
    std::int64_t integer = 0;
    double floating = 0.0;
    uint32_t codepoint = 0;
    std::string string;
    DomainValue* domain = nullptr;
    std::shared_ptr<StructValue> object;
    std::shared_ptr<std::vector<Value>> vector;
    std::shared_ptr<std::vector<Value>> queue;
    std::shared_ptr<std::vector<std::pair<Value, Value>>> map;
    std::shared_ptr<std::vector<Value>> set;
    // Staged key-protocol lookup indexes. The vectors above retain snapshot
    // order; aliases share both the values and their index. Independent Moss
    // copies clone all three pieces in independent().
    std::shared_ptr<HashIndex> map_index;
    std::shared_ptr<std::vector<std::int64_t>> map_hashes;
    std::shared_ptr<HashIndex> set_index;
    std::string element_type;

    static Value unit();
    static Value boolean_value(bool value);
    static Value int_value(std::int64_t value);
    static Value float_value(double value);
    static Value char_value(uint32_t cp);
    static Value string_value(std::string value);
    static Value callable_value(std::string name);
    static Value struct_value(std::string type);
    static Value vector_value(std::vector<Value> values, std::string element_type = {});
    static Value queue_value();
    static Value map_value();
    static Value set_value();
    bool truthy() const;
    std::string display() const;
  };

  // Phase 25 S-B echo display contract (docs/FAST_DEBUG.md): scalars and
  // Vectors of displayable elements only. The checker rejects every other
  // type with ECHO_DISPLAY_UNSUPPORTED; this guard keeps Fast Debug from
  // inventing text (such as "Map(1)" or a record's type name) if an
  // unchecked path ever reaches it.
  static std::string echo_text(const Value& value, int line) {
    switch (value.kind) {
      case Value::Kind::Bool:
      case Value::Kind::Int:
      case Value::Kind::Float:
      case Value::Kind::Char:
      case Value::Kind::String:
        return value.display();
      case Value::Kind::Vector: {
        std::string out = "[";
        if (value.vector)
          for (size_t i = 0; i < value.vector->size(); ++i) {
            if (i) out += ", ";
            out += echo_text((*value.vector)[i], line);
          }
        return out + "]";
      }
      default:
        throw RuntimeError(line, "[ECHO_DISPLAY_UNSUPPORTED] echo cannot display this "
                                 "value; echo supports Int, Float, Bool, Char, String, "
                                 "and Vector of those");
    }
  }

  struct StructValue {
    std::string type;
    std::string case_name;
    std::unordered_map<std::string, Value> fields;
  };

  struct DomainValue {
    const ConcreteDomainInstance* concrete = nullptr;
    const Domain* definition = nullptr;
    std::unordered_map<std::string, Value> state;
    std::unordered_map<std::string, DomainValue*> routes;
    bool initialized = false;
  };
  struct Place { Value* value = nullptr; DomainValue* domain = nullptr; std::string path; };
  struct Frame {
    std::string function;
    std::string functional_context;
    std::string specialization_key;
    std::string source_file;
    std::string semantic_identity;
    std::string specialization_identity;
    std::unordered_map<std::string, Value> locals;
    std::unordered_map<std::string, std::string> local_types;
    std::unordered_map<std::string, Place> aliases;
    DomainValue* domain = nullptr;
    std::string handler;
    bool handler_scope = false;
    // Lexical block scopes. Each entry records the binding a block-local
    // declaration shadowed so leaving the block restores it, matching the
    // native Rust block structure.
    struct ScopedBinding {
      std::string name;
      std::optional<Value> value;
      std::optional<std::string> type;
      std::optional<Place> alias;
    };
    std::vector<std::vector<ScopedBinding>> scopes;
    // Names whose value was consumed but which remain lexically declared;
    // `name = expr` reinitializes them rather than declaring a new binding.
    std::set<std::string> consumed;
  };

  // Records `name` as declared in the innermost block before it is bound.
  static void declare_local(Frame& frame, const std::string& name) {
    if (frame.scopes.empty() || name.empty() || name == "_") return;
    auto& scope = frame.scopes.back();
    for (const auto& entry : scope) if (entry.name == name) return;
    Frame::ScopedBinding saved{name, std::nullopt, std::nullopt, std::nullopt};
    auto value = frame.locals.find(name);
    if (value != frame.locals.end()) saved.value = value->second;
    auto type = frame.local_types.find(name);
    if (type != frame.local_types.end()) saved.type = type->second;
    auto alias = frame.aliases.find(name);
    if (alias != frame.aliases.end()) {
      saved.alias = alias->second;
      frame.aliases.erase(alias);
    }
    scope.push_back(std::move(saved));
  }

  struct BlockScope {
    Frame& frame;
    // Names bound in every alternative of the enclosing compound statement:
    // native lowering hoists them, so they outlive this block.
    // A block left by a raise did not complete its path, so its joined names
    // are discarded (the exceptional edge snapshot keeps them for on_fail).
    const std::set<std::string>* keep = nullptr;
    int uncaught = 0;
    explicit BlockScope(Frame& f, const std::set<std::string>* joined = nullptr)
        : frame(f), keep(joined), uncaught(std::uncaught_exceptions()) {
      frame.scopes.emplace_back();
    }
    ~BlockScope() {
      auto scope = std::move(frame.scopes.back());
      frame.scopes.pop_back();
      if (keep && std::uncaught_exceptions() == uncaught) {
        std::vector<Frame::ScopedBinding> dropped;
        for (auto& item : scope) {
          // A name that already resolves outside the block (a local, an
          // alias, or the handler's domain state) is shadowed, not joined:
          // native keeps such branch bindings block-local.
          const bool shadows_state = frame.handler_scope && frame.domain &&
              (frame.domain->state.count(item.name) || frame.domain->routes.count(item.name));
          if (!keep->count(item.name) || item.value || item.alias || shadows_state) {
            dropped.push_back(std::move(item));
            continue;
          }
          if (!frame.scopes.empty()) {
            bool known = false;
            for (const auto& outer : frame.scopes.back()) known = known || outer.name == item.name;
            if (!known) frame.scopes.back().push_back(std::move(item));
          }
        }
        scope = std::move(dropped);
      }
      for (auto item = scope.rbegin(); item != scope.rend(); ++item) {
        frame.consumed.erase(item->name);
        frame.locals.erase(item->name);
        frame.local_types.erase(item->name);
        frame.aliases.erase(item->name);
        if (item->value) frame.locals[item->name] = *item->value;
        if (item->type) frame.local_types[item->name] = *item->type;
        if (item->alias) frame.aliases[item->name] = *item->alias;
      }
    }
    BlockScope(const BlockScope&) = delete;
    BlockScope& operator=(const BlockScope&) = delete;
  };

  struct Flow {
    bool returned = false;
    Value value;
  };

  struct ExpressionHandlesScope {
    const std::vector<est::Handle>*& slot;
    const std::vector<est::Handle>* prior;
    ExpressionHandlesScope(const std::vector<est::Handle>*& target,
                           const std::vector<est::Handle>* current)
        : slot(target), prior(target) { slot = current; }
    ~ExpressionHandlesScope() { slot = prior; }
  };

  struct RaisedSignal {
    RaisedIdentity identity;
    Value value;
    int line = 0;
    std::string source_file;
    // Bindings visible at the raise point whose blocks have since unwound;
    // on_fail captures read them at the exceptional edge.
    std::unordered_map<std::string, Value> edge_locals;
  };

  const Program& program_;
  const std::vector<est::Handle>* active_expression_handles_ = nullptr;
  const RaiseSet* active_checked_exceptional_alternatives_ = nullptr;
  Options options_;
  std::vector<TraceEvent> trace_;
  std::vector<int> event_stack_;
  std::vector<int> call_stack_;
  std::vector<int> message_stack_;
  std::vector<int> handler_stack_;
  std::vector<int> control_stack_;
  std::vector<RaisedSignal> selected_errors_;
  std::map<std::string, std::unique_ptr<DomainValue>> instances_;

  bool recovery_pattern_matches(const est::PatternNode& pattern,
                                const RaisedIdentity& identity) const {
    using Kind = est::PatternNode::Kind;
    if (pattern.kind == Kind::RecoveryBinding ||
        pattern.kind == Kind::RecoveryCatchall) return true;
    if (pattern.kind == Kind::GroupedAlternatives) {
      for (const auto& alternative : pattern.alternatives)
        if (recovery_pattern_matches(alternative, identity)) return true;
      return false;
    }
    return pattern.kind == Kind::QualifiedRecoveryCase &&
        pattern.type_name == identity.type &&
        pattern.case_name == identity.variant;
  }

  bool recovery_matches(const Stmt& recovery,
                        const RaisedIdentity& identity) const {
    if (moss_test_drop_fact("patterns") ||
        moss_test_perturb_fact("patterns"))
      throw RuntimeError(recovery.line,
          "MOSS_INTERNAL_INVARIANT checked recovery pattern fact was dropped or perturbed");
    return recovery.est_pattern &&
        recovery_pattern_matches(*recovery.est_pattern, identity);
  }

  std::vector<std::pair<std::string, std::pair<Value, std::string>>>
  error_pattern_payloads(const est::PatternNode& root,
                         const RaisedSignal& raised, int line) const {
    if (moss_test_drop_fact("patterns") ||
        moss_test_perturb_fact("patterns"))
      throw RuntimeError(line,
          "MOSS_INTERNAL_INVARIANT checked recovery payload pattern was dropped or perturbed");
    std::vector<const est::PatternNode*> patterns;
    if (root.kind == est::PatternNode::Kind::GroupedAlternatives) {
      for (const auto& pattern : root.alternatives) patterns.push_back(&pattern);
    } else {
      patterns.push_back(&root);
    }
    for (const auto* pattern : patterns) {
      if (!recovery_pattern_matches(*pattern, raised.identity) ||
          !pattern->payload_pattern) continue;
      if (!raised.value.object)
        throw RuntimeError(line, "checked error payload is absent");
      const EnumCase* variant = nullptr;
      for (const auto& declaration : program_.enums)
        if (declaration.name == raised.identity.type)
          for (const auto& item : declaration.cases)
            if (item.name == raised.identity.variant) variant = &item;
      if (!variant || pattern->payload_bindings.size() != variant->fields.size())
        throw RuntimeError(line, "checked error payload shape is invalid");
      std::vector<std::pair<std::string, std::pair<Value, std::string>>> bindings;
      for (size_t index = 0; index < pattern->payload_bindings.size(); ++index) {
        const auto& name = pattern->payload_bindings[index];
        const auto& field = variant->fields[index];
        auto value = raised.value.object->fields.find(field.name);
        if (value == raised.value.object->fields.end())
          throw RuntimeError(line,
              "checked error payload field '" + field.name + "' is absent");
        bindings.push_back({name, {independent(value->second), field.type}});
      }
      return bindings;
    }
    return {};
  }

  static std::string checked_binding_type(const Stmt& statement,
                                          const Frame& frame) {
    auto specialized = statement.semantic_types_by_context.find(frame.functional_context);
    return specialized == statement.semantic_types_by_context.end()
        ? statement.semantic_type : specialized->second;
  }

  static std::string escape(const std::string& value) {
    std::string result;
    for (char c : value) {
      if (c == '\\' || c == '"') result.push_back('\\');
      if (c == '\n') result += "\\n";
      else if (c == '\r') result += "\\r";
      else if (c == '\t') result += "\\t";
      else result.push_back(c);
    }
    return result;
  }

  int emit(const std::string& kind, const Frame& frame, int line,
            std::string detail = {}) {
    if (!options_.trace) return 0;
    TraceEvent event;
    event.event_id = static_cast<int>(trace_.size()) + 1;
    event.parent_event_id = event_stack_.empty() ? 0 : event_stack_.back();
    event.call_event_id = call_stack_.empty() ? 0 : call_stack_.back();
    event.message_event_id = message_stack_.empty() ? 0 : message_stack_.back();
    event.handler_event_id = handler_stack_.empty() ? 0 : handler_stack_.back();
    event.control_event_id = control_stack_.empty() ? 0 : control_stack_.back();
    event.depth = static_cast<int>(event_stack_.size());
    event.kind = kind; event.function = frame.function; event.source_file = frame.source_file.empty() ? options_.source_file : frame.source_file;
    event.semantic_identity = frame.semantic_identity;
    event.specialization = frame.specialization_identity;
    event.line = line;
    event.detail = detail.substr(0, 256);
    if (frame.domain) { event.instance = frame.domain->concrete->identity;
      event.specialization = frame.domain->concrete->specialization; event.handler = frame.handler; }
    trace_.push_back(std::move(event));
    return trace_.back().event_id;
  }

  struct EventScope {
    FastInterpreter& interpreter;
    int event_id = 0;
    bool call = false;
    bool handler = false;
    bool control = false;
    explicit EventScope(FastInterpreter& owner, int id, bool call_scope = false,
                        bool handler_scope = false, bool control_scope = false)
        : interpreter(owner), event_id(id), call(call_scope),
          handler(handler_scope), control(control_scope) {
      if (!event_id) return;
      interpreter.event_stack_.push_back(event_id);
      if (call) interpreter.call_stack_.push_back(event_id);
      if (handler) interpreter.handler_stack_.push_back(event_id);
      if (control) interpreter.control_stack_.push_back(event_id);
    }
    ~EventScope() {
      if (!event_id) return;
      if (control) interpreter.control_stack_.pop_back();
      if (handler) interpreter.handler_stack_.pop_back();
      if (call) interpreter.call_stack_.pop_back();
      interpreter.event_stack_.pop_back();
    }
  };

  struct MessageScope {
    FastInterpreter& interpreter;
    int event_id = 0;
    explicit MessageScope(FastInterpreter& owner, int id)
        : interpreter(owner), event_id(id) {
      if (event_id) interpreter.message_stack_.push_back(event_id);
    }
    ~MessageScope() {
      if (event_id) interpreter.message_stack_.pop_back();
    }
  };

  void annotate_last_handler(int handler_event_id) {
    if (options_.trace && !trace_.empty())
      trace_.back().handler_event_id = handler_event_id;
  }

  static std::string trim_copy(std::string value) {
    auto is_space = [](unsigned char c) { return std::isspace(c); };
    size_t begin = 0;
    while (begin < value.size() && is_space(static_cast<unsigned char>(value[begin]))) ++begin;
    size_t end = value.size();
    while (end > begin && is_space(static_cast<unsigned char>(value[end - 1]))) --end;
    return value.substr(begin, end - begin);
  }

  struct ExpressionTreeView {
    const est::Arena* arena = nullptr;
    est::ExprId root = 0;
    std::string source;
    mutable std::unordered_map<est::ExprId, std::string> derived_text;
    std::uint32_t arena_index = 0;
  };

  const ExpressionTreeView* active_expression_tree_ = nullptr;
  est::ExprId active_expression_node_ = 0;

  std::optional<ExpressionTreeView> expression_tree(
      std::string_view source, std::string_view caller = {},
      std::optional<std::pair<est::ExprSlot, std::uint32_t>> handle_key = {}) const {
    ExpressionTreeView view;
    if (handle_key) {
      if (active_expression_handles_) {
        for (const auto& handle : *active_expression_handles_) {
          if (handle.slot != handle_key->first ||
              handle.argument_index != handle_key->second ||
              handle.arena_index >= program_.expression_arenas.size() ||
              !program_.expression_arenas[handle.arena_index])
            continue;
          view.source.assign(source);
          view.arena = program_.expression_arenas[handle.arena_index].get();
          view.root = handle.expr;
          view.arena_index = handle.arena_index;
          return view;
        }
      }
      throw RuntimeError(0,
          "MOSS_INTERNAL_INVARIANT missing checked expression handle caller=" +
          std::string(caller) + " source=" + std::string(source) +
          " slot=" + std::to_string(static_cast<unsigned>(handle_key->first)) +
          " index=" + std::to_string(handle_key->second) +
          " handles=" + std::to_string(active_expression_handles_
              ? active_expression_handles_->size() : 0));
    }
    view.source.assign(source);
    throw RuntimeError(0,
        "MOSS_INTERNAL_INVARIANT missing checked expression node source=" +
        std::string(source) + " active=" +
        (active_expression_tree_
            ? std::string(expression_node_text(*active_expression_tree_,
                                               active_expression_node_))
            : std::string("<none>")) + " caller=" + std::string(caller));
  }

  Value evaluate_tree_node(const ExpressionTreeView& tree, est::ExprId node,
                           Frame& frame, int line, std::ostream& output) {
    const auto* previous_tree = active_expression_tree_;
    const auto previous_node = active_expression_node_;
    active_expression_tree_ = &tree;
    active_expression_node_ = node;
    try {
      Value result = evaluate_expression_tree(tree, node, frame, line, output);
      active_expression_tree_ = previous_tree;
      active_expression_node_ = previous_node;
      return result;
    } catch (...) {
      active_expression_tree_ = previous_tree;
      active_expression_node_ = previous_node;
      throw;
    }
  }

  static std::string_view expression_node_text(const ExpressionTreeView& tree,
                                               est::ExprId id) {
    const auto span = tree.arena->span(id);
    if (span.source_begin <= span.source_end &&
        span.source_end <= tree.source.size())
      return std::string_view(tree.source).substr(
          span.source_begin, span.source_end - span.source_begin);
    if (auto found = tree.derived_text.find(id);
        found != tree.derived_text.end())
      return found->second;
    std::function<std::string(est::ExprId)> render = [&](est::ExprId node_id) {
      const auto& node = tree.arena->at(node_id);
      return std::visit([&](const auto& value) -> std::string {
        using T = std::decay_t<decltype(value)>;
        const auto child = [&](est::ExprId expression) {
          return std::string(expression_node_text(tree, expression));
        };
        const auto join = [&](const std::vector<est::ExprId>& expressions) {
          std::string result;
          for (std::size_t index = 0; index < expressions.size(); ++index) {
            if (index) result += ", ";
            result += child(expressions[index]);
          }
          return result;
        };
        if constexpr (std::is_same_v<T, est::Identifier>) return value.name;
        else if constexpr (std::is_same_v<T, est::Literal>) return value.spelling;
        else if constexpr (std::is_same_v<T, est::Placeholder>) return "_";
        else if constexpr (std::is_same_v<T, est::Unary>)
          return value.op + child(value.operand);
        else if constexpr (std::is_same_v<T, est::Binary>)
          return "(" + child(value.left) + " " + value.op + " " +
              child(value.right) + ")";
        else if constexpr (std::is_same_v<T, est::Call>)
          return child(value.callee) + "(" + join(value.arguments) + ")";
        else if constexpr (std::is_same_v<T, est::Take>)
          return "take(" + join(value.arguments) + ")";
        else if constexpr (std::is_same_v<T, est::Member>)
          return child(value.base) + "." + value.name;
        else if constexpr (std::is_same_v<T, est::Index>)
          return child(value.base) + "[" + child(value.index) + "]";
        else if constexpr (std::is_same_v<T, est::Parenthesized>)
          return "(" + child(value.inner) + ")";
        else if constexpr (std::is_same_v<T, est::Message>)
          return "message " + child(value.target) + "." + value.handler +
              "(" + join(value.arguments) + ")";
        else if constexpr (std::is_same_v<T, est::NamedArgument>)
          return value.name + value.separator + " " + child(value.value);
        else if constexpr (std::is_same_v<T, est::Collection>) {
          const char close = value.delimiter == '[' ? ']' :
              value.delimiter == '{' ? '}' : ')';
          return std::string(1, value.delimiter) + join(value.elements) + close;
        } else if constexpr (std::is_same_v<T, est::Tuple>)
          return "(" + join(value.elements) + ")";
        else if constexpr (std::is_same_v<T, est::Constructor>) {
          std::string result = child(value.type);
          if (!value.type_arguments.empty())
            result += "[" + join(value.type_arguments) + "]";
          return result + "(" + join(value.arguments) + ")";
        } else if constexpr (std::is_same_v<T, est::Pipeline>) {
          std::string result = child(value.input);
          for (const auto stage : value.stages) result += " |> " + child(stage);
          return result;
        } else return std::string{};
      }, node);
    };
    const auto [inserted, _] = tree.derived_text.emplace(id, render(id));
    return inserted->second;
  }

  Place locate_tree_node(const ExpressionTreeView& tree, est::ExprId node,
                         Frame& frame, int line, std::ostream& output) {
    const auto& value = tree.arena->at(node);
    if (const auto* grouped = std::get_if<est::Parenthesized>(&value))
      return locate_tree_node(tree, grouped->inner, frame, line, output);
    if (const auto* identifier = std::get_if<est::Identifier>(&value)) {
      if (auto alias = frame.aliases.find(identifier->name);
          alias != frame.aliases.end())
        return alias->second;
      if (auto local = frame.locals.find(identifier->name);
          local != frame.locals.end())
        return Place{&local->second, nullptr, {}};
      if (frame.handler_scope && frame.domain &&
          frame.domain->state.count(identifier->name))
        return Place{&frame.domain->state.at(identifier->name), frame.domain,
                     identifier->name};
      return {};
    }
    if (const auto* member = std::get_if<est::Member>(&value)) {
      Place place = locate_tree_node(tree, member->base, frame, line, output);
      if (!place.value || !place.value->object ||
          !place.value->object->fields.count(member->name))
        return {};
      place.value = &place.value->object->fields.at(member->name);
      if (place.domain) place.path += "." + member->name;
      return place;
    }
    // One-argument Map.get is a checked borrowed place. This path is used
    // only when a checker ownership fact asks the consumer to preserve the
    // borrow; ordinary value evaluation remains independent.
    if (const auto* call = std::get_if<est::Call>(&value)) {
      const auto* method = std::get_if<est::Member>(&tree.arena->at(call->callee));
      if (method && method->name == "get" && call->arguments.size() == 1) {
        Place receiver = locate_tree_node(tree, method->base, frame, line, output);
        if (!receiver.value || receiver.value->kind != Value::Kind::Map ||
            !receiver.value->map)
          return {};
        Value key = evaluate_tree_node(tree, call->arguments.front(), frame,
                                       line, output);
        if (auto position = map_position(*receiver.value, key, frame, line,
                                         output)) {
          receiver.value = &receiver.value->map->at(*position).second;
          if (receiver.domain) receiver.path += ".get";
          return receiver;
        }
      }
    }
    if (const auto* index = std::get_if<est::Index>(&value)) {
      Place place = locate_tree_node(tree, index->base, frame, line, output);
      if (!place.value) return {};
      Value key = evaluate_tree_node(tree, index->index, frame, line, output);
      if (place.value->map) {
        for (auto& entry : *place.value->map) {
          if (protocol_key_equal(entry.first, key, frame, line, output)) {
            place.value = &entry.second;
            return place;
          }
        }
        raise_collection_error(frame, line, "MissingKey");
      }
      auto values = place.value->vector ? place.value->vector : place.value->queue;
      if (!values || key.kind != Value::Kind::Int || key.integer < 0 ||
          static_cast<std::uint64_t>(key.integer) >= values->size())
        throw RuntimeError(line, "vector index out of bounds");
      place.value = &values->at(static_cast<size_t>(key.integer));
      if (place.domain)
        place.path += "[" + std::to_string(key.integer) + "]";
      return place;
    }
    return {};
  }

  static std::string message_invocation_from_est(
      const ExpressionTreeView& tree, const est::Message& message) {
    std::string invocation(expression_node_text(tree, message.target));
    invocation += "." + message.handler + "(";
    for (std::size_t index = 0; index < message.arguments.size(); ++index) {
      if (index) invocation += ", ";
      invocation += expression_node_text(tree, message.arguments[index]);
    }
    invocation += ")";
    return invocation;
  }

  std::optional<std::pair<std::string, std::vector<std::string>>>
  expression_call(std::string_view source,
                  std::string_view caller) const {
    auto tree = expression_tree(source, caller);
    if (!tree) return std::nullopt;
    const auto& node = tree->arena->at(tree->root);
    const std::vector<est::ExprId>* node_arguments = nullptr;
    est::ExprId callee = 0;
    if (const auto* call = std::get_if<est::Call>(&node)) {
      node_arguments = &call->arguments;
      callee = call->callee;
    } else if (const auto* constructor =
                   std::get_if<est::Constructor>(&node)) {
      node_arguments = &constructor->arguments;
      callee = constructor->type;
    } else return std::nullopt;
    std::vector<std::string> arguments;
    arguments.reserve(node_arguments->size());
    for (const auto argument : *node_arguments)
      arguments.emplace_back(expression_node_text(*tree, argument));
    return std::make_pair(
        std::string(expression_node_text(*tree, callee)),
        std::move(arguments));
  }

  bool expression_call(std::string_view source, std::string& callee,
                       std::vector<std::string>& arguments,
                       std::string_view caller) const {
    auto parsed = expression_call(source, caller);
    if (!parsed) return false;
    callee = std::move(parsed->first);
    arguments = std::move(parsed->second);
    return true;
  }

  bool expression_call(const ExpressionTreeView& tree, est::ExprId root,
                       std::string& callee,
                       std::vector<std::string>& arguments) const {
    const auto& node = tree.arena->at(root);
    const std::vector<est::ExprId>* node_arguments = nullptr;
    est::ExprId callee_node = 0;
    if (const auto* call = std::get_if<est::Call>(&node)) {
      node_arguments = &call->arguments;
      callee_node = call->callee;
    } else if (const auto* constructor =
                   std::get_if<est::Constructor>(&node)) {
      node_arguments = &constructor->arguments;
      callee_node = constructor->type;
    } else {
      return false;
    }
    callee.assign(expression_node_text(tree, callee_node));
    arguments.clear();
    arguments.reserve(node_arguments->size());
    for (const auto argument : *node_arguments)
      arguments.emplace_back(expression_node_text(tree, argument));
    return true;
  }

  std::optional<std::pair<std::string, std::string>> expression_member(
      std::string_view source) const {
    auto tree = expression_tree(source, "expression_member");
    if (!tree) return std::nullopt;
    return expression_member(*tree, tree->root);
  }

  std::optional<std::pair<std::string, std::string>> expression_member(
      const ExpressionTreeView& tree, est::ExprId root_id) const {
    const auto& root = tree.arena->at(root_id);
    const est::Member* member = std::get_if<est::Member>(&root);
    const est::Call* call = std::get_if<est::Call>(&root);
    const est::Constructor* constructor =
        std::get_if<est::Constructor>(&root);
    est::ExprId member_callee = 0;
    const std::vector<est::ExprId>* arguments = nullptr;
    if (call) {
      member_callee = call->callee;
      arguments = &call->arguments;
    } else if (constructor) {
      member_callee = constructor->type;
      arguments = &constructor->arguments;
    }
    if (!member && (call || constructor))
      member = std::get_if<est::Member>(&tree.arena->at(member_callee));
    if (!member) return std::nullopt;
    std::string suffix = member->name;
    if (arguments && member == std::get_if<est::Member>(&
                                   tree.arena->at(member_callee))) {
      suffix += "(";
      for (std::size_t index = 0; index < arguments->size(); ++index) {
        if (index) suffix += ", ";
        suffix += expression_node_text(tree, arguments->at(index));
      }
      suffix += ")";
    }
    return std::make_pair(
        std::string(expression_node_text(tree, member->base)),
        std::move(suffix));
  }

  std::optional<std::pair<std::string, std::string>> expression_binary(
      std::string_view source,
      const std::vector<std::string>& operators) const {
    auto tree = expression_tree(source, "expression_binary");
    if (!tree) return std::nullopt;
    const auto* binary = std::get_if<est::Binary>(&tree->arena->at(tree->root));
    if (!binary || std::find(operators.begin(), operators.end(), binary->op) ==
                       operators.end())
      return std::nullopt;
    return std::make_pair(
        std::string(expression_node_text(*tree, binary->left)),
        std::string(expression_node_text(*tree, binary->right)));
  }

  std::string expression_binary_operator(std::string_view source) const {
    auto tree = expression_tree(source, "expression_binary_operator");
    if (!tree) return {};
    if (const auto* binary =
            std::get_if<est::Binary>(&tree->arena->at(tree->root)))
      return binary->op;
    return {};
  }

  bool expression_identifier(std::string_view source) const {
    auto tree = expression_tree(source, "expression_identifier");
    if (!tree) return false;
    return std::holds_alternative<est::Identifier>(
        tree->arena->at(tree->root));
  }

  std::vector<std::string> expression_elements(std::string_view source) const {
    auto tree = expression_tree(source, "expression_elements");
    if (!tree && source.find(',') != std::string_view::npos) {
      std::string tuple_source = "(";
      tuple_source.append(source);
      tuple_source += ")";
      tree = expression_tree(tuple_source, "expression_elements_tuple");
    }
    if (!tree) return {};
    const auto& node = tree->arena->at(tree->root);
    const std::vector<est::ExprId>* elements = nullptr;
    if (const auto* tuple = std::get_if<est::Tuple>(&node))
      elements = &tuple->elements;
    else if (const auto* collection = std::get_if<est::Collection>(&node))
      elements = &collection->elements;
    else if (const auto* call = std::get_if<est::Call>(&node))
      elements = &call->arguments;
    else if (const auto* constructor =
                 std::get_if<est::Constructor>(&node))
      elements = &constructor->arguments;
    if (!elements) {
      const auto value = trim_copy(std::string(source));
      return value.empty() ? std::vector<std::string>{} :
                             std::vector<std::string>{value};
    }
    std::vector<std::string> result;
    result.reserve(elements->size());
    for (const auto element : *elements)
      result.emplace_back(expression_node_text(*tree, element));
    return result;
  }

  std::vector<std::string> expression_alternatives(
      std::string_view source) const {
    auto tree = expression_tree(source, "expression_alternatives");
    if (!tree && source.find(',') != std::string_view::npos) {
      std::string tuple_source = "(";
      tuple_source.append(source);
      tuple_source += ")";
      tree = expression_tree(tuple_source, "expression_alternatives_tuple");
    }
    if (tree) {
      if (const auto* tuple =
              std::get_if<est::Tuple>(&tree->arena->at(tree->root))) {
        std::vector<std::string> result;
        result.reserve(tuple->elements.size());
        for (const auto element : tuple->elements)
          result.emplace_back(expression_node_text(*tree, element));
        return result;
      }
    }
    const auto value = trim_copy(std::string(source));
    return value.empty() ? std::vector<std::string>{} :
                           std::vector<std::string>{value};
  }

  static bool binding_identifier(const std::string& value) {
    if (value.empty() || !(std::isalpha(static_cast<unsigned char>(value.front())) || value.front() == '_')) return false;
    for (size_t i = 1; i < value.size(); ++i)
      if (!(std::isalnum(static_cast<unsigned char>(value[i])) || value[i] == '_')) return false;
    return true;
  }

  static bool outer_parentheses(const std::string& value) {
    if (value.size() < 2 || value.front() != '(' || value.back() != ')') return false;
    int depth = 0; bool quoted = false; bool escaped = false;
    for (size_t i = 0; i < value.size(); ++i) {
      char c = value[i];
      if (quoted) {
        if (escaped) escaped = false;
        else if (c == '\\') escaped = true;
        else if (c == '"') quoted = false;
        continue;
      }
      if (c == '"') quoted = true;
      else if (c == '(') ++depth;
      else if (c == ')' && --depth == 0 && i + 1 != value.size()) return false;
    }
    return depth == 0;
  }

  static std::optional<std::pair<std::string, std::string>> split_operator(
      const std::string& input, const std::vector<std::string>& operators) {
    int parens = 0, brackets = 0, braces = 0; bool quoted = false;
    for (size_t pos = input.size(); pos-- > 0;) {
      char c = input[pos];
      if (c == '"') {
        // Scanning backwards: a quote preceded by an odd run of backslashes
        // is escaped string content.
        size_t slashes = 0;
        while (slashes < pos && input[pos - 1 - slashes] == '\\') ++slashes;
        if (slashes % 2 == 0) quoted = !quoted;
        continue;
      }
      if (quoted) continue;
      if (c == ')') ++parens; else if (c == '(') --parens;
      else if (c == ']') ++brackets; else if (c == '[') --brackets;
      else if (c == '}') ++braces; else if (c == '{') --braces;
      if (parens || brackets || braces) continue;
      for (const auto& op : operators) {
        if (pos + op.size() <= input.size() && input.compare(pos, op.size(), op) == 0) {
          if (op == "|") {
            if (pos + 1 < input.size() && (input[pos + 1] == '>' || input[pos + 1] == '|' || input[pos + 1] == '='))
              continue;
            if (pos > 0 && input[pos - 1] == '|')
              continue;
          }
          if (op == "&") {
            if (pos + 1 < input.size() && (input[pos + 1] == '&' || input[pos + 1] == '='))
              continue;
            if (pos > 0 && input[pos - 1] == '&')
              continue;
          }
          if (op == "^") {
            if (pos + 1 < input.size() && input[pos + 1] == '=')
              continue;
          }
          if (op == "<") {
            if (pos + 1 < input.size() && (input[pos + 1] == '<' || input[pos + 1] == '='))
              continue;
            if (pos > 0 && input[pos - 1] == '<')
              continue;
          }
          if (op == ">") {
            if (pos + 1 < input.size() && (input[pos + 1] == '>' || input[pos + 1] == '='))
              continue;
            if (pos > 0 && (input[pos - 1] == '>' || input[pos - 1] == '|'))
              continue;
          }
          if (op == "-" || op == "+") {
            // An exponent sign (`1e-3`) belongs to its numeric literal.
            if (pos >= 2 && (input[pos - 1] == 'e' || input[pos - 1] == 'E') &&
                inside_numeric_literal(input, pos))
              continue;
            size_t before = pos;
            while (before > 0 && std::isspace(static_cast<unsigned char>(input[before - 1])))
              --before;
            if (before == 0) continue;
            char previous = input[before - 1];
            if (previous == '(' || previous == '[' || previous == '{' ||
                previous == ',' || previous == ':' || previous == '+' ||
                previous == '-' || previous == '*' || previous == '/' ||
                previous == '%' || previous == '=' || previous == '<' ||
                previous == '>' || previous == '!' || previous == '|' ||
                previous == '&' || previous == '^' || previous == '~')
              continue;
          }
          return std::make_pair(trim_copy(input.substr(0, pos)),
                                trim_copy(input.substr(pos + op.size())));
        }
      }
    }
    return std::nullopt;
  }

  static std::string operator_between(const std::string& whole,
                                      const std::string& left,
                                      const std::string& right,
                                      const std::vector<std::string>& operators) {
    int parens = 0, brackets = 0, braces = 0; bool quoted = false; bool escaped = false;
    for (size_t pos = 0; pos < whole.size(); ++pos) {
      char c = whole[pos];
      if (quoted) {
        if (escaped) escaped = false;
        else if (c == '\\') escaped = true;
        else if (c == '"') quoted = false;
        continue;
      }
      if (c == '"') { quoted = true; continue; }
      if (c == '(') ++parens; else if (c == ')') --parens;
      else if (c == '[') ++brackets; else if (c == ']') --brackets;
      else if (c == '{') ++braces; else if (c == '}') --braces;
      if (parens || brackets || braces) continue;
      for (const auto& op : operators) {
        if (pos + op.size() > whole.size() || whole.compare(pos, op.size(), op) != 0) continue;
        if (op == "|") {
          if (pos + 1 < whole.size() && (whole[pos + 1] == '>' || whole[pos + 1] == '|' || whole[pos + 1] == '='))
            continue;
          if (pos > 0 && whole[pos - 1] == '|')
            continue;
        }
        if (op == "&") {
          if (pos + 1 < whole.size() && (whole[pos + 1] == '&' || whole[pos + 1] == '='))
            continue;
          if (pos > 0 && whole[pos - 1] == '&')
            continue;
        }
        if (op == "^") {
          if (pos + 1 < whole.size() && whole[pos + 1] == '=')
            continue;
        }
        if (op == "<") {
          if (pos + 1 < whole.size() && (whole[pos + 1] == '<' || whole[pos + 1] == '='))
            continue;
          if (pos > 0 && whole[pos - 1] == '<')
            continue;
        }
        if (op == ">") {
          if (pos + 1 < whole.size() && (whole[pos + 1] == '>' || whole[pos + 1] == '='))
            continue;
          if (pos > 0 && (whole[pos - 1] == '>' || whole[pos - 1] == '|'))
            continue;
        }
        if (trim_copy(whole.substr(0, pos)) == left &&
            trim_copy(whole.substr(pos + op.size())) == right)
          return op;
      }
    }
    return {};
  }

  static bool parse_call_legacy(const std::string& input, std::string& callee,
                         std::vector<std::string>& args) {
    auto value = trim_copy(input);
    auto lp = value.find('(');
    if (lp == std::string::npos || value.back() != ')') return false;
    int depth = 0; bool quoted = false;
    for (size_t i = lp; i < value.size(); ++i) {
      if (quoted) {
        if (value[i] == '\\') ++i;
        else if (value[i] == '"') quoted = false;
        continue;
      }
      if (value[i] == '"') { quoted = true; continue; }
      if (value[i] == '(') ++depth;
      else if (value[i] == ')' && --depth == 0 && i + 1 != value.size()) return false;
    }
    if (depth != 0) return false;
    callee = trim_copy(value.substr(0, lp));
    if (callee.empty()) return false;
    auto inside = value.substr(lp + 1, value.size() - lp - 2);
    args.clear();
    int p = 0, b = 0, s = 0; size_t start = 0; quoted = false;
    for (size_t i = 0; i < inside.size(); ++i) {
      char c = inside[i];
      if (quoted) {
        if (c == '\\') ++i;
        else if (c == '"') quoted = false;
        continue;
      }
      if (c == '"') { quoted = true; continue; }
      if (c == '(') ++p; else if (c == ')') --p;
      else if (c == '[') ++b; else if (c == ']') --b;
      else if (c == '{') ++s; else if (c == '}') --s;
      else if (c == ',' && p == 0 && b == 0 && s == 0) {
        args.push_back(trim_copy(inside.substr(start, i - start))); start = i + 1;
      }
    }
    if (!trim_copy(inside.substr(start)).empty()) args.push_back(trim_copy(inside.substr(start)));
    return true;
  }

  static std::optional<std::pair<std::string, std::string>> top_level_member_legacy(
      const std::string& input) {
    int p = 0, b = 0, s = 0; bool quoted = false;
    for (size_t i = input.size(); i-- > 0;) {
      char c = input[i];
      if (c == '"') {
        // Scanning backwards: a quote preceded by an odd run of backslashes
        // is escaped string content.
        size_t slashes = 0;
        while (slashes < i && input[i - 1 - slashes] == '\\') ++slashes;
        if (slashes % 2 == 0) quoted = !quoted;
        continue;
      }
      if (quoted) continue;
      if (c == ')') ++p; else if (c == '(') --p;
      else if (c == ']') ++b; else if (c == '[') --b;
      else if (c == '}') ++s; else if (c == '{') --s;
      else if (c == '.' && p == 0 && b == 0 && s == 0)
        return std::make_pair(trim_copy(input.substr(0, i)), trim_copy(input.substr(i + 1)));
    }
    return std::nullopt;
  }

  const Function* function(const std::string& name) const {
    for (const auto& candidate : program_.functions)
      if (candidate.name == name) return &candidate;
    return nullptr;
  }

  const Function* function_signature(const std::string& name) const {
    if (const auto* executable = function(name)) return executable;
    for (const auto& candidate : program_.file_scope_pruned_functions)
      if (candidate.name == name) return &candidate;
    return nullptr;
  }

  const FunctionSpecialization* specialization_for_call(
      const Function& target, const Frame& caller, int line) const {
    if (!target.static_dispatch) return nullptr;
    const auto checked = checked_call_edge_specialization(
        program_, caller.functional_context, target.name, line);
    if (checked.test_fact_dropped || checked.test_fact_perturbed)
      throw RuntimeError(line,
          "MOSS_INTERNAL_INVARIANT checked specialization fact was dropped or perturbed");
    return checked.kind == CheckedCallEdgeSelectionKind::Unique
        ? checked.unique : nullptr;
  }

  bool checked_call_requires_specialization(
      const Function& target, const Frame& caller, int line) const {
    if (!target.static_dispatch) return false;
    return checked_call_edge_specialization(
        program_, caller.functional_context, target.name, line).kind !=
        CheckedCallEdgeSelectionKind::None;
  }

  const ObjectType* object_type(const std::string& name) const {
    for (const auto& candidate : program_.objects)
      if (candidate.name == name) return &candidate;
    return nullptr;
  }

  const Method* method(const std::string& owner, const std::string& name) const {
    auto object = object_type(owner);
    if (!object) return nullptr;
    for (const auto& candidate : object->methods)
      if (candidate.name == name) return &candidate;
    return nullptr;
  }

  static std::int64_t wrapping_add(std::int64_t left, std::int64_t right) {
    return static_cast<std::int64_t>(static_cast<std::uint64_t>(left) +
                                     static_cast<std::uint64_t>(right));
  }
  static std::int64_t wrapping_sub(std::int64_t left, std::int64_t right) {
    return static_cast<std::int64_t>(static_cast<std::uint64_t>(left) -
                                     static_cast<std::uint64_t>(right));
  }
  static std::int64_t wrapping_mul(std::int64_t left, std::int64_t right) {
    return static_cast<std::int64_t>(static_cast<std::uint64_t>(left) *
                                     static_cast<std::uint64_t>(right));
  }

  std::optional<ParsedFunctionalPipeline> functional_pipeline_from_est(
      const ExpressionTreeView& tree, est::ExprId root) const {
    const auto resolve_stage = [](std::string_view name)
        -> std::optional<FunctionalNodeKind> {
      for (const auto& operation : functional_operations())
        if (name == operation.name) return operation.kind;
      // Preserve the legacy interpreter's handling of user-named stages: it
      // evaluates the pipeline input and reports the first unsupported stage
      // at runtime. Source denotes an unrecognized stage in EST, not a source
      // parse failure.
      return FunctionalNodeKind::Source;
    };
    if (!std::holds_alternative<est::Pipeline>(tree.arena->at(root)))
      return std::nullopt;
    return est::functional_pipeline(*tree.arena, root,
        expression_node_text(tree, root), resolve_stage);
  }

  Value invoke_pipeline_callable(const std::string& callable,
                                 const std::vector<Value>& values,
                                 Frame& frame, int line, std::ostream& output) {
    Frame callback = frame;
    std::vector<std::string> names;
    for (size_t i = 0; i < values.size(); ++i) {
      std::string name = "__moss_pipeline_arg_" + std::to_string(i);
      callback.locals[name] = values[i]; names.push_back(name);
    }
    if (values.size() == 1) callback.locals["_"] = values.front();
    std::string target_callable = trim_copy(callable);
    auto local_it = callback.locals.find(target_callable);
    if (local_it != callback.locals.end() && local_it->second.kind == Value::Kind::Callable)
      target_callable = local_it->second.string;
    if (auto fn = function(target_callable))
      return call(*fn, names, callback, line, output, nullptr, nullptr,
                  &values);
    throw RuntimeError(line,
        "MOSS_INTERNAL_INVARIANT missing checked pipeline callable target");
  }

  Value invoke_pipeline_expression(const ExpressionTreeView& tree,
                                  est::ExprId expression,
                                  const std::vector<Value>& values,
                                  Frame& frame, int line,
                                  std::ostream& output) {
    Frame callback = frame;
    if (values.size() == 1) callback.locals["_"] = values.front();
    if (const auto* identifier = std::get_if<est::Identifier>(
            &tree.arena->at(expression))) {
      auto local = callback.locals.find(identifier->name);
      if (local != callback.locals.end() &&
          local->second.kind == Value::Kind::Callable)
        return invoke_pipeline_callable(local->second.string, values,
                                        frame, line, output);
      if (function(identifier->name))
        return invoke_pipeline_callable(identifier->name, values, frame,
                                        line, output);
    }
    return evaluate_tree_node(tree, expression, callback, line, output);
  }

  std::optional<std::string> checked_pipeline_stage_output_type(
      const ParsedFunctionalPipeline& parsed, size_t stage_index,
      const Frame& frame, int line) const {
    for (const auto& pipeline : program_.functional_pipelines) {
      if (pipeline.line != line || pipeline.context != frame.functional_context ||
          stage_index >= pipeline.nodes.size() ||
          pipeline.nodes.size() != parsed.stages.size() + 1 ||
          trim_copy(pipeline.source_expression) != trim_copy(parsed.source))
        continue;
      bool matches = true;
      for (size_t index = 1; index < pipeline.nodes.size(); ++index)
        if (trim_copy(pipeline.nodes[index].source_text) !=
            trim_copy(parsed.stages[index - 1].raw)) {
          matches = false;
          break;
        }
      if (!matches) continue;
      return pipeline.nodes[stage_index].output_type;
    }
    return std::nullopt;
  }

  static std::optional<std::string> checked_vector_element_type(
      const std::string& type) {
    if (type.rfind("vector[", 0) != 0 || type.size() <= 8 || type.back() != ']')
      return std::nullopt;
    return type.substr(7, type.size() - 8);
  }

  Value eval_pipeline(const ParsedFunctionalPipeline& parsed, Frame& frame,
                     int line, std::ostream& output,
                     const ExpressionTreeView& tree,
                     const est::Pipeline& pipeline_node) {
    if (parsed.stages.empty()) return Value::unit();
    // A FileIO chunk source (`<fileio>.chunks(n)`) never reaches here in
    // practice: FileIO.open always throws first (see interp_phase20_statement
    // in interpreter_phase20.inc) because there is no Agent B FileIO runtime
    // wired into Fast Debug yet. Chunk-pipeline differential testing happens
    // against native codegen's test-only Rust shim instead; see
    // tests/tooling/check_phase20_executor_fileio.py and AGENTS.md
    // corrective-pass items 2 and 24.
    Value current = evaluate_tree_node(tree, pipeline_node.input, frame, line, output);
    for (size_t stage_index = 1; stage_index <= parsed.stages.size(); ++stage_index) {
      const auto& stage = parsed.stages[stage_index - 1];
      std::string callee;
      for (const auto& operation : functional_operations())
        if (operation.kind == stage.kind) callee = operation.name;
      if (callee.empty()) {
        std::vector<std::string> ignored_arguments;
        if (!expression_call(stage.raw, callee, ignored_arguments,
                             "pipeline_stage"))
          callee = stage.raw;
      }
      const auto& args = stage.arguments;
      if (callee == "sum") {
        if (current.kind != Value::Kind::Vector) throw RuntimeError(line, "pipeline sum requires a Vector");
        Value total = (current.element_type == "Float" || current.element_type == "float")
            ? Value::float_value(0.0) : Value::int_value(0);
        for (const auto& value : *current.vector) {
          if (value.kind == Value::Kind::Float || total.kind == Value::Kind::Float) {
            double left = total.kind == Value::Kind::Float ? total.floating : total.integer;
            double right = value.kind == Value::Kind::Float ? value.floating : value.integer;
            total = Value::float_value(left + right);
          } else total = Value::int_value(wrapping_add(total.integer, value.integer));
        }
        current = total;
      } else if (callee == "count") {
        if (current.kind != Value::Kind::Vector) throw RuntimeError(line, "pipeline count requires a Vector");
        current = Value::int_value(static_cast<std::int64_t>(current.vector->size()));
      } else if (callee == "map" || callee == "filter") {
        if (current.kind != Value::Kind::Vector || args.size() != 1)
          throw RuntimeError(line, "invalid checked pipeline stage '" + callee + "'");
        std::vector<Value> values;
        std::optional<std::string> inferred_map_element_type;
        if (stage_index - 1 >= pipeline_node.stages.size())
          throw RuntimeError(line,
              "MOSS_INTERNAL_INVARIANT missing checked pipeline stage node");
        const auto* stage_call = std::get_if<est::Call>(
            &tree.arena->at(pipeline_node.stages[stage_index - 1]));
        if (!stage_call || stage_call->arguments.size() != 1)
          throw RuntimeError(line,
              "MOSS_INTERNAL_INVARIANT missing checked pipeline callable node");
        const auto stage_callable = stage_call->arguments.front();
        for (const auto& value : *current.vector) {
          Value transformed = invoke_pipeline_expression(
              tree, stage_callable, {value}, frame, line, output);
          if (callee == "map") {
            switch (transformed.kind) {
              case Value::Kind::Bool: inferred_map_element_type = "bool"; break;
              case Value::Kind::Int: inferred_map_element_type = "int"; break;
              case Value::Kind::Float: inferred_map_element_type = "float"; break;
              case Value::Kind::String: inferred_map_element_type = "string"; break;
              case Value::Kind::Char: inferred_map_element_type = "char"; break;
              case Value::Kind::Vector: inferred_map_element_type = "vector[" + transformed.element_type + "]"; break;
              case Value::Kind::Struct: if (transformed.object) inferred_map_element_type = transformed.object->type; break;
              default: break;
            }
            values.push_back(std::move(transformed));
          } else if (transformed.truthy()) values.push_back(value);
        }
        std::string element_type = current.element_type;
        if (callee == "map") {
          auto output_type = checked_pipeline_stage_output_type(
              parsed, stage_index, frame, line);
          if (!output_type) {
#ifndef NDEBUG
            if (const char* count = std::getenv("MOSS_TEST_COUNT_FACT");
                count && std::string(count) == "g3")
              std::cerr << "MOSS_TEST_FACT_COUNTER|g3|line=" << line
                        << "|context=" << frame.functional_context << "\n";
#endif
          }
          if (!output_type && !args.empty()) {
            std::string callback = trim_copy(args.front());
            std::string callback_name = callback;
            std::vector<std::string> callback_args;
            expression_call(callback, callback_name, callback_args,
                            "pipeline_callback");
            for (std::size_t separator = callback_name.find('.');
                 separator != std::string::npos;
                 separator = callback_name.find('.', separator + 2))
              callback_name.replace(separator, 1, "__");
            auto callback_function = function_signature(callback_name);
            if (!callback_function) {
              auto local = frame.locals.find(callback_name);
              if (local != frame.locals.end() && local->second.kind == Value::Kind::Callable)
                callback_function = function_signature(local->second.string);
            }
            if (callback_function && callback_function->return_type)
              output_type = "vector[" + *callback_function->return_type + "]";
          }
          auto output_element = output_type ? checked_vector_element_type(*output_type) : std::nullopt;
          if (!output_element && inferred_map_element_type)
            output_element = inferred_map_element_type;
          if (!output_element)
            throw RuntimeError(line, "internal error: missing checked map output type");
          element_type = *output_element;
        }
        current = Value::vector_value(std::move(values),
                                      std::move(element_type));
      } else if (callee == "reduce") {
        if (current.kind != Value::Kind::Vector || args.size() != 2)
          throw RuntimeError(line, "invalid checked pipeline stage 'reduce'");
        Value reduced;
        if (stage_index - 1 >= pipeline_node.stages.size())
          throw RuntimeError(line, "MOSS_INTERNAL_INVARIANT missing checked reduce node");
        const auto* call = std::get_if<est::Call>(
            &tree.arena->at(pipeline_node.stages[stage_index - 1]));
        if (!call || call->arguments.size() != 2)
          throw RuntimeError(line, "MOSS_INTERNAL_INVARIANT missing checked reduce arguments");
        reduced = evaluate_tree_node(tree, call->arguments[0], frame, line, output);
        for (const auto& value : *current.vector)
          reduced = invoke_pipeline_expression(tree, call->arguments[1],
              {reduced, value}, frame, line, output);
        current = reduced;
      } else if (callee == "any" || callee == "all") {
        if (current.kind != Value::Kind::Vector || args.size() != 1)
          throw RuntimeError(line, "invalid checked pipeline stage '" + callee + "'");
        if (stage_index - 1 >= pipeline_node.stages.size())
          throw RuntimeError(line,
              "MOSS_INTERNAL_INVARIANT missing checked pipeline stage node");
        const auto* stage_call = std::get_if<est::Call>(
            &tree.arena->at(pipeline_node.stages[stage_index - 1]));
        if (!stage_call || stage_call->arguments.size() != 1)
          throw RuntimeError(line,
              "MOSS_INTERNAL_INVARIANT missing checked pipeline predicate node");
        const auto predicate_node = stage_call->arguments.front();
        bool result = callee == "all";
        for (const auto& value : *current.vector) {
          Value predicate_value;
          predicate_value = invoke_pipeline_expression(
              tree, predicate_node, {value}, frame, line, output);
          const bool predicate = predicate_value.truthy();
          if (callee == "any" && predicate) result = true;
          if (callee == "all" && !predicate) result = false;
        }
        current = Value::boolean_value(result);
      } else throw RuntimeError(line, "unsupported checked pipeline stage '" + callee + "'");
    }
    return current;
  }

#include "interpreter_domains.inc"
#include "interpreter_phase20.inc"

  Value evaluate_expression_source(const std::string& expression, Frame& frame, int line,
             std::ostream& output,
             std::optional<std::pair<est::ExprSlot, std::uint32_t>> handle_key = {}) {
    const std::string e = trim_copy(expression);
    if (e.empty()) return Value::unit();
    auto tree = expression_tree(expression,
                                "evaluate_expression_source line=" +
                                    std::to_string(line),
                                handle_key);
    if (!tree) {
      if (e.rfind("assert ", 0) == 0) {
        const std::string condition = e.substr(7);
        auto condition_tree = expression_tree(condition,
                                               "assert_condition");
        if (!condition_tree)
          throw RuntimeError(line, "unsupported assertion condition");
        if (!evaluate_tree_node(*condition_tree, condition_tree->root,
                                frame, line, output).truthy()) {
          emit("AssertionFailure", frame, line, condition);
          throw RuntimeError(line, "assertion failed: " + condition);
        }
        return Value::unit();
      }
      throw RuntimeError(line, "unsupported expression '" + e + "'");
    }
    return evaluate_expression_tree(*tree, tree->root, frame, line, output);
  }

  Value evaluate_expression_tree(const ExpressionTreeView& tree_view,
                                 est::ExprId root, Frame& frame, int line,
                                 std::ostream& output) {
    const auto* previous_tree = active_expression_tree_;
    const auto previous_node = active_expression_node_;
    active_expression_tree_ = &tree_view;
    active_expression_node_ = root;
    struct ActiveExpressionTreeScope {
      const ExpressionTreeView*& tree;
      est::ExprId& node;
      const ExpressionTreeView* previous_tree;
      est::ExprId previous_node;
      ~ActiveExpressionTreeScope() {
        tree = previous_tree;
        node = previous_node;
      }
    } active_tree_scope{active_expression_tree_, active_expression_node_,
                        previous_tree, previous_node};
    std::string e = trim_copy(
        std::string(expression_node_text(tree_view, root)));
    const ExpressionTreeView* tree = &tree_view;
    est::ExprId current_root = root;
    while (tree) {
      const auto* parenthesized =
          std::get_if<est::Parenthesized>(&tree->arena->at(current_root));
      if (!parenthesized) break;
      current_root = parenthesized->inner;
      e = trim_copy(std::string(expression_node_text(*tree, current_root)));
    }
    active_expression_node_ = current_root;
    if (e.empty()) return Value::unit();
    if (tree && std::holds_alternative<est::Placeholder>(
                    tree->arena->at(current_root))) {
      auto placeholder = frame.locals.find("_");
      if (placeholder == frame.locals.end())
        throw RuntimeError(line,
            "MOSS_INTERNAL_INVARIANT missing checked pipeline placeholder binding");
      return placeholder->second;
    }
    if (e.size() >= 9 && e.compare(e.size() - 2, 2, "()") == 0) {
      std::string constructor = e.substr(0, e.size() - 2);
      constructor.erase(std::remove_if(constructor.begin(), constructor.end(),
          [](unsigned char ch) { return std::isspace(ch); }), constructor.end());
      if (constructor.rfind("Vector[", 0) == 0 && constructor.back() == ']')
        return Value::vector_value({}, constructor.substr(7, constructor.size() - 8));
    }
    if (auto pipeline = functional_pipeline_from_est(*tree, current_root)) {
      const auto* pipeline_node = tree
          ? std::get_if<est::Pipeline>(&tree->arena->at(current_root)) : nullptr;
      if (!pipeline_node)
        throw RuntimeError(line, "internal error: checked pipeline has no EST node");
      return eval_pipeline(*pipeline, frame, line, output, *tree, *pipeline_node);
    }
    if (tree) {
      if (const auto* take_node =
              std::get_if<est::Take>(&tree->arena->at(current_root))) {
        (void)take_node;
        if (!program_.typed_semantics)
          throw RuntimeError(line,
              "MOSS_INTERNAL_INVARIANT missing typed semantics for take");
        const TypedTakeRequirementV1* requirement = nullptr;
        const TypedTakeFactV1* fact = nullptr;
        try {
          if (!frame.specialization_identity.empty()) {
            requirement = &program_.typed_semantics->find_take_requirement(
                tree->arena_index, current_root, frame.functional_context,
                frame.specialization_identity);
          } else {
            requirement = &program_.typed_semantics->find_take_requirement(
                tree->arena_index, current_root, frame.functional_context);
            if (!requirement->specialization_identity.empty()) {
              throw std::invalid_argument(
                  "take requirement expects specialization identity '" +
                  requirement->specialization_identity + "' but active frame has none");
            }
          }
          fact = program_.typed_semantics->find_take_fact(*requirement);
        } catch (const std::exception& exc) {
          throw RuntimeError(line,
              "MOSS_INTERNAL_INVARIANT " + std::string(exc.what()));
        }
        if (!requirement || !fact || !fact->is_authoritative())
          throw RuntimeError(line,
              "MOSS_INTERNAL_INVARIANT unauthoritative take fact");

        Place place = locate_tree_node(*tree, fact->place_occurrence_id,
                                       frame, line, output);
        if (!place.value)
          throw RuntimeError(line,
              "MOSS_INTERNAL_INVARIANT unresolvable place for checked take");

        Value previous = std::move(*place.value);
        switch (fact->replacement) {
          case TypedTakeReplacementV1::OptionNone: {
            Value none = Value::struct_value("option");
            none.object->case_name = "None";
            *place.value = std::move(none);
            break;
          }
          case TypedTakeReplacementV1::VectorEmpty: {
            *place.value = Value::vector_value({}, previous.element_type);
            break;
          }
          case TypedTakeReplacementV1::MapEmpty: {
            *place.value = Value::map_value();
            break;
          }
          case TypedTakeReplacementV1::QueueEmpty: {
            *place.value = Value::queue_value();
            break;
          }
        }
        if (place.domain) {
          state_event("state_write", frame, line, place, summary(previous),
                      summary(*place.value));
        } else {
          emit("LocalWrite", frame, line, fact->root_name);
        }
        return previous;
      }
      if (const auto* message_node =
              std::get_if<est::Message>(&tree->arena->at(current_root)))
        return message(*tree, *message_node, frame, line, output);
    }
    if (e.rfind("message ", 0) == 0)
      return message(e.substr(8), frame, line, output);
    if (tree) {
      if (const auto* binary =
              std::get_if<est::Binary>(&tree->arena->at(current_root))) {
        Value left = evaluate_tree_node(*tree, binary->left, frame, line, output);
        if (binary->op == "or" && left.truthy())
          return Value::boolean_value(true);
        if (binary->op == "and" && !left.truthy())
          return Value::boolean_value(false);
        Value right = evaluate_tree_node(*tree, binary->right, frame, line, output);
        if (binary->op == "or" || binary->op == "and")
          return Value::boolean_value(right.truthy());
        if (binary->op == "xor")
          return Value::boolean_value(left.truthy() != right.truthy());
        const auto& op = binary->op;
        if (op == "==") return Value::boolean_value(equal(left, right));
        if (op == "!=") return Value::boolean_value(!equal(left, right));
        if (op == "<" || op == ">" || op == "<=" || op == ">=") {
          if (left.kind == Value::Kind::Char && right.kind == Value::Kind::Char) {
            if (op == "<") return Value::boolean_value(left.codepoint < right.codepoint);
            if (op == ">") return Value::boolean_value(left.codepoint > right.codepoint);
            if (op == "<=") return Value::boolean_value(left.codepoint <= right.codepoint);
            return Value::boolean_value(left.codepoint >= right.codepoint);
          }
          if (left.kind == Value::Kind::Int && right.kind == Value::Kind::Int) {
            if (op == "<") return Value::boolean_value(left.integer < right.integer);
            if (op == ">") return Value::boolean_value(left.integer > right.integer);
            if (op == "<=") return Value::boolean_value(left.integer <= right.integer);
            return Value::boolean_value(left.integer >= right.integer);
          }
          const double lhs = left.kind == Value::Kind::Int ? left.integer : left.floating;
          const double rhs = right.kind == Value::Kind::Int ? right.integer : right.floating;
          if (op == "<") return Value::boolean_value(lhs < rhs);
          if (op == ">") return Value::boolean_value(lhs > rhs);
          if (op == "<=") return Value::boolean_value(lhs <= rhs);
          return Value::boolean_value(lhs >= rhs);
        }
        if (op == "|" || op == "^" || op == "&" || op == "<<" || op == ">>") {
          if (left.kind != Value::Kind::Int || right.kind != Value::Kind::Int)
            throw RuntimeError(line, "bitwise operations require Int operands");
          if (op == "|") return Value::int_value(left.integer | right.integer);
          if (op == "^") return Value::int_value(left.integer ^ right.integer);
          if (op == "&") return Value::int_value(left.integer & right.integer);
          if (op == "<<") return Value::int_value(moss_shl(left.integer, right.integer));
          return Value::int_value(moss_shr(left.integer, right.integer));
        }
        if (op == "+" && left.kind == Value::Kind::String &&
            right.kind == Value::Kind::String)
          return Value::string_value(left.string + right.string);
        if (left.kind == Value::Kind::Int && right.kind == Value::Kind::Int) {
          if (op == "+") return Value::int_value(wrapping_add(left.integer, right.integer));
          if (op == "-") return Value::int_value(wrapping_sub(left.integer, right.integer));
          if (op == "*") return Value::int_value(wrapping_mul(left.integer, right.integer));
          if (right.integer == 0)
            throw RuntimeError(line, op == "%" ? "integer remainder by zero"
                                                   : "integer division by zero");
          if (left.integer == std::numeric_limits<std::int64_t>::min() && right.integer == -1)
            return Value::int_value(op == "%" ? 0 : std::numeric_limits<std::int64_t>::min());
          if (op == "%") return Value::int_value(left.integer % right.integer);
          return Value::int_value(left.integer / right.integer);
        }
        const double lhs = left.kind == Value::Kind::Int ? left.integer : left.floating;
        const double rhs = right.kind == Value::Kind::Int ? right.integer : right.floating;
        if (op == "+") return Value::float_value(lhs + rhs);
        if (op == "-") return Value::float_value(lhs - rhs);
        if (op == "*") return Value::float_value(lhs * rhs);
        if (op == "%") throw RuntimeError(line, "integer remainder requires Int operands");
        return Value::float_value(lhs / rhs);
      }
    }
    if (auto place = locate_tree_node(*tree, current_root, frame, line, output);
        place.value) {
      if (place.domain) state_event("state_read", frame, line, place);
      else emit("LocalRead", frame, line, e);
      return *place.value;
    }
    {
      std::string qualified;
      std::vector<std::string> arguments;
      bool called = tree
          ? expression_call(*tree, current_root, qualified, arguments)
          : expression_call(e, qualified, arguments, "enum_string_fallback");
      const std::vector<est::ExprId>* enum_argument_nodes = nullptr;
      if (tree) {
        const auto& root_node = tree->arena->at(current_root);
        if (const auto* call_node = std::get_if<est::Call>(&root_node))
          enum_argument_nodes = &call_node->arguments;
        else if (const auto* constructor_node =
                     std::get_if<est::Constructor>(&root_node))
          enum_argument_nodes = &constructor_node->arguments;
      }
      if (!called) qualified = e;
      auto dot = qualified.find('.');
      if (dot != std::string::npos && qualified.find('.', dot + 1) == std::string::npos) {
        std::string enum_name = qualified.substr(0, dot);
        std::string case_name = qualified.substr(dot + 1);
        for (const auto& declaration : program_.enums)
          if (declaration.name == enum_name) {
            Value result = Value::struct_value(enum_name);
            result.object->case_name = case_name;
            for (std::size_t index = 0; index < arguments.size(); ++index) {
              const auto& argument = arguments[index];
              auto colon = argument.find(':');
              if (colon == std::string::npos)
                throw RuntimeError(line, "enum constructor requires named fields");
              std::string name = trim_copy(argument.substr(0, colon));
              if (enum_argument_nodes && index < enum_argument_nodes->size()) {
                const auto* named = std::get_if<est::NamedArgument>(
                    &tree->arena->at(enum_argument_nodes->at(index)));
                if (named) {
                  name = named->name;
                  result.object->fields[name] = evaluate_tree_node(
                      *tree, named->value, frame, line, output);
                  continue;
                }
              }
              throw RuntimeError(line, "internal error: enum field has no EST named argument");
            }
            return result;
          }
      }
    }
    if (frame.handler_scope && frame.domain && frame.domain->routes.count(e)) {
      Value value; value.kind = Value::Kind::DomainHandle; value.domain = frame.domain->routes.at(e); return value;
    }
    if (tree) {
      if (const auto* identifier_node =
              std::get_if<est::Identifier>(&tree->arena->at(current_root))) {
        if (identifier_node->name == "true") return Value::boolean_value(true);
        if (identifier_node->name == "false") return Value::boolean_value(false);
        if (identifier_node->name == "None") {
          Value none = Value::struct_value("option");
          none.object->case_name = "None";
          return none;
        }
      }
      if (const auto* member_node =
              std::get_if<est::Member>(&tree->arena->at(current_root))) {
        if (const auto* enum_identifier =
                std::get_if<est::Identifier>(
                    &tree->arena->at(member_node->base))) {
          const auto* checked_facts = tree->arena->facts(
              current_root, frame.functional_context);
          if (checked_facts && checked_facts->type) {
            Value result = Value::struct_value(
                checked_facts->type->to_canonical_string());
            result.object->case_name = member_node->name;
            return result;
          }
          for (const auto& declaration : program_.enums)
            if (declaration.name == enum_identifier->name) {
              Value result = Value::struct_value(declaration.name);
              result.object->case_name = member_node->name;
              return result;
            }
        }
      }
    }
    if (e == "true") return Value::boolean_value(true);
    if (e == "false") return Value::boolean_value(false);
    if (e == "None") {
      Value none = Value::struct_value("option");
      none.object->case_name = "None";
      return none;
    }
    if (e == "not" || e == "!") return Value::boolean_value(false);
    if (tree) {
      if (const auto* unary =
              std::get_if<est::Unary>(&tree->arena->at(current_root))) {
        const std::string operand(
            expression_node_text(*tree, unary->operand));
        if (unary->op == "not")
          return Value::boolean_value(
              !evaluate_tree_node(*tree, unary->operand, frame, line, output).truthy());
        if (unary->op == "~") {
          Value value = evaluate_tree_node(*tree, unary->operand, frame, line, output);
          if (value.kind != Value::Kind::Int)
            throw RuntimeError(line, "bitwise NOT requires Int operand");
          return Value::int_value(~value.integer);
        }
        if (unary->op == "-") {
          if (trim_copy(operand) == "9223372036854775808")
            return Value::int_value(std::numeric_limits<std::int64_t>::min());
          Value value = evaluate_tree_node(*tree, unary->operand, frame, line, output);
          if (value.kind == Value::Kind::Int)
            return Value::int_value(wrapping_sub(0, value.integer));
          if (value.kind == Value::Kind::Float)
            return Value::float_value(-value.floating);
        }
        if (unary->op == "+") {
          if (auto number = decode_numeric_literal_expression(e))
            return number->kind == NumericLiteral::Kind::Int
                ? Value::int_value(number->integer)
                : Value::float_value(number->floating);
          est::ExprId operand_node = unary->operand;
          const auto* operand_parenthesized =
              std::get_if<est::Parenthesized>(
                  &tree->arena->at(operand_node));
          if (operand_parenthesized)
            operand_node = operand_parenthesized->inner;
          if (const auto* operand_literal =
                  std::get_if<est::Literal>(&tree->arena->at(operand_node))) {
            if (auto number =
                    decode_numeric_literal_expression(operand_literal->spelling))
              return number->kind == NumericLiteral::Kind::Int
                  ? Value::int_value(number->integer)
                  : Value::float_value(number->floating);
          }
          if (const auto* operand_identifier =
                  std::get_if<est::Identifier>(&tree->arena->at(operand_node))) {
            auto place = locate(operand_identifier->name, frame, line, output);
            if (place.value &&
                (place.value->kind == Value::Kind::Int ||
                 place.value->kind == Value::Kind::Float))
              return *place.value;
          }
        }
      }
    }
    if (e.rfind("assert ", 0) == 0) {
      auto condition = e.substr(7);
      auto condition_tree = expression_tree(condition,
                                             "assert_condition_tree");
      if (!condition_tree || !evaluate_tree_node(
              *condition_tree, condition_tree->root, frame, line, output).truthy()) {
        emit("AssertionFailure", frame, line, condition);
        throw RuntimeError(line, "assertion failed: " + condition);
      }
      return Value::unit();
    }

    // Numeric literals decode through the shared specification used by the
    // checker and native lowering (numeric_literal.hpp).
    if (tree) {
      if (const auto* literal =
              std::get_if<est::Literal>(&tree->arena->at(current_root))) {
        if (auto number = decode_numeric_literal_expression(literal->spelling))
          return number->kind == NumericLiteral::Kind::Int
              ? Value::int_value(number->integer)
              : Value::float_value(number->floating);
        if (literal->spelling.size() >= 2 && literal->spelling.front() == '"')
          return Value::string_value(decode_string(literal->spelling.substr(
              1, literal->spelling.size() - 2)));
        if (literal->spelling.size() >= 2 && literal->spelling.front() == '\'')
          return Value::char_value(decode_char(literal->spelling.substr(
              1, literal->spelling.size() - 2)));
      }
    }

    // A final index may follow a computed value, including a vector literal.
    // Match the final bracket pair instead of treating every expression that
    // starts and ends with '['/']' as one literal (`[x][0]`). Lvalue-shaped
    // indices were already handled by locate() above.
    if (tree) {
      if (const auto* index_node =
              std::get_if<est::Index>(&tree->arena->at(current_root))) {
        Value base = evaluate_tree_node(*tree, index_node->base, frame, line, output);
        Value index = evaluate_tree_node(*tree, index_node->index, frame, line, output);
        if (base.kind == Value::Kind::Map && base.map) {
          if (auto position = map_position(base, index, frame, line, output))
            return base.map->at(*position).second;
          raise_collection_error(frame, line, "MissingKey");
        }
        auto values = base.kind == Value::Kind::Vector ? base.vector :
            base.kind == Value::Kind::Queue ? base.queue : nullptr;
        if (!values || index.kind != Value::Kind::Int || index.integer < 0 ||
            static_cast<std::uint64_t>(index.integer) >= values->size())
          throw RuntimeError(line, "vector index out of bounds");
        return values->at(static_cast<size_t>(index.integer));
      }
    }
    const auto* collection_node = tree ?
        std::get_if<est::Collection>(&tree->arena->at(current_root)) : nullptr;
    if (collection_node && collection_node->delimiter == '[') {
      std::vector<Value> values;
      for (const auto part : collection_node->elements)
        values.push_back(evaluate_tree_node(*tree, part, frame, line, output));
      return Value::vector_value(std::move(values));
    }

    std::string callee; std::vector<std::string> args;
    if ((tree && expression_call(*tree, current_root, callee, args)) ||
        (!tree && expression_call(e, callee, args, "call_string_fallback"))) {
      const std::vector<est::ExprId>* call_argument_nodes = nullptr;
      if (tree) {
        const auto& call_root = tree->arena->at(current_root);
        if (const auto* call_node = std::get_if<est::Call>(&call_root))
          call_argument_nodes = &call_node->arguments;
        else if (const auto* constructor_node =
                     std::get_if<est::Constructor>(&call_root))
          call_argument_nodes = &constructor_node->arguments;
      }
      if ((moss_test_drop_fact("call-targets") ||
           moss_test_perturb_fact("call-targets")) && function(callee)) {
        const auto* facts = tree
            ? tree->arena->facts(current_root, frame.functional_context)
            : nullptr;
        if (!facts || facts->resolved_target.empty() ||
            facts->resolved_target ==
                "fn:__moss_test_perturbed_call_target__")
          throw RuntimeError(line,
              "MOSS_INTERNAL_INVARIANT checked call target fact was dropped or perturbed");
      }
      auto evaluate_call_argument = [&](std::size_t index) {
        if (call_argument_nodes && index < call_argument_nodes->size())
          return evaluate_tree_node(*tree, call_argument_nodes->at(index),
                                    frame, line, output);
        throw RuntimeError(line, "internal error: callable argument has no EST node");
      };
      // Fast Debug may see the whitespace-preserving spelling used by typed
      // empty-vector construction through a callable expression.
      std::string compact_callee = callee;
      compact_callee.erase(std::remove_if(compact_callee.begin(), compact_callee.end(),
          [](unsigned char ch) { return std::isspace(ch); }), compact_callee.end());
      if (args.empty() && compact_callee.rfind("Vector[", 0) == 0 &&
          compact_callee.size() > 8 && compact_callee.back() == ']')
        return Value::vector_value({}, compact_callee.substr(7, compact_callee.size() - 8));
      if (callee.find('.') == std::string::npos) {
        if (callee == "replace" && args.size() == 2) {
          Value replacement = evaluate_call_argument(1);
          const auto* replacement_identifier =
              call_argument_nodes && call_argument_nodes->size() > 1
                  ? std::get_if<est::Identifier>(
                        &tree->arena->at(call_argument_nodes->at(1)))
                  : nullptr;
          if (replacement_identifier &&
              frame.locals.count(replacement_identifier->name)) {
            auto type = frame.local_types.find(replacement_identifier->name);
            if (type == frame.local_types.end() || type->second.empty())
              throw RuntimeError(line, "internal error: replace replacement lacks a checked type");
            if (!copy_type_name(type->second)) {
              frame.locals.erase(replacement_identifier->name);
              frame.consumed.insert(replacement_identifier->name);
            }
          }
          auto place = call_argument_nodes && !call_argument_nodes->empty()
              ? locate_tree_node(*tree, call_argument_nodes->front(), frame,
                                 line, output)
              : Place{};
          if (!place.value)
            throw RuntimeError(line, "replace requires an assignable place");
          Value previous = std::move(*place.value);
          *place.value = std::move(replacement);
          if (place.domain)
            state_event("state_write", frame, line, place, summary(previous),
                        summary(*place.value));
          else emit("LocalWrite", frame, line, args[0]);
          return previous;
        }
        if (callee == "Some" && args.size() == 1) {
          Value some = Value::struct_value("option");
          some.object->case_name = "Some";
          some.object->fields["value"] = evaluate_call_argument(0);
          return some;
        }
        if (callee == "copy" && args.size() == 1) {
          Value val = evaluate_call_argument(0);
          return independent(val);
        }
        if (args.empty() && callee.rfind("Vector[", 0) == 0 &&
            callee.size() > 8 && callee.back() == ']')
          return Value::vector_value({}, callee.substr(7, callee.size() - 8));
        if (callee == "Queue" && args.empty()) return Value::queue_value();
        if (callee == "Map" && args.empty()) return Value::map_value();
        if (staged_abi_v10() && callee == "Set" && args.empty())
          return Value::set_value();
        if (callee == "assert") {
          if (args.size() != 1) throw RuntimeError(line, "assert expects one argument");
          if (!evaluate_call_argument(0).truthy()) {
            emit("AssertionFailure", frame, line, args.front());
            throw RuntimeError(line, "assertion failed: " + args.front());
          }
          return Value::unit();
        }
        if (callee == "assertEqual") {
          if (args.size() != 2) throw RuntimeError(line, "assertEqual expects two arguments");
          auto actual = evaluate_call_argument(0);
          auto expected = evaluate_call_argument(1);
          if (!equal(actual, expected)) {
            emit("AssertionFailure", frame, line, e);
            throw RuntimeError(line, "assertEqual failed: actual=" + actual.display() +
                               ", expected=" + expected.display());
          }
          return Value::unit();
        }
        if (callee == "sqrt") {
          if (args.size() != 1) throw RuntimeError(line, "sqrt expects one argument");
          auto value = evaluate_call_argument(0);
          return Value::float_value(std::sqrt(value.kind == Value::Kind::Int ? value.integer : value.floating));
        }
        if (callee == "is_nan" || callee == "is_finite") {
          if (args.size() != 1)
            throw RuntimeError(line, callee + " expects one Float argument");
          auto value = evaluate_call_argument(0);
          if (value.kind != Value::Kind::Float)
            throw RuntimeError(line, callee + " argument must have type Float");
          return Value::boolean_value(callee == "is_nan"
              ? std::isnan(value.floating) : std::isfinite(value.floating));
        }
        const std::string_view numeric_name = numeric_builtin_base(callee);
        if (numeric_name == "parse_int" || numeric_name == "parse_float") {
        auto argument = evaluate_call_argument(0);
          NumericParseError error = NumericParseError::Invalid;
          if (numeric_name == "parse_int") {
            std::int64_t number = 0;
            if (numeric_parse_int(argument.string, number, error))
              return Value::int_value(number);
          } else {
            double number = 0.0;
            if (numeric_parse_float(argument.string, number, error))
              return Value::float_value(number);
          }
          const std::string variant = error == NumericParseError::Invalid
              ? "Invalid" : "Overflow";
          const std::string error_type = numeric_builtin_error_type(callee);
          Value raised = Value::struct_value(error_type);
          raised.object->case_name = variant;
          emit("raise", frame, line, "variant:" + error_type + "." + variant);
          throw RaisedSignal{RaisedIdentity::enum_variant(error_type, variant),
                             std::move(raised), line, frame.source_file, {}};
        }
        if (numeric_name == "to_int") {
          auto argument = evaluate_call_argument(0);
          const double number = argument.floating;
          const std::string variant = !std::isfinite(number) ? "NonFinite" :
              (std::trunc(number) < -9223372036854775808.0 ||
               std::trunc(number) >= 9223372036854775808.0) ? "Overflow" : "";
          if (variant.empty()) return Value::int_value(static_cast<std::int64_t>(number));
          const std::string error_type = numeric_builtin_error_type(callee);
          Value raised = Value::struct_value(error_type);
          raised.object->case_name = variant;
          emit("raise", frame, line, "variant:" + error_type + "." + variant);
          throw RaisedSignal{RaisedIdentity::enum_variant(error_type, variant),
                             std::move(raised), line, frame.source_file, {}};
        }
        if (callee == "Char" && args.size() == 1 && !function(callee)) {
          auto cp_val = evaluate_call_argument(0);
          if (cp_val.kind != Value::Kind::Int)
            throw RuntimeError(line, "Char expects an Int codepoint");
          int64_t cp = cp_val.integer;
          if (cp < 0 || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF))
            throw RuntimeError(line, "ValueError: invalid Unicode scalar value " + std::to_string(cp));
          return Value::char_value(static_cast<uint32_t>(cp));
        }
        if (callee == "less" && args.size() == 2 && !function(callee)) {
          auto left = evaluate_call_argument(0);
          auto right = evaluate_call_argument(1);
          return Value::boolean_value(moss_less(left, right));
        }
        if (callee == "isequal" && args.size() == 2 && !function(callee)) {
          auto left = evaluate_call_argument(0);
          auto right = evaluate_call_argument(1);
          return Value::boolean_value(moss_isequal(left, right));
        }
        if (callee == "hash" && args.size() == 1 && !function(callee)) {
          auto val = evaluate_call_argument(0);
          return Value::int_value(moss_hash(val));
        }
        if (callee == "sort" && (args.size() == 1 || args.size() == 2) && !function(callee)) {
          Place place = call_argument_nodes && !call_argument_nodes->empty()
              ? locate_tree_node(*tree, call_argument_nodes->front(), frame,
                                 line, output)
              : Place{};
          if (!place.value || place.value->kind != Value::Kind::Vector || !place.value->vector)
            throw RuntimeError(line, "sort requires a writable Vector");
          auto& vec = *place.value->vector;
          if (args.size() == 1) {
            std::stable_sort(vec.begin(), vec.end(), [&](const Value& a, const Value& b) {
              return moss_less(a, b);
            });
          } else {
            if (!call_argument_nodes || call_argument_nodes->size() < 2)
              throw RuntimeError(line,
                  "MOSS_INTERNAL_INVARIANT missing checked sort key node");
            est::ExprId key_node = call_argument_nodes->at(1);
            if (const auto* named = std::get_if<est::NamedArgument>(
                    &tree->arena->at(key_node)))
              key_node = named->value;
            bool is_reverse = false;
            const auto& key_value = tree->arena->at(key_node);
            const std::vector<est::ExprId>* reverse_arguments = nullptr;
            est::ExprId reverse_callee = 0;
            if (const auto* call = std::get_if<est::Call>(&key_value)) {
              reverse_arguments = &call->arguments;
              reverse_callee = call->callee;
            } else if (const auto* constructor =
                           std::get_if<est::Constructor>(&key_value)) {
              reverse_arguments = &constructor->arguments;
              reverse_callee = constructor->type;
            }
            if (reverse_arguments && reverse_arguments->size() == 1 &&
                std::holds_alternative<est::Identifier>(
                    tree->arena->at(reverse_callee)) &&
                std::get<est::Identifier>(
                    tree->arena->at(reverse_callee)).name == "Reverse") {
              is_reverse = true;
              key_node = reverse_arguments->front();
              if (const auto* named = std::get_if<est::NamedArgument>(
                      &tree->arena->at(key_node)))
                key_node = named->value;
            }
            const auto* key_identifier = std::get_if<est::Identifier>(
                &tree->arena->at(key_node));
            std::stable_sort(vec.begin(), vec.end(), [&](const Value& a, const Value& b) {
              auto key = [&](const Value& item) {
                if (key_identifier && function(key_identifier->name))
                  return invoke_pipeline_callable(key_identifier->name,
                      {item}, frame, line, output);
                Frame callback = frame;
                callback.locals["_"] = item;
                return evaluate_tree_node(*tree, key_node, callback, line,
                                          output);
              };
              Value ka = key(a);
              Value kb = key(b);
              return is_reverse ? moss_less(kb, ka) : moss_less(ka, kb);
            });
          }
          return Value::unit();
        }
        if (auto fn = function(callee))
          return call(*fn, args, frame, line, output, tree,
                      call_argument_nodes);
        auto callable_local = frame.locals.find(callee);
        const Value* callable_val = callable_local != frame.locals.end() ? &callable_local->second : nullptr;
        if (!callable_val) {
          auto alias_it = frame.aliases.find(callee);
          if (alias_it != frame.aliases.end() && alias_it->second.value) {
            callable_val = alias_it->second.value;
          }
        }
        if (callable_val && callable_val->kind == Value::Kind::Callable) {
          if (auto fn = function(callable_val->string))
            return call(*fn, args, frame, line, output, tree,
                        call_argument_nodes);
          throw RuntimeError(line, program_.file_check_scope
              ? "internal error: FAST_DEBUG_UNCHECKED_EXECUTION: callable '" +
                    callable_val->string + "' was not checked in this run"
              : "unresolved callable identity '" + callable_val->string + "'");
        }
        if (auto object = object_type(callee))
          return construct(*object, args, frame, line, output, tree,
                           call_argument_nodes);
        auto self_it = frame.locals.find("self");
        if (self_it != frame.locals.end() && self_it->second.kind == Value::Kind::Struct && self_it->second.object) {
          if (auto target = method(self_it->second.object->type, callee)) {
            return call_method(*target, self_it->second, args, frame, line, output,
                               locate("self", frame, line, output), tree,
                               call_argument_nodes);
          }
        }
        throw RuntimeError(line, program_.file_check_scope
            ? "internal error: FAST_DEBUG_UNCHECKED_EXECUTION: callable '" +
                  callee + "' was not checked in this run"
            : "unsupported or unresolved callable '" + callee + "'");
      }
    }

    if (auto member = expression_member(*tree, current_root)) {
      const est::Member* member_node = nullptr;
      const std::vector<est::ExprId>* member_argument_nodes = nullptr;
      if (tree) {
        const auto& member_root = tree->arena->at(current_root);
        est::ExprId member_callee = current_root;
        if (const auto* call_node = std::get_if<est::Call>(&member_root)) {
          member_callee = call_node->callee;
          member_argument_nodes = &call_node->arguments;
        } else if (const auto* constructor_node =
                       std::get_if<est::Constructor>(&member_root)) {
          member_callee = constructor_node->type;
          member_argument_nodes = &constructor_node->arguments;
        }
        member_node = std::get_if<est::Member>(&tree->arena->at(member_callee));
      }
      auto evaluate_member_receiver = [&]() {
        if (member_node)
          return evaluate_tree_node(*tree, member_node->base, frame, line, output);
        throw RuntimeError(line, "internal error: member expression has no EST base");
      };
      if (member->second.find('(') != std::string::npos) {
        std::string method_name; std::vector<std::string> method_args;
        bool method_called = false;
        if (member_node && member_argument_nodes) {
          method_name = member_node->name;
          method_args.reserve(member_argument_nodes->size());
          for (const auto argument : *member_argument_nodes)
            method_args.emplace_back(expression_node_text(*tree, argument));
          method_called = true;
        } else {
          method_called = expression_call(member->second, method_name,
                                          method_args, "member_method");
        }
        if (method_called) {
          auto evaluate_method_argument = [&](std::size_t index) {
            if (member_argument_nodes && index < member_argument_nodes->size())
              return evaluate_tree_node(*tree, member_argument_nodes->at(index),
                                        frame, line, output);
            throw RuntimeError(line, "internal error: method argument has no EST node");
          };
          if ((method_name == "reserve" || method_name == "append") &&
              method_args.size() == 1) {
            Place receiver_place = locate(member->first, frame, line, output);
            if (receiver_place.value &&
                receiver_place.value->kind == Value::Kind::String) {
              Value argument = evaluate_method_argument(0);
              const std::string before = summary(*receiver_place.value);
              if (method_name == "reserve") {
                if (argument.kind != Value::Kind::Int)
                  throw RuntimeError(line, "String reserve requires an Int capacity");
                if (argument.integer > 0)
                  receiver_place.value->string.reserve(
                      static_cast<size_t>(argument.integer));
              } else {
                if (argument.kind != Value::Kind::String)
                  throw RuntimeError(line, "String append requires a String part");
                receiver_place.value->string += argument.string;
              }
              if (receiver_place.domain)
                state_event("state_write", frame, line, receiver_place, before,
                            summary(*receiver_place.value));
              else
                emit("LocalWrite", frame, line, member->first);
              return Value::unit();
            }
          }
          if (member->first == "Int" || member->first == "Float" || member->first == "Char" ||
              member->first == "String" || member->first == "Bool") {
            if (method_name == "less" && method_args.size() == 2) {
              Value a = evaluate_method_argument(0);
              Value b = evaluate_method_argument(1);
              return Value::boolean_value(moss_less(a, b));
            }
            if (method_name == "isequal" && method_args.size() == 2) {
              Value a = evaluate_method_argument(0);
              Value b = evaluate_method_argument(1);
              return Value::boolean_value(moss_isequal(a, b));
            }
            if (method_name == "hash" && method_args.size() == 1) {
              Value a = evaluate_method_argument(0);
              return Value::int_value(moss_hash(a));
            }
          }
          auto receiver = evaluate_member_receiver();
          if (tree && member_node && member_argument_nodes &&
              ((method_name == "pop" && method_args.empty() &&
                receiver.kind == Value::Kind::Queue) ||
               (method_name == "remove" && method_args.size() == 1 &&
                receiver.kind == Value::Kind::Map))) {
            const auto operation = method_name == "pop"
                ? TypedCollectionOutcomeFactV1::Operation::QueuePop
                : TypedCollectionOutcomeFactV1::Operation::MapRemove;
            const auto* outcome = resolve_collection_outcome_fact(
                *tree, current_root, operation, frame, line);
            if (outcome) {
              if (method_name == "remove") (void)evaluate_method_argument(0);
              raise_collection_error(frame, line,
                  outcome->error_variant_identity == "Empty"
                      ? "Empty" : "MissingKey");
            }
          }
          if (receiver.kind == Value::Kind::Float) {
            if (method_name == "to_string" && method_args.empty()) {
              return Value::string_value(rust_f64_to_string(receiver.floating));
            }
            if (method_name == "less" && method_args.size() == 1) {
              Value other = evaluate_method_argument(0);
              return Value::boolean_value(moss_less(receiver, other));
            }
            if (method_name == "isequal" && method_args.size() == 1) {
              Value other = evaluate_method_argument(0);
              return Value::boolean_value(moss_isequal(receiver, other));
            }
            if (method_name == "hash" && method_args.empty()) {
              return Value::int_value(moss_hash(receiver));
            }
          }
          if (receiver.kind == Value::Kind::Int) {
            if (method_name == "to_string" && method_args.empty()) {
              return Value::string_value(std::to_string(receiver.integer));
            }
            if (method_name == "less" && method_args.size() == 1) {
              Value other = evaluate_method_argument(0);
              return Value::boolean_value(moss_less(receiver, other));
            }
            if (method_name == "isequal" && method_args.size() == 1) {
              Value other = evaluate_method_argument(0);
              return Value::boolean_value(moss_isequal(receiver, other));
            }
            if (method_name == "hash" && method_args.empty()) {
              return Value::int_value(moss_hash(receiver));
            }
          }
          if (receiver.kind == Value::Kind::Char) {
            if (method_name == "to_string" && method_args.empty()) {
              return Value::string_value(utf8_from_codepoint(receiver.codepoint));
            }
            if (method_name == "codepoint" && method_args.empty()) {
              return Value::int_value(static_cast<int64_t>(receiver.codepoint));
            }
            if (method_name == "less" && method_args.size() == 1) {
              Value other = evaluate_method_argument(0);
              return Value::boolean_value(moss_less(receiver, other));
            }
            if (method_name == "isequal" && method_args.size() == 1) {
              Value other = evaluate_method_argument(0);
              return Value::boolean_value(moss_isequal(receiver, other));
            }
            if (method_name == "hash" && method_args.empty()) {
              return Value::int_value(moss_hash(receiver));
            }
          }
          if (receiver.kind == Value::Kind::String) {
            auto characters = [&]() {
              std::vector<std::string> result;
              const std::string& s = receiver.string;
              for (size_t i = 0; i < s.size();) {
                unsigned char byte = static_cast<unsigned char>(s[i]);
                size_t width = byte < 0x80 ? 1 : (byte & 0xe0) == 0xc0 ? 2 :
                    (byte & 0xf0) == 0xe0 ? 3 : (byte & 0xf8) == 0xf0 ? 4 : 0;
                if (!width || i + width > s.size())
                  throw RuntimeError(line, "invalid UTF-8 String");
                for (size_t j = 1; j < width; ++j)
                  if ((static_cast<unsigned char>(s[i+j]) & 0xc0) != 0x80)
                    throw RuntimeError(line, "invalid UTF-8 String");
                result.push_back(s.substr(i, width));
                i += width;
              }
              return result;
            };
            if (method_name == "length" && method_args.empty())
              return Value::int_value(static_cast<int64_t>(characters().size()));
            if (method_name == "char_at" && method_args.size() == 1) {
              Value index = evaluate_method_argument(0);
              auto chars = characters();
              if (index.kind != Value::Kind::Int || index.integer < 0 ||
                  static_cast<uint64_t>(index.integer) >= chars.size())
                throw RuntimeError(line, "String character index out of bounds");
              uint32_t cp = decode_char(chars[static_cast<size_t>(index.integer)]);
              return Value::char_value(cp);
            }
            if (method_name == "chars" && method_args.empty()) {
              std::vector<Value> result;
              for (const auto& c : characters())
                result.push_back(Value::char_value(decode_char(c)));
              return Value::vector_value(std::move(result), "char");
            }
            if (method_name == "less" && method_args.size() == 1) {
              Value other = evaluate_method_argument(0);
              return Value::boolean_value(moss_less(receiver, other));
            }
            if (method_name == "isequal" && method_args.size() == 1) {
              Value other = evaluate_method_argument(0);
              return Value::boolean_value(moss_isequal(receiver, other));
            }
            if (method_name == "hash" && method_args.empty()) {
              return Value::int_value(moss_hash(receiver));
            }
            if (method_name == "find" && method_args.size() == 1) {
              Value pattern = evaluate_method_argument(0);
              if (pattern.kind != Value::Kind::String)
                throw RuntimeError(line, "String find requires a String pattern");
              size_t byte = receiver.string.find(pattern.string);
              if (byte == std::string::npos) return Value::int_value(-1);
              int64_t index = 0;
              for (size_t i = 0; i < byte; ++i)
                if ((static_cast<unsigned char>(receiver.string[i]) & 0xc0) != 0x80)
                  ++index;
              return Value::int_value(index);
            }
            if (method_name == "slice" && method_args.size() == 2) {
              Value start = evaluate_method_argument(0);
              Value length = evaluate_method_argument(1);
              if (start.kind != Value::Kind::Int || length.kind != Value::Kind::Int)
                throw RuntimeError(line, "String slice requires Int start and length");
              auto chars = characters();
              if (start.integer < 0 || length.integer < 0 ||
                  static_cast<uint64_t>(start.integer) > chars.size() ||
                  static_cast<uint64_t>(length.integer) >
                      chars.size() - static_cast<uint64_t>(start.integer))
                return Value::string_value("");
              std::string result;
              const size_t begin = static_cast<size_t>(start.integer);
              const size_t end = begin + static_cast<size_t>(length.integer);
              for (size_t index = begin; index < end; ++index) result += chars[index];
              return Value::string_value(std::move(result));
            }
            if (method_name == "ascii_lower" && method_args.empty()) {
              std::string result = receiver.string;
              for (char& byte : result)
                if (byte >= 'A' && byte <= 'Z') byte = static_cast<char>(byte + ('a' - 'A'));
              return Value::string_value(std::move(result));
            }
            if (method_name == "ascii_upper" && method_args.empty()) {
              std::string result = receiver.string;
              for (char& byte : result)
                if (byte >= 'a' && byte <= 'z') byte = static_cast<char>(byte - ('a' - 'A'));
              return Value::string_value(std::move(result));
            }
            if (method_name == "split" && method_args.size() == 1) {
              Value separator = evaluate_method_argument(0);
              if (separator.kind != Value::Kind::String || separator.string.empty())
                throw RuntimeError(line, "String split requires a nonempty separator");
              std::vector<Value> result;
              size_t start = 0;
              while (true) {
                size_t at = receiver.string.find(separator.string, start);
                if (at == std::string::npos) {
                  result.push_back(Value::string_value(receiver.string.substr(start)));
                  break;
                }
                result.push_back(Value::string_value(receiver.string.substr(start, at-start)));
                start = at + separator.string.size();
              }
              return Value::vector_value(std::move(result));
            }
            if (method_name == "join" && method_args.size() == 1) {
              Value parts = evaluate_method_argument(0);
              if (parts.kind != Value::Kind::Vector)
                throw RuntimeError(line, "String join requires Vector[String]");
              std::string joined;
              for (const auto& part : *parts.vector) {
                if (part.kind != Value::Kind::String)
                  throw RuntimeError(line, "String join requires Vector[String]");
                if (!joined.empty() || &part != &parts.vector->front()) joined += receiver.string;
                joined += part.string;
              }
              return Value::string_value(joined);
            }
          }
          if (receiver.kind == Value::Kind::Vector || receiver.kind == Value::Kind::Queue) {
            auto values = receiver.kind == Value::Kind::Vector ? receiver.vector : receiver.queue;
            if (method_name == "push" && method_args.size() == 1) {
              values->push_back(evaluate_method_argument(0));
              return Value::unit();
            }
            if (method_name == "pop" && method_args.empty()) {
              if (values->empty())
                raise_collection_error(frame, line, "Empty");
              Value result = receiver.kind == Value::Kind::Queue ? values->front() : values->back();
              if (receiver.kind == Value::Kind::Queue) values->erase(values->begin());
              else values->pop_back();
              return result;
            }
            if (method_name == "remove" && method_args.size() == 1) {
              Value idx_val = evaluate_method_argument(0);
              if (idx_val.kind != Value::Kind::Int) throw RuntimeError(line, "remove expects integer index");
              int64_t idx = idx_val.integer;
              if (idx < 0 || static_cast<size_t>(idx) >= values->size())
                raise_collection_error(frame, line, "InvalidIndex");
              Value result = (*values)[idx];
              values->erase(values->begin() + idx);
              return result;
            }
            if (method_name == "swap_remove" && method_args.size() == 1) {
              Value idx_val = evaluate_method_argument(0);
              if (idx_val.kind != Value::Kind::Int) throw RuntimeError(line, "swap_remove expects integer index");
              int64_t idx = idx_val.integer;
              if (idx < 0 || static_cast<size_t>(idx) >= values->size())
                raise_collection_error(frame, line, "InvalidIndex");
              Value result = (*values)[idx];
              if (static_cast<size_t>(idx) + 1 < values->size()) {
                (*values)[idx] = values->back();
              }
              values->pop_back();
              return result;
            }
          }
          if (receiver.kind == Value::Kind::Map) {
            if (method_name == "delete" && method_args.size() == 3) {
              Value key = evaluate_method_argument(0);
              Value fallback = evaluate_method_argument(1);
              if (auto position = map_position(receiver, key, frame, line, output)) {
                Value removed = independent(receiver.map->at(*position).second);
                receiver.map->erase(receiver.map->begin() + *position);
                if (staged_abi_v10()) {
                  receiver.map_hashes->erase(receiver.map_hashes->begin() + *position);
                  rebuild_map_index(receiver);
                }
                assign(method_args[2], Value::boolean_value(true), frame, line, output);
                return removed;
              }
              assign(method_args[2], Value::boolean_value(false), frame, line, output);
              return independent(fallback);
            }
            if (method_name == "remove" && method_args.size() == 1) {
              Value key = evaluate_method_argument(0);
              if (auto position = map_position(receiver, key, frame, line, output)) {
                Value removed = independent(receiver.map->at(*position).second);
                receiver.map->erase(receiver.map->begin() + *position);
                if (staged_abi_v10()) {
                  receiver.map_hashes->erase(receiver.map_hashes->begin() + *position);
                  rebuild_map_index(receiver);
                }
                return removed;
              }
              raise_collection_error(frame, line, "MissingKey");
            }
            if (method_name == "get" && method_args.size() == 1) {
              Value key = evaluate_method_argument(0);
              if (auto position = map_position(receiver, key, frame, line, output))
                return independent(receiver.map->at(*position).second);
              raise_collection_error(frame, line, "MissingKey");
            }
            if (method_name == "get" && method_args.size() == 2) {
              Value key = evaluate_method_argument(0);
              Value fallback = evaluate_method_argument(1);
              if (auto position = map_position(receiver, key, frame, line, output))
                return independent(receiver.map->at(*position).second);
              return independent(fallback);
            }
            if ((method_name == "keys" || method_name == "values") && method_args.empty()) {
              std::vector<Value> values;
              for (const auto& entry : *receiver.map)
                values.push_back(independent(method_name == "keys" ? entry.first : entry.second));
              return Value::vector_value(std::move(values));
            }
          }
          if (staged_abi_v10() && receiver.kind == Value::Kind::Set) {
            if (method_name == "values" && method_args.empty()) {
              std::vector<Value> values;
              for (const auto& key : *receiver.set)
                values.push_back(independent(key));
              return Value::vector_value(std::move(values));
            }
            if ((method_name == "insert" || method_name == "contains") &&
                method_args.size() == 1) {
              Value key = evaluate_method_argument(0);
              std::int64_t hash = 0;
              if (set_position(receiver, key, frame, line, output, &hash))
                return method_name == "contains" ? Value::boolean_value(true)
                                                 : Value::unit();
              if (method_name == "contains") return Value::boolean_value(false);
              (*receiver.set_index)[hash].push_back(receiver.set->size());
              receiver.set->push_back(independent(key));
              return Value::unit();
            }
          }
          if (receiver.kind != Value::Kind::Struct || !receiver.object)
            throw RuntimeError(line, "method receiver is not a Moss object");
          auto target = method(receiver.object->type, method_name);
          if (!target)
            throw RuntimeError(line, program_.file_check_scope
                ? "internal error: FAST_DEBUG_UNCHECKED_EXECUTION: method '" +
                      method_name + "' was not checked in this run"
                : "unresolved method '" + method_name + "'");
          return call_method(*target, receiver, method_args, frame, line, output,
                             locate(member->first, frame, line, output), tree,
                             member_argument_nodes);
        }
      }
      auto receiver = evaluate_member_receiver();
      if (receiver.kind != Value::Kind::Struct || !receiver.object)
        throw RuntimeError(line, "field receiver is not a Moss object");
      auto field = receiver.object->fields.find(member->second);
      if (field == receiver.object->fields.end()) throw RuntimeError(line, "unknown field '" + member->second + "'");
      return field->second;
    }

    if (tree) {
      const auto* identifier =
          std::get_if<est::Identifier>(&tree->arena->at(current_root));
      if (identifier) {
        const auto& name = identifier->name;
        auto alias = frame.aliases.find(name);
        if (alias != frame.aliases.end() && alias->second.value) {
          emit("LocalRead", frame, line, name);
          return *alias->second.value;
        }
        auto found = frame.locals.find(name);
        if (found != frame.locals.end()) {
          emit("LocalRead", frame, line, name);
          return found->second;
        }
        if (function(name)) return Value::callable_value(name);
        if (name == "None") {
          Value none = Value::struct_value("option");
          none.object->case_name = "None";
          return none;
        }
        throw RuntimeError(line, "unknown local '" + name + "'");
      }
    } else {
      throw RuntimeError(line,
          "MOSS_INTERNAL_INVARIANT expression execution has no checked tree");
    }
    throw RuntimeError(line, "unsupported expression '" + e + "'");
  }

  // Native lowering hands String literals to Rust unchanged, so decode the
  // same escape set: \n \r \t \0 \\ \' \" \xHH (ASCII) and \u{HEX}.
  static std::string decode_string(const std::string& value) {
    std::string result;
    for (size_t i = 0; i < value.size(); ++i) {
      char c = value[i];
      if (c != '\\' || i + 1 >= value.size()) { result.push_back(c); continue; }
      char e = value[++i];
      if (e == 'n') result.push_back('\n');
      else if (e == 'r') result.push_back('\r');
      else if (e == 't') result.push_back('\t');
      else if (e == '0') result.push_back('\0');
      else if (e == 'x' && i + 2 < value.size() &&
               std::isxdigit(static_cast<unsigned char>(value[i + 1])) &&
               std::isxdigit(static_cast<unsigned char>(value[i + 2]))) {
        result.push_back(static_cast<char>(std::stoi(value.substr(i + 1, 2), nullptr, 16)));
        i += 2;
      } else if (e == 'u' && i + 1 < value.size() && value[i + 1] == '{') {
        size_t close = value.find('}', i + 2);
        if (close == std::string::npos) { result += "\\u"; continue; }
        result += utf8_from_codepoint(static_cast<uint32_t>(
            std::stoul(value.substr(i + 2, close - i - 2), nullptr, 16)));
        i = close;
      } else result.push_back(e);
    }
    return result;
  }

  static std::vector<std::string> split_arguments_legacy(const std::string& input) {
    std::vector<std::string> result; int p = 0, b = 0, s = 0; bool quoted = false; size_t start = 0;
    for (size_t i = 0; i < input.size(); ++i) {
      char c = input[i];
      if (quoted) {
        if (c == '\\') ++i;
        else if (c == '"') quoted = false;
        continue;
      }
      if (c == '"') { quoted = true; continue; }
      if (c == '(') ++p; else if (c == ')') --p; else if (c == '[') ++b; else if (c == ']') --b;
      else if (c == '{') ++s; else if (c == '}') --s;
      else if (c == ',' && p == 0 && b == 0 && s == 0) {
        result.push_back(trim_copy(input.substr(start, i - start))); start = i + 1;
      }
    }
    auto tail = trim_copy(input.substr(start)); if (!tail.empty()) result.push_back(tail);
    return result;
  }

  static inline std::int64_t moss_shl(std::int64_t value, std::int64_t shift) {
    if (shift < 0 || shift >= 64) return 0;
    return static_cast<std::int64_t>(static_cast<std::uint64_t>(value) << static_cast<std::uint32_t>(shift));
  }

  static inline std::int64_t moss_shr(std::int64_t value, std::int64_t shift) {
    if (shift < 0 || shift >= 64) return value < 0 ? -1 : 0;
    return value >> shift;
  }

  static bool equal(const Value& left, const Value& right) {
    if (left.kind == Value::Kind::Int && right.kind == Value::Kind::Float)
      return static_cast<double>(left.integer) == right.floating;
    if (left.kind == Value::Kind::Float && right.kind == Value::Kind::Int)
      return left.floating == static_cast<double>(right.integer);
    if (left.kind != right.kind) return false;
    switch (left.kind) {
      case Value::Kind::DomainHandle: return left.domain == right.domain;
      case Value::Kind::Unit: return true;
      case Value::Kind::Bool: return left.boolean == right.boolean;
      case Value::Kind::Int: return left.integer == right.integer;
      // IEEE-754 operator semantics: NaN != NaN and -0.0 == +0.0. Total
      // isequal semantics are reserved for isequal() and Map key identity.
      case Value::Kind::Float: return left.floating == right.floating;
      case Value::Kind::Char: return left.codepoint == right.codepoint;
      case Value::Kind::String: return left.string == right.string;
      case Value::Kind::Callable: return left.string == right.string;
      case Value::Kind::Struct:
        if (!left.object || !right.object || left.object->type != right.object->type ||
            left.object->case_name != right.object->case_name ||
            left.object->fields.size() != right.object->fields.size()) return false;
        for (const auto& entry : left.object->fields) {
          auto other = right.object->fields.find(entry.first);
          if (other == right.object->fields.end() || !equal(entry.second, other->second)) return false;
        }
        return true;
      case Value::Kind::Vector:
        if (!left.vector || !right.vector || left.vector->size() != right.vector->size()) return !left.vector && !right.vector;
        for (size_t i = 0; i < left.vector->size(); ++i) if (!equal((*left.vector)[i], (*right.vector)[i])) return false;
        return true;
      case Value::Kind::Queue:
        if (!left.queue || !right.queue || left.queue->size() != right.queue->size()) return !left.queue && !right.queue;
        for (size_t i = 0; i < left.queue->size(); ++i) if (!equal((*left.queue)[i], (*right.queue)[i])) return false;
        return true;
      case Value::Kind::Map:
        if (!left.map || !right.map || left.map->size() != right.map->size()) return !left.map && !right.map;
        for (const auto& entry : *left.map) {
          bool found = false;
          for (const auto& other : *right.map)
            if (key_equal(entry.first, other.first) && equal(entry.second, other.second)) { found = true; break; }
          if (!found) return false;
        }
        return true;
      case Value::Kind::Set:
        // Set operator equality is not in the staged source contract.
        return false;
    }
    return false;
  }

  // Map key identity follows key-protocol-v1 isequal, not operator ==.
  static bool key_equal(const Value& left, const Value& right) {
    if (left.kind == Value::Kind::Float && right.kind == Value::Kind::Float)
      return moss_isequal(left, right);
    return equal(left, right);
  }

  [[noreturn]] void raise_collection_error(Frame& frame, int line,
                                            const std::string& variant) {
    const std::string error_type = "std__CollectionError";
    const auto identity = RaisedIdentity::enum_variant(error_type, variant);
    if (!active_checked_exceptional_alternatives_ ||
        !active_checked_exceptional_alternatives_->count(identity))
      throw RuntimeError(line,
          "MOSS_INTERNAL_INVARIANT missing checked CollectionError variant fact: " +
          identity.canonical());
    Value raised = Value::struct_value(error_type);
    raised.object->case_name = variant;
    emit("raise", frame, line,
         "variant:" + error_type + "." + variant);
    throw RaisedSignal{identity,
                       std::move(raised), line, frame.source_file, {}};
  }

  std::optional<size_t> map_position(const Value& receiver, const Value& key,
                                     Frame& frame, int line, std::ostream& output,
                                     std::int64_t* key_hash = nullptr) {
    if (staged_abi_v10() && receiver.map_index) {
      const std::int64_t hash = protocol_key_hash(key, frame, line, output);
      if (key_hash) *key_hash = hash;
      auto bucket = receiver.map_index->find(hash);
      if (bucket == receiver.map_index->end()) return std::nullopt;
      for (size_t position : bucket->second)
        if (protocol_key_equal(receiver.map->at(position).first, key,
                               frame, line, output)) return position;
      return std::nullopt;
    }
    for (size_t position = 0; position < receiver.map->size(); ++position)
      if (protocol_key_equal(receiver.map->at(position).first, key,
                             frame, line, output)) return position;
    return std::nullopt;
  }

  std::optional<size_t> set_position(const Value& receiver, const Value& key,
                                     Frame& frame, int line, std::ostream& output,
                                     std::int64_t* key_hash = nullptr) {
    const std::int64_t hash = protocol_key_hash(key, frame, line, output);
    if (key_hash) *key_hash = hash;
    auto bucket = receiver.set_index->find(hash);
    if (bucket == receiver.set_index->end()) return std::nullopt;
    for (size_t position : bucket->second)
      if (protocol_key_equal(receiver.set->at(position), key,
                             frame, line, output)) return position;
    return std::nullopt;
  }

  static void rebuild_map_index(Value& receiver) {
    receiver.map_index->clear();
    for (size_t position = 0; position < receiver.map_hashes->size(); ++position)
      (*receiver.map_index)[receiver.map_hashes->at(position)].push_back(position);
  }

  int64_t protocol_key_hash(const Value& key, Frame& frame, int line,
                            std::ostream& output) {
    if (staged_abi_v10() && key.kind == Value::Kind::Struct && key.object) {
      const Method* selected = method(key.object->type, "hash");
      if (!selected) throw RuntimeError(line, "missing nominal key hash proof");
      Value result = call_method(*selected, key, {}, frame, line, output, {});
      if (result.kind != Value::Kind::Int)
        throw RuntimeError(line, "nominal key hash must return Int");
      return result.integer;
    }
    return moss_hash(key);
  }

  bool protocol_key_equal(const Value& left, const Value& right,
                          Frame& frame, int line, std::ostream& output) {
    if (!staged_abi_v10() || left.kind != Value::Kind::Struct ||
        right.kind != Value::Kind::Struct)
      return key_equal(left, right);
    if (!left.object || !right.object || left.object->type != right.object->type)
      return false;
    if (protocol_key_hash(left, frame, line, output) !=
        protocol_key_hash(right, frame, line, output)) return false;
    const Method* selected = method(left.object->type, "isequal");
    if (!selected) throw RuntimeError(line, "missing nominal key equality proof");
    Frame args = frame;
    args.locals["__moss_set_key_other"] = independent(right);
    Value result = call_method(*selected, left, {"__moss_set_key_other"},
                               args, line, output, {});
    if (result.kind != Value::Kind::Bool)
      throw RuntimeError(line, "nominal key equality must return Bool");
    return result.boolean;
  }

  static bool moss_less(const Value& left, const Value& right) {
    if (left.kind == Value::Kind::Int && right.kind == Value::Kind::Int) {
      return left.integer < right.integer;
    }
    if (left.kind == Value::Kind::Float && right.kind == Value::Kind::Float) {
      if (std::isnan(left.floating)) return false;
      if (std::isnan(right.floating)) return true;
      if (left.floating == 0.0 && right.floating == 0.0) {
        uint64_t lb = 0, rb = 0;
        std::memcpy(&lb, &left.floating, sizeof(lb));
        std::memcpy(&rb, &right.floating, sizeof(rb));
        return (lb >> 63) > (rb >> 63);
      }
      return left.floating < right.floating;
    }
    if (left.kind == Value::Kind::Char && right.kind == Value::Kind::Char) {
      return left.codepoint < right.codepoint;
    }
    if (left.kind == Value::Kind::String && right.kind == Value::Kind::String) {
      return left.string < right.string;
    }
    if (left.kind == Value::Kind::Bool && right.kind == Value::Kind::Bool) {
      return !left.boolean && right.boolean;
    }
    return false;
  }

  static bool moss_isequal(const Value& left, const Value& right) {
    if (left.kind == Value::Kind::Int && right.kind == Value::Kind::Int) {
      return left.integer == right.integer;
    }
    if (left.kind == Value::Kind::Float && right.kind == Value::Kind::Float) {
      if (std::isnan(left.floating) && std::isnan(right.floating)) return true;
      if (left.floating == 0.0 && right.floating == 0.0) {
        uint64_t lb = 0, rb = 0;
        std::memcpy(&lb, &left.floating, sizeof(lb));
        std::memcpy(&rb, &right.floating, sizeof(rb));
        return lb == rb;
      }
      return left.floating == right.floating;
    }
    if (left.kind == Value::Kind::Char && right.kind == Value::Kind::Char) {
      return left.codepoint == right.codepoint;
    }
    if (left.kind == Value::Kind::String && right.kind == Value::Kind::String) {
      return left.string == right.string;
    }
    if (left.kind == Value::Kind::Bool && right.kind == Value::Kind::Bool) {
      return left.boolean == right.boolean;
    }
    return false;
  }

  static int64_t moss_hash(const Value& val) {
    if (val.kind == Value::Kind::Int) return val.integer;
    if (val.kind == Value::Kind::Float) {
      if (std::isnan(val.floating)) return static_cast<int64_t>(0x7ff8000000000000ULL);
      uint64_t bits = 0;
      std::memcpy(&bits, &val.floating, sizeof(bits));
      return static_cast<int64_t>(bits);
    }
    if (val.kind == Value::Kind::Char) return static_cast<int64_t>(val.codepoint);
    if (val.kind == Value::Kind::String) {
      uint64_t h = 0xcbf29ce484222325ULL;
      for (unsigned char b : val.string) {
        h ^= b;
        h *= 0x100000001b3ULL;
      }
      return static_cast<int64_t>(h);
    }
    if (val.kind == Value::Kind::Bool) return val.boolean ? 1 : 0;
    return 0;
  }

  Value construct(const ObjectType& object, const std::vector<std::string>& args,
                  Frame& frame, int line, std::ostream& output,
                  const ExpressionTreeView* tree = nullptr,
                  const std::vector<est::ExprId>* argument_nodes = nullptr) {
    Value result = Value::struct_value(object.name);
    std::unordered_map<std::string,std::string> supplied;
    std::unordered_map<std::string,est::ExprId> supplied_nodes;
    for (std::size_t index = 0; index < args.size(); ++index) {
      const auto& argument = args[index];
      if (tree && argument_nodes && index < argument_nodes->size()) {
        const auto node_id = argument_nodes->at(index);
        if (const auto* named = std::get_if<est::NamedArgument>(
                &tree->arena->at(node_id))) {
          supplied[named->name] = std::string(
              expression_node_text(*tree, named->value));
          supplied_nodes[named->name] = named->value;
          continue;
        }
      }
      auto equal_sign = argument.find_first_of(":=");
      if (equal_sign == std::string::npos)
        throw RuntimeError(line, "object constructors require named fields");
      supplied[trim_copy(argument.substr(0, equal_sign))] =
          argument.substr(equal_sign + 1);
      if (tree && argument_nodes && index < argument_nodes->size())
        supplied_nodes[trim_copy(argument.substr(0, equal_sign))] =
            argument_nodes->at(index);
    }
    for (const auto& field : object.fields) {
      auto found = supplied.find(field.name);
      if (found != supplied.end())
        if (auto node = supplied_nodes.find(field.name);
            tree && node != supplied_nodes.end())
          result.object->fields[field.name] = evaluate_tree_node(
              *tree, node->second, frame, line, output);
        else
          result.object->fields[field.name] = evaluate_expression_source(
              found->second, frame, line, output);
      else if (!field.init.empty()) {
        ExpressionHandlesScope field_handles_scope(
            active_expression_handles_, &field.expression_handles);
        result.object->fields[field.name] = evaluate_expression_source(
            field.init, frame, field.line, output,
            std::pair{est::ExprSlot::Initializer, 0u});
      }
      else result.object->fields[field.name] = default_semantic_value(field.type, frame, line, output);
    }
    for (const auto& entry : supplied)
      if (!result.object->fields.count(entry.first))
        throw RuntimeError(line, "unknown field '" + entry.first + "'");
    return result;
  }

  Value call(const Function& target, const std::vector<std::string>& arguments,
             Frame& caller, int line, std::ostream& output,
             const ExpressionTreeView* tree = nullptr,
             const std::vector<est::ExprId>* argument_nodes = nullptr,
             const std::vector<Value>* argument_values = nullptr) {
    if (arguments.size() != target.params.size())
      throw RuntimeError(line, "wrong number of arguments for '" + target.name + "'");
    Frame frame;
    frame.function = target.name;
    const auto* specialization = specialization_for_call(target, caller, line);
    if (!specialization && checked_call_requires_specialization(target, caller, line)) {
#ifndef NDEBUG
      if (const char* count = std::getenv("MOSS_TEST_COUNT_FACT");
          count && std::string(count) == "g4")
        std::cerr << "MOSS_TEST_FACT_COUNTER|g4|line=" << line
                  << "|context=" << caller.functional_context
                  << "|callee=" << target.name << "\n";
#endif
    }
    frame.functional_context = functional_function_context(target, specialization);
    frame.specialization_key = specialization
        ? specialization->stable_type_key : std::string();
    frame.source_file = target.execution_source_file.empty()
        ? target.source_file : target.execution_source_file;
    frame.semantic_identity = "fn:" + target.name + "@" +
        std::to_string(target.line);
    if (specialization)
      frame.specialization_identity = frame.functional_context + "@" +
          std::to_string(target.line);
    bind_parameters(target.params,
                    specialization ? specialization->parameter_effects : target.parameter_effects,
                    arguments, caller, frame, line, output,
                    specialization ? &specialization->parameter_types : nullptr,
                    tree, argument_nodes, argument_values);
    int enter_id = emit("FunctionEnter", frame, target.line);
    EventScope function_scope(*this, enter_id, true);
    const auto& executable_body = target.execution_body
        ? *target.execution_body : target.body;
    Flow flow = execute(executable_body, frame, output);
    ExpressionHandlesScope result_handles_scope(
        active_expression_handles_, &target.expression_handles);
    Value result = flow.returned ? flow.value :
        (target.execution_result_expression
             ? evaluate_expression_source(*target.execution_result_expression,
                   frame, target.result_line, output,
                   std::pair{est::ExprSlot::ReturnValue, 0u})
             : target.result_expression
                 ? evaluate_expression_source(*target.result_expression, frame,
                       target.result_line, output,
                       std::pair{est::ExprSlot::ReturnValue, 0u})
                 : Value::unit());
    if (!flow.returned)
      emit("Return", frame, target.result_line ? target.result_line : target.line,
           result.display());
    emit("FunctionExit", frame, target.result_line ? target.result_line : target.line);
    return result;
  }

  Value call_method(const Method& target, Value receiver,
                    const std::vector<std::string>& arguments, Frame& caller,
                    int line, std::ostream& output, Place origin,
                    const ExpressionTreeView* tree = nullptr,
                    const std::vector<est::ExprId>* argument_nodes = nullptr) {
    if (arguments.size() != target.params.size()) throw RuntimeError(line, "wrong number of method arguments");
    Frame frame;
    frame.function = target.owner + "." + target.name;
    frame.functional_context = functional_method_context(target);
    frame.source_file = target.execution_source_file.empty()
        ? target.source_file : target.execution_source_file;
    frame.semantic_identity = "method:" + target.owner + "." +
        target.name + "@" + std::to_string(target.line);
    frame.locals["self"] = receiver;
    for (auto& field : receiver.object->fields)
      frame.aliases[field.first] = {&field.second, origin.domain,
          origin.domain ? origin.path + "." + field.first : ""};
    if (origin.domain) frame.aliases["self"] = {&frame.locals.at("self"), origin.domain, origin.path};
    if (target.receiver_effect == Effect::Consume && origin.value) {
      if (origin.domain) state_event("state_consume", caller, line, origin, summary(*origin.value), "<moved>");
      *origin.value = Value::unit();
    }
    bind_parameters(target.params, target.parameter_effects, arguments, caller,
                    frame, line, output, nullptr, tree, argument_nodes);
    int enter_id = emit("MethodEnter", frame, target.line);
    EventScope method_scope(*this, enter_id, true);
    const auto& executable_body = target.execution_body
        ? *target.execution_body : target.body;
    Flow flow = execute(executable_body, frame, output);
    ExpressionHandlesScope result_handles_scope(
        active_expression_handles_, &target.expression_handles);
    Value result = flow.returned ? flow.value :
        (target.execution_result_expression
             ? evaluate_expression_source(*target.execution_result_expression,
                   frame, target.result_line, output,
                   std::pair{est::ExprSlot::ReturnValue, 0u})
             : target.result_expression
                 ? evaluate_expression_source(*target.result_expression, frame,
                       target.result_line, output,
                       std::pair{est::ExprSlot::ReturnValue, 0u})
                 : Value::unit());
    if (!flow.returned)
      emit("Return", frame, target.result_line ? target.result_line : target.line,
           result.display());
    emit("MethodExit", frame, target.result_line ? target.result_line : target.line);
    return result;
  }

  void assign(const std::string& target, Value value, Frame& frame, int line,
              std::ostream& output,
              const ExpressionTreeView* tree = nullptr,
              std::optional<est::ExprId> target_node = std::nullopt) {
    auto name = trim_copy(target);
    if (name == "_") return;
    auto bracket = name.find('[');
    if (bracket != std::string::npos && name.back() == ']') {
      Value receiver;
      Value key;
      const auto* index_node = tree && target_node
          ? std::get_if<est::Index>(&tree->arena->at(*target_node)) : nullptr;
      if (index_node) {
        receiver = evaluate_tree_node(*tree, index_node->base, frame, line,
                                      output);
      } else {
        receiver = evaluate_expression_source(name.substr(0, bracket), frame,
                                             line, output);
      }
      if (receiver.kind == Value::Kind::Map && receiver.map) {
        key = index_node
            ? evaluate_tree_node(*tree, index_node->index, frame, line, output)
            : evaluate_expression_source(
                  name.substr(bracket + 1, name.size() - bracket - 2), frame,
                  line, output);
        std::int64_t hash = 0;
        if (auto position = map_position(receiver, key, frame, line, output, &hash)) {
          receiver.map->at(*position).second = std::move(value);
          return;
        }
        if (staged_abi_v10()) {
          (*receiver.map_index)[hash].push_back(receiver.map->size());
          receiver.map_hashes->push_back(hash);
        }
        receiver.map->push_back({std::move(key), std::move(value)});
        return;
      }
    }
    auto place = tree && target_node
        ? locate_tree_node(*tree, *target_node, frame, line, output)
        : locate(name, frame, line, output);
    if (place.value) {
      std::string before = summary(*place.value);
      *place.value = std::move(value);
      if (place.domain) state_event("state_write", frame, line, place, before, summary(*place.value));
      else {
        int event_id = emit("LocalWrite", frame, line, name);
        if (event_id) {
          trace_.back().before = before;
          trace_.back().after = summary(*place.value);
        }
      }
    } else if (binding_identifier(name)) {
      std::string after = summary(value);
      // An implicit `name = expr` declares, unless it reinitializes a
      // consumed binding that is still lexically in scope.
      if (!frame.consumed.erase(name)) declare_local(frame, name);
      frame.locals[name] = std::move(value);
      int event_id = emit("LocalWrite", frame, line, name);
      if (event_id) trace_.back().after = after;
    } else throw RuntimeError(line, "unsupported assignment target '" + name + "'");
  }

  const TypedEchoArgumentFactV1& resolve_echo_argument_fact(
      const Stmt& statement, const Frame& frame, size_t argument_index,
      int line) const {
    if (!program_.typed_semantics)
      throw RuntimeError(line,
          "MOSS_INTERNAL_INVARIANT typed Echo argument facts are unavailable");
    const est::Handle* matched_handle = nullptr;
    for (const auto& handle : statement.expression_handles) {
      if (handle.slot != est::ExprSlot::Argument ||
          handle.argument_index != argument_index)
        continue;
      if (matched_handle)
        throw RuntimeError(line,
            "MOSS_INTERNAL_INVARIANT duplicate Echo argument expression handles");
      matched_handle = &handle;
    }
    if (!matched_handle)
      throw RuntimeError(line,
          "MOSS_INTERNAL_INVARIANT missing Echo argument expression handle");
    std::string context = frame.functional_context;
    if (!statement.echo_types_by_context.count(context)) {
      if (!statement.echo_types_by_context.count(std::string()))
        throw RuntimeError(line,
            "MOSS_INTERNAL_INVARIANT missing exact Echo specialization context");
      context.clear();
    }
    try {
      std::string query_context = context;
#ifdef MOSS_TEST_BUILD
      if (std::getenv("MOSS_TEST_CONSUMER_WRONG_ECHO_ARGUMENT_CONTEXT"))
        query_context += ":wrong-context";
#endif
      const auto& requirement =
          program_.typed_semantics->find_echo_argument_requirement(
              matched_handle->arena_index, matched_handle->expr,
              static_cast<std::uint32_t>(argument_index), query_context);
      auto exact_requirement = requirement;
#ifdef MOSS_TEST_BUILD
      if (requirement.mode ==
              TypedEchoArgumentModeV1::BorrowedVectorElement &&
          std::getenv(
              "MOSS_TEST_CONSUMER_PERTURB_ECHO_ARGUMENT_CONTEXT"))
        exact_requirement.est_fact_context += ":consumer-perturbed";
#endif
      const auto& fact = program_.typed_semantics->find_echo_argument_fact(
          exact_requirement);
      if (!fact.is_authoritative() || fact.required_access != Effect::Read ||
          !fact.display_result_non_escaping || fact.permits_copy)
        throw std::invalid_argument(
            "checked Echo argument fact is not authoritative for display");
      return fact;
    } catch (const std::exception& error) {
      throw RuntimeError(line,
          "MOSS_INTERNAL_INVARIANT " + std::string(error.what()));
    }
  }

  const TypedLocalOwnershipFactV1* resolve_local_ownership_fact(
      const Stmt& s, const Frame& frame, int line) const {
    if (!program_.typed_semantics) return nullptr;

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

    const std::string context = frame.functional_context.empty() ? "main" : frame.functional_context;

    const TypedLocalOwnershipRequirementV1* requirement = nullptr;
    const TypedLocalOwnershipFactV1* found_fact = nullptr;
    try {
      requirement = program_.typed_semantics->find_local_ownership_requirement(
          matched_handle->arena_index, matched_handle->expr, context);
      if (!requirement) {
        return nullptr;
      }
      found_fact = &program_.typed_semantics->find_local_ownership_fact(*requirement);
    } catch (const std::exception& e) {
      throw RuntimeError(line,
          "MOSS_INTERNAL_INVARIANT " + std::string(e.what()));
    }

    if (!found_fact) {
      throw RuntimeError(line,
          "MOSS_INTERNAL_INVARIANT required local ownership fact record missing");
    }

    if (found_fact->is_intentionally_unsupported()) {
      return nullptr;
    }

    if (!found_fact->is_authoritative()) {
      throw RuntimeError(line,
          "MOSS_INTERNAL_INVARIANT local ownership fact unavailable availability=" +
          std::to_string(static_cast<int>(found_fact->availability)));
    }

    const est::Arena* arena = nullptr;
    if (found_fact->est_arena_index < program_.expression_arenas.size() &&
        program_.expression_arenas[found_fact->est_arena_index]) {
      arena = program_.expression_arenas[found_fact->est_arena_index].get();
    } else if (found_fact->est_arena_index == 0 && program_.expression_arena) {
      arena = program_.expression_arena.get();
    }
    if (!arena) {
      throw RuntimeError(line, "MOSS_INTERNAL_INVARIANT missing EST arena");
    }

    const auto* tree_facts = arena->facts(found_fact->expression_occurrence_id,
                                          found_fact->specialization_context);
    if (!tree_facts || !tree_facts->inferred_effect ||
        *tree_facts->inferred_effect != found_fact->access) {
      throw RuntimeError(line,
          "MOSS_INTERNAL_INVARIANT contradictory EST ownership fact in arena");
    }

    return found_fact;
  }

  std::vector<const TypedLoopOwnershipFactV1*> resolve_loop_ownership_facts(
      const Stmt& s, const Frame& frame, int line) const {
    if (!program_.typed_semantics) return {};

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

    const std::string context = frame.functional_context.empty() ? "main" : frame.functional_context;

    std::vector<const TypedLoopOwnershipFactV1*> facts;
    for (const auto& req : program_.typed_semantics->required_loop_ownership_occurrences) {
      if (req.loop_est_arena_index == matched_handle->arena_index &&
          req.loop_expression_occurrence_id == matched_handle->expr &&
          req.specialization_context == context &&
          req.specialization_identity == frame.specialization_key) {
        if (req.availability != TypedLoopOwnershipFactV1::Availability::Present)
          continue;
        try {
          const auto* fact = program_.typed_semantics->find_loop_ownership_fact(req);
          if (!fact) {
            throw RuntimeError(line, "MOSS_INTERNAL_INVARIANT required loop ownership fact missing");
          }
          facts.push_back(fact);
        } catch (const std::exception& e) {
          throw RuntimeError(line, "MOSS_INTERNAL_INVARIANT " + std::string(e.what()));
        }
      }
    }
    for (const auto& fact : program_.typed_semantics->loop_ownership_facts) {
      if (fact.loop_est_arena_index == matched_handle->arena_index &&
          fact.loop_expression_occurrence_id == matched_handle->expr &&
          fact.specialization_context == context &&
          fact.specialization_identity == frame.specialization_key) {
        if (fact.availability != TypedLoopOwnershipFactV1::Availability::Present)
          continue;
        bool found_req = false;
        for (const auto& req : program_.typed_semantics->required_loop_ownership_occurrences) {
          if (req.loop_est_arena_index == fact.loop_est_arena_index &&
              req.loop_expression_occurrence_id == fact.loop_expression_occurrence_id &&
              req.specialization_context == fact.specialization_context &&
              req.specialization_identity == fact.specialization_identity &&
              req.definition_identity == fact.definition_identity &&
              req.edge_kind == fact.edge_kind) {
            found_req = true;
            break;
          }
        }
        if (!found_req) {
          throw RuntimeError(line, "MOSS_INTERNAL_INVARIANT loop ownership requirement missing for fact");
        }
      }
    }
    return facts;
  }

  std::vector<const TypedExceptionalOwnershipFactV1*> resolve_exceptional_ownership_facts(
      const Stmt& s, const Frame& frame, int line) const {
    if (!program_.typed_semantics) return {};
    (void)s;
    const std::string context = frame.functional_context.empty() ? "main" : frame.functional_context;

    std::vector<const TypedExceptionalOwnershipFactV1*> facts;
    for (const auto& req : program_.typed_semantics->required_exceptional_ownership_occurrences) {
      if (req.specialization_context == context &&
          req.specialization_identity == frame.specialization_key) {
        if (req.availability != TypedExceptionalOwnershipFactV1::Availability::Present)
          continue;
        try {
          const auto& fact = program_.typed_semantics->find_exceptional_ownership_fact(req);
          facts.push_back(&fact);
        } catch (const std::exception& e) {
          throw RuntimeError(line, "MOSS_INTERNAL_INVARIANT " + std::string(e.what()));
        }
      }
    }
    for (const auto& fact : program_.typed_semantics->exceptional_ownership_facts) {
      if (fact.specialization_context == context &&
          fact.specialization_identity == frame.specialization_key) {
        if (fact.availability != TypedExceptionalOwnershipFactV1::Availability::Present)
          continue;
        bool found_req = false;
        for (const auto& req : program_.typed_semantics->required_exceptional_ownership_occurrences) {
          if (req.source_statement_occurrence_id == fact.source_statement_occurrence_id &&
              req.specialization_context == fact.specialization_context &&
              req.specialization_identity == fact.specialization_identity &&
              req.recovery_statement_occurrence_id == fact.recovery_statement_occurrence_id &&
              req.raised_variant_identity == fact.raised_variant_identity) {
            found_req = true;
            break;
          }
        }
        if (!found_req) {
          throw RuntimeError(line, "MOSS_INTERNAL_INVARIANT exceptional ownership requirement missing for fact");
        }
      }
    }
    return facts;
  }

  const TypedMatchOwnershipFactV1* resolve_match_ownership_fact(
      const Stmt& s, const Frame& frame, int line) const {
    if (!program_.typed_semantics)
      throw RuntimeError(line,
          "MOSS_INTERNAL_INVARIANT typed match semantics are unavailable");
    const est::Handle* matched_handle = nullptr;
    for (const auto& handle : s.expression_handles) {
      if (handle.slot == est::ExprSlot::Scrutinee && handle.argument_index == 0) {
        matched_handle = &handle;
        break;
      }
    }
    if (!matched_handle)
      throw RuntimeError(line,
          "MOSS_INTERNAL_INVARIANT checked match scrutinee occurrence is missing");
    const std::string context = frame.functional_context.empty() ? "main" : frame.functional_context;
    std::string active_specialization_identity = frame.specialization_key;
    const TypedMatchOwnershipRequirementV1* requirement = nullptr;
#ifdef MOSS_TEST_BUILD
    const bool drop_match_requirement =
        std::getenv("MOSS_TEST_CONSUMER_DROP_MATCH_REQUIREMENT") != nullptr;
    if (std::getenv("MOSS_TEST_CONSUMER_COLLIDE_MATCH_SPECIALIZATION_IDENTITY")) {
      const auto delimiter = context.find('<');
      active_specialization_identity = context.substr(0, delimiter);
    }
#else
    constexpr bool drop_match_requirement = false;
#endif
    size_t requirement_count = 0;
    for (const auto& req : program_.typed_semantics->required_match_occurrences) {
      if (!drop_match_requirement &&
          req.est_arena_index == matched_handle->arena_index &&
          req.scrutinee_occurrence_id == matched_handle->expr &&
          req.specialization_context == context &&
          req.specialization_identity == active_specialization_identity) {
        requirement = &req;
        ++requirement_count;
      }
    }
    if (requirement_count != 1 || !requirement)
      throw RuntimeError(line,
          "MOSS_INTERNAL_INVARIANT required checked match requirement missing or ambiguous");
#ifdef MOSS_TEST_BUILD
    if (std::getenv("MOSS_TEST_CONSUMER_PERTURB_MATCH_ARM_ACCESS")) {
      auto corrupted = *requirement;
      if (corrupted.arms.empty())
        throw RuntimeError(line,
            "MOSS_INTERNAL_INVARIANT cannot perturb empty checked match arms");
      auto& arm = corrupted.arms.front();
      arm.payload_access = arm.payload_access == Effect::Write
          ? Effect::Read : Effect::Write;
      try {
        (void)program_.typed_semantics->find_match_ownership_fact(corrupted);
      } catch (const std::exception& e) {
        throw RuntimeError(line, "MOSS_INTERNAL_INVARIANT " + std::string(e.what()));
      }
      throw RuntimeError(line,
          "MOSS_INTERNAL_INVARIANT perturbed match access was accepted");
    }
    if (std::getenv("MOSS_TEST_CONSUMER_DROP_MATCH_FACT")) {
      auto corrupted = *program_.typed_semantics;
      auto fact = std::find_if(corrupted.match_ownership_facts.begin(),
          corrupted.match_ownership_facts.end(), [&](const auto& candidate) {
            return candidate.est_arena_index == requirement->est_arena_index &&
                candidate.scrutinee_occurrence_id == requirement->scrutinee_occurrence_id &&
                candidate.statement_occurrence_id == requirement->statement_occurrence_id &&
                candidate.specialization_identity == requirement->specialization_identity &&
                candidate.specialization_context == requirement->specialization_context;
          });
      if (fact == corrupted.match_ownership_facts.end())
        throw RuntimeError(line,
            "MOSS_INTERNAL_INVARIANT cannot drop absent checked match fact");
      corrupted.match_ownership_facts.erase(fact);
      try {
        (void)corrupted.find_match_ownership_fact(*requirement);
      } catch (const std::exception& e) {
        throw RuntimeError(line, "MOSS_INTERNAL_INVARIANT " + std::string(e.what()));
      }
      throw RuntimeError(line,
          "MOSS_INTERNAL_INVARIANT missing checked match fact was accepted");
    }
#endif
    try {
      return program_.typed_semantics->find_match_ownership_fact(*requirement);
    } catch (const std::exception& e) {
      throw RuntimeError(line, "MOSS_INTERNAL_INVARIANT " + std::string(e.what()));
    }
  }

  const TypedCollectionOutcomeFactV1* resolve_collection_outcome_fact(
      const ExpressionTreeView& tree, est::ExprId operation_expression,
      TypedCollectionOutcomeFactV1::Operation operation,
      const Frame& frame, int line) const {
    if (!program_.typed_semantics || !tree.arena)
      throw RuntimeError(line,
          "MOSS_INTERNAL_INVARIANT typed collection outcome semantics are unavailable");
    const std::string context = frame.functional_context.empty()
        ? "main" : frame.functional_context;
    const TypedCollectionOutcomeRequirementV1* requirement = nullptr;
    std::size_t count = 0;
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
         program_.typed_semantics->required_collection_outcome_occurrences) {
      if (!drop_requirement && candidate.operation == operation &&
          candidate.operation_est_arena_index == tree.arena_index &&
          candidate.operation_expression_occurrence_id == operation_expression &&
          candidate.specialization_identity == frame.specialization_key &&
          candidate.specialization_context == context) {
        requirement = &candidate;
        ++count;
        if (duplicate_requirement) ++count;
      }
    }
    if (count != 1 || !requirement) {
      const auto* semantic = tree.arena->facts(operation_expression, context);
      if (count || (semantic && semantic->type &&
                    semantic->type->kind == TypeKind::Never))
        throw RuntimeError(line,
            "MOSS_INTERNAL_INVARIANT required checked collection outcome missing or ambiguous");
      return nullptr;
    }
    try {
#ifdef MOSS_TEST_BUILD
      if (std::getenv("MOSS_TEST_CONSUMER_DROP_COLLECTION_OUTCOME_FACT"))
        throw std::invalid_argument(
            "required checked collection outcome fact missing");
      if (std::getenv("MOSS_TEST_CONSUMER_DUPLICATE_COLLECTION_OUTCOME_FACT"))
        throw std::invalid_argument(
            "duplicate checked collection outcome facts");
      if (std::getenv("MOSS_TEST_CONSUMER_PERTURB_COLLECTION_OUTCOME_REQUIREMENT") ||
          std::getenv("MOSS_TEST_CONSUMER_PERTURB_COLLECTION_OUTCOME_FACT")) {
        auto corrupted = *requirement;
        corrupted.error_variant_identity = "forged";
        (void)program_.typed_semantics->find_collection_outcome_fact(corrupted);
        throw RuntimeError(line,
            "MOSS_INTERNAL_INVARIANT perturbed collection outcome authority unexpectedly passed");
      }
#endif
      return program_.typed_semantics->find_collection_outcome_fact(*requirement);
    } catch (const RuntimeError&) {
      throw;
    } catch (const std::exception& error) {
      throw RuntimeError(line,
          "MOSS_INTERNAL_INVARIANT " + std::string(error.what()));
    }
  }

  static const std::unordered_map<std::string, std::string>&
  checked_joined_types(const Stmt& statement, const Frame& frame) {
    if (moss_test_drop_fact("joins") || moss_test_perturb_fact("joins"))
      throw RuntimeError(statement.line,
          "MOSS_INTERNAL_INVARIANT checked join fact was dropped or perturbed");
    auto exact = statement.joined_types_by_context.find(frame.functional_context);
    return exact == statement.joined_types_by_context.end()
        ? statement.joined_types : exact->second;
  }

  Flow execute_block(const std::vector<Stmt>& statements, Frame& frame,
                     std::ostream& output, size_t begin, size_t end,
                     const Stmt* join_statement = nullptr) {
    std::set<std::string> joined_names;
    if (join_statement) {
      const auto& joined_types = checked_joined_types(*join_statement, frame);
      for (const auto& [name, _] : joined_types) joined_names.insert(name);
    }
    BlockScope scope(frame, joined_names.empty() ? nullptr : &joined_names);
    try {
      return execute(statements, frame, output, begin, end);
    } catch (RaisedSignal& raised) {
      record_edge_locals(frame, raised);
      throw;
    }
  }

  // Innermost visible binding wins: deeper blocks unwind (and record) first.
  static void record_edge_locals(const Frame& frame, RaisedSignal& raised) {
    if (frame.scopes.empty()) return;
    for (const auto& entry : frame.scopes.back()) {
      if (raised.edge_locals.count(entry.name)) continue;
      auto value = frame.locals.find(entry.name);
      if (value != frame.locals.end()) raised.edge_locals[entry.name] = value->second;
    }
  }

  Flow execute(const std::vector<Stmt>& statements, Frame& frame,
               std::ostream& output, size_t begin = 0, size_t end = SIZE_MAX) {
    if (end == SIZE_MAX) end = statements.size();
    size_t index = begin;
    while (index < end) {
      const Stmt& statement = statements[index];
      RaiseSet checked_exceptional_alternatives =
          statement.exceptional_alternatives;
#ifdef MOSS_TEST_BUILD
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
#endif
      struct ExceptionalAlternativesScope {
        const RaiseSet*& slot;
        const RaiseSet* prior;
        ExceptionalAlternativesScope(const RaiseSet*& target,
                                     const RaiseSet& current)
            : slot(target), prior(target) { slot = &current; }
        ~ExceptionalAlternativesScope() { slot = prior; }
      } exceptional_scope(active_checked_exceptional_alternatives_,
                          checked_exceptional_alternatives);
      ExpressionHandlesScope expression_handles_scope(
          active_expression_handles_, &statement.expression_handles);
      if (statement.kind == Stmt::Kind::Try) {
        auto ex_facts = resolve_exceptional_ownership_facts(statement, frame, statement.line);
        size_t try_end = index + 1;
        while (try_end < end && statements[try_end].indent > statement.indent)
          ++try_end;
        size_t after = try_end;
        while (after < end &&
               statements[after].kind == Stmt::Kind::Recover &&
               statements[after].indent == statement.indent) {
          ++after;
          while (after < end &&
                 statements[after].indent > statement.indent)
            ++after;
        }
        try {
          Flow flow = execute_block(statements, frame, output, index + 1, try_end,
                                    &statement);
          if (flow.returned) return flow;
        } catch (const RaisedSignal& raised) {
          size_t arm = try_end;
          bool handled = false;
          while (arm < after) {
            const Stmt& recovery = statements[arm];
            size_t arm_end = arm + 1;
            while (arm_end < after &&
                   statements[arm_end].indent > recovery.indent)
              ++arm_end;
            if (!handled && recovery_matches(recovery, raised.identity)) {
              handled = true;
              for (const auto* fact : ex_facts) {
                const bool variant_matches =
                    raised.identity.kind == RaisedIdentityKind::EnumVariant
                        ? (!raised.identity.variant.empty() &&
                           fact->raised_variant_identity.find(raised.identity.variant) != std::string::npos &&
                           fact->raised_variant_identity.find(raised.identity.type) != std::string::npos)
                        : (fact->raised_variant_identity.find(raised.identity.type) != std::string::npos ||
                           fact->raised_variant_identity.find(raised.identity.canonical()) != std::string::npos);
                if (variant_matches) {
                  for (const auto& bs : fact->state_at_raise) {
                    if (bs.consumed) {
                      frame.locals.erase(bs.name);
                      frame.consumed.insert(bs.name);
                    } else if (!bs.name.empty()) {
                      frame.consumed.erase(bs.name);
                    }
                  }
                }
              }
              emit("recover", frame, recovery.line,
                   raised.identity.canonical());
              const bool bound = !recovery.a.empty() &&
                  recovery.est_pattern &&
                  recovery.est_pattern->kind == est::PatternNode::Kind::RecoveryBinding;
              struct SavedBinding {
                std::string name;
                std::optional<Value> value;
                std::optional<std::string> type;
              };
              std::vector<SavedBinding> saved;
              const auto bind = [&](const std::string& name, Value value,
                                    const std::string& type) {
                SavedBinding prior;
                prior.name = name;
                if (auto found = frame.locals.find(name);
                    found != frame.locals.end())
                  prior.value = found->second;
                if (auto found = frame.local_types.find(name);
                    found != frame.local_types.end())
                  prior.type = found->second;
                saved.push_back(std::move(prior));
                frame.locals[name] = std::move(value);
                frame.local_types[name] = type;
              };
              const auto restore = [&](bool preserve_joined = false) {
                const auto& joined_types = checked_joined_types(statement, frame);
                for (auto item = saved.rbegin(); item != saved.rend(); ++item) {
                  if (preserve_joined && joined_types.count(item->name)) continue;
                  if (item->value) frame.locals[item->name] = *item->value;
                  else frame.locals.erase(item->name);
                  if (item->type) frame.local_types[item->name] = *item->type;
                  else frame.local_types.erase(item->name);
                }
              };
              if (bound) {
                bind(recovery.est_pattern->binding, independent(raised.value),
                     raised.identity.type);
              }
              for (const auto& item : error_pattern_payloads(*recovery.est_pattern, raised, recovery.line))
                bind(item.first, item.second.first, item.second.second);
              selected_errors_.push_back(raised);
              try {
                Flow flow = execute_block(
                    statements, frame, output, arm + 1, arm_end,
                    &statement);
                selected_errors_.pop_back();
                restore(!flow.returned);
                if (flow.returned) return flow;
              } catch (...) {
                selected_errors_.pop_back();
                restore();
                throw;
              }
            }
            arm = arm_end;
          }
          if (!handled) throw;
        }
        index = after;
        continue;
      }
      if (statement.kind == Stmt::Kind::Match) {
        Value selected = evaluate_expression_source(statement.a, frame,
            statement.line, output,
            std::pair{est::ExprSlot::Scrutinee, 0u});
        if (selected.kind != Value::Kind::Struct || !selected.object ||
            selected.object->case_name.empty())
          throw RuntimeError(statement.line, "match requires an enum value");
        if (statement.b == "consume" && binding_identifier(statement.a)) {
          frame.locals.erase(statement.a);
          frame.consumed.insert(statement.a);
        }
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
        const auto* match_fact = resolve_match_ownership_fact(statement, frame, statement.line);
        if (!match_fact || !match_fact->scrutinee_type)
          throw RuntimeError(statement.line,
              "MOSS_INTERNAL_INVARIANT checked match fact lacks its scrutinee type");
        const bool is_option_match =
            match_fact->scrutinee_kind == TypedMatchScrutineeKindV1::Option;
        const std::string checked_match_type =
            match_fact->scrutinee_type->to_canonical_string();
        if (is_option_match &&
            (match_fact->scrutinee_type->kind != TypeKind::Builtin ||
             match_fact->scrutinee_type->name != "option"))
          throw RuntimeError(statement.line,
              "MOSS_INTERNAL_INVARIANT checked Option match fact has a non-Option type");
        if (!is_option_match && selected.object->type != checked_match_type)
          throw RuntimeError(statement.line,
              "MOSS_INTERNAL_INVARIANT runtime enum type contradicts checked match fact");
        const EnumType* declaration = nullptr;
        if (is_option_match) {
          declaration = &option_enum;
        } else {
          for (const auto& candidate : program_.enums)
            if (candidate.name == checked_match_type) declaration = &candidate;
        }
        if (!declaration) throw RuntimeError(statement.line, "unknown matched enum");
        size_t cursor = index + 1;
        bool taken = false;
        Flow flow;
        while (cursor < end && statements[cursor].indent == statement.indent + 1) {
          const Stmt& arm = statements[cursor];
          size_t arm_end = cursor + 1;
          while (arm_end < end && statements[arm_end].indent > arm.indent) ++arm_end;
          const auto* pattern = arm.est_pattern ? &*arm.est_pattern : nullptr;
          if (!pattern || (pattern->kind != est::PatternNode::Kind::EnumTag &&
                           pattern->kind != est::PatternNode::Kind::EnumCase))
            throw RuntimeError(arm.line,
                "MOSS_INTERNAL_INVARIANT checked enum pattern is absent");
          if (pattern->case_name == "__moss_test_perturbed_pattern__")
            throw RuntimeError(arm.line,
                "MOSS_INTERNAL_INVARIANT checked enum pattern was perturbed");
          if (pattern->case_name == selected.object->case_name) {
            taken = true;
            emit("BranchTaken", frame, arm.line, pattern->case_name);
            std::unordered_map<std::string,std::optional<Value>> saved_locals;
            std::unordered_map<std::string,std::optional<std::string>> saved_local_types;
            std::unordered_map<std::string,std::optional<Place>> saved_aliases;
            const EnumCase* item = nullptr;
            for (const auto& candidate : declaration->cases)
              if (candidate.name == pattern->case_name) item = &candidate;
            if (!item) throw RuntimeError(arm.line, "unknown enum case");
            const auto checked_arm = std::find_if(
                match_fact->arms.begin(), match_fact->arms.end(),
                [&](const TypedMatchArmFactV1& candidate) {
                  return candidate.case_name == pattern->case_name;
                });
            if (!pattern->payload_bindings.empty() &&
                (checked_arm == match_fact->arms.end() ||
                 checked_arm->payload_binding_names != pattern->payload_bindings ||
                 checked_arm->payload_types.size() != pattern->payload_bindings.size()))
              throw RuntimeError(arm.line,
                  "MOSS_INTERNAL_INVARIANT checked match arm fact is missing or contradictory");
            for (size_t field_index = 0; field_index < pattern->payload_bindings.size(); ++field_index) {
              const auto& binding = pattern->payload_bindings[field_index];
              auto prior_local = frame.locals.find(binding);
              saved_locals[binding] = prior_local == frame.locals.end()
                  ? std::nullopt : std::optional<Value>(prior_local->second);
              auto prior_type = frame.local_types.find(binding);
              saved_local_types[binding] = prior_type == frame.local_types.end()
                  ? std::nullopt : std::optional<std::string>(prior_type->second);
              auto prior_alias = frame.aliases.find(binding);
              saved_aliases[binding] = prior_alias == frame.aliases.end()
                  ? std::nullopt : std::optional<Place>(prior_alias->second);
              frame.locals.erase(binding);
              frame.local_types.erase(binding);
              frame.aliases.erase(binding);
              auto field = is_option_match
                  ? selected.object->fields.find("value")
                  : selected.object->fields.find(item->fields[field_index].name);
              if (field == selected.object->fields.end())
                throw RuntimeError(arm.line, "missing enum payload");
              if (statement.b == "consume" ||
                  (checked_arm->payload_access != Effect::Write &&
                   copy_type_name(item->fields[field_index].type)))
                frame.locals[binding] = field->second;
              else
                frame.aliases[binding] = Place{&field->second, nullptr, {}};
              const auto& payload_type = checked_arm->payload_types[field_index];
              if (!payload_type)
                throw RuntimeError(arm.line,
                    "MOSS_INTERNAL_INVARIANT checked match payload type is missing");
              frame.local_types[binding] = payload_type->to_canonical_string();
            }
            flow = execute_block(statements, frame, output, cursor + 1, arm_end,
                                 &statement);
            for (const auto& binding : pattern->payload_bindings) {
              frame.locals.erase(binding);
              frame.local_types.erase(binding);
              frame.aliases.erase(binding);
              if (saved_locals.at(binding))
                frame.locals[binding] = *saved_locals.at(binding);
              if (saved_local_types.at(binding))
                frame.local_types[binding] = *saved_local_types.at(binding);
              if (saved_aliases.at(binding))
                frame.aliases[binding] = *saved_aliases.at(binding);
            }
          }
          cursor = arm_end;
        }
        if (!taken) throw RuntimeError(statement.line, "non-exhaustive checked match");
        if (flow.returned) return flow;
        index = cursor;
        continue;
      }
      if (statement.kind == Stmt::Kind::If) {
        size_t body_end = index + 1;
        while (body_end < end && statements[body_end].indent > statement.indent) ++body_end;
        size_t after = body_end, else_end = body_end;
        if (body_end < end && statements[body_end].kind == Stmt::Kind::Else &&
            statements[body_end].indent == statement.indent) {
          else_end = body_end + 1;
          while (else_end < end && statements[else_end].indent > statement.indent) ++else_end;
          after = else_end;
        }
        bool condition = evaluate_expression_source(statement.a, frame,
            statement.line, output,
            std::pair{est::ExprSlot::Condition, 0u}).truthy();
        int branch_id = emit("BranchTaken", frame, statement.line,
                             condition ? "then" : "else");
        Flow flow;
        {
          EventScope branch_scope(*this, branch_id, false, false, true);
          flow = condition
              ? execute_block(statements, frame, output, index + 1, body_end,
                              &statement)
              : (after != body_end ? execute_block(statements, frame, output, body_end + 1, else_end,
                                                   &statement)
                                   : Flow{});
        }
        if (flow.returned) return flow;
        index = after;
        continue;
      }
      if (statement.kind == Stmt::Kind::Else) { ++index; continue; }
      if (statement.kind == Stmt::Kind::While) {
        auto loop_facts = resolve_loop_ownership_facts(statement, frame, statement.line);
        size_t body_end = index + 1;
        while (body_end < end && statements[body_end].indent > statement.indent) ++body_end;
        // No interpreter-only iteration cap: a checked program must run the
        // same number of iterations as native code.
        uint64_t iterations = 0;
        while (evaluate_expression_source(statement.a, frame, statement.line,
                   output, std::pair{est::ExprSlot::Condition, 0u}).truthy()) {
          ++iterations;
          int loop_id = emit("LoopIteration", frame, statement.line, std::to_string(iterations));
          Flow flow;
          {
            EventScope loop_scope(*this, loop_id, false, false, true);
            flow = execute_block(statements, frame, output, index + 1, body_end);
          }
          if (flow.returned) return flow;
          for (const auto* fact : loop_facts) {
            if (fact->edge_kind == TypedLoopOwnershipFactV1::EdgeKind::Backedge &&
                fact->value_state == TypedLoopOwnershipFactV1::ValueState::Available) {
              frame.consumed.erase(fact->binding_identity);
            }
          }
        }
        index = body_end;
        continue;
      }
      if (statement.kind == Stmt::Kind::For) {
        auto loop_facts = resolve_loop_ownership_facts(statement, frame, statement.line);
        size_t body_end = index + 1;
        while (body_end < end && statements[body_end].indent > statement.indent) ++body_end;
        auto iterable_tree = expression_tree(statement.b, "for_iterable",
            std::pair{est::ExprSlot::Iterable, 0u});
        if (!iterable_tree)
          throw RuntimeError(statement.line,
              "MOSS_INTERNAL_INVARIANT missing checked for-iterable node");
        std::string callee;
        std::vector<std::string> range_args;
        bool is_range = expression_call(*iterable_tree, iterable_tree->root,
                                        callee, range_args) &&
            callee == "range";
        const auto* range_call = is_range
            ? std::get_if<est::Call>(
                  &iterable_tree->arena->at(iterable_tree->root))
            : nullptr;

        Flow flow;
        uint64_t iterations = 0;
        // The loop variable is scoped to the loop, like native `for x in ..`;
        // an outer binding of the same name is restored afterwards.
        BlockScope loop_variable_scope(frame);
        try {
        const auto run_iteration = [&](Value element) {
          ++iterations;
          frame.locals[statement.a] = std::move(element);
          emit("LocalWrite", frame, statement.line, statement.a);
          int loop_id = emit("LoopIteration", frame, statement.line, std::to_string(iterations));
          EventScope loop_scope(*this, loop_id, false, false, true);
          flow = execute_block(statements, frame, output, index + 1, body_end);
          for (const auto* fact : loop_facts) {
            if (fact->edge_kind == TypedLoopOwnershipFactV1::EdgeKind::Backedge &&
                fact->value_state == TypedLoopOwnershipFactV1::ValueState::Available) {
              frame.consumed.erase(fact->binding_identity);
            }
          }
          return !flow.returned;
        };

        if (is_range) {
          if (!range_call || range_call->arguments.size() < 2 ||
              range_call->arguments.size() > 3)
            throw RuntimeError(statement.line, "range() expects 2 or 3 arguments");
          int64_t start = evaluate_tree_node(*iterable_tree,
              range_call->arguments[0], frame, statement.line, output).integer;
          int64_t stop = evaluate_tree_node(*iterable_tree,
              range_call->arguments[1], frame, statement.line, output).integer;
          int64_t step = 1;
          if (range_call->arguments.size() == 3) {
            step = evaluate_tree_node(*iterable_tree,
                range_call->arguments[2], frame, statement.line, output).integer;
            if (step <= 0) throw RuntimeError(statement.line, "range() step must be positive");
          }
          declare_local(frame, statement.a);
          frame.local_types[statement.a] = "int";
          // Native lowering is Rust `start..stop` (`.step_by(step)`), which
          // never overflows; stop instead of wrapping past Int max.
          for (int64_t cur = start; cur < stop;) {
            if (!run_iteration(Value::int_value(cur))) break;
            int64_t next = 0;
            if (__builtin_add_overflow(cur, step, &next)) break;
            cur = next;
          }
        } else {
          Value iterable = evaluate_tree_node(*iterable_tree,
              iterable_tree->root, frame, statement.line, output);
          declare_local(frame, statement.a);
          if (iterable.kind == Value::Kind::Vector) {
            auto source = iterable.vector;
            if (!iterable.element_type.empty())
              frame.local_types[statement.a] = iterable.element_type;
            const auto elements = *source;
            for (const auto& element : elements)
              if (!run_iteration(element)) break;
          } else if (iterable.kind == Value::Kind::Struct && iterable.object) {
            const Method* next = method(iterable.object->type, "next");
            const Method* factory = next ? nullptr : method(iterable.object->type, "iter");
            Value iterator = iterable;
            if (factory) {
              iterator = call_method(*factory, iterable, {}, frame, statement.line,
                                     output, locate(statement.b, frame,
                                                    statement.line, output));
              if (iterator.kind != Value::Kind::Struct || !iterator.object)
                throw RuntimeError(statement.line,
                                   "internal error: checked Iterator factory returned no object");
              next = method(iterator.object->type, "next");
            }
            if (!next)
              throw RuntimeError(statement.line,
                                 "internal error: checked Iterator has no next method");
            if (!statement.semantic_type.empty())
              frame.local_types[statement.a] = statement.semantic_type;
            while (true) {
              Value result = call_method(*next, iterator, {}, frame,
                  statement.line, output,
                  locate(statement.b, frame, statement.line, output));
              if (result.kind != Value::Kind::Struct || !result.object ||
                  result.object->type != "option")
                throw RuntimeError(statement.line,
                                   "internal error: checked Iterator next returned no Option");
              if (result.object->case_name == "None") break;
              if (result.object->case_name != "Some" ||
                  !result.object->fields.count("value"))
                throw RuntimeError(statement.line,
                                   "internal error: checked Iterator returned malformed Option");
              if (!run_iteration(result.object->fields.at("value"))) break;
            }
          } else {
            throw RuntimeError(statement.line,
                               "internal error: checked For source has no Fast Debug iterator");
          }
        }
        } catch (RaisedSignal& raised) {
          record_edge_locals(frame, raised);
          throw;
        }

        if (flow.returned) return flow;
        index = body_end;
        continue;
      }
      Flow flow;
      if (compose(statement, frame, output)) { ++index; continue; }
      if (interp_phase20_statement(statement, frame, output)) { ++index; continue; }
      switch (statement.kind) {
        case Stmt::Kind::Let:
        case Stmt::Kind::Var: {
          const auto* ownership_fact = resolve_local_ownership_fact(statement, frame, statement.line);
          Value initial = evaluate_expression_source(statement.b, frame,
              statement.line, output,
              std::pair{est::ExprSlot::Initializer, 0u});
          declare_local(frame, statement.a);
          if (ownership_fact && !ownership_fact->killed_definition_identity.empty()) {
            frame.consumed.erase(statement.a);
            frame.aliases.erase(statement.a);
          }
          if (ownership_fact && (ownership_fact->access == Effect::Read || ownership_fact->access == Effect::Write)) {
            auto rhs_tree = expression_tree(statement.b, "locate_init",
                std::pair{est::ExprSlot::Initializer, 0u});
#ifdef MOSS_TEST_BUILD
            if (std::getenv("MOSS_TEST_CONSUMER_DROP_BORROW_EXPR_TREE")) rhs_tree.reset();
#endif
            if (!rhs_tree)
              throw RuntimeError(statement.line,
                  "MOSS_INTERNAL_INVARIANT missing checked expression tree for borrowed place");
            auto place = locate_tree_node(*rhs_tree, rhs_tree->root, frame, statement.line, output);
            if (place.value) {
              frame.aliases[statement.a] = place;
            } else {
              frame.locals[statement.a] = std::move(initial);
            }
          } else {
            frame.aliases.erase(statement.a);
            frame.locals[statement.a] = std::move(initial);
          }
          if (ownership_fact && ownership_fact->access == Effect::Consume) {
            std::string source_name;
            auto colon = ownership_fact->source_binding_identity.rfind(':');
            if (colon != std::string::npos) source_name = ownership_fact->source_binding_identity.substr(colon + 1);
            else if (binding_identifier(statement.b)) source_name = statement.b;
            if (!source_name.empty() && frame.locals.count(source_name)) {
              auto type = frame.local_types.find(source_name);
              if (type != frame.local_types.end() && !type->second.empty() && !copy_type_name(type->second)) {
                frame.locals.erase(source_name);
                frame.consumed.insert(source_name);
              }
            }
          }
          frame.local_types[statement.a] = checked_binding_type(statement, frame);
          if (emit("LocalWrite", frame, statement.line, statement.a))
            trace_.back().after = summary(frame.locals.count(statement.a) ? frame.locals.at(statement.a) : (frame.aliases.count(statement.a) && frame.aliases.at(statement.a).value ? *frame.aliases.at(statement.a).value : Value::unit()));
          break;
        }
        case Stmt::Kind::Assign: {
          const auto* ownership_fact = resolve_local_ownership_fact(statement, frame, statement.line);
          auto target_tree = expression_tree(statement.a, "assignment_target",
              std::pair{est::ExprSlot::LhsPlace, 0u});
          Value assigned = evaluate_expression_source(
              statement.b, frame, statement.line, output,
              std::pair{est::ExprSlot::RhsValue, 0u});
          if (ownership_fact && !ownership_fact->killed_definition_identity.empty()) {
            frame.consumed.erase(statement.a);
          }
          if (binding_identifier(statement.a) && ownership_fact &&
              (ownership_fact->access == Effect::Read || ownership_fact->access == Effect::Write)) {
            auto rhs_tree = expression_tree(statement.b, "locate_rhs",
                std::pair{est::ExprSlot::RhsValue, 0u});
#ifdef MOSS_TEST_BUILD
            if (std::getenv("MOSS_TEST_CONSUMER_DROP_BORROW_EXPR_TREE")) rhs_tree.reset();
#endif
            if (!rhs_tree)
              throw RuntimeError(statement.line,
                  "MOSS_INTERNAL_INVARIANT missing checked expression tree for borrowed place");
            auto place = locate_tree_node(*rhs_tree, rhs_tree->root, frame, statement.line, output);
            if (place.value) {
              frame.aliases[statement.a] = place;
              if (statement.a != "_")
                frame.local_types[statement.a] = checked_binding_type(statement, frame);
              break;
            }
          }
          if (ownership_fact && ownership_fact->access == Effect::Consume &&
              !ownership_fact->killed_definition_identity.empty()) {
            frame.aliases.erase(statement.a);
          }
          assign(statement.a, std::move(assigned), frame, statement.line,
                 output, target_tree ? &*target_tree : nullptr,
                 target_tree ? std::optional<est::ExprId>(target_tree->root)
                             : std::nullopt);
          if (ownership_fact && ownership_fact->access == Effect::Consume) {
            std::string source_name;
            auto colon = ownership_fact->source_binding_identity.rfind(':');
            if (colon != std::string::npos) source_name = ownership_fact->source_binding_identity.substr(colon + 1);
            else if (binding_identifier(statement.b)) source_name = statement.b;
            if (!source_name.empty() && frame.locals.count(source_name)) {
              auto type = frame.local_types.find(source_name);
              if (type != frame.local_types.end() && !type->second.empty() && !copy_type_name(type->second)) {
                frame.locals.erase(source_name);
                frame.consumed.insert(source_name);
              }
            }
          }
          if (binding_identifier(statement.a) && statement.a != "_")
            frame.local_types[statement.a] = checked_binding_type(statement, frame);
          break;
        }
        case Stmt::Kind::Echo: {
          std::vector<std::string> rendered_args;
          rendered_args.reserve(statement.args.size());
          for (size_t i = 0; i < statement.args.size(); ++i) {
            (void)resolve_echo_argument_fact(statement, frame, i,
                                            statement.line);
            rendered_args.push_back(echo_text(evaluate_expression_source(statement.args[i],
                                frame, statement.line, output,
                                std::pair{est::ExprSlot::Argument,
                                          static_cast<std::uint32_t>(i)}),
                                statement.line));
          }
          for (size_t i = 0; i < rendered_args.size(); ++i) {
            if (i) output << " ";
            output << rendered_args[i];
          }
          output << "\n";
          break;
        }
        case Stmt::Kind::Call:
        case Stmt::Kind::Raw:
          if (!statement.text.empty())
            evaluate_expression_source(statement.text, frame, statement.line,
                output, std::pair{est::ExprSlot::Expression, 0u});
          break;
        case Stmt::Kind::Pass:
          break;
        case Stmt::Kind::Return:
          flow.returned = true;
          flow.value = statement.a.empty() ? Value::unit()
              : evaluate_expression_source(statement.a, frame, statement.line,
                    output, std::pair{est::ExprSlot::ReturnValue, 0u});
          emit("Return", frame, statement.line, flow.value.display());
          return flow;
        case Stmt::Kind::Message: {
          std::string invocation = statement.a + "." + statement.b + "(";
          for (size_t i = 0; i < statement.args.size(); ++i) { if (i) invocation += ", "; invocation += statement.args[i]; }
          const std::string checked_source = "message " + invocation + ")";
          const est::Handle* checked_handle = nullptr;
          for (const auto& handle : statement.expression_handles)
            if (handle.slot == est::ExprSlot::Receiver &&
                handle.argument_index == 0) {
              checked_handle = &handle;
              break;
            }
          if (!checked_handle || checked_handle->arena_index >=
                  program_.expression_arenas.size() ||
              !program_.expression_arenas[checked_handle->arena_index])
            throw RuntimeError(statement.line,
                "MOSS_INTERNAL_INVARIANT missing checked message expression");
          ExpressionTreeView message_tree;
          message_tree.arena = program_.expression_arenas[
              checked_handle->arena_index].get();
          message_tree.root = checked_handle->expr;
          message_tree.source = checked_source;
          message_tree.arena_index = checked_handle->arena_index;
          const auto* message_node = std::get_if<est::Message>(
              &message_tree.arena->at(message_tree.root));
          if (!message_node)
            throw RuntimeError(statement.line,
                "MOSS_INTERNAL_INVARIANT checked message node has wrong kind");
          Value result = message(message_tree, *message_node, frame,
                                 statement.line, output);
          if (!statement.message_result.empty()) {
            assign(statement.message_result, std::move(result), frame, statement.line, output);
            frame.local_types[statement.message_result] = checked_binding_type(statement, frame);
          }
          break;
        }
        case Stmt::Kind::Reply:
          flow.returned = true;
          flow.value = independent(evaluate_expression_source(statement.a,
              frame, statement.line, output,
              std::pair{est::ExprSlot::ReturnValue, 0u}));
          emit("reply", frame, statement.line, summary(flow.value));
          return flow;
        case Stmt::Kind::Raise: {
          if (statement.a.empty()) {
            if (selected_errors_.empty())
              throw RuntimeError(statement.line,
                                 "bare raise has no selected recovery error");
            emit("raise", frame, statement.line,
                 selected_errors_.back().identity.canonical());
            throw selected_errors_.back();
          }
          Value value = independent(evaluate_expression_source(statement.a, frame,
              statement.line, output,
              std::pair{est::ExprSlot::RaisedValue, 0u}));
          RaisedIdentity identity;
          if (value.kind == Value::Kind::Struct && value.object &&
              !value.object->case_name.empty()) {
            identity = RaisedIdentity::enum_variant(
                value.object->type, value.object->case_name);
          } else {
            std::vector<RaisedIdentity> scalar_candidates;
            for (const auto& candidate : statement.exceptional_alternatives) {
              if (candidate.kind != RaisedIdentityKind::ScalarType) continue;
              const bool matches =
                  (value.kind == Value::Kind::Int &&
                   (candidate.type == "Int" || candidate.type == "int")) ||
                  (value.kind == Value::Kind::Float &&
                   (candidate.type == "Float" || candidate.type == "float")) ||
                  (value.kind == Value::Kind::Bool &&
                   (candidate.type == "Bool" || candidate.type == "bool"));
              if (matches) scalar_candidates.push_back(candidate);
            }
            if (scalar_candidates.size() != 1)
              throw RuntimeError(statement.line,
                                 "checked raise lacks one concrete error identity");
            identity = scalar_candidates.front();
          }
          if (!statement.exceptional_alternatives.count(identity))
            throw RuntimeError(statement.line,
                               "raised error variant is outside the checked effect set");
          RaisedSignal raised{identity, std::move(value),
                              statement.line, frame.source_file, {}};
          emit("raise", frame, statement.line, raised.identity.canonical());
          throw raised;
        }
        case Stmt::Kind::For:
        case Stmt::Kind::Try:
        case Stmt::Kind::Recover:
        case Stmt::Kind::If:
        case Stmt::Kind::Else:
        case Stmt::Kind::While:
        case Stmt::Kind::Match:
        case Stmt::Kind::Case:
          break;
      }
      ++index;
    }
    return {};
  }
};

inline FastInterpreter::Value FastInterpreter::Value::unit() { return {}; }
inline FastInterpreter::Value FastInterpreter::Value::boolean_value(bool value) {
  Value result; result.kind = Kind::Bool; result.boolean = value; return result;
}
inline FastInterpreter::Value FastInterpreter::Value::int_value(std::int64_t value) {
  Value result; result.kind = Kind::Int; result.integer = value; return result;
}
inline FastInterpreter::Value FastInterpreter::Value::float_value(double value) {
  Value result; result.kind = Kind::Float; result.floating = value; return result;
}
inline FastInterpreter::Value FastInterpreter::Value::char_value(uint32_t cp) {
  Value result; result.kind = Kind::Char; result.codepoint = cp; return result;
}
inline FastInterpreter::Value FastInterpreter::Value::string_value(std::string value) {
  Value result; result.kind = Kind::String; result.string = std::move(value); return result;
}
inline FastInterpreter::Value FastInterpreter::Value::callable_value(std::string name) {
  Value result; result.kind = Kind::Callable; result.string = std::move(name); return result;
}
inline FastInterpreter::Value FastInterpreter::Value::struct_value(std::string type) {
  Value result; result.kind = Kind::Struct;
  result.object = std::make_shared<StructValue>();
  result.object->type = std::move(type);
  return result;
}
inline FastInterpreter::Value FastInterpreter::Value::vector_value(std::vector<Value> values,
                                                                     std::string element_type) {
  Value result; result.kind = Kind::Vector;
  result.vector = std::make_shared<std::vector<Value>>(std::move(values));
  result.element_type = std::move(element_type);
  return result;
}
inline FastInterpreter::Value FastInterpreter::Value::queue_value() {
  Value result; result.kind = Kind::Queue;
  result.queue = std::make_shared<std::vector<Value>>();
  return result;
}
inline FastInterpreter::Value FastInterpreter::Value::map_value() {
  Value result; result.kind = Kind::Map;
  result.map = std::make_shared<std::vector<std::pair<Value, Value>>>();
  result.map_index = std::make_shared<HashIndex>();
  result.map_hashes = std::make_shared<std::vector<std::int64_t>>();
  return result;
}
inline FastInterpreter::Value FastInterpreter::Value::set_value() {
  Value result; result.kind = Kind::Set;
  result.set = std::make_shared<std::vector<Value>>();
  result.set_index = std::make_shared<HashIndex>();
  return result;
}
inline bool FastInterpreter::Value::truthy() const {
  if (kind == Kind::Bool) return boolean;
  if (kind == Kind::Int) return integer != 0;
  if (kind == Kind::Float) return floating != 0.0;
  if (kind == Kind::Char) return codepoint != 0;
  if (kind == Kind::String) return !string.empty();
  if (kind == Kind::Vector) return vector && !vector->empty();
  if (kind == Kind::Queue) return queue && !queue->empty();
  if (kind == Kind::Map) return map && !map->empty();
  if (kind == Kind::Set) return set && !set->empty();
  return kind != Kind::Unit;
}
inline std::string moss_canonical_float_text(double value) {
  if (std::isnan(value)) return "NaN";
  if (std::isinf(value)) return std::signbit(value) ? "-inf" : "inf";
  if (value == 0.0) return std::signbit(value) ? "-0.0" : "0.0";

  char buffer[128];
  auto converted = std::to_chars(buffer, buffer + sizeof(buffer),
                                 std::fabs(value), std::chars_format::scientific);
  if (converted.ec != std::errc{})
    throw std::runtime_error("failed to format Float");
  std::string scientific(buffer, converted.ptr);
  size_t exponent_at = scientific.find('e');
  if (exponent_at == std::string::npos)
    throw std::runtime_error("scientific Float format lacks exponent");
  int exponent = std::stoi(scientific.substr(exponent_at + 1));
  std::string digits;
  for (size_t index = 0; index < exponent_at; ++index)
    if (scientific[index] != '.') digits += scientific[index];

  std::string result;
  if (exponent >= -4 && exponent < 16) {
    int decimal_at = exponent + 1;
    if (decimal_at <= 0)
      result = "0." + std::string(static_cast<size_t>(-decimal_at), '0') + digits;
    else if (static_cast<size_t>(decimal_at) >= digits.size())
      result = digits + std::string(static_cast<size_t>(decimal_at) - digits.size(), '0') + ".0";
    else
      result = digits.substr(0, static_cast<size_t>(decimal_at)) + "." +
          digits.substr(static_cast<size_t>(decimal_at));
  } else {
    result = digits.substr(0, 1);
    if (digits.size() > 1) result += "." + digits.substr(1);
    result += "e" + std::to_string(exponent);
  }
  return std::signbit(value) ? "-" + result : result;
}

inline std::string rust_f64_to_string(double value) {
  if (std::isnan(value)) return "NaN";
  if (std::isinf(value)) return std::signbit(value) ? "-inf" : "inf";
  if (value == 0.0) return std::signbit(value) ? "-0" : "0";
  double magnitude = std::fabs(value);
  auto decimal = moss_float_display_detail::grisu_shortest(magnitude);
  if (!decimal)
    decimal = moss_float_display_detail::dragon_shortest(magnitude);
  std::string result = moss_float_display_detail::fixed_decimal(
      decimal->first, decimal->second);
  return std::signbit(value) ? "-" + result : result;
}

inline std::string FastInterpreter::Value::display() const {
  std::ostringstream out;
  switch (kind) {
    case Kind::DomainHandle: return domain ? domain->concrete->identity : "<domain>";
    case Kind::Unit: return "()";
    case Kind::Bool: return boolean ? "true" : "false";
    case Kind::Int: return std::to_string(integer);
    case Kind::Float: return moss_canonical_float_text(floating);
    case Kind::Char: return utf8_from_codepoint(codepoint);
    case Kind::String: return string;
    case Kind::Callable: return "<callable:" + string + ">";
    case Kind::Struct:
      return object ? object->type +
          (object->case_name.empty() ? "" : "." + object->case_name) : "<struct>";
    case Kind::Vector:
      out << "[";
      if (vector) for (size_t i = 0; i < vector->size(); ++i) {
        if (i) out << ", ";
        out << (*vector)[i].display();
      }
      out << "]";
      return out.str();
    case Kind::Queue:
      out << "Queue(" << (queue ? std::to_string(queue->size()) : "0") << ")";
      return out.str();
    case Kind::Map:
      out << "Map(" << (map ? std::to_string(map->size()) : "0") << ")";
      return out.str();
    case Kind::Set:
      out << "Set(" << (set ? std::to_string(set->size()) : "0") << ")";
      return out.str();
  }
  return "()";
}

}  // namespace moss
