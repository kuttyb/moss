#define main moss_cli_entry_for_statement_call_occurrence_test
#include "../../src/moss.cpp"
#undef main

#include <cassert>

using namespace moss;

int main() {
  assert(checker_unit_test_statement_call_occurrence_roles());
}
