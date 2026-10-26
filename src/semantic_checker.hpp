#pragma once

#include "ast.hpp"
#include "diagnostics.hpp"

#include <string>
#include <unordered_map>
#include <utility>
#include <vector>
#include <memory>

namespace moss {

// Owns one Checker and its pointer-keyed solved state for the full checked
// program lifetime. The implementation remains private to semantic_checker.cpp.
class CheckedProgram {
 public:
  explicit CheckedProgram(Program& program);
  ~CheckedProgram();
  CheckedProgram(CheckedProgram&&) noexcept;
  CheckedProgram& operator=(CheckedProgram&&) noexcept;
  CheckedProgram(const CheckedProgram&) = delete;
  CheckedProgram& operator=(const CheckedProgram&) = delete;

  void run();
  const std::vector<Warning>& warnings() const;

 private:
  class Impl;
  std::unique_ptr<Impl> impl_;
};

std::vector<Warning> check_program(Program& program);

std::vector<std::pair<std::string, std::string>>
checker_legal_functional_operations(
    Program& program, const std::string& expression,
    const std::unordered_map<std::string, std::string>& environment);

// Direct C++ regression probes exercise Checker-owned facts without exposing
// the Checker implementation class across translation units.
bool checker_unit_test_executor_invoke_outside_main();
bool checker_unit_test_control_join_binding_facts();
bool checker_unit_test_deferred_operator_requirement_occurrences();
bool checker_unit_test_deferred_unary_invalid_type_rejected();
bool checker_unit_test_builtin_index_occurrence_fact();
bool checker_unit_test_vector_literal_operand_occurrences();
bool checker_unit_test_statement_call_occurrence_roles();
bool checker_unit_test_negative_parse_cache_replay();

}
