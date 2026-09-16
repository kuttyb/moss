#!/bin/sh
set -eu

compiler=${1:-./moss}
test_build=${2:-build/tests}
mkdir -p "$test_build"

fail() {
  echo "test failure: $*" >&2
  exit 1
}

compile_case() {
  name=$1
  source=$2
  "$compiler" --check "$source"
  "$compiler" "$source" -o "$test_build/$name.rs"
  if grep -Eq 'std::sync::mpsc|mpsc::channel' "$test_build/$name.rs"; then
    fail "$name emitted forbidden Rust message-passing transport"
  fi
  grep -F 'struct MossChannel<T> { state: Mutex<' "$test_build/$name.rs" >/dev/null ||
    fail "$name did not emit the lock-backed shared-memory channel"
  rustc -D warnings "$test_build/$name.rs" -o "$test_build/$name"
}

compile_optimized_case() {
  name=$1
  source=$2
  "$compiler" --check "$source"
  "$compiler" -Oshared-memory "$source" -o "$test_build/$name.rs"
  if grep -Eq 'std::sync::mpsc|mpsc::channel' "$test_build/$name.rs"; then
    fail "$name emitted forbidden Rust message-passing transport"
  fi
  rustc -D warnings "$test_build/$name.rs" -o "$test_build/$name"
}

compile_cluster_case() {
  name=$1
  source=$2
  cluster=$3
  "$compiler" --check "$source"
  "$compiler" "--cluster=$cluster" "$source" -o "$test_build/$name.rs"
  if grep -Eq 'std::sync::mpsc|mpsc::channel' "$test_build/$name.rs"; then
    fail "$name emitted forbidden Rust message-passing transport"
  fi
  rustc -D warnings "$test_build/$name.rs" -o "$test_build/$name"
}

compile_shared_memory_case() {
  name=$1
  source=$2
  compile_optimized_case "$name" "$source"
  grep -F 'Message optimization: direct shared-memory dispatch' \
    "$test_build/$name.rs" >/dev/null ||
    fail "$name did not select direct shared-memory dispatch"
  grep -E 'Moss backend plan: .* = Direct(Mutex|RwLock|Atomic)' \
    "$test_build/$name.rs" >/dev/null ||
    fail "$name did not emit a planned direct domain lowering"
  grep -F 'fn __moss_require_send<T: Send>()' "$test_build/$name.rs" >/dev/null ||
    fail "$name did not emit the Rust Send boundary check"
}

compile_unchecked_await_case() {
  name=$1
  source=$2
  "$compiler" --check "$source"
  "$compiler" -Oshared-memory --no-await-error-handling "$source" \
    -o "$test_build/$name.rs"
  if grep -F 'unwrap_or_else' "$test_build/$name.rs" >/dev/null; then
    fail "$name retained per-await error handling"
  fi
  grep -F 'unwrap_unchecked' "$test_build/$name.rs" >/dev/null ||
    fail "$name did not emit unchecked await extraction"
  grep -F 'Await error handling disabled' "$test_build/$name.rs" >/dev/null ||
    fail "$name did not record its unchecked-await mode"
  rustc -D warnings "$test_build/$name.rs" -o "$test_build/$name"
}

run_case() {
  name=$1
  source=$2
  expected=$3
  compile_case "$name" "$source"
  actual=$("$test_build/$name")
  if [ "$actual" != "$expected" ]; then
    printf 'test failure: %s output\nexpected:\n%s\nactual:\n%s\n' "$name" "$expected" "$actual" >&2
    exit 1
  fi
}

run_case_either_order() {
  name=$1
  source=$2
  first=$3
  second=$4
  compile_case "$name" "$source"
  actual=$("$test_build/$name")
  forward=$(printf '%s\n%s' "$first" "$second")
  reverse=$(printf '%s\n%s' "$second" "$first")
  if [ "$actual" != "$forward" ] && [ "$actual" != "$reverse" ]; then
    printf 'test failure: %s output\nexpected either:\n%s\nor:\n%s\nactual:\n%s\n' \
      "$name" "$forward" "$reverse" "$actual" >&2
    exit 1
  fi
}

run_shared_memory_case() {
  name=$1
  source=$2
  expected=$3
  compile_shared_memory_case "$name" "$source"
  actual=$("$test_build/$name")
  if [ "$actual" != "$expected" ]; then
    printf 'test failure: %s output\nexpected:\n%s\nactual:\n%s\n' "$name" "$expected" "$actual" >&2
    exit 1
  fi
}

run_optimized_case() {
  name=$1
  source=$2
  expected=$3
  compile_optimized_case "$name" "$source"
  actual=$("$test_build/$name")
  if [ "$actual" != "$expected" ]; then
    printf 'test failure: %s output\nexpected:\n%s\nactual:\n%s\n' "$name" "$expected" "$actual" >&2
    exit 1
  fi
}

run_phase4_differential() {
  name=$1
  source=$2
  expected=$3
  o0_rs="$test_build/${name}_o0.rs"
  optimized_rs="$test_build/${name}_optimized.rs"
  o0_bin="$test_build/${name}_o0"
  optimized_bin="$test_build/${name}_optimized"

  "$compiler" -O0 "$source" -o "$o0_rs"
  "$compiler" -O "$source" -o "$optimized_rs"
  rustc -D warnings "$o0_rs" -o "$o0_bin"
  rustc -D warnings "$optimized_rs" -o "$optimized_bin"
  o0_output=$("$o0_bin")
  optimized_output=$("$optimized_bin")
  [ "$o0_output" = "$expected" ] ||
    fail "$name -O0 output differed from its expected Moss result"
  [ "$optimized_output" = "$expected" ] ||
    fail "$name optimized output differed from its expected Moss result"
  [ "$optimized_output" = "$o0_output" ] ||
    fail "$name optimized output differed from -O0"
}

reject_source() {
  name=$1
  source=$2
  expected=$3
  stdout="$test_build/$name.stdout"
  stderr="$test_build/$name.stderr"
  if "$compiler" --check "$source" >"$stdout" 2>"$stderr"; then
    fail "$name unexpectedly passed"
  fi
  grep -Eq '^moss:[0-9]+: error:' "$stderr" || fail "$name did not produce a line-numbered diagnostic"
  grep -F "$expected" "$stderr" >/dev/null || {
    echo "test failure: $name diagnostic did not contain: $expected" >&2
    sed -n '1,20p' "$stderr" >&2
    exit 1
  }
  if grep -Eiq 'rust|borrow|lifetime|moved' "$stderr"; then
    fail "$name Moss diagnostic leaked backend ownership terminology"
  fi
}

reject_case() {
  name=$1
  expected=$2
  reject_source "$name" "tests/negative/$name.moss" "$expected"
}

reject_cluster() {
  name=$1
  source=$2
  cluster=$3
  expected=$4
  stdout="$test_build/$name.stdout"
  stderr="$test_build/$name.stderr"
  if "$compiler" "--cluster=$cluster" --check "$source" >"$stdout" 2>"$stderr"; then
    fail "$name unexpectedly accepted its cluster configuration"
  fi
  grep -F "$expected" "$stderr" >/dev/null || {
    echo "test failure: $name diagnostic did not contain: $expected" >&2
    sed -n '1,20p' "$stderr" >&2
    exit 1
  }
}

run_case counter examples/counter.moss 'counter: 41
counter: 42'
run_case frontend_syntax examples/frontend_syntax.moss 'note: 4'
run_case concrete_method tests/concrete_method.moss '12.56636'
run_case concrete_method_body tests/concrete_method_body.moss "$(printf '12.566368\n0')"
run_case duck_typed_methods tests/duck_typed_methods.moss "$(printf '6\n9')"
run_case static_trait_dispatch tests/static_trait_dispatch.moss "$(printf '18\n60')"
run_case static_duck_typing_showcase examples/static_duck_typing.moss "$(printf '112\n45')"
run_case traits_showcase examples/traits.moss "$(printf '27\n80')"
run_case collections_and_methods_showcase examples/collections_and_methods.moss "$(printf 'lead code: 106 12\ntest score: 12')"
run_case functional_dataflow_showcase examples/functional_dataflow.moss 'pipeline total: 42'
run_phase4_differential functional_basics_showcase \
  examples/functional_basics.moss \
  "$(printf 'transformed: 14 18\nsource still available: -3 4')"
run_phase4_differential functional_reductions_showcase \
  examples/functional_reductions.moss \
  "$(printf 'reductions: 38 2 true true\nreduce with initial: 106\nboolean terminals: true false\nempty terminals: 0 0 false true 7\nwrapping sum: -9223372036854775808')"
run_phase4_differential functional_static_callables_showcase \
  examples/functional_static_callables.moss \
  "$(printf 'specializations: 6 4\nbound method: 24\nmethod placeholder: 21')"
run_phase4_differential functional_effect_order_showcase \
  examples/functional_effect_order.moss \
  "$(printf 'first 1\nfirst 2\nfirst 3\nsecond 1\nsecond 2\nsecond 3\nordered total: 6\nbuild source\nbuild initial\nreduced total: 22')"
run_phase4_differential functional_objects_showcase \
  examples/functional_objects.moss \
  "$(printf 'accepted total: 20\nsensor ids: 101 103\nvalidity: true false true')"
run_phase4_differential functional_domains_showcase \
  examples/functional_domains.moss 'domain-backed total: 14'
run_phase4_differential functional_terminal_optimization_showcase \
  examples/functional_terminal_optimization.moss \
  'terminal plans: 4 4 true true'
run_phase4_differential functional_scope_fusion_showcase \
  examples/functional_scope_fusion.moss 'scope-fused total: 28'
run_phase4_differential functional_shared_traversal_showcase \
  examples/functional_shared_traversal.moss 'shared traversal: 6 4 3'
run_phase4_differential functional_materialization_showcase \
  examples/functional_materialization.moss 'materialized and shared: 12 3'
run_phase4_differential functional_semantic_optimization_showcase \
  examples/functional_semantic_optimization.moss \
  'semantic space: 68 5 34 3 4 true'
run_phase4_differential phase4_functional tests/phase4_functional.moss \
  "$(printf '6 -1 5 11 16\n12 36 6 2 true true\ntrue false\n60 10 -2\n0 0 false true 7\n-9223372036854775808\n7')"
run_phase4_differential phase4_fusion tests/phase4_fusion.moss '36'
run_phase4_differential phase4_map_map tests/phase4_map_map.moss '3 7'
run_phase4_differential phase4_object tests/phase4_object_smoke.moss '12'
run_phase4_differential phase4_bound_method tests/phase4_bound_method.moss '24'
run_phase4_differential phase4_callable_specialization \
  tests/phase4_callable_specialization.moss '6 8'
run_phase4_differential phase4_hof tests/phase4_hof_smoke.moss '6 4'
run_phase4_differential phase4_effect_order tests/phase4_effect_smoke.moss \
  "$(printf 'first 1\nfirst 2\nfirst 3\nsecond 1\nsecond 2\nsecond 3\ntotal 6')"
run_phase4_differential phase4_failure_barrier \
  tests/phase4_failure_barrier.moss '5'
run_phase4_differential phase4_domain_barrier \
  tests/phase4_domain_barrier.moss '12'
run_phase4_differential phase4_message_barrier \
  tests/phase4_message_barrier.moss '2 1 1'
run_phase4_differential phase4_await_barrier \
  tests/phase4_await_barrier.moss '6'
run_phase4_differential phase4_materialized_message \
  tests/phase4_materialized_message.moss '12'
run_phase4_differential phase4_terminal_boundary \
  tests/phase4_terminal_boundary.moss '12 12'
run_phase4_differential phase4_exact_pipeline_id \
  tests/phase4_exact_pipeline_id.moss '12 18'
run_phase4_differential phase4_reduce_initializer_effect \
  tests/phase4_reduce_initializer_effect.moss "$(printf 'source\ninitial\ntotal 22')"
run_phase4_differential phase4_result_provenance \
  tests/phase4_result_provenance.moss '10'
run_phase4_differential phase45_terminal_simplification \
  tests/phase45_terminal_simplification.moss '4 4'
run_phase4_differential phase45_short_circuit \
  tests/phase45_short_circuit.moss \
  "$(printf 'true true false true true\nfalse false true\nfalse true')"
run_phase4_differential phase45_short_circuit_single \
  tests/phase45_short_circuit_single.moss "$(printf 'true\nfalse\ntrue')"
run_phase4_differential phase45_short_circuit_effect_barrier \
  tests/phase45_short_circuit_effect_barrier.moss \
  "$(printf 'observe 1\nobserve 2\nobserve 3\nretain 1\nretain 2\nretain 3\nresults true 3')"
run_phase4_differential phase45_short_circuit_failure_barrier \
  tests/phase45_short_circuit_failure_barrier.moss 'true'
run_phase4_differential phase45_cross_let_fusion \
  tests/phase45_cross_let_fusion.moss '28'
run_phase4_differential phase45_cross_let_result \
  tests/phase45_cross_let_result.moss '8'
run_phase4_differential phase45_later_use_materializes \
  tests/phase45_later_use_materializes.moss '2 12'
run_phase4_differential phase45_multiple_consumers \
  tests/phase45_multiple_consumers.moss '12 3'
run_phase4_differential phase45_shared_traversal \
  tests/phase45_shared_traversal.moss '6 4 3'
run_phase4_differential phase45_source_mutation_barrier \
  tests/phase45_source_mutation_barrier.moss '6 4'
run_phase4_differential phase45_cross_let_effect_barrier \
  tests/phase45_cross_let_effect_barrier.moss "$(printf 'between stages\n12')"
run_phase4_differential phase45_mutable_binding_materializes \
  tests/phase45_mutable_binding_materializes.moss '12'
run_phase4_differential phase45_shared_any_all \
  tests/phase45_shared_any_all.moss 'true false 3'
