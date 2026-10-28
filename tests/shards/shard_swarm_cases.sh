#!/bin/sh
set -eu

compiler=${1:-./moss}
test_build=${2:-build/tests/swarm_cases}
mkdir -p "$test_build"

. "$(dirname "$0")/../lib_test.sh"

run_case swarm_001_parenthesized_pipeline tests/swarm_001_parenthesized_pipeline.moss '4 2'
run_case swarm_002_typed_pop tests/swarm_002_typed_pop.moss '7'
grep -F 'holder.pop()' "$test_build/swarm_002_typed_pop.rs" >/dev/null ||
  fail 'swarm_002 user pop did not retain its method spelling'
grep -F 'values.pop()' "$test_build/swarm_002_typed_pop.rs" >/dev/null ||
  fail 'swarm_002 Vector pop did not retain Vec lowering'
grep -F 'queue.pop_front()' "$test_build/swarm_002_typed_pop.rs" >/dev/null ||
  fail 'swarm_002 Queue pop did not use VecDeque lowering'
run_case swarm_004_indexed_binary tests/swarm_004_indexed_binary.moss "$(printf 'true\n5\ntrue')"
run_case swarm_005_implicit_method_return tests/swarm_005_implicit_method_return.moss "$(printf '5\n9')"
run_case swarm_006_empty_map_context tests/swarm_006_empty_map_context.moss '1'
run_case swarm_007_map_string_key tests/swarm_007_map_string_key.moss '7'
run_case swarm_008_map_default_lookup tests/swarm_008_map_default_lookup.moss "$(printf '3\n0\nred\nunknown')"
run_case swarm_010_map_views tests/swarm_010_map_views.moss '2 16 16'
reject_case swarm_008_map_get_wrong_key 'map get key type mismatch'
reject_case swarm_008_map_get_wrong_default 'map get default type mismatch'
run_case swarm_011_sibling_field_borrow tests/swarm_011_sibling_field_borrow.moss '1 1 8'
grep -F 'let __moss_call_argument_' "$test_build/swarm_011_sibling_field_borrow.rs" >/dev/null ||
  fail 'swarm_011 did not pre-evaluate sibling READ before WRITE borrow'
grep -F 'let __moss_index_' "$test_build/swarm_011_sibling_field_borrow.rs" >/dev/null ||
  fail 'swarm_011 did not pre-evaluate sibling index before WRITE borrow'
reject_case swarm_011_overlapping_access \
  "'values' is passed to 'overwrite_from' through overlapping WRITE and READ accesses"
