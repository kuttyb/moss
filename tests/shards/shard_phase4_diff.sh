#!/bin/sh
set -eu

compiler=${1:-./moss}
test_build=${2:-build/tests/phase4_diff}
mkdir -p "$test_build"

. "$(dirname "$0")/../lib_test.sh"

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
  tests/phase4_callable_specialization.moss '6 8.0'
run_phase4_differential phase4_hof tests/phase4_hof_smoke.moss '6 4'
run_phase4_differential phase4_effect_order tests/phase4_effect_smoke.moss \
  "$(printf 'first 1\nfirst 2\nfirst 3\nsecond 1\nsecond 2\nsecond 3\ntotal 6')"
run_phase4_differential phase4_failure_barrier \
  tests/phase4_failure_barrier.moss '5'
run_phase4_differential phase4_domain_barrier \
  tests/phase4_domain_barrier.moss '12'
run_phase4_differential phase4_message_barrier \
  tests/phase4_message_barrier.moss '2 1 1'
run_phase4_differential phase4_message_result_barrier \
  tests/phase4_message_result_barrier.moss '6'
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
run_phase4_differential phase45_single_assignment_var_virtualizes \
  tests/phase45_single_assignment_var_virtualizes.moss '12'
run_phase4_differential phase45_var_mutation_materializes \
  tests/phase45_var_mutation_materializes.moss '16'
run_phase4_differential phase45_var_reassignment_materializes \
  tests/phase45_var_reassignment_materializes.moss '15'
var_mutation_interp=$($compiler run --interp tests/phase45_var_mutation_materializes.moss)
[ "$var_mutation_interp" = '16' ] ||
  fail 'Fast Debug differed from native after Var producer mutation'
var_reassignment_interp=$($compiler run --interp tests/phase45_var_reassignment_materializes.moss)
[ "$var_reassignment_interp" = '15' ] ||
  fail 'Fast Debug differed from native after Var producer reassignment'
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
if lowered_rust "$test_build/phase4_fusion_optimized.rs" | grep -F 'Vec::new()' >/dev/null; then
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
  "$test_build/phase4_message_result_barrier_optimized.rs" >/dev/null ||
  fail 'message callback did not stop fusion'
grep -F 'Moss backend: EAGER FUNCTIONAL PIPELINE reference semantics' \
  "$test_build/phase4_message_result_barrier_optimized.rs" >/dev/null ||
  fail 'message callback did not stop fusion'
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
if lowered_rust "$test_build/phase45_divergent_work_skipping_optimized.rs" | grep -F 'break;' >/dev/null; then
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

[ "$(lowered_rust "$test_build/phase45_short_circuit_single_optimized.rs" |
    grep -c 'break;')" -eq 3 ] ||
  fail 'optimized standalone any/all did not short circuit'
if lowered_rust "$test_build/phase45_short_circuit_single_o0.rs" | grep -F 'break;' >/dev/null; then
  fail '-O0 any/all did not retain complete eager traversal'
fi
grep -F 'Moss backend: EAGER FUNCTIONAL PIPELINE reference semantics' \
  "$test_build/phase45_short_circuit_effect_barrier_optimized.rs" >/dev/null ||
  fail 'observable any/map callbacks did not block skipping work'
if lowered_rust "$test_build/phase45_short_circuit_effect_barrier_optimized.rs" | grep -F 'break;' >/dev/null; then
  fail 'effectful any callback was incorrectly short circuited'
fi
grep -F 'Moss backend: EAGER FUNCTIONAL PIPELINE reference semantics' \
  "$test_build/phase45_short_circuit_failure_barrier_optimized.rs" >/dev/null ||
  fail 'possibly failing any callback did not retain eager traversal'
if lowered_rust "$test_build/phase45_short_circuit_failure_barrier_optimized.rs" | grep -F 'break;' >/dev/null; then
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
if lowered_rust "$test_build/phase45_cross_let_fusion_optimized.rs" | grep -F 'Vec::new()' >/dev/null; then
  fail 'cross-binding fusion allocated an intermediate vector'
fi
grep -F "FUNCTIONAL INTERMEDIATE 'normalized' VIRTUALIZED" \
  "$test_build/phase45_cross_let_result_optimized.rs" >/dev/null ||
  fail 'function-result consumer did not virtualize its immutable producer'
grep -Eq '^[[:space:]]*let mut normalized = \{' \
  "$test_build/phase45_later_use_materializes_optimized.rs" ||
  fail 'later independent use did not force collection materialization'
if grep -F "FUNCTIONAL INTERMEDIATE 'normalized' VIRTUALIZED" \
    "$test_build/phase45_cross_let_effect_barrier_optimized.rs" >/dev/null; then
  fail 'an intervening observable effect was crossed by binding fusion'
fi
grep -F "FUNCTIONAL INTERMEDIATE 'normalized' VIRTUALIZED" \
  "$test_build/phase45_single_assignment_var_virtualizes_optimized.rs" >/dev/null ||
  fail 'single-assignment var functional binding was not virtualized'
if grep -Eq '^[[:space:]]*let mut normalized = \{' \
    "$test_build/phase45_single_assignment_var_virtualizes_optimized.rs"; then
  fail 'virtualized var producer was still materialized'
fi
for case in phase45_var_mutation_materializes phase45_var_reassignment_materializes; do
  if grep -F "FUNCTIONAL INTERMEDIATE 'normalized' VIRTUALIZED" \
      "$test_build/${case}_optimized.rs" >/dev/null; then
    fail "$case unexpectedly virtualized a mutated or reassigned binding"
  fi
  grep -Eq '^[[:space:]]*let mut normalized = \{' \
      "$test_build/${case}_optimized.rs" ||
    fail "$case did not materialize its producer collection"
done
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
grep -F 'short-circuit any disabled; reason: callback may panic' \
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
grep -F 'fusion stopped: possible panic ordering' \
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
"$compiler" -O --explain-fusion --check tests/phase4_message_result_barrier.moss \
  >"$test_build/phase4_message_result.explain"
grep -F 'fusion stopped: message send' \
  "$test_build/phase4_message_result.explain" >/dev/null ||
  fail 'fusion explanation omitted the synchronous message barrier'
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