run_phase4_differential phase45_shared_wrapping \
  tests/phase45_shared_wrapping.moss '-9223372036854775808 2'
run_phase4_differential phase45_shared_dependency_barrier \
  tests/phase45_shared_dependency_barrier.moss '6 12'
run_phase4_differential phase45_divergent_work_skipping \
  tests/phase45_divergent_work_skipping.moss '3 true true'
run_phase4_differential phase45_divergence_transitive \
  tests/phase45_divergence_transitive.moss '3'
run_phase4_differential phase45_divergent_shared_any_all \
  tests/phase45_divergent_shared_any_all.moss 'true false 3'
run_phase4_differential phase45_cross_let_skip_revalidation \
  tests/phase45_cross_let_skip_revalidation.moss '3'
run_phase4_differential phase45_cross_let_filter_count \
  tests/phase45_cross_let_filter_count.moss '2'
run_phase4_differential phase45_divergence_fusion \
  tests/phase45_divergence_fusion.moss '6'
run_phase4_differential phase45_divergence_method \
  tests/phase45_divergence_method.moss '2'
run_phase4_differential phase46_semantic_rewrites \
  tests/phase46_semantic_rewrites.moss \
  'semantic 22 4 9 3 4 true true false true 3'
run_phase4_differential phase46_semantic_barriers \
  tests/phase46_semantic_barriers.moss \
  "$(printf 'map 1\nmap 2\nmap 3\nfilter 1\nfilter 2\nfilter 3\nmap 1\nmap 2\nmap 3\nmap 1\nmap 2\nmap 3\nmap 1\nmap 2\nmap 3\nresults 3 3 2 2 2 2 6 9 true true true')"
run_phase4_differential phase46_scope_rewrites \
  tests/phase46_scope_rewrites.moss 'scope 14 3 20 4'

grep -F 'Moss backend: EAGER FUNCTIONAL PIPELINE reference semantics' \
  "$test_build/phase4_fusion_o0.rs" >/dev/null ||
  fail 'Phase 4 -O0 did not retain eager functional reference semantics'
grep -F 'Moss backend: FUSED FUNCTIONAL PIPELINE; one explicit loop, intermediates eliminated' \
  "$test_build/phase4_fusion_optimized.rs" >/dev/null ||
  fail 'pure map/filter/map/sum pipeline was not fused'
[ "$(grep -c 'for __moss_item_ref in' "$test_build/phase4_fusion_optimized.rs")" -eq 1 ] ||
  fail 'fused reduction pipeline did not lower to exactly one explicit loop'
if grep -F '__moss_stage_' "$test_build/phase4_fusion_optimized.rs" >/dev/null; then
  fail 'fused reduction pipeline retained an intermediate collection'
fi
if grep -E '\.iter\(\)\.(map|filter)|\.into_iter\(\)\.(map|filter)' \
    "$test_build/phase4_fusion_optimized.rs" >/dev/null; then
  fail 'functional pipeline was delegated to a Rust iterator chain'
fi
if grep -F 'Vec::new()' "$test_build/phase4_fusion_optimized.rs" >/dev/null; then
  fail 'fused terminal reduction allocated an intermediate Vec'
fi
[ "$(grep -c 'for __moss_item_ref in' "$test_build/phase4_map_map_optimized.rs")" -eq 1 ] ||
  fail 'map/map did not lower to one explicit loop'
if grep -F '__moss_stage_' "$test_build/phase4_map_map_optimized.rs" >/dev/null; then
  fail 'map/map retained an intermediate vector'
fi
grep -F 'Moss backend: EAGER FUNCTIONAL PIPELINE reference semantics' \
  "$test_build/phase4_effect_order_optimized.rs" >/dev/null ||
  fail 'I/O callback did not stop fusion'
grep -F 'Moss backend: EAGER FUNCTIONAL PIPELINE reference semantics' \
  "$test_build/phase4_failure_barrier_optimized.rs" >/dev/null ||
  fail 'possibly failing callback did not stop fusion'
grep -F 'Moss backend: EAGER FUNCTIONAL PIPELINE reference semantics' \
  "$test_build/phase4_domain_barrier_optimized.rs" >/dev/null ||
  fail 'domain-state observation did not stop fusion'
grep -F 'Moss backend: EAGER FUNCTIONAL PIPELINE reference semantics' \
  "$test_build/phase4_message_barrier_optimized.rs" >/dev/null ||
  fail 'message callback did not stop fusion'
grep -F 'Moss backend: EAGER FUNCTIONAL PIPELINE reference semantics' \
  "$test_build/phase4_await_barrier_optimized.rs" >/dev/null ||
  fail 'await callback did not stop fusion'
[ "$(grep -c 'Moss backend: FUSED FUNCTIONAL PIPELINE' \
    "$test_build/phase4_exact_pipeline_id_optimized.rs")" -eq 1 ] ||
  fail 'exact pipeline plan did not fuse the pure identical-text context'
[ "$(grep -c 'Moss backend: EAGER FUNCTIONAL PIPELINE' \
    "$test_build/phase4_exact_pipeline_id_optimized.rs")" -eq 1 ] ||
  fail 'exact pipeline plan did not preserve the effectful identical-text context'
grep -F 'semantic fn:local_total@4:result' \
  "$test_build/phase4_exact_pipeline_id_optimized.rs" >/dev/null ||
  fail 'fused code did not consume the exact local pipeline plan'
grep -F 'semantic handler:Analyzer.Total@11:expression:0' \
  "$test_build/phase4_exact_pipeline_id_optimized.rs" >/dev/null ||
  fail 'eager code did not consume the exact domain-effect pipeline plan'
grep -F 'Moss backend: EAGER FUNCTIONAL PIPELINE reference semantics' \
  "$test_build/phase4_reduce_initializer_effect_optimized.rs" >/dev/null ||
  fail 'observable reduce initializer did not block fusion'
grep -F '// Moss line 6: values |> map(_ * 2) |> sum' \
  "$test_build/phase4_result_provenance_optimized.rs" >/dev/null ||
  fail 'method result pipeline did not retain its real source line'

# Phase 4.5 terminal, materialization, cross-binding, and dataflow-DAG plans.
[ "$(grep -c 'Moss backend: COUNT -> EXACT LENGTH' \
    "$test_build/phase45_terminal_simplification_optimized.rs")" -eq 2 ] ||
  fail 'exact count and pure mapped-count did not both lower to length'
if grep -F 'for __moss_item_ref' \
    "$test_build/phase45_terminal_simplification_optimized.rs" >/dev/null; then
  fail 'length-simplified count retained a source traversal'
fi
if grep -F 'double(__moss_' \
    "$test_build/phase45_terminal_simplification_optimized.rs" >/dev/null; then
  fail 'dead pure map callback remained in mapped-count lowering'
fi
if grep -F 'Moss backend: COUNT -> EXACT LENGTH' \
    "$test_build/phase45_terminal_simplification_o0.rs" >/dev/null; then
  fail '-O0 used the optimized count-to-length plan'
fi

if grep -F 'Moss backend: COUNT -> EXACT LENGTH' \
    "$test_build/phase45_divergent_work_skipping_optimized.rs" >/dev/null; then
  fail 'potentially divergent mapped-count was incorrectly lowered to length'
fi
grep -F 'spin(__moss_shared_value_' \
  "$test_build/phase45_divergent_work_skipping_optimized.rs" >/dev/null ||
  fail 'potentially divergent callback disappeared from optimized traversal'
if grep -F 'break;' \
    "$test_build/phase45_divergent_work_skipping_optimized.rs" >/dev/null; then
  fail 'potentially divergent any/all callback was incorrectly short circuited'
fi
if grep -F 'Moss backend: COUNT -> EXACT LENGTH' \
    "$test_build/phase45_divergence_transitive_optimized.rs" >/dev/null; then
  fail 'transitively divergent mapped-count was incorrectly lowered to length'
fi
grep -F 'outer(__moss_value_' \
  "$test_build/phase45_divergence_transitive_optimized.rs" >/dev/null ||
  fail 'transitively divergent callback disappeared from optimized traversal'
grep -F 'Moss backend: SHARED FUNCTIONAL SOURCE TRAVERSAL (3 terminal consumers)' \
  "$test_build/phase45_divergent_shared_any_all_optimized.rs" >/dev/null ||
  fail 'divergence-safe shared traversal DAG was not retained'
[ "$(grep -c 'inspect(__moss_shared_value_' \
    "$test_build/phase45_divergent_shared_any_all_optimized.rs")" -eq 2 ] ||
  fail 'shared any/all branches dropped a potentially divergent callback'
if grep -E 'if !?__moss_shared_result_' \
    "$test_build/phase45_divergent_shared_any_all_optimized.rs" >/dev/null; then
  fail 'shared any/all branch was guarded despite potential divergence'
fi
if grep -F 'Moss backend: COUNT -> EXACT LENGTH' \
    "$test_build/phase45_cross_let_skip_revalidation_optimized.rs" >/dev/null; then
  fail 'cross-binding count ignored divergent upstream work'
fi
grep -F 'spin(__moss_value_' \
  "$test_build/phase45_cross_let_skip_revalidation_optimized.rs" >/dev/null ||
  fail 'cross-binding fusion dropped a divergent upstream callback'
if grep -F 'Moss backend: COUNT -> EXACT LENGTH' \
    "$test_build/phase45_cross_let_filter_count_optimized.rs" >/dev/null; then
  fail 'cross-binding count ignored a cardinality-changing filter'
fi
grep -F 'Moss backend: FUSED FUNCTIONAL PIPELINE' \
  "$test_build/phase45_divergence_fusion_optimized.rs" >/dev/null ||
  fail 'potential divergence unnecessarily disabled invocation-preserving fusion'
grep -F 'spin(__moss_value_' \
  "$test_build/phase45_divergence_fusion_optimized.rs" >/dev/null ||
  fail 'ordinary fusion dropped its potentially divergent callback'
if grep -F 'Moss backend: COUNT -> EXACT LENGTH' \
    "$test_build/phase45_divergence_method_optimized.rs" >/dev/null; then
  fail 'potentially divergent method callback was incorrectly eliminated'
fi
grep -F '__moss_value_0.inspect()' \
  "$test_build/phase45_divergence_method_optimized.rs" >/dev/null ||
  fail 'potentially divergent method callback disappeared from traversal'

[ "$(grep -c 'break;' \
    "$test_build/phase45_short_circuit_single_optimized.rs")" -eq 3 ] ||
  fail 'optimized standalone any/all did not short circuit'
if grep -F 'break;' "$test_build/phase45_short_circuit_single_o0.rs" >/dev/null; then
  fail '-O0 any/all did not retain complete eager traversal'
fi
grep -F 'Moss backend: EAGER FUNCTIONAL PIPELINE reference semantics' \
  "$test_build/phase45_short_circuit_effect_barrier_optimized.rs" >/dev/null ||
  fail 'observable any/map callbacks did not block skipping work'
if grep -F 'break;' \
    "$test_build/phase45_short_circuit_effect_barrier_optimized.rs" >/dev/null; then
  fail 'effectful any callback was incorrectly short circuited'
fi
grep -F 'Moss backend: EAGER FUNCTIONAL PIPELINE reference semantics' \
  "$test_build/phase45_short_circuit_failure_barrier_optimized.rs" >/dev/null ||
  fail 'possibly failing any callback did not retain eager traversal'
if grep -F 'break;' \
    "$test_build/phase45_short_circuit_failure_barrier_optimized.rs" >/dev/null; then
  fail 'possibly failing any callback was incorrectly short circuited'
fi

grep -F "FUNCTIONAL INTERMEDIATE 'normalized' VIRTUALIZED" \
  "$test_build/phase45_cross_let_fusion_optimized.rs" >/dev/null ||
  fail 'single-use immutable functional binding was not virtualized'
if grep -Eq '^[[:space:]]*let (mut )?normalized =' \
    "$test_build/phase45_cross_let_fusion_optimized.rs"; then
  fail 'cross-binding fusion retained the normalized collection'
fi
[ "$(grep -c 'for __moss_item_ref in' \
    "$test_build/phase45_cross_let_fusion_optimized.rs")" -eq 1 ] ||
  fail 'cross-binding map/filter/map/sum did not lower to one traversal'
if grep -F 'Vec::new()' \
    "$test_build/phase45_cross_let_fusion_optimized.rs" >/dev/null; then
  fail 'cross-binding fusion allocated an intermediate vector'
fi
grep -F "FUNCTIONAL INTERMEDIATE 'normalized' VIRTUALIZED" \
  "$test_build/phase45_cross_let_result_optimized.rs" >/dev/null ||
  fail 'function-result consumer did not virtualize its immutable producer'
grep -Eq '^[[:space:]]*let normalized = \{' \
  "$test_build/phase45_later_use_materializes_optimized.rs" ||
  fail 'later independent use did not force collection materialization'
if grep -F "FUNCTIONAL INTERMEDIATE 'normalized' VIRTUALIZED" \
    "$test_build/phase45_cross_let_effect_barrier_optimized.rs" >/dev/null; then
  fail 'an intervening observable effect was crossed by binding fusion'
fi
if grep -F "FUNCTIONAL INTERMEDIATE 'normalized' VIRTUALIZED" \
    "$test_build/phase45_mutable_binding_materializes_optimized.rs" >/dev/null; then
  fail 'a mutable functional binding was incorrectly virtualized'
fi
grep -Eq '^[[:space:]]*let (mut )?materialized = \{' \
  "$test_build/phase4_materialized_message_optimized.rs" ||
  fail 'message-bound functional collection did not remain materialized'
if grep -F "FUNCTIONAL INTERMEDIATE 'materialized' VIRTUALIZED" \
    "$test_build/phase4_materialized_message_optimized.rs" >/dev/null; then
  fail 'scope optimization crossed a message ownership boundary'
