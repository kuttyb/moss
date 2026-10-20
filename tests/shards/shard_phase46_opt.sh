#!/bin/sh
set -eu

compiler=${1:-./moss}
test_build=${2:-build/tests/phase46_opt}
mkdir -p "$test_build"

. "$(dirname "$0")/../lib_test.sh"

run_phase4_differential phase4_bound_method tests/phase4_bound_method.moss '24'
run_phase4_differential phase4_callable_specialization \
  tests/phase4_callable_specialization.moss '6 8.0'
run_phase4_differential phase4_hof tests/phase4_hof_smoke.moss '6 4'

run_phase4_differential phase46_semantic_rewrites \
  tests/phase46_semantic_rewrites.moss \
  'semantic 22 4 9 3 4 true true false true 3'
run_phase4_differential phase46_semantic_barriers \
  tests/phase46_semantic_barriers.moss \
  "$(printf 'map 1\nmap 2\nmap 3\nfilter 1\nfilter 2\nfilter 3\nmap 1\nmap 2\nmap 3\nmap 1\nmap 2\nmap 3\nmap 1\nmap 2\nmap 3\nresults 3 3 2 2 2 2 6 9 true true true')"
run_phase4_differential phase46_scope_rewrites \
  tests/phase46_scope_rewrites.moss 'scope 14 3 20 4'


# Phase 4.6 bounded Moss-to-Moss semantic rewrites.
"$compiler" -O --dump-functional-ir --check \
  tests/phase46_semantic_rewrites.moss \
  >"$test_build/phase46_semantic_rewrites.first"
"$compiler" -O --dump-functional-ir --check \
  tests/phase46_semantic_rewrites.moss \
  >"$test_build/phase46_semantic_rewrites.second"
cmp "$test_build/phase46_semantic_rewrites.first" \
    "$test_build/phase46_semantic_rewrites.second" >/dev/null ||
  fail 'Phase 4.6 semantic rewrite plan was not deterministic'
grep -F 'ComposedMap origins=main@7:expression:0:stage:1,main@7:expression:0:stage:2' \
  "$test_build/phase46_semantic_rewrites.first" >/dev/null ||
  fail 'adjacent maps were not composed in functional semantic IR'
grep -F 'ComposedFilter origins=main@8:expression:0:stage:1,main@8:expression:0:stage:2' \
  "$test_build/phase46_semantic_rewrites.first" >/dev/null ||
  fail 'adjacent filters were not composed in functional semantic IR'
grep -F 'semantic-opt: predicate-pushdown; filter moved to the earlier value through map(_)' \
  "$test_build/phase46_semantic_rewrites.first" >/dev/null ||
  fail 'proven identity-map predicate pushdown was not recorded'
grep -F 'semantic-opt: dead-map; mapped values are unused by terminal count' \
  "$test_build/phase46_semantic_rewrites.first" >/dev/null ||
  fail 'terminal count did not remove a safe trailing map'
grep -F 'semantic-opt: terminal-filter-count; count matching elements without materializing filter output' \
  "$test_build/phase46_semantic_rewrites.first" >/dev/null ||
  fail 'filter/count terminal plan was not represented semantically'
grep -F 'semantic-opt: short-circuit-any' \
  "$test_build/phase46_semantic_rewrites.first" >/dev/null ||
  fail 'any short-circuit fact was not represented as a semantic rewrite'
grep -F 'semantic-opt: short-circuit-all' \
  "$test_build/phase46_semantic_rewrites.first" >/dev/null ||
  fail 'all short-circuit fact was not represented as a semantic rewrite'
grep -F 'semantic-opt: known-size-count; inert vector literal has 3 elements' \
  "$test_build/phase46_semantic_rewrites.first" >/dev/null ||
  fail 'known literal cardinality was not simplified in semantic IR'
grep -F 'Moss backend: COUNT -> KNOWN SIZE' \
  "$test_build/phase46_semantic_rewrites_optimized.rs" >/dev/null ||
  fail 'known-size count did not reach generated Rust'
if grep -F 'Moss backend: COUNT -> KNOWN SIZE' \
    "$test_build/phase46_semantic_rewrites_o0.rs" >/dev/null; then
  fail '-O0 used a Phase 4.6 known-size rewrite'
