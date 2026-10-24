#pragma once

#include "ast.hpp"
#include "diagnostics.hpp"

#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace moss {

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

}