fi

grep -F 'Moss backend: SHARED FUNCTIONAL SOURCE TRAVERSAL (3 terminal consumers)' \
  "$test_build/phase45_shared_traversal_optimized.rs" >/dev/null ||
  fail 'three compatible terminal consumers did not share a traversal'
[ "$(grep -c 'for __moss_shared_item_ref_' \
    "$test_build/phase45_shared_traversal_optimized.rs")" -eq 1 ] ||
  fail 'shared dataflow group did not emit exactly one source loop'
grep -F 'Moss backend: SHARED FUNCTIONAL SOURCE TRAVERSAL (2 terminal consumers)' \
  "$test_build/phase45_multiple_consumers_optimized.rs" >/dev/null ||
  fail 'materialized multi-consumer value did not share its terminal traversal'
grep -F "FUNCTIONAL INTERMEDIATE 'normalized' MATERIALIZED; reason: multiple consumers" \
  "$test_build/phase45_multiple_consumers_optimized.rs" >/dev/null ||
  fail 'generated Rust did not explain required multi-consumer materialization'
if grep -F 'Moss backend: SHARED FUNCTIONAL SOURCE TRAVERSAL' \
    "$test_build/phase45_source_mutation_barrier_optimized.rs" >/dev/null; then
  fail 'source mutation barrier was crossed by traversal sharing'
fi
if grep -F 'Moss backend: SHARED FUNCTIONAL SOURCE TRAVERSAL' \
    "$test_build/phase45_shared_dependency_barrier_optimized.rs" >/dev/null; then
  fail 'dependent terminal consumers were incorrectly combined'
fi

"$compiler" -O --dump-functional-ir --check \
  tests/phase45_terminal_simplification.moss \
  >"$test_build/phase45_terminal_simplification.ir"
grep -F 'count -> len; reason: exact source cardinality known' \
  "$test_build/phase45_terminal_simplification.ir" >/dev/null ||
  fail 'functional explanation omitted count-to-length reasoning'
grep -F 'map stage eliminated; reason: output unused and callback is pure/non-failing/non-divergent' \
  "$test_build/phase45_terminal_simplification.ir" >/dev/null ||
  fail 'functional explanation omitted dead-map reasoning'
"$compiler" -O --dump-functional-ir --check \
  tests/phase45_divergent_work_skipping.moss \
  >"$test_build/phase45_divergent_work_skipping.ir"
grep -F 'callable=spin PURE+MAY_DIVERGE' \
  "$test_build/phase45_divergent_work_skipping.ir" >/dev/null ||
  fail 'functional IR omitted direct callback divergence'
grep -F 'map elimination: disabled; reason: callback may diverge' \
  "$test_build/phase45_divergent_work_skipping.ir" >/dev/null ||
  fail 'functional explanation omitted the divergent map-elimination barrier'
grep -F 'short-circuit any disabled; reason: skipped callback may diverge' \
  "$test_build/phase45_divergent_work_skipping.ir" >/dev/null ||
  fail 'functional explanation omitted the divergent any barrier'
grep -F 'short-circuit all disabled; reason: skipped callback may diverge' \
  "$test_build/phase45_divergent_work_skipping.ir" >/dev/null ||
  fail 'functional explanation omitted the divergent all barrier'
"$compiler" -O --dump-functional-ir --check \
  tests/phase45_divergence_transitive.moss \
  >"$test_build/phase45_divergence_transitive.ir"
grep -F 'callable=outer PURE+MAY_DIVERGE' \
  "$test_build/phase45_divergence_transitive.ir" >/dev/null ||
  fail 'functional IR omitted transitive callback divergence'
"$compiler" -O --dump-functional-ir --check \
  tests/phase45_cross_let_skip_revalidation.moss \
  >"$test_build/phase45_cross_let_skip_revalidation.ir"
grep -F 'count -> len disabled; reason: callback may diverge' \
  "$test_build/phase45_cross_let_skip_revalidation.ir" >/dev/null ||
  fail 'cross-binding work elimination was not revalidated for divergence'
"$compiler" -O --dump-functional-ir --check \
  tests/phase45_cross_let_filter_count.moss \
  >"$test_build/phase45_cross_let_filter_count.ir"
grep -F 'count -> len disabled; reason: upstream stage changes cardinality' \
  "$test_build/phase45_cross_let_filter_count.ir" >/dev/null ||
  fail 'cross-binding count omitted its cardinality barrier explanation'
"$compiler" -O --dump-functional-ir --check \
  tests/phase45_divergence_fusion.moss \
  >"$test_build/phase45_divergence_fusion.ir"
grep -F 'callable=spin PURE+MAY_DIVERGE' \
  "$test_build/phase45_divergence_fusion.ir" >/dev/null ||
  fail 'ordinary fusion IR omitted callback divergence provenance'
grep -F 'decision: fused stages 1-4; intermediates eliminated' \
  "$test_build/phase45_divergence_fusion.ir" >/dev/null ||
  fail 'potential divergence was treated as a generic fusion barrier'
"$compiler" -O --dump-functional-ir --check \
  tests/phase45_divergence_method.moss \
  >"$test_build/phase45_divergence_method.ir"
grep -F 'callable=placeholder:_.inspect() PURE+MAY_DIVERGE' \
  "$test_build/phase45_divergence_method.ir" >/dev/null ||
  fail 'functional IR omitted method callback divergence'
"$compiler" -O --dump-functional-ir --check \
  tests/phase45_short_circuit_effect_barrier.moss \
  >"$test_build/phase45_short_circuit_effect_barrier.ir"
grep -F 'short-circuit any disabled; reason: observable callback effect' \
  "$test_build/phase45_short_circuit_effect_barrier.ir" >/dev/null ||
  fail 'functional explanation omitted the observable short-circuit barrier'
grep -F 'count -> len disabled; reason: observable callback effect' \
  "$test_build/phase45_short_circuit_effect_barrier.ir" >/dev/null ||
  fail 'functional explanation omitted the effectful mapped-count barrier'
"$compiler" -O --dump-functional-ir --check \
  tests/phase45_short_circuit_single.moss \
  >"$test_build/phase45_short_circuit_single.ir"
grep -F 'short-circuit any enabled' \
  "$test_build/phase45_short_circuit_single.ir" >/dev/null ||
  fail 'functional explanation omitted legal any short-circuiting'
grep -F 'short-circuit all enabled' \
  "$test_build/phase45_short_circuit_single.ir" >/dev/null ||
  fail 'functional explanation omitted legal all short-circuiting'
"$compiler" -O --dump-functional-ir --check \
  tests/phase45_short_circuit_failure_barrier.moss \
  >"$test_build/phase45_short_circuit_failure_barrier.ir"
grep -F 'short-circuit any disabled; reason: callback may fail' \
  "$test_build/phase45_short_circuit_failure_barrier.ir" >/dev/null ||
  fail 'functional explanation omitted the failure short-circuit barrier'
"$compiler" -O --dump-functional-ir --check \
  tests/phase45_cross_let_fusion.moss \
  >"$test_build/phase45_cross_let_fusion.ir"
grep -F 'intermediate normalized: virtualized across immutable binding' \
  "$test_build/phase45_cross_let_fusion.ir" >/dev/null ||
  fail 'functional IR omitted the virtualized named intermediate'
"$compiler" -O --dump-functional-ir --check \
  tests/phase45_multiple_consumers.moss \
  >"$test_build/phase45_multiple_consumers.ir"
grep -F 'intermediate normalized: materialized; reason: multiple consumers' \
  "$test_build/phase45_multiple_consumers.ir" >/dev/null ||
  fail 'functional IR omitted the multiple-consumer materialization reason'
"$compiler" -O --dump-functional-ir --check \
  tests/phase45_cross_let_effect_barrier.moss \
  >"$test_build/phase45_cross_let_effect_barrier.ir"
grep -F 'intermediate normalized: materialized; reason: intervening observable effect' \
  "$test_build/phase45_cross_let_effect_barrier.ir" >/dev/null ||
  fail 'functional IR omitted the cross-binding observable barrier'
"$compiler" -O --dump-functional-ir --check \
  tests/phase45_shared_traversal.moss \
  >"$test_build/phase45_shared_traversal.first"
"$compiler" -O --dump-functional-ir --check \
  tests/phase45_shared_traversal.moss \
  >"$test_build/phase45_shared_traversal.second"
cmp "$test_build/phase45_shared_traversal.first" \
    "$test_build/phase45_shared_traversal.second" >/dev/null ||
  fail 'scope-level dataflow IR dump was not deterministic'
grep -F 'DataflowGroup %1 [main] semantic=main@3:shared-source' \
  "$test_build/phase45_shared_traversal.first" >/dev/null ||
  fail 'dataflow IR omitted its stable shared-source identity'
grep -F 'decision: source traversal shared; consumers: sum count count' \
  "$test_build/phase45_shared_traversal.first" >/dev/null ||
  fail 'dataflow IR omitted its terminal consumer DAG decision'
grep -F 'provenance: main@3:expression:0:source' \
  "$test_build/phase45_shared_traversal.first" >/dev/null ||
  fail 'shared traversal discarded source provenance'

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

grep -F 'fn __moss_specialize_transform_0(xs: &Vec<i64>) -> Vec<i64>' \
  "$test_build/phase4_hof_optimized.rs" >/dev/null ||
  fail 'static higher-order helper did not erase its compile-time callable parameter'
[ "$(grep -c '^fn __moss_specialize_transform_' \
    "$test_build/phase4_hof_optimized.rs")" -eq 2 ] ||
  fail 'higher-order helper was not separately specialized for two callable identities'
if grep -Eq 'dyn Fn|Box<dyn|fn\(i64\)' "$test_build/phase4_hof_optimized.rs"; then
  fail 'static higher-order helper emitted runtime callable machinery'
fi
grep -F '.apply(__moss_value_0)' \
  "$test_build/phase4_bound_method_optimized.rs" >/dev/null ||
  fail 'bound method stage did not lower to a statically selected concrete call'
if grep -Eq 'dyn Fn|Box<dyn|fn\(i64\)' \
    "$test_build/phase4_bound_method_optimized.rs"; then
  fail 'bound method stage emitted runtime callable machinery'
fi
[ "$(grep -c '^fn __moss_specialize_double_' \
    "$test_build/phase4_callable_specialization_optimized.rs")" -eq 2 ] ||
  fail 'functional callable did not retain distinct Int and Float specializations'

"$compiler" -O --dump-functional-ir --check tests/phase4_fusion.moss \
  >"$test_build/phase4_functional_ir.first"
"$compiler" -O --dump-functional-ir --check tests/phase4_fusion.moss \
  >"$test_build/phase4_functional_ir.second"
cmp "$test_build/phase4_functional_ir.first" \
    "$test_build/phase4_functional_ir.second" >/dev/null ||
  fail 'functional IR dump was not deterministic'
grep -F 'Map vector[int] callable=normalize PURE span=10:1 origin=main@9:expression:0:stage:1 materialization=eliminated' \
  "$test_build/phase4_functional_ir.first" >/dev/null ||
  fail 'functional IR omitted typed callable, source span, or materialization provenance'
grep -F 'decision: fused stages 1-4; intermediates eliminated' \
  "$test_build/phase4_functional_ir.first" >/dev/null ||
  fail 'functional IR dump omitted its fusion decision'
"$compiler" -O --dump-functional-ir --check tests/phase4_bound_method.moss \
  >"$test_build/phase4_bound_method.ir"
grep -F 'callable=scaler.apply PURE+CAPTURE_READ' \
  "$test_build/phase4_bound_method.ir" >/dev/null ||
  fail 'functional IR omitted the statically bound method capture/effect summary'
grep -F 'captures=scaler' "$test_build/phase4_bound_method.ir" >/dev/null ||
  fail 'functional IR omitted the bound receiver capture'
"$compiler" -O --explain-fusion --check tests/phase4_effect_smoke.moss \
  >"$test_build/phase4_effect.explain"
grep -F 'fusion stopped: external/I/O effect' \
  "$test_build/phase4_effect.explain" >/dev/null ||
  fail 'fusion explanation omitted the I/O barrier'
"$compiler" -O --explain-fusion --check tests/phase4_failure_barrier.moss \
  >"$test_build/phase4_failure.explain"
grep -F 'fusion stopped: possible failure ordering' \
  "$test_build/phase4_failure.explain" >/dev/null ||
  fail 'fusion explanation omitted the possible-failure barrier'
"$compiler" -O --explain-fusion --check tests/phase4_domain_barrier.moss \
  >"$test_build/phase4_domain.explain"
grep -F 'fusion stopped: observable domain READ' \
  "$test_build/phase4_domain.explain" >/dev/null ||
  fail 'fusion explanation omitted the domain-state barrier'
"$compiler" -O --explain-fusion --check tests/phase4_message_barrier.moss \
  >"$test_build/phase4_message.explain"
grep -F 'fusion stopped: message send' \
  "$test_build/phase4_message.explain" >/dev/null ||
  fail 'fusion explanation omitted the message barrier'
"$compiler" -O --explain-fusion --check tests/phase4_await_barrier.moss \
  >"$test_build/phase4_await.explain"
grep -F 'fusion stopped: await' \
  "$test_build/phase4_await.explain" >/dev/null ||
  fail 'fusion explanation omitted the await barrier'
"$compiler" -O --dump-functional-ir --check \
  tests/phase4_exact_pipeline_id.moss >"$test_build/phase4_exact_pipeline_id.ir"
grep -E '^Pipeline %[0-9]+ \[fn:local_total\] semantic=fn:local_total@4:result' \
  "$test_build/phase4_exact_pipeline_id.ir" >/dev/null ||
  fail 'functional IR omitted the stable local semantic identity'