fi
if grep -F 'vec![10_i64, 20_i64, 30_i64]' \
    "$test_build/phase46_semantic_rewrites_optimized.rs" >/dev/null; then
  fail 'known-size count retained its inert source literal allocation'
fi
grep -F ' && ' "$test_build/phase46_semantic_rewrites_optimized.rs" >/dev/null ||
  fail 'composed filters did not lower with left-to-right short circuiting'
[ "$(grep -c 'double(__moss_shared_value_' \
    "$test_build/phase46_semantic_rewrites_optimized.rs")" -eq 1 ] ||
  fail 'terminal-aware dead-map elimination retained an unused double callback'

"$compiler" -O --dump-functional-ir --check \
  tests/phase46_semantic_barriers.moss \
  >"$test_build/phase46_semantic_barriers.ir"
grep -F 'semantic-opt: compose-filter disabled; reason: observable callback effect' \
  "$test_build/phase46_semantic_barriers.ir" >/dev/null ||
  fail 'observable filter-composition barrier was not explained'
grep -F 'semantic-opt: compose-map disabled; reason: observable callback effect' \
  "$test_build/phase46_semantic_barriers.ir" >/dev/null ||
  fail 'observable map-composition barrier was not explained'
grep -F 'semantic-opt: compose-map; composed 2 adjacent maps' \
  "$test_build/phase46_semantic_barriers.ir" >/dev/null ||
  fail 'potential divergence unnecessarily blocked invocation-preserving map composition'
[ "$(grep -c 'predicate-pushdown disabled; reason: map is not a proven identity' \
    "$test_build/phase46_semantic_barriers.ir")" -ge 2 ] ||
  fail 'non-identity predicate-pushdown barriers were not retained'
grep -F 'predicate-pushdown disabled; reason: pipeline has observable callback effect' \
  "$test_build/phase46_semantic_barriers.ir" >/dev/null ||
  fail 'effectful predicate-pushdown barrier was not explained'
grep -F 'map elimination: disabled; reason: callback may diverge' \
  "$test_build/phase46_semantic_barriers.ir" >/dev/null ||
  fail 'divergence did not block Phase 4.6 dead-map elimination'
grep -F 'short-circuit any disabled; reason: skipped callback may diverge' \
  "$test_build/phase46_semantic_barriers.ir" >/dev/null ||
  fail 'divergence did not block Phase 4.6 any short circuiting'
grep -F 'short-circuit all disabled; reason: skipped callback may diverge' \
  "$test_build/phase46_semantic_barriers.ir" >/dev/null ||
  fail 'divergence did not block Phase 4.6 all short circuiting'
[ "$(grep -c 'spin(__moss' \
    "$test_build/phase46_semantic_barriers_optimized.rs")" -eq 4 ] ||
  fail 'optimized divergence barriers skipped a required callback invocation path'

"$compiler" -O --dump-functional-ir --check \
  tests/phase46_scope_rewrites.moss \
  >"$test_build/phase46_scope_rewrites.ir"
grep -F "semantic-opt: collapse-single-use-temporary; binding 'selected'" \
  "$test_build/phase46_scope_rewrites.ir" >/dev/null ||
  fail 'single-use Phase 4.6 temporary was not collapsed semantically'
grep -F 'mapped values are unused by downstream terminal count' \
  "$test_build/phase46_scope_rewrites.ir" >/dev/null ||
  fail 'cross-binding terminal count retained a dead trailing map'
grep -F 'intermediate materialized: materialized; reason: multiple consumers' \
  "$test_build/phase46_scope_rewrites.ir" >/dev/null ||
  fail 'Phase 4.6 collapsed a multiple-use temporary'
grep -F "FUNCTIONAL INTERMEDIATE 'selected' VIRTUALIZED" \
  "$test_build/phase46_scope_rewrites_optimized.rs" >/dev/null ||
  fail 'single-use semantic collapse did not reach lowering'
grep -F "FUNCTIONAL INTERMEDIATE 'materialized' MATERIALIZED; reason: multiple consumers" \
  "$test_build/phase46_scope_rewrites_optimized.rs" >/dev/null ||
  fail 'multiple-use semantic boundary disappeared during lowering'