run_case swarm_012_not_bool tests/swarm_012_not_bool.moss '1'
reject_case swarm_012_not_int "not operand must have type 'Bool'"
reject_case swarm_012_not_float "not operand must have type 'Bool'"
reject_case swarm_012_not_string "not operand must have type 'Bool'"
run_case swarm_013_immutable_let_positive tests/swarm_013_immutable_let_positive.moss '1'
run_case swarm_014_integer_remainder tests/swarm_014_integer_remainder.moss '1'
run_case swarm_015_backend_symbol_hygiene tests/swarm_015_backend_symbol_hygiene.moss '1 2 3'
run_case swarm_017_typed_empty_vector tests/swarm_017_typed_empty_vector.moss '4'
run_case swarm_018_queue_context tests/swarm_018_queue_context.moss '4'
run_case swarm_019_collection_pop tests/swarm_019_collection_pop.moss '3 7'
run_case swarm_020_typed_vector_factory tests/swarm_020_typed_vector_factory.moss '0'
run_case swarm_021_field_collections tests/swarm_021_field_collections.moss '16'
run_case swarm_021_map_value_semantics tests/swarm_021_map_value_semantics.moss "$(printf '99\n7\n1\n1\n7')"
run_case swarm_021_map_get_fallback_owned tests/swarm_021_map_get_fallback_owned.moss '1'
run_case swarm_022_fast_debug_map_state tests/swarm_022_fast_debug_map_state.moss '0'
run_case swarm_023_fast_debug_pipelines tests/swarm_023_fast_debug_pipelines.moss "$(printf '12\n3\n6\ntrue\ntrue')"
run_case swarm_023_eager_terminals tests/swarm_023_eager_terminals.moss "$(printf '1\n2\n3\ntrue\n1\n2\n3\nfalse')"
run_case swarm_023_empty_map_types tests/swarm_023_empty_map_types.moss '0.0 0 0.0'
run_case swarm_023_pipeline_context_identity tests/swarm_023_pipeline_context_identity.moss "$(printf '0.0\n0.0\n0.0\n0.0\n0.0\n0.0 0')"
run_case swarm_023_pipeline_test_context tests/swarm_023_pipeline_test_context.moss 'test context'
run_case swarm_025_fast_debug_sibling_method tests/swarm_025_fast_debug_sibling_method.moss '25'
reject_case swarm_026_in_expression "binary 'in' expression is not supported"
run_case swarm_036_bitwise_and tests/swarm_036_bitwise_and.moss '2'
run_case swarm_036_bitwise_or tests/swarm_036_bitwise_or.moss '7'
run_case swarm_036_bitwise_xor tests/swarm_036_bitwise_xor.moss '5'
run_case swarm_036_parenthesized_rhs tests/swarm_036_parenthesized_rhs.moss '2'
run_case swarm_036_shift_left tests/swarm_036_shift_left.moss '6'
run_case swarm_036_shift_right tests/swarm_036_shift_right.moss '3'
reject_case swarm_036_symbolic_and "binary '&&' expression is not supported"
reject_case swarm_036_symbolic_or "binary '||' expression is not supported"
reject_case swarm_036_bitwise_and_type "bitwise operator '&' requires Int operands"
reject_case swarm_036_bitwise_or_type "bitwise operator '|' requires Int operands"
reject_case swarm_036_shift_type "shift operator '<<' requires Int operands"
reject_case swarm_036_bitwise_not_type "bitwise operator '~' requires an Int operand"
run_case swarm_036_word_boolean_operators tests/swarm_036_word_boolean_operators.moss '1'
run_case swarm_037_boolean_operators tests/swarm_037_boolean_operators.moss "$(printf 'true\nfalse\ntrue\ntrue\nfalse\ntrue\ntrue\ntrue\nfalse\nfalse\ntrue')"
reject_case swarm_049_string_lt "String ordering operators"
reject_case swarm_049_string_le "String ordering operators"
reject_case swarm_049_string_gt "String ordering operators"
reject_case swarm_049_string_ge "String ordering operators"
run_case swarm_049_string_builtin_operators tests/swarm_049_string_builtin_operators.moss "$(printf 'true\ntrue\nab')"
reject_case swarm_037_and_non_bool "boolean 'and' operands must have type 'Bool'"
reject_case swarm_037_or_non_bool "boolean 'or' operands must have type 'Bool'"
reject_case swarm_037_xor_non_bool "boolean 'xor' operands must have type 'Bool'"
reject_case swarm_052_vector_trait "trait types are static structural constraints"
reject_case swarm_052_nested_vector_trait "trait types are static structural constraints"
run_case swarm_052_concrete_vector_trait_parameter tests/swarm_052_concrete_vector_trait_parameter.moss "$(printf '0\n9')"
run_case swarm_027_indexed_collection_method tests/swarm_027_indexed_collection_method.moss '10'
run_case swarm_028_self_method_argument_borrow tests/swarm_028_self_method_argument_borrow.moss '100'
reject_case swarm_029_nested_indexed_mutation 'nested indexed mutation is not supported'
run_case swarm_030_string_comparison_borrowed tests/swarm_030_string_comparison_borrowed.moss '1'
run_case swarm_031_parenthesized_lowering tests/swarm_031_parenthesized_lowering.moss "$(printf '48\n4\n3')"
run_case swarm_034_tail_push_unit tests/swarm_034_tail_push_unit.moss "$(printf '1\n1')"
run_case swarm_035_nested_mutating_calls tests/swarm_035_nested_mutating_calls.moss '1'
run_case swarm_048_callable_argument tests/swarm_048_callable_argument.moss "$(printf '1\n3\ntrue')"
run_case swarm_047_reduce_map tests/swarm_047_reduce_map.moss "$(printf '2\n1\n0')"
run_case swarm_047_reduce_map_control_flow tests/swarm_047_reduce_map_control_flow.moss "$(printf '4\n4')"
run_case swarm_050_nested_specialization tests/swarm_050_nested_specialization.moss "$(printf '4\n9')"
python3 tests/tooling/check_swarm_050_targets.py \
  "$test_build/swarm_050_nested_specialization.rs" outer inner First,Second
