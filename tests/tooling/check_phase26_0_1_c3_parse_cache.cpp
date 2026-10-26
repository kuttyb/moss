#define main moss_cli_entry_for_negative_parse_cache_test
#include "../../src/moss.cpp"
#undef main

#include <cassert>

int main() {
  assert(moss::checker_unit_test_negative_parse_cache_replay());
}
