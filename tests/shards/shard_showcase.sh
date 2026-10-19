#!/bin/sh
set -eu

compiler=${1:-./moss}
test_build=${2:-build/tests/showcase}
mkdir -p "$test_build"

. "$(dirname "$0")/../lib_test.sh"

run_case counter examples/counter.moss 'counter: 41
counter: 42'
run_case frontend_syntax examples/frontend_syntax.moss 'note: 4'
run_case concrete_method tests/concrete_method.moss '12.56636'
run_case concrete_method_body tests/concrete_method_body.moss "$(printf '12.566368\n0')"
run_case duck_typed_methods tests/duck_typed_methods.moss "$(printf '6\n9')"
run_case swarm_043_typed_field_and_method tests/swarm_043_typed_field_and_method.moss "$(printf 'A\nA\nB')"
run_fast_debug_case swarm_043_typed_field_and_method tests/swarm_043_typed_field_and_method.moss "$(printf 'A\nA\nB')"
reject_case swarm_043_untyped_field 'requires a concrete receiver type'
reject_case swarm_043_alias_field 'requires a concrete receiver type'
run_case static_trait_dispatch tests/static_trait_dispatch.moss "$(printf '18\n60')"
run_case static_duck_typing_showcase examples/static_duck_typing.moss "$(printf '112\n45')"
run_case traits_showcase examples/traits.moss "$(printf '27\n80')"
run_case collections_and_methods_showcase examples/collections_and_methods.moss "$(printf 'lead code: 106 12\ntest score: 12')"
run_case functional_dataflow_showcase examples/functional_dataflow.moss 'pipeline total: 42'

run_case mini_application_showcase examples/mini_application.moss "$(printf 'queue positions: 1 2\npublic codes: 1101 2007\nscores: 37 36\nscheduler snapshot: 201\nrecorded total: 73')"
duck_specializations=$(grep -c '^fn __moss_specialize_describe_' \
  "$test_build/duck_typed_methods.rs")
[ "$duck_specializations" -eq 2 ] ||
  fail "duck-typed function did not emit two concrete specializations"
trait_specializations=$(grep -c '^fn __moss_specialize_render_' \
  "$test_build/static_trait_dispatch.rs")
[ "$trait_specializations" -eq 2 ] ||
  fail "trait-typed function did not emit two concrete specializations"
# Backend access traits use static generic dispatch; Moss traits remain erased.
if lowered_rust "$test_build/static_trait_dispatch.rs" | grep -Eq '(dyn[[:space:]]|vtable)' ||
   grep -E '^trait[[:space:]]' "$test_build/static_trait_dispatch.rs" | grep -v '^trait MossAccess_' >/dev/null; then
  fail "static trait dispatch emitted runtime trait machinery"
fi
compile_case method_ast_ownership tests/method_ast_ownership.moss
grep -F '// Moss line 10: fn square(x: Int) = x * x' "$test_build/frontend_syntax.rs" >/dev/null ||
  fail "frontend syntax example omitted its function source annotation"
grep -F 'fn square(x: i64) -> i64' "$test_build/frontend_syntax.rs" >/dev/null ||
  fail "frontend syntax example did not infer its function signature"
grep -F 'square((quote).size)' "$test_build/frontend_syntax.rs" >/dev/null ||
  fail "frontend syntax example did not lower its pipeline"
run_case inferred_frontend tests/inferred_frontend.moss '2 3 MOSS 12.5'
grep -E 'fn Latest_shared\(&self\) -> Option<Quote<(String|MossStr), f64>>' \
  "$test_build/inferred_frontend.rs" >/dev/null ||
  fail "inferred reply handler did not lower with its inferred reply type"
grep -F '// Moss line 6: value = 0' "$test_build/inferred_frontend.rs" >/dev/null ||
  fail "inferred domain state did not retain its source declaration"
grep -F '// Moss line 13: value: Int = 3' "$test_build/inferred_frontend.rs" >/dev/null ||
  fail "optional domain-state annotation was not retained"
grep -F '// Moss line 20: reply Quote(symbol = "MOSS", price = 12.5)' \
  "$test_build/inferred_frontend.rs" >/dev/null ||
  fail "named constructor did not retain its '=' source syntax"
run_case inferred_reply_statement tests/inferred_reply_statement.moss 'done'
run_case shared_memory_example examples/shared_memory.moss 'shared total: 10 42'
shared_memory_message_line=$(awk '/^  message client.Run\(\)/ { print NR; exit }' examples/shared_memory.moss)
grep -F "// Moss line $shared_memory_message_line: message client.Run()" "$test_build/shared_memory_example.rs" >/dev/null ||
  fail "shared-memory example omitted its Moss source-line annotation"
grep -F 'let result = __moss_body_' \
  "$test_build/shared_memory_example.rs" >/dev/null ||
  fail "shared-memory example omitted its synchronous direct lowering annotation"
run_case checkout examples/checkout.moss 'charged: 75
order completed
order rejected: insufficient inventory'
run_case object_pipeline examples/object_pipeline.moss 'created: widget 1 false
observed: widget 1 false
revised: widget 2 true
verified: widget 2 true
archive: widget 2 true'