grep -F 'decision: fused stages 1-2; intermediates eliminated' \
  "$test_build/phase4_exact_pipeline_id.ir" >/dev/null ||
  fail 'pure identical-text pipeline context was not fused'
grep -E '^Pipeline %[0-9]+ \[handler:Analyzer.Total\] semantic=handler:Analyzer.Total@11:expression:0' \
  "$test_build/phase4_exact_pipeline_id.ir" >/dev/null ||
  fail 'functional IR omitted the distinct effectful semantic identity'
grep -F 'decision: fusion stopped: observable domain READ' \
  "$test_build/phase4_exact_pipeline_id.ir" >/dev/null ||
  fail 'effectful identical-text pipeline context was incorrectly fused'
"$compiler" -O --dump-functional-ir --check \
  tests/phase4_reduce_initializer_effect.moss \
  >"$test_build/phase4_reduce_initializer_effect.ir"
grep -F 'Reduce vector[int] -> int callable=add IO' \
  "$test_build/phase4_reduce_initializer_effect.ir" >/dev/null ||
  fail 'reduce IR node omitted initializer effects'
grep -F 'decision: fusion stopped: external/I/O effect' \
  "$test_build/phase4_reduce_initializer_effect.ir" >/dev/null ||
  fail 'reduce initializer effect did not govern fusion legality'
"$compiler" -O --dump-functional-ir --check \
  tests/phase4_result_provenance.moss >"$test_build/phase4_result_provenance.ir"
grep -F 'semantic=method:Calculator.total@6:result line 6' \
  "$test_build/phase4_result_provenance.ir" >/dev/null ||
  fail 'method-result functional IR used an artificial source line'
run_case mini_application_showcase examples/mini_application.moss "$(printf 'queue positions: 1 2\npublic codes: 1101 2007\nscores: 37 36\nscheduler snapshot: 201\nrecorded total: 73')"
duck_specializations=$(grep -c '^fn __moss_specialize_describe_' \
  "$test_build/duck_typed_methods.rs")
[ "$duck_specializations" -eq 2 ] ||
  fail "duck-typed function did not emit two concrete specializations"
trait_specializations=$(grep -c '^fn __moss_specialize_render_' \
  "$test_build/static_trait_dispatch.rs")
[ "$trait_specializations" -eq 2 ] ||
  fail "trait-typed function did not emit two concrete specializations"
if grep -Eq '(^trait[[:space:]]|dyn[[:space:]]|vtable)' \
    "$test_build/static_trait_dispatch.rs"; then
  fail "static trait dispatch emitted runtime trait machinery"
fi
compile_case method_ast_ownership tests/method_ast_ownership.moss
grep -F '// Moss line 10: fn square(x: Int) = x * x' "$test_build/frontend_syntax.rs" >/dev/null ||
  fail "frontend syntax example omitted its function source annotation"
grep -F 'fn square(x: i64) -> i64' "$test_build/frontend_syntax.rs" >/dev/null ||
  fail "frontend syntax example did not infer its function signature"
grep -F 'square(quote.size)' "$test_build/frontend_syntax.rs" >/dev/null ||
  fail "frontend syntax example did not lower its pipeline"
run_case inferred_frontend tests/inferred_frontend.moss '2 3 MOSS 12.5'
grep -F 'fn Latest_shared(&self, __moss_reply: MossSender<Quote>)' \
  "$test_build/inferred_frontend.rs" >/dev/null ||
  fail "inferred reply handler did not lower with its inferred reply type"
grep -F '// Moss line 6: value = 0' "$test_build/inferred_frontend.rs" >/dev/null ||
  fail "inferred domain state did not retain its source declaration"
grep -F '// Moss line 13: value: Int = 3' "$test_build/inferred_frontend.rs" >/dev/null ||
  fail "optional domain-state annotation was not retained"
grep -F '// Moss line 20: reply Quote(symbol = "MOSS", price = 12.5)' \
  "$test_build/inferred_frontend.rs" >/dev/null ||
  fail "named constructor did not retain its '=' source syntax"
run_case inferred_reply_one_way tests/inferred_reply_one_way.moss 'done'
run_case shared_memory_example examples/shared_memory.moss 'shared total: 10 42'
grep -F '// Moss line 21: message client.Run(counter)' "$test_build/shared_memory_example.rs" >/dev/null ||
  fail "shared-memory example omitted its Moss source-line annotation"
grep -F '// Moss backend: MESSAGE/MAILBOX version: enqueue the Moss send in a lock-backed shared-memory queue' \
  "$test_build/shared_memory_example.rs" >/dev/null ||
  fail "shared-memory example omitted its mailbox lowering annotation"
run_case checkout examples/checkout.moss 'charged: 75
order completed
order rejected: insufficient inventory'
run_case object_pipeline examples/object_pipeline.moss 'created: widget 1 false
observed: widget 1 false
revised: widget 2 true
verified: widget 2 true
archive: widget 2 true'
run_case await_main tests/await_main.moss 'main: true'
run_case main_helper_await tests/main_helper_await.moss '9'
run_case branch_same_domain_type tests/branch_same_domain_type.moss '7'
run_optimized_case branch_same_domain_type_optimized \
  tests/branch_same_domain_type.moss '7'
run_case sequential_awaits tests/sequential_awaits.moss 'sequential: true'
run_case ignored_reply tests/ignored_reply.moss 'ignored reply completed'
run_case primitive_assignment tests/primitive_assignment.moss 'primitive: 1 1'
run_case domain_ref_message tests/domain_ref_message.moss 'ping
ping'
run_case fresh_message_payload tests/fresh_message_payload.moss 'fresh: 7'
run_case fresh_reply_payload tests/fresh_reply_payload.moss 'fresh reply: 9'
compile_case locked_mailbox_contention tests/shared_memory_contention.moss
iteration=1
while [ "$iteration" -le 20 ]; do
  actual=$("$test_build/locked_mailbox_contention")
  [ "$actual" = 'shared total: 2000 true true' ] ||
    fail "lock-backed mailbox lost or reordered work on iteration $iteration"
  iteration=$((iteration + 1))
done
run_shared_memory_case shared_memory_checkout examples/checkout.moss 'charged: 75
order completed
order rejected: insufficient inventory'
grep -F 'state: Arc<Mutex<InventoryState>>' "$test_build/shared_memory_checkout.rs" >/dev/null ||
  fail "checkout did not promote its awaited Inventory domain"
grep -F 'tx: MossSender<CheckoutMsg>' "$test_build/shared_memory_checkout.rs" >/dev/null ||
  fail "checkout incorrectly promoted an asynchronously called domain"
run_shared_memory_case shared_memory_example_optimized examples/shared_memory.moss \
  'shared total: 10 42'
grep -F '// Moss backend plan: Counter = DirectAtomic.' \
  "$test_build/shared_memory_example_optimized.rs" >/dev/null ||
  fail "shared-memory example did not select the atomic Counter lowering"
grep -F 'total: AtomicI64' "$test_build/shared_memory_example_optimized.rs" >/dev/null ||
  fail "shared-memory example did not lower Counter state to AtomicI64"
grep -F 'tx: MossSender<ClientMsg>' "$test_build/shared_memory_example_optimized.rs" >/dev/null ||
  fail "shared-memory example did not retain the asynchronous Client mailbox"
grep -F '// Moss line 14: first = await counter.Add(10)' \
  "$test_build/shared_memory_example_optimized.rs" >/dev/null ||
  fail "shared-memory optimization omitted its Moss await annotation"
grep -F '// Moss backend: ATOMIC DOMAIN await: execute one SeqCst state action directly' \
  "$test_build/shared_memory_example_optimized.rs" >/dev/null ||
  fail "shared-memory optimization omitted its atomic lowering annotation"
compile_unchecked_await_case unchecked_awaits examples/shared_memory.moss
[ "$("$test_build/unchecked_awaits")" = 'shared total: 10 42' ] ||
  fail "unchecked-await mode changed successful execution"
run_optimized_case shared_memory_object_pipeline examples/object_pipeline.moss \
  'created: widget 1 false
observed: widget 1 false
revised: widget 2 true
verified: widget 2 true
archive: widget 2 true'
grep -F 'Message transport: lock-backed shared-memory mailboxes.' "$test_build/shared_memory_object_pipeline.rs" >/dev/null ||
  fail "object pipeline did not use lock-backed shared-memory transport"
run_optimized_case shared_memory_ignored_reply tests/ignored_reply.moss \
  'ignored reply completed'
grep -F 'tx: MossSender<WorkerMsg>' "$test_build/shared_memory_ignored_reply.rs" >/dev/null ||
  fail "ignored reply call was incorrectly made synchronous"
run_optimized_case shared_memory_domain_ref tests/domain_ref_message.moss 'ping
ping'
run_shared_memory_case shared_memory_contention tests/shared_memory_contention.moss \
  'shared total: 2000 true true'
grep -F '// Moss backend plan: Counter = DirectAtomic.' \
  "$test_build/shared_memory_contention.rs" >/dev/null ||
  fail "contention case did not promote its one-action Counter to atomics"
grep -F 'tx: MossSender<ProducerMsg>' "$test_build/shared_memory_contention.rs" >/dev/null ||
  fail "contention case incorrectly promoted asynchronous Producer messages"
iteration=1
while [ "$iteration" -le 20 ]; do
  actual=$("$test_build/shared_memory_contention")
  [ "$actual" = 'shared total: 2000 true true' ] ||
    fail "shared-memory contention lost or reordered work on iteration $iteration"
  iteration=$((iteration + 1))
done

# Phase 2.5 backend planning: batching, lock regions, RwLock specialization,
# and whole-domain atomics. Each generated case is compiled with denied Rust
# warnings by compile_optimized_case/run_optimized_case.
run_optimized_case phase25_batching tests/phase25_batching.moss \
  "$(printf 'record: 1\nrecord: 2\nrecord: 3')"
grep -F '// Moss backend: BATCHED MAILBOX SEND (3 messages)' \
  "$test_build/phase25_batching.rs" >/dev/null ||
  fail "three adjacent messages were not planned as one batch"
[ "$(grep -c 'sink.__moss_send_batch' "$test_build/phase25_batching.rs")" -eq 1 ] ||
  fail "batched message region did not use exactly one enqueue call"
grep -F 'self.tracker.begin_n(count);' "$test_build/phase25_batching.rs" >/dev/null ||
  fail "batched enqueue did not coalesce tracker accounting"
grep -F 'self.tracker.end_n(unsent.len());' "$test_build/phase25_batching.rs" >/dev/null ||
  fail "failed batched enqueue did not unwind all tracker entries"

compile_optimized_case phase25_batch_targets tests/phase25_batch_targets.moss
if grep -F '// Moss backend: BATCHED MAILBOX SEND (' \
    "$test_build/phase25_batch_targets.rs" >/dev/null; then
  fail "messages to different receiver instances were incorrectly batched"
fi
compile_optimized_case phase25_batch_break tests/phase25_batch_break.moss
if grep -F '// Moss backend: BATCHED MAILBOX SEND (' \
    "$test_build/phase25_batch_break.rs" >/dev/null; then
  fail "an observable payload expression did not break message batching"
fi

run_optimized_case phase25_lock_coalesce tests/phase25_lock_coalesce.moss \
  'set set set'
grep -F '// Moss backend: COALESCED LOCK REGION (3 operations)' \
  "$test_build/phase25_lock_coalesce.rs" >/dev/null ||
  fail "exclusive adjacent awaits did not form a coalesced lock region"
sed -n '/^fn main() {/,/^}/p' "$test_build/phase25_lock_coalesce.rs" \
  >"$test_build/phase25_lock_coalesce.main.rs"
[ "$(grep -c 'state.lock().unwrap()' "$test_build/phase25_lock_coalesce.main.rs")" -eq 1 ] ||
  fail "coalesced direct operations did not acquire exactly one state lock"

run_optimized_case phase25_lock_multi_caller tests/phase25_lock_multi_caller.moss \
  "$(printf 'ready ready\nready ready')"
if grep -F 'COALESCED LOCK REGION' "$test_build/phase25_lock_multi_caller.rs" >/dev/null; then
  fail "a target with multiple callers received unsafe lock coalescing"
fi

run_optimized_case phase25_rwlock tests/phase25_rwlock.moss 'draft stable stable'
grep -F '// Moss backend plan: Catalog = DirectRwLock.' \
  "$test_build/phase25_rwlock.rs" >/dev/null ||
  fail "read-heavy direct domain did not select RwLock"
grep -F '// Moss backend: READ-SHARED RwLock handler wrapper' \
  "$test_build/phase25_rwlock.rs" >/dev/null ||
  fail "READ-only handler did not receive a shared-read wrapper"
grep -F 'self.state.read().unwrap()' "$test_build/phase25_rwlock.rs" >/dev/null ||
  fail "READ-only handler did not acquire an RwLock read guard"
grep -F 'self.state.write().unwrap()' "$test_build/phase25_rwlock.rs" >/dev/null ||
  fail "WRITE handler did not retain an exclusive RwLock guard"

run_optimized_case phase25_rwlock_read_region \
  tests/phase25_rwlock_read_region.moss 'stable stable'
grep -F '// Moss backend: COALESCED LOCK REGION (2 operations)' \
  "$test_build/phase25_rwlock_read_region.rs" >/dev/null ||
  fail "exclusive sequence of read handlers did not form a shared lock region"
sed -n '/^fn main() {/,/^}/p' "$test_build/phase25_rwlock_read_region.rs" \
  >"$test_build/phase25_rwlock_read_region.main.rs"
[ "$(grep -c 'state.read().unwrap()' "$test_build/phase25_rwlock_read_region.main.rs")" -eq 1 ] ||
  fail "coalesced READ region did not acquire exactly one shared guard"