[ "$(grep -c 'double(__moss_value_' \
    "$test_build/phase46_scope_rewrites_optimized.rs")" -eq 2 ] ||
  fail 'cross-binding count did not remove only its dead map callback'

grep -F 'fn __moss_specialize_transform_0(xs: &std::vec::Vec<i64>) -> std::vec::Vec<i64>' \
  "$test_build/phase4_hof_optimized.rs" >/dev/null ||
  fail 'static higher-order helper did not erase its compile-time callable parameter'
[ "$(grep -c '^fn __moss_specialize_transform_' \
    "$test_build/phase4_hof_optimized.rs")" -eq 2 ] ||
  fail 'higher-order helper was not separately specialized for two callable identities'
if lowered_rust "$test_build/phase4_hof_optimized.rs" | grep -Eq 'dyn Fn|Box<dyn|fn\(i64\)'; then
  fail 'static higher-order helper emitted runtime callable machinery'
fi
grep -F '.apply(__moss_value_0)' \
  "$test_build/phase4_bound_method_optimized.rs" >/dev/null ||
  fail 'bound method stage did not lower to a statically selected concrete call'
if lowered_rust "$test_build/phase4_bound_method_optimized.rs" | grep -Eq 'dyn Fn|Box<dyn|fn\(i64\)'; then
  fail 'bound method stage emitted runtime callable machinery'
fi
[ "$(grep -c '^fn __moss_specialize_double_' \
    "$test_build/phase4_callable_specialization_optimized.rs")" -eq 2 ] ||
  fail 'functional callable did not retain distinct Int and Float specializations'

# Phase 10.5: named, bound-method, placeholder, and higher-order callables
# must be concrete before their functional effect summaries are consumed.
"$compiler" -O --dump-functional-ir --check \
  tests/phase105_static_callable_effects.moss \
  >"$test_build/phase105_static_callable_effects.ir"
grep -F 'callable=announce IO' \
  "$test_build/phase105_static_callable_effects.ir" >/dev/null ||
  fail 'named functional callable did not retain its concrete I/O effect'
grep -F 'semantic=fn:transform<vector[int],callable:announce>@14:expression:0' \
  "$test_build/phase105_static_callable_effects.ir" >/dev/null ||
  fail 'higher-order callable parameter was not concretely specialized'
grep -F 'callable=scaler.apply PURE+CAPTURE_READ' \
  "$test_build/phase105_static_callable_effects.ir" >/dev/null ||
  fail 'bound method callable did not resolve before effect propagation'
grep -F 'callable=placeholder:_ + 1 PURE' \
  "$test_build/phase105_static_callable_effects.ir" >/dev/null ||
  fail 'placeholder callable did not resolve before effect propagation'
"$compiler" calls fn:transform --source \
  tests/phase105_static_callable_effects.moss --json \
  >"$test_build/phase105_calls.json"
grep -F '"source_identity"' "$test_build/phase105_calls.json" >/dev/null ||
  fail 'semantic query omitted source identity'
grep -F '"callers"' "$test_build/phase105_calls.json" >/dev/null ||
  fail 'semantic query omitted retained caller edges'
"$compiler" inspect main --source \
  tests/phase105_static_callable_effects.moss --json \
  >"$test_build/phase105_inspect.json"
grep -F '"resolved": true' "$test_build/phase105_inspect.json" >/dev/null ||
  fail 'semantic query omitted resolved call identity'
grep -F '"target_kind": "method"' "$test_build/phase105_inspect.json" >/dev/null ||
  fail 'semantic query omitted concrete method target kind'
grep -F '"specialization_identity": "specialization:transform<vector[int],callable:announce>"' \
  "$test_build/phase105_inspect.json" >/dev/null ||
  fail 'semantic query omitted concrete specialization identity'
"$compiler" agent schema --json >"$test_build/phase105_schema.json"
grep -F '"synchronization_diagnostics"' "$test_build/phase105_schema.json" >/dev/null ||
  fail 'agent schema omitted reserved synchronization diagnostics'
