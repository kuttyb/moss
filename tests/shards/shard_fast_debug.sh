#!/bin/sh
set -eu

compiler=${1:-./moss}
test_build=${2:-build/tests/fast_debug}
mkdir -p "$test_build"

. "$(dirname "$0")/../lib_test.sh"

for interp_case in swarm_018_queue_context swarm_019_collection_pop swarm_020_typed_vector_factory swarm_021_field_collections swarm_021_map_value_semantics swarm_021_map_get_fallback_owned swarm_022_fast_debug_map_state swarm_023_fast_debug_pipelines swarm_023_eager_terminals swarm_023_empty_map_types swarm_023_pipeline_context_identity swarm_023_pipeline_test_context swarm_025_fast_debug_sibling_method swarm_027_indexed_collection_method swarm_028_self_method_argument_borrow swarm_030_string_comparison_borrowed swarm_038_effects_loop_helper; do
  [ -x "$test_build/$interp_case" ] || compile_case "$interp_case" "tests/$interp_case.moss"
done
# Fast Debug: the checked Moss AST executes directly without rustc.
interp_basic_output=$($compiler run --interp tests/phase10_interpreter_basic.moss)
[ "$interp_basic_output" = '13 5' ] || fail 'fast interpreter basic execution differed'
interp_struct_output=$($compiler run --interp tests/phase10_interpreter_structs.moss)

[ "$interp_struct_output" = '9' ] || fail 'fast interpreter struct execution differed'
interp_loop_output=$($compiler run --interp tests/phase10_interpreter_loop.moss)
[ "$interp_loop_output" = '10' ] || fail 'fast interpreter loop execution differed'
interp_remainder_output=$($compiler run --interp tests/swarm_014_integer_remainder.moss)
[ "$interp_remainder_output" = '1' ] || fail 'fast interpreter integer remainder execution differed'
for interp_case in swarm_018_queue_context swarm_019_collection_pop swarm_020_typed_vector_factory swarm_021_field_collections swarm_021_map_value_semantics swarm_021_map_get_fallback_owned swarm_022_fast_debug_map_state swarm_023_fast_debug_pipelines swarm_023_eager_terminals swarm_023_empty_map_types swarm_023_pipeline_context_identity swarm_023_pipeline_test_context swarm_025_fast_debug_sibling_method swarm_027_indexed_collection_method swarm_028_self_method_argument_borrow swarm_030_string_comparison_borrowed swarm_038_effects_loop_helper; do
  native_output=$("$test_build/$interp_case")
  interp_output=$($compiler run --interp "tests/$interp_case.moss")
  [ "$interp_output" = "$native_output" ] || fail "fast interpreter $interp_case execution differed"
done
interp_pipeline_context_test=$($compiler test --interp tests/swarm_023_pipeline_test_context.moss)
printf '%s\n' "$interp_pipeline_context_test" | grep -F 'PASS pipeline context' >/dev/null ||
  fail 'fast interpreter test-context pipeline execution differed'
interp_test_output=$($compiler test --interp tests/phase10_interpreter_tests.moss)
printf '%s\n' "$interp_test_output" | grep -F '2 passed' >/dev/null ||
  fail 'fast interpreter test runner did not report passing tests'
interp_trace=$($compiler run --interp --trace tests/phase10_interpreter_basic.moss 2>&1 >/dev/null)
printf '%s\n' "$interp_trace" | grep -F '"event":"FunctionEnter"' >/dev/null ||
  fail 'fast interpreter did not emit structured trace events'
printf '%s\n' "$interp_trace" | grep -F '"semantic_identity":"fn:transform@1"' >/dev/null ||
  fail 'fast interpreter trace omitted semantic identity'
printf '%s\n' "$interp_trace" | grep -F '"event":"LocalRead"' >/dev/null ||
  fail 'fast interpreter trace omitted local-read events'
printf '%s\n' "$interp_trace" | grep -F '"event":"Return"' >/dev/null ||
  fail 'fast interpreter trace omitted return events'
if command -v python3 >/dev/null 2>&1; then
  PYTHONDONTWRITEBYTECODE=1 python3 tests/tooling/check_margo.py "$compiler"
  PYTHONDONTWRITEBYTECODE=1 python3 tests/tooling/check_module_provider_ambiguity.py "$compiler"
  python3 tests/tooling/check_phase10_fast_debug_project.py "$compiler" ||
    fail 'fast interpreter did not execute the complete project source closure'
  PYTHONDONTWRITEBYTECODE=1 python3 tests/tooling/check_phase224_debug_query.py "$compiler" ||
    fail 'structured debug-query trace slicing regressed'
  PYTHONDONTWRITEBYTECODE=1 python3 tests/tooling/check_phase224_native.py "$compiler" ||
    fail 'Phase 22.4 native/rustc or Margo parity regressed'
fi