compile_optimized_case phase25_rwlock_contention tests/phase25_rwlock_contention.moss
grep -F 'state: Arc<RwLock<CatalogState>>' \
  "$test_build/phase25_rwlock_contention.rs" >/dev/null ||
  fail "read contention target did not use RwLock"
iteration=1
while [ "$iteration" -le 10 ]; do
  actual=$("$test_build/phase25_rwlock_contention")
  [ "$actual" = "$(printf 'consistent\nconsistent')" ] ||
    fail "RwLock read contention changed observable results on iteration $iteration"
  iteration=$((iteration + 1))
done

run_optimized_case phase25_atomic_counter tests/phase25_atomic_counter.moss '3 2 3 2'
grep -F '// Moss backend plan: Counter = DirectAtomic.' \
  "$test_build/phase25_atomic_counter.rs" >/dev/null ||
  fail "simple integer counter did not select atomic lowering"
grep -F '.fetch_add(__moss_operand, Ordering::SeqCst)' \
  "$test_build/phase25_atomic_counter.rs" >/dev/null ||
  fail "integer increment did not lower to SeqCst fetch_add"
grep -F '.fetch_sub(__moss_operand, Ordering::SeqCst)' \
  "$test_build/phase25_atomic_counter.rs" >/dev/null ||
  fail "integer decrement did not lower to SeqCst fetch_sub"
if grep -Eq 'MossChannel|Mutex|Condvar|thread::spawn' "$test_build/phase25_atomic_counter.rs"; then
  fail "fully atomic domain retained a mailbox, condition variable, mutex, or worker thread"
fi
grep -F '// Moss backend: ATOMIC DOMAIN one-way execution' \
  "$test_build/phase25_atomic_counter.rs" >/dev/null ||
  fail "eligible one-way atomic handler was not executed directly"

# Moss integers wrap at their concrete i64 width. Compile both the mailbox
# reference and atomic lowering with Rust overflow checks explicitly enabled so
# neither Rust profile nor the backend choice can affect observable results.
overflow_expected=$(printf '%s\n%s\n%s\n%s\n%s\n%s\n%s\n%s\n%s' \
  '-9223372036854775808' '9223372036854775807' \
  '-9223372036854775808' '9223372036854775807' \
  '-9223372036854775808' '-9223372036854775808' \
  '-9223372036854775808' '3' \
  '-9223372036854775808')
"$compiler" --check tests/phase25_integer_overflow.moss
"$compiler" -O0 tests/phase25_integer_overflow.moss \
  -o "$test_build/phase25_integer_overflow_o0.rs"
"$compiler" -Oshared-memory tests/phase25_integer_overflow.moss \
  -o "$test_build/phase25_integer_overflow.rs"
rustc -D warnings -C overflow-checks=yes \
  "$test_build/phase25_integer_overflow_o0.rs" \
  -o "$test_build/phase25_integer_overflow_o0"
rustc -D warnings -C overflow-checks=yes \
  "$test_build/phase25_integer_overflow.rs" \
  -o "$test_build/phase25_integer_overflow"
rustc -D warnings -C overflow-checks=no \
  "$test_build/phase25_integer_overflow_o0.rs" \
  -o "$test_build/phase25_integer_overflow_o0_no_checks"
rustc -D warnings -C overflow-checks=no \
  "$test_build/phase25_integer_overflow.rs" \
  -o "$test_build/phase25_integer_overflow_no_checks"
overflow_o0_output=$("$test_build/phase25_integer_overflow_o0")
overflow_optimized_output=$("$test_build/phase25_integer_overflow")
overflow_o0_no_checks_output=$("$test_build/phase25_integer_overflow_o0_no_checks")
overflow_optimized_no_checks_output=$("$test_build/phase25_integer_overflow_no_checks")
[ "$overflow_o0_output" = "$overflow_expected" ] ||
  fail "-O0 integer overflow did not follow Moss wrapping semantics"
[ "$overflow_optimized_output" = "$overflow_expected" ] ||
  fail "optimized atomic integer overflow did not follow Moss wrapping semantics"
[ "$overflow_o0_output" = "$overflow_optimized_output" ] ||
  fail "integer overflow differed between -O0 and optimized execution"
[ "$overflow_o0_output" = "$overflow_o0_no_checks_output" ] ||
  fail "Rust overflow checks changed -O0 Moss integer results"
[ "$overflow_optimized_output" = "$overflow_optimized_no_checks_output" ] ||
  fail "Rust overflow checks changed optimized Moss integer results"
[ "$(grep -c ' = DirectAtomic\.' \
    "$test_build/phase25_integer_overflow.rs")" -eq 4 ] ||
  fail "overflow boundary domains did not all select DirectAtomic"
grep -F '.wrapping_add(' "$test_build/phase25_integer_overflow_o0.rs" >/dev/null ||
  fail "-O0 integer addition did not use explicit wrapping arithmetic"
grep -F '.wrapping_sub(' "$test_build/phase25_integer_overflow_o0.rs" >/dev/null ||
  fail "-O0 integer subtraction did not use explicit wrapping arithmetic"
grep -F '.wrapping_mul(' "$test_build/phase25_integer_overflow_o0.rs" >/dev/null ||
  fail "ordinary integer multiplication did not use explicit wrapping arithmetic"
grep -F '.wrapping_div(' "$test_build/phase25_integer_overflow_o0.rs" >/dev/null ||
  fail "ordinary integer division overflow did not use explicit wrapping arithmetic"
grep -F 'fn __moss_specialize_duck_add_0(value: i64) -> i64' \
  "$test_build/phase25_integer_overflow_o0.rs" >/dev/null ||
  fail "duck-typed integer arithmetic was not statically specialized before Rust generation"
[ "$(grep -c '^fn __moss_specialize_duck_add_' \
    "$test_build/phase25_integer_overflow_o0.rs")" -eq 2 ] ||
  fail "duck-typed arithmetic did not retain both concrete call-site types"
grep -F '.fetch_add(' "$test_build/phase25_integer_overflow.rs" >/dev/null ||
  fail "optimized overflowing add did not use an atomic fetch_add"
grep -F '.fetch_sub(' "$test_build/phase25_integer_overflow.rs" >/dev/null ||
  fail "optimized overflowing subtract did not use an atomic fetch_sub"
grep -F '__moss_previous.wrapping_add(__moss_operand)' \
  "$test_build/phase25_integer_overflow.rs" >/dev/null ||
  fail "atomic add reply did not reconstruct the new value with wrapping arithmetic"
grep -F '__moss_previous.wrapping_sub(__moss_operand)' \
  "$test_build/phase25_integer_overflow.rs" >/dev/null ||
  fail "atomic subtract reply did not reconstruct the new value with wrapping arithmetic"
if grep -F 'unsafe {' "$test_build/phase25_integer_overflow.rs" >/dev/null; then
  fail "integer wrapping or atomic overflow lowering used unsafe code"
fi

run_optimized_case phase25_atomic_bool tests/phase25_atomic_bool.moss 'true false'
grep -F 'enabled: AtomicBool' "$test_build/phase25_atomic_bool.rs" >/dev/null ||
  fail "boolean flag did not use AtomicBool"
grep -F '.fetch_xor(true, Ordering::SeqCst)' \
  "$test_build/phase25_atomic_bool.rs" >/dev/null ||
  fail "boolean toggle did not use SeqCst fetch_xor"

run_optimized_case phase25_atomic_swap tests/phase25_atomic_swap.moss '7'
grep -F '.swap(__moss_next, Ordering::SeqCst)' \
  "$test_build/phase25_atomic_swap.rs" >/dev/null ||
  fail "scalar exchange did not lower to SeqCst swap"

run_optimized_case phase25_atomic_copy_boundary \
  tests/phase25_atomic_copy_boundary.moss 'original true'
grep -F '// Moss backend plan: Gate = DirectAtomic.' \
  "$test_build/phase25_atomic_copy_boundary.rs" >/dev/null ||
  fail "copy-boundary fixture did not select atomic lowering"
if grep -F '(token).clone()' "$test_build/phase25_atomic_copy_boundary.rs" >/dev/null; then
  fail "synchronous atomic payload path retained an unnecessary clone"
fi
grep -F 'Remember_shared(true, &(token))' \
  "$test_build/phase25_atomic_copy_boundary.rs" >/dev/null ||
  fail "atomic request did not pass its non-Copy payload by immutable reference"

run_optimized_case phase25_atomic_multi_field tests/phase25_atomic_multi_field.moss \
  '1 true'
grep -F 'count: AtomicI64' "$test_build/phase25_atomic_multi_field.rs" >/dev/null ||
  fail "first field in multi-field atomic domain was not atomic"
grep -F 'enabled: AtomicBool' "$test_build/phase25_atomic_multi_field.rs" >/dev/null ||
  fail "second field in multi-field atomic domain was not atomic"
[ "$(grep -c 'Ordering::SeqCst' "$test_build/phase25_atomic_multi_field.rs")" -ge 2 ] ||
  fail "multi-field atomic domain did not retain one SeqCst total order"

run_optimized_case phase25_atomic_invariant_fallback \
  tests/phase25_atomic_invariant_fallback.moss '1'
grep -F '// Moss backend plan: Pair = DirectMutex.' \
  "$test_build/phase25_atomic_invariant_fallback.rs" >/dev/null ||
  fail "two-field invariant handler did not fall back to locking"
run_optimized_case phase25_atomic_effect_fallback \
  tests/phase25_atomic_effect_fallback.moss "$(printf 'incremented\n1')"
grep -F '// Moss backend plan: Counter = DirectMutex.' \
  "$test_build/phase25_atomic_effect_fallback.rs" >/dev/null ||
  fail "handler with echo did not fall back to locking"
compile_optimized_case phase25_atomic_external_fallback \
  tests/phase25_atomic_external_fallback.moss
grep -F '// Moss backend plan: Counter = DirectMutex.' \
  "$test_build/phase25_atomic_external_fallback.rs" >/dev/null ||
  fail "handler with message, await, and echo did not fall back to locking"
run_optimized_case phase25_atomic_float_fallback \
  tests/phase25_atomic_float_fallback.moss '1.5'
grep -F '// Moss backend plan: Gauge = DirectMutex.' \
  "$test_build/phase25_atomic_float_fallback.rs" >/dev/null ||
  fail "floating-point state was incorrectly lowered to atomics"

compile_optimized_case phase25_atomic_contention tests/phase25_atomic_contention.moss
grep -F '// Moss backend plan: Counter = DirectAtomic.' \
  "$test_build/phase25_atomic_contention.rs" >/dev/null ||
  fail "contention counter did not select atomic lowering"
iteration=1
while [ "$iteration" -le 10 ]; do
  actual=$("$test_build/phase25_atomic_contention")
  [ "$actual" = '2000 true true' ] ||
    fail "atomic counter lost work on contention iteration $iteration"
  iteration=$((iteration + 1))
done

# -O0 remains the semantic reference. Compare representative optimized paths
# with independently generated baseline mailbox executables.
for phase25_name in phase25_batching phase25_lock_coalesce phase25_rwlock \
                    phase25_atomic_counter phase25_atomic_bool phase25_atomic_multi_field; do
  "$compiler" -O0 "tests/$phase25_name.moss" \
    -o "$test_build/${phase25_name}_o0.rs"
  grep -F 'struct MossChannel<T> { state: Mutex<' \
    "$test_build/${phase25_name}_o0.rs" >/dev/null ||
    fail "$phase25_name -O0 output did not retain the mailbox reference lowering"
  if grep -E 'Moss backend plan: .* = Direct' \
      "$test_build/${phase25_name}_o0.rs" >/dev/null; then
    fail "$phase25_name -O0 output selected an optimized direct representation"
  fi
  rustc -D warnings "$test_build/${phase25_name}_o0.rs" \
    -o "$test_build/${phase25_name}_o0"
  baseline_output=$("$test_build/${phase25_name}_o0")
  optimized_output=$("$test_build/$phase25_name")
  [ "$baseline_output" = "$optimized_output" ] ||
    fail "$phase25_name produced different observable output under -O0 and -O"
done
for phase25_rust in phase25_batching phase25_lock_coalesce phase25_rwlock \
                    phase25_atomic_counter phase25_atomic_bool \
                    phase25_atomic_multi_field; do
  if grep -F 'unsafe {' "$test_build/$phase25_rust.rs" >/dev/null; then
    fail "$phase25_rust used unsafe code for a backend optimization"
  fi
done

reject_source use_after_transfer examples/use_after_transfer.moss \
  "value 'original' was transferred to 'destination' at line 10"
reject_case naked_cross_domain_call "naked cross-domain call 'worker.Ping' requires 'message' or 'await'"
reject_case invalid_await "await requires an assignment target"
reject_case unresolved_field "cannot infer type for field 'Unresolved.field'"
reject_case unresolved_state "cannot infer type for state field 'Worker.value'"
reject_case state_annotation_mismatch "state field 'Counter.value' is annotated 'int' but its initializer has type 'float'"
reject_case conflicting_reply_types "conflicting reply types in handler 'Worker.Maybe'"
reject_case main_return_value "main cannot return a value"
run_case message_payload_copy tests/negative/message_transfer.moss "$(printf 'detached\ndetached')"
run_case state_payload_copy tests/negative/domain_state_transfer.moss '34'
run_case await_payload_copy tests/negative/await_object_transfer.moss '7'
run_case reply_payload_copy tests/negative/object_reply_transfer.moss '7'
run_case nested_payload_copy tests/negative/nested_object_transfer.moss '7'
run_case string_payload_copy tests/negative/string_transfer.moss 'fresh'
run_case phase47_iteration tests/phase47_iteration.moss '12 1 20 6 9 3 3'
run_case phase47_element_effects tests/phase47_element_effects.moss 'element effects'
phase47_element_effects_json="$test_build/phase47_element_effects.json"
"$compiler" effects fn:updateAll \
  --source tests/phase47_element_effects.moss --json \
  >"$phase47_element_effects_json"
