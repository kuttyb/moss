#!/bin/sh
set -eu

compiler=${1:-./moss}
test_build=${2:-build/tests/negative}
mkdir -p "$test_build"

. "$(dirname "$0")/../lib_test.sh"

reject_case swarm_043_untyped_field 'requires a concrete receiver type'
reject_case swarm_043_alias_field 'requires a concrete receiver type'
reject_case swarm_008_map_get_wrong_key 'map get key type mismatch'
reject_case swarm_008_map_get_wrong_default 'map get default type mismatch'
reject_case swarm_011_overlapping_access \
  "'values' is passed to 'overwrite_from' through overlapping WRITE and READ accesses"
reject_case swarm_012_not_int "not operand must have type 'Bool'"
reject_case swarm_012_not_float "not operand must have type 'Bool'"
reject_case swarm_012_not_string "not operand must have type 'Bool'"
reject_case swarm_026_in_expression "binary 'in' expression is not supported"
reject_case swarm_036_symbolic_and "binary '&&' expression is not supported"
reject_case swarm_036_symbolic_or "binary '||' expression is not supported"
reject_case swarm_036_bitwise_and_type "bitwise operator '&' requires Int operands"
reject_case swarm_036_bitwise_or_type "bitwise operator '|' requires Int operands"
reject_case swarm_036_shift_type "shift operator '<<' requires Int operands"
reject_case swarm_036_bitwise_not_type "bitwise operator '~' requires an Int operand"
reject_case swarm_049_string_lt "String ordering operators"
reject_case swarm_049_string_le "String ordering operators"
reject_case swarm_049_string_gt "String ordering operators"
reject_case swarm_049_string_ge "String ordering operators"
reject_case swarm_037_and_non_bool "boolean 'and' operands must have type 'Bool'"
reject_case swarm_037_or_non_bool "boolean 'or' operands must have type 'Bool'"
reject_case swarm_037_xor_non_bool "boolean 'xor' operands must have type 'Bool'"
reject_case swarm_052_vector_trait "trait types are static structural constraints"
reject_case swarm_052_nested_vector_trait "trait types are static structural constraints"
reject_case swarm_029_nested_indexed_mutation 'nested indexed mutation is not supported'
reject_case swarm_044_nonindexable 'is not an indexable container'
reject_case swarm_044_bad_key 'vector and queue indices must be Int'
reject_case swarm_044_bad_write 'indexed assignment requires value of type'
reject_case swarm_038_impure_state_init 'domain state initializers must be side-effect-free'
reject_case swarm_038_divergent_loop_state_init 'domain state initializers must be side-effect-free'
reject_case swarm_038_empty_pop_state_init 'domain state initializers must be side-effect-free'
reject_case for_loop_local_leaked "unknown identifier 'y'"
reject_case for_loop_local_leaked_empty "unknown identifier 'y'"
reject_case for_loop_induction_leaked "unknown identifier 'i'"
reject_case swarm_024_queue_constructor_arguments 'built-in Queue constructor takes no arguments'
reject_case swarm_024_map_constructor_arguments 'built-in Map constructor takes no arguments'
reject_case swarm_014_remainder_float "integer remainder operands must have type 'Int'"
reject_case swarm_014_remainder_string "integer remainder operands must have type 'Int'"
reject_case swarm_017_typed_empty_vector_unknown 'invalid typed Vector constructor'
reject_case swarm_017_typed_empty_vector_arity 'invalid typed Vector constructor'
reject_case swarm_017_local_type_annotation 'local type annotations are not supported'
reject_case field_replacement_rhs_consumed 'was consumed'
reject_source use_after_transfer examples/use_after_transfer.moss \
  "value 'original' was transferred to 'destination' at line 10"
reject_case naked_cross_domain_call "domain handler 'Worker.Ping' must be called with message"
reject_case unresolved_field_value "unbound open result type for variable 'value'"
reject_case partial_arity_map_field "[MALFORMED_TYPE_EXPRESSION] malformed type expression 'map[string]'"
reject_case partial_arity_vector_parameter "[MALFORMED_TYPE_EXPRESSION] malformed type expression 'vector[int,int]'"
reject_case enum_index_map_key "[ENUM_MAP_KEY_UNSUPPORTED] enum values cannot be Map keys"
reject_case unresolved_state "cannot infer type for state field 'Worker.value'"
reject_case state_annotation_mismatch "state field 'Counter.value' is annotated 'int' but its initializer has type 'float'"
reject_case conflicting_reply_types "conflicting reply types in handler 'Worker.Maybe'"
reject_case main_return_value "main cannot return a value"
reject_case phase47_mutation_during_for \
  "cannot structurally mutate collection 'values' during an active READ traversal"