run_case swarm_050_join_specialization tests/swarm_050_join_specialization.moss "$(printf '3\n8\n8\n3')"
python3 tests/tooling/check_swarm_050_targets.py \
  "$test_build/swarm_050_join_specialization.rs" outer inner First,Second
python3 tests/tooling/check_swarm_050_targets.py \
  "$test_build/swarm_050_join_specialization.rs" outer_reversed inner First,Second
run_case swarm_054_indexed_field tests/swarm_054_indexed_field.moss '9'
run_case indexed_field_assignment tests/indexed_field_assignment.moss "$(printf '9\n12\n15\n18 21 24\n27\n30\n33')"
run_case swarm_044_index_specialization tests/swarm_044_index_specialization.moss "$(printf 'old\n1\n2\nnew\n3\nb\n5\nc')"
run_case swarm_044_typed_string_map_index tests/swarm_044_typed_string_map_index.moss '7'
run_fast_debug_case swarm_044_typed_string_map_index tests/swarm_044_typed_string_map_index.moss '7'
reject_case swarm_044_nonindexable 'is not an indexable container'
reject_case swarm_044_bad_key 'vector and queue indices must be Int'
reject_case swarm_044_bad_write 'indexed assignment requires value of type'
run_case swarm_064_rust_keyword_field tests/swarm_064_rust_keyword_field.moss '3'
run_case swarm_065_pipeline_return tests/swarm_065_pipeline_return.moss "$(printf 'true\n2')"
run_case swarm_038_effects_loop_helper tests/swarm_038_effects_loop_helper.moss '4'
reject_case swarm_038_impure_state_init 'domain state initializers must be side-effect-free'
reject_case swarm_038_divergent_loop_state_init 'domain state initializers must be side-effect-free'
reject_case swarm_038_empty_pop_state_init 'domain state initializers must be side-effect-free'
run_case for_loop_mutable_update tests/for_loop_mutable_update.moss '60'
run_case for_shadow_restores_outer tests/for_shadow_restores_outer.moss "$(printf '1\n2\ntext\n7\n8\n9\n5')"
run_case for_shadow_open_type tests/for_shadow_open_type.moss "$(printf '1\n2\nouter')"
run_case branch_confined_open_type tests/branch_confined_open_type.moss "$(printf '1\nouter\nafter\n1\ndone')"
run_case trait_open_forwarding tests/trait_open_forwarding.moss '42'
reject_case for_loop_local_leaked "unknown identifier 'y'"
reject_case for_loop_local_leaked_empty "unknown identifier 'y'"
reject_case for_loop_induction_leaked "unknown identifier 'i'"
reject_case swarm_024_queue_constructor_arguments 'built-in Queue constructor takes no arguments'
reject_case swarm_024_map_constructor_arguments 'built-in Map constructor takes no arguments'
compile_case swarm_019_empty_pop tests/swarm_019_empty_pop.moss
if "$test_build/swarm_019_empty_pop" >/dev/null 2>&1; then
  fail 'native empty collection pop unexpectedly returned'
fi
if "$compiler" run --interp tests/swarm_019_empty_pop.moss >/dev/null 2>&1; then
  fail 'Fast Debug empty collection pop unexpectedly returned'
fi
compile_case swarm_019_empty_queue_pop tests/swarm_019_empty_queue_pop.moss
if "$test_build/swarm_019_empty_queue_pop" >/dev/null 2>&1; then
  fail 'native empty Queue pop unexpectedly returned'
fi
if "$compiler" run --interp tests/swarm_019_empty_queue_pop.moss >/dev/null 2>&1; then
  fail 'Fast Debug empty Queue pop unexpectedly returned'
fi
run_case field_replacement_receiver_available tests/field_replacement_receiver_available.moss '3'
reject_case swarm_014_remainder_float "integer remainder operands must have type 'Int'"
reject_case swarm_014_remainder_string "integer remainder operands must have type 'Int'"
reject_case swarm_017_typed_empty_vector_unknown 'invalid typed Vector constructor'
reject_case swarm_017_typed_empty_vector_arity 'invalid typed Vector constructor'
reject_case swarm_017_local_type_annotation 'local type annotations are not supported'
reject_case field_replacement_rhs_consumed 'was consumed'