grep -F '"name": "payload", "type": "Payload", "effect": "WRITE"' \
  "$phase47_element_effects_json" >/dev/null ||
  fail 'for-loop effect analysis did not resolve the iterator element method effect'

# One source domain may be inferred independently for each declared instance.
# These semantic facts are compiler-owned and keep the source free of explicit
# generic type syntax or a dynamic value fallback.
implicit_domain_specialization_json="$test_build/implicit_domain_specialization.json"
run_case implicit_domain_specialization tests/implicit_domain_specialization.moss \
  "$(printf '10\n3.5')"
run_case implicit_domain_specialization_same_instance \
  tests/implicit_domain_specialization_same_instance.moss '20'
run_case implicit_domain_parameter_specialization \
  tests/implicit_domain_parameter_specialization.moss "$(printf '10\n20')"
run_case_either_order implicit_domain_parameter_multiple_instances \
  tests/implicit_domain_parameter_multiple_instances.moss '10' '2.5'
run_case implicit_domain_typed_parameter_instances \
  tests/implicit_domain_typed_parameter_instances.moss ''
grep -F 'fn Ping_shared(&self, worker: WorkerHandle)' \
  "$test_build/implicit_domain_typed_parameter_instances.rs" >/dev/null ||
  fail 'typed domain parameter specialization did not retain a uniform nominal Worker handle'
if grep -F 'fn Ping_shared(&self, worker: Worker__' \
    "$test_build/implicit_domain_typed_parameter_instances.rs" >/dev/null; then
  fail 'typed domain parameter specialization leaked a declared instance identity'
fi
run_case nominal_handle_conversion_paths \
  tests/nominal_handle_conversion_paths.moss "$(printf '1\n2')"
grep -F 'fn take(worker: AHandle)' \
  "$test_build/nominal_handle_conversion_paths.rs" >/dev/null ||
  fail 'ordinary function domain parameter did not use nominal handle representation'
grep -E 'fn Send\(&self, (mut )?worker: AHandle\)' \
  "$test_build/nominal_handle_conversion_paths.rs" >/dev/null ||
  fail 'method domain parameter did not use nominal handle representation'
grep -F 'fn Send_shared(&self, worker: AHandle)' \
  "$test_build/nominal_handle_conversion_paths.rs" >/dev/null ||
  fail 'message handler domain parameter did not use nominal handle representation'
grep -F 'struct A__helper__specialized_' \
  "$test_build/nominal_handle_conversion_paths.rs" >/dev/null ||
  fail 'specialized A/helper backend layout was not disambiguated from nominal A__helper'
"$compiler" inspect 'domain-specialization:Box:intBox' \
  --source tests/implicit_domain_specialization.moss --json \
  >"$implicit_domain_specialization_json"
grep -F '"semantic_identity": "domain-specialization:Box:intBox"' \
  "$implicit_domain_specialization_json" >/dev/null ||
  fail 'implicit Box Int specialization was not retained'
grep -F '"name": "value", "type": "int"' \
  "$implicit_domain_specialization_json" >/dev/null ||
  fail 'implicit Box Int specialization did not infer value: int'
"$compiler" inspect 'domain-specialization:Box:floatBox' \
  --source tests/implicit_domain_specialization.moss --json \
  >"$test_build/implicit_domain_specialization_float.json"
grep -F '"semantic_identity": "domain-specialization:Box:floatBox"' \
  "$test_build/implicit_domain_specialization_float.json" >/dev/null ||
  fail 'implicit Box Float specialization was not retained'
grep -F '"name": "value", "type": "float"' \
  "$test_build/implicit_domain_specialization_float.json" >/dev/null ||
  fail 'implicit Box Float specialization did not infer value: float'
grep -F 'struct Box__intBoxState' "$test_build/implicit_domain_specialization.rs" >/dev/null ||
  fail 'implicit Box Int specialization did not get a concrete Rust layout'
grep -F 'value: i64' "$test_build/implicit_domain_specialization.rs" >/dev/null ||
  fail 'implicit Box Int layout was not statically concrete'
grep -F 'struct Box__floatBoxState' "$test_build/implicit_domain_specialization.rs" >/dev/null ||
  fail 'implicit Box Float specialization did not get a concrete Rust layout'
grep -F 'value: f64' "$test_build/implicit_domain_specialization.rs" >/dev/null ||
  fail 'implicit Box Float layout was not statically concrete'
if grep -Eq 'Any|Box<dyn|type_id|TypeId' \
  "$test_build/implicit_domain_specialization.rs"; then
  fail 'implicit domain specialization introduced a dynamic Rust value representation'
fi
"$compiler" effects 'handler:Box.Set' \
  --source tests/implicit_domain_specialization.moss --json \
  >"$test_build/implicit_domain_specialization_effects.json"
grep -F '"domain_write": true' \
  "$test_build/implicit_domain_specialization_effects.json" >/dev/null ||
  fail 'implicit Box Set did not retain its WRITE effect summary'

# Await edges use exact declared instances, not the shared Worker source name.
implicit_domain_specialization_await_json="$test_build/implicit_domain_specialization_await.json"
run_case implicit_domain_specialization_await \
  tests/implicit_domain_specialization_await.moss ''
"$compiler" awaits 'domain:Worker' \
  --source tests/implicit_domain_specialization_await.moss --json \
  >"$implicit_domain_specialization_await_json"
grep -F '"source_instance": "intWorker", "target_instance": "floatWorker"' \
  "$implicit_domain_specialization_await_json" >/dev/null ||
  fail 'exact-instance await query collapsed the positive Worker edge'
"$compiler" inspect 'domain-specialization:Worker:intWorker' \
  --source tests/implicit_domain_specialization_await.moss --json \
  >"$test_build/implicit_domain_specialization_await_int.json"
grep -F '"name": "payload", "type": "int"' \
  "$test_build/implicit_domain_specialization_await_int.json" >/dev/null ||
  fail 'implicit intWorker specialization did not infer payload: int'
"$compiler" inspect 'domain-specialization:Worker:floatWorker' \
  --source tests/implicit_domain_specialization_await.moss --json \
  >"$test_build/implicit_domain_specialization_await_float.json"
grep -F '"name": "payload", "type": "float"' \
  "$test_build/implicit_domain_specialization_await_float.json" >/dev/null ||
  fail 'implicit floatWorker specialization did not infer payload: float'
grep -F 'struct Worker__intWorkerState' \
  "$test_build/implicit_domain_specialization_await.rs" >/dev/null ||
  fail 'implicit intWorker specialization did not get a concrete Rust layout'
grep -F 'struct Worker__floatWorkerState' \
  "$test_build/implicit_domain_specialization_await.rs" >/dev/null ||
  fail 'implicit floatWorker specialization did not get a concrete Rust layout'
reject_source implicit_domain_specialization_await_cycle \
  tests/negative/implicit_domain_specialization_await_cycle.moss \
  'await cycle detected:'
grep -F 'intWorker' "$test_build/implicit_domain_specialization_await_cycle.stderr" >/dev/null ||
  fail 'exact-instance await cycle omitted intWorker'
grep -F 'floatWorker' "$test_build/implicit_domain_specialization_await_cycle.stderr" >/dev/null ||
  fail 'exact-instance await cycle omitted floatWorker'

reject_case phase47_mutation_during_for \
  "cannot structurally mutate collection 'values' during an active READ traversal"
reject_case phase47_missing_iterator \
  "type 'NotAnIterator' does not satisfy Iterator: missing next() or iter()"
reject_case phase47_zero_step "range step must be a positive non-zero Int literal"
run_case read_alias tests/read_alias.moss "$(printf '7 7\n7')"
run_case primitive_field_projection tests/primitive_field_projection.moss \
  "$(printf '42\n7')"
run_case method_receiver_effects tests/method_receiver_effects.moss \
  "$(printf '42\n7')"
run_case generic_method_effects tests/generic_method_effects.moss '12 5'
run_case phase2_safety examples/phase2_safety.moss \
  "$(printf 'read aliases: 7 7\nprimitive projection: 7\nafter await copy: 7 7\ntransferred payload: original')"

# Phase 10.1A: the checked Moss AST executes directly without rustc.
interp_basic_output=$($compiler run --interp tests/phase10_interpreter_basic.moss)
[ "$interp_basic_output" = '13 5' ] || fail 'fast interpreter basic execution differed'
interp_struct_output=$($compiler run --interp tests/phase10_interpreter_structs.moss)
[ "$interp_struct_output" = '9' ] || fail 'fast interpreter struct execution differed'
interp_loop_output=$($compiler run --interp tests/phase10_interpreter_loop.moss)
[ "$interp_loop_output" = '10' ] || fail 'fast interpreter loop execution differed'
interp_test_output=$($compiler test --interp tests/phase10_interpreter_tests.moss)
printf '%s\n' "$interp_test_output" | grep -F '2 passed' >/dev/null ||
  fail 'fast interpreter test runner did not report passing tests'
interp_trace=$($compiler run --interp --trace tests/phase10_interpreter_basic.moss 2>&1 >/dev/null)
printf '%s\n' "$interp_trace" | grep -F '"event":"FunctionEnter"' >/dev/null ||
  fail 'fast interpreter did not emit structured trace events'
if command -v python3 >/dev/null 2>&1; then
  python3 tests/tooling/check_phase10_fast_debug_project.py "$compiler" ||
    fail 'fast interpreter did not execute the complete project source closure'
fi

# Phase 2.6: incoming message payloads are immutable READ snapshots. Direct
# synchronous handlers may borrow non-Copy payloads, while mailbox sends keep
# an owned snapshot at the queue boundary.
run_optimized_case phase26_direct_payload tests/phase26_direct_payload.moss '7 8'
grep -F 'fn Process_locked(&self, state: &mut WorkerState, payload: &Payload)' \
  "$test_build/phase26_direct_payload.rs" >/dev/null ||
  fail 'direct handler did not receive a non-Copy payload by immutable reference'
grep -F 'worker.Process_shared(&(data))' "$test_build/phase26_direct_payload.rs" >/dev/null ||
  fail 'direct call did not pass the payload by immutable reference'
if grep -F '(data).clone()' "$test_build/phase26_direct_payload.rs" >/dev/null; then
  fail 'direct synchronous payload boundary inserted an unnecessary clone'
fi
run_optimized_case phase26_lock_payload tests/phase26_lock_payload.moss '7 8'
grep -F 'COALESCED LOCK REGION' "$test_build/phase26_lock_payload.rs" >/dev/null ||
  fail 'payload lock-coalescing fixture did not form one lock region'
if grep -F 'First_locked(&mut' "$test_build/phase26_lock_payload.rs" | grep -F '.clone()' >/dev/null ||
   grep -F 'Second_locked(&mut' "$test_build/phase26_lock_payload.rs" | grep -F '.clone()' >/dev/null; then
  fail 'coalesced synchronous payload calls retained an unnecessary clone'
fi
run_case phase26_mailbox_payload tests/phase26_mailbox_payload.moss '9'
grep -F '(data).clone()' "$test_build/phase26_mailbox_payload.rs" >/dev/null ||
  fail 'mailbox payload did not retain an owned snapshot copy'
run_case phase26_payload_forward tests/phase26_payload_forward.moss '11'
run_optimized_case phase26_copy_payloads tests/phase26_copy_payloads.moss \
  "11 8
forwarded: 10"

reject_case phase26_payload_write \
  "cannot WRITE incoming message payload 'payload'"
reject_case phase26_payload_transitive_write \
  "cannot WRITE incoming message payload 'payload'"
reject_case phase26_payload_consume \
  "cannot CONSUME incoming message payload 'payload'"
reject_case phase26_payload_state_store \
  "cannot CONSUME incoming message payload 'payload'"
reject_case phase26_payload_reply \
  "cannot reply with incoming message payload 'payload'"
reject_case phase26_payload_alias_reply \
  "cannot CONSUME incoming message payload 'payload'"
reject_case phase26_payload_reply_copy \
  "cannot reply with incoming message payload 'x'"
reject_case phase26_payload_rebind \
  "cannot WRITE incoming message payload 'payload'"
reject_case phase26_payload_rebind_copy \
  "cannot WRITE incoming message payload 'x'"
grep -F 'fn read_code(&self)' "$test_build/method_receiver_effects.rs" >/dev/null ||
  fail "copyable field read did not retain a READ receiver"
grep -F 'fn take_payload(self)' "$test_build/method_receiver_effects.rs" >/dev/null ||
  fail "nontrivial field movement did not lower a CONSUME receiver"
warning_stdout="$test_build/large_payload_warning.stdout"
warning_stderr="$test_build/large_payload_warning.stderr"
"$compiler" --check tests/large_payload_warning.moss >"$warning_stdout" 2>"$warning_stderr"
grep -F 'warning: message payload copies 1088 bytes across a domain boundary' \
  "$warning_stderr" >/dev/null || fail "large payload did not report its copy cost"

reject_case branch_divergent_domain_types \
  "binding 'target' has conflicting domain types across control-flow paths: Alpha and Beta"
reject_case branch_domain_await_cycle \
  "binding 'target' has conflicting domain types across control-flow paths: Right and Bypass"
reject_case unbounded_await_handler \
  "await target 'target' cannot be statically and conservatively bounded to one concrete domain"
reject_case unbounded_await_main_helper \
  "await target 'target' cannot be statically and conservatively bounded to one concrete domain"

reject_case direct_await_cycle "await cycle detected:"
grep -F 'Left --await line 3--> Right' \
  "$test_build/direct_await_cycle.stderr" >/dev/null ||
  fail "direct cycle witness omitted the Left await source site"