reject_case phase47_missing_iterator \
  "type 'NotAnIterator' does not satisfy Iterator: missing next() or iter()"
reject_case phase47_zero_step "range step must be a positive non-zero Int literal"
reject_case message_unknown_receiver \
  "message target 'missing' is not a static domain instance or declared route"
reject_case message_unknown_handler "domain Worker has no message handler 'Missing'"
reject_case message_wrong_arity "message Worker.Work expects 1 arguments, got 0"
reject_case message_argument_type "argument 1 to message Worker.Work has type 'string', expected 'int'"
reject_source example_domain_route_cycle examples/errors/domain_route_cycle.moss \
  "concrete domain route cycle detected"
reject_case await_retired "await is retired: message is synchronous"
reject_case spawn_retired "'spawn' is retired"
reject_case phase26_payload_write \
  "cannot WRITE incoming message payload 'payload'"
reject_case phase26_payload_transitive_write \
  "cannot WRITE incoming message payload 'payload'"
reject_case phase26_payload_consume \
  "cannot CONSUME incoming message payload 'payload'"
reject_case phase26_payload_state_store \
  "cannot CONSUME incoming message payload 'payload'"
reject_case phase26_payload_alias_reply \
  "cannot CONSUME incoming message payload 'payload'"
reject_case phase26_payload_rebind \
  "cannot WRITE incoming message payload 'payload'"
reject_case phase26_payload_rebind_copy \
  "cannot WRITE incoming message payload 'x'"
reject_case branch_divergent_domain_types \
  "domain handles cannot be used as ordinary values or payloads"
reject_case recursive_function "recursive local call cycle: recurse -> recurse"
reject_case mutually_recursive_functions "recursive local call cycle: first -> second -> first"
reject_case write_read_alias "'item' is passed to 'conflict' through overlapping WRITE and READ accesses"
reject_case write_write_alias "'item' is passed to 'conflict' through overlapping WRITE and WRITE accesses"
reject_case consume_read_alias "'item' is passed to 'conflict' through overlapping CONSUME and READ accesses"
reject_case consume_write_alias "'item' is passed to 'conflict' through overlapping CONSUME and WRITE accesses"
reject_case double_consume_alias "'item' is passed to 'conflict' through overlapping CONSUME and CONSUME accesses"
reject_case nontrivial_field_move "value 'packet' was transferred"
reject_case branch_join_consume "value 'item' was transferred"
reject_source example_recursive_call examples/errors/recursive_call.moss \
  "recursive local call cycle: countdown -> countdown"
reject_source example_conflicting_access examples/errors/conflicting_access.moss \
  "'counter' is passed to 'increment_from' through overlapping WRITE and READ accesses"
reject_source fallthrough tests/fallthrough.moss \
  "must reply on every normal control-flow path"
reject_case phase106_missing_reply \
  "must reply on every normal control-flow path"
reject_case same_domain_handler_message \
  "handler 'D.B' cannot be entered on the same domain instance"
reject_case domain_route_cycle \
  "concrete domain route cycle detected"
reject_case phase106b_impure_state_initializer \
  "domain state initializers must be side-effect-free"
reject_case phase106b_route_reassign \
  "domain route 'worker' is immutable"
reject_case reply_main "reply is only valid in a handler declaring '-> Type'"
reject_case self_send_rejected "handler 'Worker.Work' cannot be messaged through self"
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
reject_case domain_handle_prefix_collision_message "domain handles cannot be passed as handler payloads"
reject_case domain_handle_prefix_collision_function "domain handles cannot be passed as ordinary function parameters"
reject_case domain_handle_prefix_collision_method "domain handles cannot be passed as ordinary method parameters"
reject_case phase106b1_handler_payload "domain handles cannot be passed as handler payloads"
reject_case phase106b1_historical_handle_calls "domain handles cannot be passed as handler payloads"
reject_case phase106b1_historical_handle_reply "domain handles cannot be returned through reply"
reject_case unresolved_collection_type "[ECHO_DISPLAY_UNSUPPORTED]"
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
  "callback 'captured.push(_)' for map writes captured binding 'captured'"
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
reject_case functional_callback_alias \
  "'item' is passed to 'conflict' through overlapping WRITE and READ accesses"
reject_case functional_placeholder_noncopy_identity \
  "functional map result aliases nontrivial source storage through '_'"
reject_case functional_placeholder_noncopy_field \
  "functional map result aliases nontrivial source storage through '_'"
reject_case functional_helper_mutable_capture \
  "functional placeholder requires WRITE access to captured binding 'captured'"

PYTHONDONTWRITEBYTECODE=1 python3 tests/tooling/check_phase25_baseline_regressions.py "$compiler"

echo "all negative cases passed"
