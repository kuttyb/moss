#include "ast.hpp"

#include <cassert>
#include <iostream>

int main() {
  auto var = [](std::uint32_t ordinal) {
    return moss::Type::var(moss::TypeVarId{0, 0, ordinal});
  };
  moss::OpenCallableSummaryFacts summary;
  summary.bound_variable_count = 2;
  summary.parameter_types.push_back(var(0));
  summary.result_type = moss::Type::builtin("vector", {var(0)});
  assert(moss::close_open_summary_ordinals(summary));
  assert(summary.bound_variable_count == 1);

  summary.bound_variable_count = 3;
  summary.result_type = moss::Type::builtin("vector", {var(2)});
  assert(!moss::close_open_summary_ordinals(summary));
  assert(summary.bound_variable_count == 3);

  summary.result_type = moss::Type::builtin("vector", {var(1)});
  assert(moss::close_open_summary_ordinals(summary));
  assert(summary.bound_variable_count == 2);
  std::cout << "open summary ordinal density: PASS\n";
}