grep -F 'Right --await line 11--> Left' \
  "$test_build/direct_await_cycle.stderr" >/dev/null ||
  fail "direct cycle witness omitted the Right await source site"

reject_case transitive_await_cycle "await cycle detected:"
grep -F 'First --await line 3--> Second' \
  "$test_build/transitive_await_cycle.stderr" >/dev/null ||
  fail "transitive cycle witness omitted the First await source site"
grep -F 'Second --await line 11--> Third' \
  "$test_build/transitive_await_cycle.stderr" >/dev/null ||
  fail "transitive cycle witness omitted the Second await source site"
grep -F 'Third --await line 16--> First' \
  "$test_build/transitive_await_cycle.stderr" >/dev/null ||
  fail "transitive cycle witness omitted the Third await source site"

reject_case function_await_cycle "await cycle detected:"
grep -F 'Left --await line 2--> Right' \
  "$test_build/function_await_cycle.stderr" >/dev/null ||
  fail "helper-hidden cycle witness omitted the helper await source site"
grep -F 'Right --await line 15--> Left' \
  "$test_build/function_await_cycle.stderr" >/dev/null ||
  fail "helper-hidden cycle witness omitted the returning await source site"

reject_case repeated_await_cycle_edges "await cycle detected:"
grep -F 'Left --await line 3--> Right' \
  "$test_build/repeated_await_cycle_edges.stderr" >/dev/null ||
  fail "repeated-edge cycle witness did not report the edge used by DFS"
grep -F 'Right --await line 12--> Left' \
  "$test_build/repeated_await_cycle_edges.stderr" >/dev/null ||
  fail "repeated-edge cycle witness omitted its closing edge"
if grep -F 'Left --await line 4--> Right' \
    "$test_build/repeated_await_cycle_edges.stderr" >/dev/null; then
  fail "repeated-edge cycle witness reported an unrelated duplicate await site"
fi
reject_case recursive_function "recursive local call cycle: recurse -> recurse"
reject_case mutually_recursive_functions "recursive local call cycle: first -> second -> first"
reject_case write_read_alias "conflicting accesses to value 'item' in call to 'conflict': mutation overlaps with read"
reject_case write_write_alias "conflicting accesses to value 'item' in call to 'conflict': mutation overlaps with mutation"
reject_case consume_read_alias "conflicting accesses to value 'item' in call to 'conflict': transfer overlaps with read"
reject_case consume_write_alias "conflicting accesses to value 'item' in call to 'conflict': transfer overlaps with mutation"
reject_case double_consume_alias "conflicting accesses to value 'item' in call to 'conflict': transfer overlaps with transfer"
reject_case nontrivial_field_move "value 'packet' was transferred"
reject_case branch_join_consume "value 'item' was transferred"
reject_source example_await_cycle examples/errors/await_cycle.moss \
  "await cycle detected:"
grep -F 'Coordinator --await line 6--> Worker' \
  "$test_build/example_await_cycle.stderr" >/dev/null ||
  fail "unreachable-branch cycle witness omitted its helper await site"
grep -F 'Worker --await line 21--> Coordinator' \
  "$test_build/example_await_cycle.stderr" >/dev/null ||
  fail "unreachable-branch cycle witness omitted its closing await site"
reject_source example_recursive_call examples/errors/recursive_call.moss \
  "recursive local call cycle: countdown -> countdown"
reject_source example_conflicting_access examples/errors/conflicting_access.moss \
  "conflicting accesses to value 'counter' in call to 'increment_from': mutation overlaps with read"

reject_cluster cluster_duplicate_spawn tests/shared_memory_contention.moss 'Counter,Producer' \
  "clustered domain type 'Producer' must be spawned exactly once in main (found 2)"
reject_cluster cluster_unknown_domain examples/counter.moss 'Counter,Missing' \
  'unknown domain in cluster: Missing'
reject_cluster cluster_await_cycle tests/cluster_await_cycle.moss 'Left,Right' \
  'await cycle detected:'

compile_cluster_case clustered_checkout examples/checkout.moss 'Checkout,Inventory,Payments'
clustered_checkout_output=$("$test_build/clustered_checkout")
[ "$clustered_checkout_output" = 'charged: 75
order completed
order rejected: insufficient inventory' ] || fail "clustered checkout output differed"
grep -F 'Domain cluster 0: static same-thread dispatch for Checkout, Inventory, Payments.' \
  "$test_build/clustered_checkout.rs" >/dev/null || fail "cluster plan was not recorded"
grep -F 'self.Inventory_Reserve_local(quantity)' "$test_build/clustered_checkout.rs" >/dev/null ||
  fail "clustered await did not select the local call version"
grep -F 'self.Payments_Charge_local((quantity).wrapping_mul(price))' \
  "$test_build/clustered_checkout.rs" >/dev/null ||
  fail "second clustered await did not select the local call version"
grep -F '// Moss backend: CLUSTER-LOCAL version: flush older local messages, then call the handler directly' \
  "$test_build/clustered_checkout.rs" >/dev/null ||
  fail "clustered checkout omitted its local lowering annotation"
[ "$(grep -c 'thread::spawn(move || {' "$test_build/clustered_checkout.rs")" -eq 1 ] ||
  fail "clustered domains did not share exactly one worker thread"
sed -n '/^impl MossCluster0Runtime {/,/^fn spawn_moss_cluster_0/p' \
  "$test_build/clustered_checkout.rs" >"$test_build/clustered_checkout.local.rs"
if grep -Eq 'Mutex|Condvar|Atomic|MossSender|_shared\(' \
    "$test_build/clustered_checkout.local.rs"; then
  fail "cluster-local call implementation retained a concurrency primitive"
fi

compile_cluster_case clustered_object_pipeline examples/object_pipeline.moss 'Archive,Workshop'
clustered_object_output=$("$test_build/clustered_object_pipeline")
[ "$clustered_object_output" = 'created: widget 1 false
observed: widget 1 false
revised: widget 2 true
verified: widget 2 true
archive: widget 2 true' ] || fail "clustered object pipeline output differed"
grep -F 'self.__moss_enqueue_local(MossCluster0LocalMsg::Archive_Store' \
  "$test_build/clustered_object_pipeline.rs" >/dev/null ||
  fail "clustered one-way call did not select the lock-free local queue"
grep -F 'fn Workshop_Start_local(&self, archive: ArchiveLocalRef)' \
  "$test_build/clustered_object_pipeline.rs" >/dev/null ||
  fail "cluster-local domain capability retained its shared representation"
if grep -F 'Workshop_Observe(item, (archive).clone())' \
    "$test_build/clustered_object_pipeline.rs" >/dev/null; then
  fail "cluster-local domain capability cloned a shared handle"
fi

compile_cluster_case cluster_mixed_fifo tests/cluster_mixed_fifo.moss 'Caller,Target'
mixed_fifo_output=$("$test_build/cluster_mixed_fifo")
[ "$mixed_fifo_output" = 'target: 1
target: 3
result: 3' ] || fail "clustered direct await overtook an older local message"
grep -F 'self.__moss_flush_Target_local();' "$test_build/cluster_mixed_fifo.rs" >/dev/null ||
  fail "clustered mixed send/await path did not emit its FIFO-preserving flush"

compile_cluster_case cluster_domain_ref tests/cluster_domain_ref.moss 'Driver,Registry,Worker'
[ "$("$test_build/cluster_domain_ref")" = 'cluster ref: ping
cluster ref: ping' ] ||
  fail "cluster-local domain reference did not retain capability behavior"
grep -F 'fn Registry_Get_local(&self, worker: WorkerLocalRef) -> Option<WorkerLocalRef>' \
  "$test_build/cluster_domain_ref.rs" >/dev/null ||
  fail "cluster-local request/reply did not use the zero-sized local capability"

compile_cluster_case cluster_external_ref tests/cluster_external_ref.moss 'Driver,Relay'
[ "$("$test_build/cluster_external_ref")" = 'external ref: ping' ] ||
  fail "cluster-local forwarding of an external capability failed"
grep -F 'fn Relay_Pass_local(&self, worker: Rc<WorkerRef>)' \
  "$test_build/cluster_external_ref.rs" >/dev/null ||
  fail "external capability did not use a non-atomic cluster-local Rc"

compile_cluster_case clustered_non_reentrant tests/non_reentrant.moss 'Waiting,Dependency'
iteration=1
while [ "$iteration" -le 20 ]; do
  actual=$("$test_build/clustered_non_reentrant")
  [ "$actual" = 'start
finish
second' ] || fail "clustered handler execution became reentrant on iteration $iteration"
  iteration=$((iteration + 1))
done

compile_case non_reentrant tests/non_reentrant.moss
iteration=1
while [ "$iteration" -le 20 ]; do
  actual=$("$test_build/non_reentrant")
  [ "$actual" = 'start
finish
second' ] || fail "non_reentrant output was out of order on iteration $iteration"
  iteration=$((iteration + 1))
done

compile_shared_memory_case shared_memory_non_reentrant tests/non_reentrant.moss
iteration=1
while [ "$iteration" -le 20 ]; do
  actual=$("$test_build/shared_memory_non_reentrant")
  [ "$actual" = 'start
finish
second' ] || fail "shared-memory non_reentrant output was out of order on iteration $iteration"
  iteration=$((iteration + 1))
done

compile_case fallthrough tests/fallthrough.moss
fallthrough_stdout="$test_build/fallthrough.stdout"
fallthrough_stderr="$test_build/fallthrough.stderr"
set +e
timeout 5 "$test_build/fallthrough" >"$fallthrough_stdout" 2>"$fallthrough_stderr"
fallthrough_status=$?
set -e
[ "$fallthrough_status" -ne 0 ] || fail "fallthrough unexpectedly succeeded"
[ "$fallthrough_status" -ne 124 ] || fail "fallthrough await hung"
grep -F 'Moss await failed: Worker.Maybe completed without a reply' "$fallthrough_stderr" >/dev/null ||
  fail "fallthrough did not report a clear await failure"

compile_shared_memory_case shared_memory_fallthrough tests/fallthrough.moss
shared_fallthrough_stdout="$test_build/shared_memory_fallthrough.stdout"
shared_fallthrough_stderr="$test_build/shared_memory_fallthrough.stderr"
set +e
timeout 5 "$test_build/shared_memory_fallthrough" >"$shared_fallthrough_stdout" 2>"$shared_fallthrough_stderr"
shared_fallthrough_status=$?
set -e
[ "$shared_fallthrough_status" -ne 0 ] || fail "shared-memory fallthrough unexpectedly succeeded"
[ "$shared_fallthrough_status" -ne 124 ] || fail "shared-memory fallthrough await hung"
grep -F 'Moss await failed: Worker.Maybe completed without a reply' "$shared_fallthrough_stderr" >/dev/null ||
  fail "shared-memory fallthrough did not report a clear await failure"

reject_case await_one_way "cannot await one-way handler 'Worker.Notify'"
reject_case reply_main "reply is only valid in a handler declaring '-> Type'"
reject_case await_unknown_receiver \
  "await target 'missing' cannot be statically and conservatively bounded to one concrete domain"
reject_case await_unknown_handler "domain Worker has no message handler 'Missing'"
reject_case await_wrong_arity 'message Worker.Work expects 1 arguments, got 0'
reject_case self_await 'a domain cannot await itself because handlers are non-reentrant'
reject_case duck_missing_method "missing required method 'describe'"
reject_case duck_wrong_method_arity "wrong arity for required method 'describe'"
reject_case duck_incompatible_method_argument "method 'draw' is incompatible with argument types (string)"
reject_case trait_missing_method "missing required trait method 'area'"
reject_case trait_incompatible_method_signature "trait method 'draw' has an incompatible parameter signature"
reject_case conflicting_method_results "conflicting result expectations for required method 'current'"
reject_case implicit_domain_specialization_conflict \
  "conflicting domain specialization for instance 'box'"
reject_case implicit_domain_specialization_conflict_transitive \
  "conflicting domain specialization for instance 'box'"
reject_case implicit_domain_parameter_conflict_zero \
  "handler 'Set' parameter 0"
reject_case implicit_domain_parameter_conflict_one \
  "handler 'Set' parameter 1"
reject_case typed_domain_parameter_await_cycle 'await cycle detected:'
grep -F 'w2' "$test_build/typed_domain_parameter_await_cycle.stderr" >/dev/null ||
  fail 'typed domain parameter await cycle omitted the concrete w2 instance'
if grep -F 'w1' "$test_build/typed_domain_parameter_await_cycle.stderr" >/dev/null; then
  fail 'typed domain parameter await cycle reported the non-cyclic w1 instance'
fi
reject_case domain_handle_prefix_collision_message "argument 1 to message Receiver.Send has type 'A__helper', expected 'A'"
reject_case domain_handle_prefix_collision_function "argument 1 to function 'take' has type 'A__helper', expected 'A'"
reject_case domain_handle_prefix_collision_method "no matching method 'Receiver.Send' for supplied arguments"
reject_case unresolved_collection_type "heterogeneous or unresolved collection element type"
reject_case heterogeneous_collection "heterogeneous or unresolved collection element type"
reject_case functional_filter_not_bool "filter predicate returns 'int'; expected 'bool'"
reject_case functional_map_argument_type \
  "functional callable 'text_length' argument 1 has type 'int', expected 'string'"
reject_case functional_unresolved_callable \
  "unresolved functional callable 'not_declared'"
reject_case functional_reduce_mismatch \
  "reduce callable returns 'bool'; expected accumulator type 'int'"
reject_case functional_reduce_initial_reuse \
  "value 'initial' was transferred"
reject_case functional_consume_element \
  "functional callable 'take' requires CONSUME access to an element"
reject_case functional_mutable_capture \
  "functional placeholder cannot mutate captured binding 'captured'"
reject_case functional_mutating_bound_method \
  "functional bound method 'accumulator.add' cannot mutate or consume captured binding 'accumulator'"
reject_case functional_consume_capture \
  "functional placeholder requires CONSUME access to captured binding 'captured'"
reject_case functional_unbounded_callable \
  "callable identity 'candidate' is not statically bounded to one function"
reject_case functional_unbounded_hof \
  "argument 2 to function 'apply' is not a statically bounded callable identity"
reject_case functional_domain_boundary \
  "functional pipeline must be materialized in a local binding before crossing a domain boundary"
reject_case functional_recursion \
  "recursive local call cycle: recurse -> recurse"
reject_case functional_hof_recursion \
  "recursive local call cycle: recurse -> recurse"
reject_case functional_await_cycle \
  "await cycle detected:"
reject_case functional_callback_alias \
  "conflicting accesses to value 'item' in call to 'conflict': mutation overlaps with read"
reject_case functional_placeholder_noncopy_identity \
  "functional map result aliases nontrivial source storage through '_'"
reject_case functional_placeholder_noncopy_field \
  "functional map result aliases nontrivial source storage through '_'"
reject_case functional_helper_mutable_capture \
  "functional placeholder requires WRITE access to captured binding 'captured'"

# Phase 5 tooling shares one deterministic compiler-owned provenance map.
tooling_o0_rs="$test_build/phase5_tooling_o0.rs"
tooling_o0_map="$test_build/phase5_tooling_o0.mossmap"
tooling_opt_rs="$test_build/phase5_tooling_opt.rs"
tooling_opt_map="$test_build/phase5_tooling_opt.mossmap"
"$compiler" -O0 tests/phase5_tooling.moss -o "$tooling_o0_rs"
cp "$tooling_o0_map" "$test_build/phase5_tooling_first.mossmap"
"$compiler" -O0 tests/phase5_tooling.moss -o "$tooling_o0_rs"
cmp "$tooling_o0_map" "$test_build/phase5_tooling_first.mossmap" >/dev/null ||
  fail "identical builds produced different Moss debug maps"
"$compiler" -O tests/phase5_tooling.moss -o "$tooling_opt_rs"
rustc -D warnings "$tooling_o0_rs" -o "$test_build/phase5_tooling_o0"
rustc -D warnings "$tooling_opt_rs" -o "$test_build/phase5_tooling_opt"
[ "$("$test_build/phase5_tooling_o0")" = "$("$test_build/phase5_tooling_opt")" ] ||
  fail "Phase 5 fixture differed between -O0 and optimized execution"
for tooling_map in "$tooling_o0_map" "$tooling_opt_map"; do
  grep -F '"format": "moss-debug-map"' "$tooling_map" >/dev/null ||
    fail "tooling map omitted its format contract"
  grep -F '"construct_kind": "function"' "$tooling_map" >/dev/null ||
    fail "tooling map omitted function provenance"
  grep -F '"construct_kind": "method"' "$tooling_map" >/dev/null ||
    fail "tooling map omitted method provenance"
  grep -F '"construct_kind": "handler"' "$tooling_map" >/dev/null ||
    fail "tooling map omitted handler provenance"
  grep -F '"construct_kind": "functional_pipeline"' "$tooling_map" >/dev/null ||
    fail "tooling map omitted functional pipeline provenance"
  grep -F '"semantic_identity": "fn:normalize@7"' "$tooling_map" >/dev/null ||
    fail "tooling map did not retain a deterministic source/provenance identity"
  grep -F '"start_line": 7' "$tooling_map" >/dev/null ||
    fail "tooling map omitted real Moss source lines"
  if grep -Eq 'transient_id|"ir_id"|"pipeline_id"' "$tooling_map"; then
    fail "tooling map exposed a transient compiler IR identity"
  fi
done
grep -F 'fn:summarize<vector[int]>@12:expression:0:stage:1' \
  "$tooling_opt_map" >/dev/null ||
  fail "optimized tooling map lost fused map-stage provenance"
grep -F 'fn:summarize<vector[int]>@12:expression:0:stage:2' \
  "$tooling_opt_map" >/dev/null ||
  fail "optimized tooling map lost fused filter-stage provenance"
grep -F 'fn:summarize<vector[int]>@12:expression:0:stage:3' \
  "$tooling_opt_map" >/dev/null ||
  fail "optimized tooling map lost fused reduction provenance"

tools/moss-build-debug tests/phase5_tooling.moss \
  -o "$test_build/phase5_tooling_debug"
[ "$("$test_build/phase5_tooling_debug")" = "$(printf '6\n7\n5')" ] ||
  fail "Phase 5 debug build changed Moss behavior"
grep -F '"debug_build": true' "$test_build/phase5_tooling_debug.mossmap" >/dev/null ||
  fail "debug map did not identify its debug-oriented build"
if command -v nm >/dev/null 2>&1; then
  nm "$test_build/phase5_tooling_debug" |
    grep -F 'moss__function__normalize__' >/dev/null ||
    fail "debug build omitted the stable native function symbol"
fi

"$compiler" agent bootstrap --json >"$test_build/phase6_bootstrap.first"
"$compiler" agent bootstrap --json >"$test_build/phase6_bootstrap.second"
cmp "$test_build/phase6_bootstrap.first" \
    "$test_build/phase6_bootstrap.second" >/dev/null ||
  fail "Phase 6A agent bootstrap was not deterministic"
grep -F '"protocol_version": 1' \
  "$test_build/phase6_bootstrap.first" >/dev/null ||
  fail "Phase 6A bootstrap omitted the versioned protocol envelope"
"$compiler" check tests/phase6_agent_api.moss --json \
  >"$test_build/phase6_check.json"
grep -F '"command": "check"' "$test_build/phase6_check.json" >/dev/null ||
  fail "Phase 6A structured check command did not execute"

if command -v python3 >/dev/null 2>&1; then
  export PYTHONDONTWRITEBYTECODE=1
  python3 tests/tooling/check_agent_api.py "$compiler"
  python3 tests/tooling/check_project_workflow.py "$compiler" "$test_build"
  python3 tests/tooling/check_multifile_project.py "$compiler"
  python3 tests/tooling/check_phase8_separate_compilation.py "$compiler"
  python3 tests/tooling/check_phase6_ai_native.py "$compiler" "$test_build"
  python3 tests/tooling/check_debug_map.py \
    "$tooling_o0_map" "$tooling_opt_map" \
    "$test_build/phase5_tooling_debug.mossmap" \
    tests/phase5_tooling.moss \
    "$test_build/phase45_shared_traversal_optimized.mossmap" \
    tests/phase45_shared_traversal.moss
  python3 tools/moss_lldb.py resolve \
    "$test_build/phase5_tooling_debug.mossmap" \
    tests/phase5_tooling.moss 8 >"$test_build/phase5_resolve.json"
  grep -F '"generated_line"' "$test_build/phase5_resolve.json" >/dev/null ||
    fail "shared map resolver did not translate a Moss source line"
else
  echo 'skipping optional Moss LLDB/map resolver tests (python3 not found)'
fi

if command -v emacs >/dev/null 2>&1; then
  emacs --batch -Q -L editors/emacs -l moss-mode-tests \
    -f ert-run-tests-batch-and-exit
else
  echo 'skipping optional Emacs moss-mode tests (emacs not found)'
fi

tooling_objdump=""
if command -v llvm-objdump >/dev/null 2>&1; then
  tooling_objdump=$(command -v llvm-objdump)
elif command -v objdump >/dev/null 2>&1; then
  tooling_objdump=$(command -v objdump)
fi
if [ -n "$tooling_objdump" ] && command -v nm >/dev/null 2>&1; then
  tooling_symbol=$(nm "$test_build/phase5_tooling_debug" |
    awk '/moss__function__normalize__/ { print $3; exit }')
  [ -n "$tooling_symbol" ] || fail "could not locate Phase 5 native symbol"
  case "$tooling_objdump" in
    *llvm-objdump)
      "$tooling_objdump" --demangle --source --line-numbers \
        "--disassemble-symbols=$tooling_symbol" \
        "$test_build/phase5_tooling_debug" \
        >"$test_build/phase5_tooling.asm" ;;
    *)
      "$tooling_objdump" --demangle --source --line-numbers \
        "--disassemble=$tooling_symbol" \
        "$test_build/phase5_tooling_debug" \
        >"$test_build/phase5_tooling.asm" ;;
  esac
  grep -F "$tooling_symbol" "$test_build/phase5_tooling.asm" >/dev/null ||
    fail "objdump could not disassemble the mapped Moss function symbol"
  tooling_fused_symbol=$(nm "$test_build/phase5_tooling_opt" |
    awk '/moss__function__summarize__/ { print $3; exit }')
  [ -n "$tooling_fused_symbol" ] ||
    fail "optimized fused pipeline has no mapped native function symbol"
  case "$tooling_objdump" in
    *llvm-objdump)
      "$tooling_objdump" --demangle --source --line-numbers \
        "--disassemble-symbols=$tooling_fused_symbol" \
        "$test_build/phase5_tooling_opt" \
        >"$test_build/phase5_tooling_fused.asm" ;;
    *)
      "$tooling_objdump" --demangle --source --line-numbers \
        "--disassemble=$tooling_fused_symbol" \
        "$test_build/phase5_tooling_opt" \
        >"$test_build/phase5_tooling_fused.asm" ;;
  esac
  grep -F "$tooling_fused_symbol" "$test_build/phase5_tooling_fused.asm" >/dev/null ||
    fail "objdump could not resolve the fused pipeline's mapped symbol"
else
  echo 'skipping optional Moss disassembly test (objdump or nm not found)'
fi

tooling_lldb_dap=""
if command -v lldb-dap >/dev/null 2>&1; then
  tooling_lldb_dap=$(command -v lldb-dap)
else
  tooling_lldb_dap_version=-1
  for tooling_lldb_dap_candidate in /usr/bin/lldb-dap-*; do
    if [ -x "$tooling_lldb_dap_candidate" ]; then
      tooling_lldb_dap_candidate_version=${tooling_lldb_dap_candidate##*-}
      case "$tooling_lldb_dap_candidate_version" in
        ''|*[!0-9]*) continue ;;
      esac
      if [ "$tooling_lldb_dap_candidate_version" -gt "$tooling_lldb_dap_version" ]; then
        tooling_lldb_dap=$tooling_lldb_dap_candidate
        tooling_lldb_dap_version=$tooling_lldb_dap_candidate_version
      fi
    fi
  done
fi

tooling_process_tracing_denied() {
  grep -E \
    'Connection shut down by remote side while waiting for reply to initial handshake packet|Operation not permitted|ptrace' \
    "$@" >/dev/null
}

if command -v lldb >/dev/null 2>&1; then
  if lldb --batch \
    -o "command script import tools/moss_lldb.py" \
    -o "target create $test_build/phase5_tooling_debug" \
    -o "moss-map-load $test_build/phase5_tooling_debug.mossmap" \
    -o "moss-break $(pwd)/tests/phase5_tooling.moss:8" \
    -o run -o "frame variable value" -o moss-where -o moss-stack \
    -o "moss-stack --all" \
    >"$test_build/phase5_lldb.stdout" \
    2>"$test_build/phase5_lldb.stderr"; then
    grep -F 'stop reason = breakpoint' "$test_build/phase5_lldb.stdout" >/dev/null ||
      fail "LLDB did not stop at the translated Moss breakpoint"
    grep -F 'phase5_tooling.moss:8' "$test_build/phase5_lldb.stdout" >/dev/null ||
      fail "LLDB did not present the mapped Moss source location"
    grep -F 'value' "$test_build/phase5_lldb.stdout" >/dev/null ||
      fail "LLDB could not inspect the ordinary Moss function local"
    grep -F '[function] fn:normalize@7' "$test_build/phase5_lldb.stdout" >/dev/null ||
      fail "moss-where did not translate the selected native frame"
    grep -F '#0 fn:normalize@7 at ' "$test_build/phase5_lldb.stdout" >/dev/null ||
      fail "moss-stack did not present the mapped Moss frame"
    grep -F 'std::rt::lang_start' "$test_build/phase5_lldb.stdout" >/dev/null ||
      fail "moss-stack --all did not expose runtime helper frames"
  elif tooling_process_tracing_denied \
    "$test_build/phase5_lldb.stdout" "$test_build/phase5_lldb.stderr"; then
    echo 'skipping optional live Moss LLDB CLI test (process tracing unavailable)'
  else
    fail "LLDB is installed but the live Moss command-bridge integration failed"
  fi
else
  echo 'skipping optional Moss LLDB CLI test (lldb not found)'
fi

if command -v python3 >/dev/null 2>&1 && [ -n "$tooling_lldb_dap" ]; then
  if PYTHONDONTWRITEBYTECODE=1 python3 tests/tooling/check_lldb_dap.py \
      "$tooling_lldb_dap" \
      "$test_build/phase5_tooling_debug" \
      "$test_build/phase5_tooling_debug.mossmap" \
      tests/phase5_tooling.moss \
      >"$test_build/phase5_lldb_dap.stdout" \
      2>"$test_build/phase5_lldb_dap.stderr"; then
    grep -F 'lldb-dap Moss debugging passed:' \
      "$test_build/phase5_lldb_dap.stdout" >/dev/null ||
      fail "lldb-dap closeout test did not reach clean process termination"
  elif tooling_process_tracing_denied \
    "$test_build/phase5_lldb_dap.stdout" \
    "$test_build/phase5_lldb_dap.stderr"; then
    echo 'skipping optional live Moss lldb-dap test (process tracing unavailable)'
  else
    fail "lldb-dap is installed but the real Moss DAP integration failed"
  fi
else
  echo 'skipping optional real Moss DAP test (lldb-dap or python3 not found)'
fi

echo 'all Moss v0.2 tests passed'
