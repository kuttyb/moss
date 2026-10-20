#!/bin/sh
set -eu

compiler=${1:-./moss}
test_build=${2:-build/tests/phase25_int_payloads}
mkdir -p "$test_build"

. "$(dirname "$0")/../lib_test.sh"

# Moss integers wrap at their concrete i64 width. Compile both the unoptimized
# reference and optimized lowering with Rust overflow checks explicitly enabled so
# neither Rust profile nor the backend choice can affect observable results.
overflow_expected=$(printf '%s\n%s\n%s\n%s\n%s\n%s\n%s\n%s\n%s' \
  '-9223372036854775808' '9223372036854775807' \
  '-9223372036854775808' '9223372036854775807' \
  '-9223372036854775808' '-9223372036854775808' \
  '-9223372036854775808' '3.0' \
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
  fail "optimized integer overflow did not follow Moss wrapping semantics"
[ "$overflow_o0_output" = "$overflow_optimized_output" ] ||
  fail "integer overflow differed between -O0 and optimized execution"
[ "$overflow_o0_output" = "$overflow_o0_no_checks_output" ] ||
  fail "Rust overflow checks changed -O0 Moss integer results"
[ "$overflow_optimized_output" = "$overflow_optimized_no_checks_output" ] ||
  fail "Rust overflow checks changed optimized Moss integer results"
assert_class_lowering "$test_build/phase25_integer_overflow.rs"
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
assert_class_lowering "$test_build/phase25_integer_overflow.rs"
if lowered_rust "$test_build/phase25_integer_overflow.rs" | grep -F 'unsafe {' >/dev/null; then
  fail "integer wrapping lowering used unsafe code"
fi

# Scalar and aggregate state semantics are covered by the 10.6D/E suites.
# Alternate atomic/coalesced/batched backend selection tests are retired.

reject_source use_after_transfer examples/use_after_transfer.moss \
  "value 'original' was transferred to 'destination' at line 10"
reject_case naked_cross_domain_call "domain handler 'Worker.Ping' must be called with message"
# An untyped field is a nominal template slot (TYPE_IR_CONTRACT §2): an
# unconstructed template is legal, but a reachable value must close it.
run_case unused_inferred_field_template tests/unused_inferred_field_template.moss '1'
reject_case unresolved_field_value "unbound open result type for variable 'value'"
# A parameterized built-in collection names every type argument (Vector and
# Queue one, Map two); partial or excess arity is malformed, not open.
reject_case partial_arity_map_field "[MALFORMED_TYPE_EXPRESSION] malformed type expression 'map[string]'"
reject_case partial_arity_vector_parameter "[MALFORMED_TYPE_EXPRESSION] malformed type expression 'vector[int,int]'"
# An enum Map key is detected from the key's term kind, not its spelling.
reject_case enum_index_map_key "[ENUM_MAP_KEY_UNSUPPORTED] enum values cannot be Map keys"
reject_case unresolved_state "cannot infer type for state field 'Worker.value'"
reject_case state_annotation_mismatch "state field 'Counter.value' is annotated 'int' but its initializer has type 'float'"
reject_case conflicting_reply_types "conflicting reply types in handler 'Worker.Maybe'"
reject_case main_return_value "main cannot return a value"
run_case message_payload_copy tests/message_payload_snapshot.moss "$(printf 'detached\ndetached')"
run_case state_payload_copy tests/state_payload_snapshot.moss '34'
run_case message_payload_copy tests/message_object_copy.moss '7'
run_case reply_payload_copy tests/reply_payload_snapshot.moss '7'
run_case nested_payload_copy tests/nested_payload_snapshot.moss '7'
run_case string_payload_copy tests/string_payload_snapshot.moss 'fresh'

run_case phase47_iteration tests/phase47_iteration.moss '12 1 20 6 9 3 3'
run_case phase47_element_effects tests/phase47_element_effects.moss 'element effects'
phase47_element_effects_json="$test_build/phase47_element_effects.json"
"$compiler" effects fn:updateAll \
  --source tests/phase47_element_effects.moss --json \
  >"$phase47_element_effects_json"
grep -F '"name": "payload", "type": "Payload", "effect": "WRITE"' \
  "$phase47_element_effects_json" >/dev/null ||
  fail 'for-loop effect analysis did not resolve the iterator element method effect'
# SWARM-066: for/range native lowering preserves induction binding and loop body.
# Tests: induction variable readable, nonzero start, computed end, pipeline end,
# accumulator mutation, and nested for/range inside an ordinary function.
run_case swarm_066_for_range_lowering \
  tests/swarm_066_for_range_lowering.moss \
  "$(printf '10\n18\n60\n7\n15\n18\n16')"

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
run_case specialized_domain_routes \
  tests/specialized_domain_routes.moss ''
grep -F 'left: WorkerHandle' \
  "$test_build/specialized_domain_routes.rs" >/dev/null ||
  fail 'static route lost the specialized Worker backend handle'
if grep -F 'fn Ping_shared(&self, worker: Worker__' \
    "$test_build/specialized_domain_routes.rs" >/dev/null; then
  fail 'typed domain parameter specialization leaked a declared instance identity'
fi
run_case domain_name_collision_routes \
  tests/domain_name_collision_routes.moss "$(printf '1\n2')"
grep -F 'fn take(value: i64)' \
  "$test_build/domain_name_collision_routes.rs" >/dev/null ||
  fail 'ordinary helper must receive the message result, not a handle'
grep -E 'fn Send\(&self, (mut )?value: i64\)' \
  "$test_build/domain_name_collision_routes.rs" >/dev/null ||
  fail 'ordinary method must receive the message result, not a handle'
grep -F 'worker: AHandle' \
  "$test_build/domain_name_collision_routes.rs" >/dev/null ||
  fail 'static route lost the specialized A backend handle'
grep -F 'struct A__helper__specialized_' \
  "$test_build/domain_name_collision_routes.rs" >/dev/null ||
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
grep -F 'struct Box__intBoxRuntime' "$test_build/implicit_domain_specialization.rs" >/dev/null ||
  fail 'implicit Box Int specialization did not get a concrete Rust layout'
grep -F 'value: i64' "$test_build/implicit_domain_specialization.rs" >/dev/null ||
  fail 'implicit Box Int layout was not statically concrete'
grep -F 'struct Box__floatBoxRuntime' "$test_build/implicit_domain_specialization.rs" >/dev/null ||
  fail 'implicit Box Float specialization did not get a concrete Rust layout'
grep -F 'value: f64' "$test_build/implicit_domain_specialization.rs" >/dev/null ||
  fail 'implicit Box Float layout was not statically concrete'
if lowered_rust "$test_build/implicit_domain_specialization.rs" | grep -Eq 'Any|Box<dyn|type_id|TypeId'; then
  fail 'implicit domain specialization introduced a dynamic Rust value representation'
fi
"$compiler" effects 'handler:Box.Set' \
  --source tests/implicit_domain_specialization.moss --json \
  >"$test_build/implicit_domain_specialization_effects.json"
grep -F '"domain_write": true' \
  "$test_build/implicit_domain_specialization_effects.json" >/dev/null ||
  fail 'implicit Box Set did not retain its WRITE effect summary'

# Exact per-instance state and handler specialization.
implicit_domain_specialization_message_json="$test_build/implicit_domain_specialization_message.json"
run_case implicit_domain_specialization_message \
  tests/implicit_domain_specialization_message.moss ''
"$compiler" inspect 'domain-specialization:Worker:intWorker' \
  --source tests/implicit_domain_specialization_message.moss --json \
  >"$test_build/implicit_domain_specialization_message_int.json"
grep -F '"name": "payload", "type": "int"' \
  "$test_build/implicit_domain_specialization_message_int.json" >/dev/null ||
  fail 'implicit intWorker specialization did not infer payload: int'
"$compiler" inspect 'domain-specialization:Worker:floatWorker' \
  --source tests/implicit_domain_specialization_message.moss --json \
  >"$test_build/implicit_domain_specialization_message_float.json"
grep -F '"name": "payload", "type": "float"' \
  "$test_build/implicit_domain_specialization_message_float.json" >/dev/null ||
  fail 'implicit floatWorker specialization did not infer payload: float'
grep -F 'struct Worker__intWorkerRuntime' \
  "$test_build/implicit_domain_specialization_message.rs" >/dev/null ||
  fail 'implicit intWorker specialization did not get a concrete Rust layout'
grep -F 'struct Worker__floatWorkerRuntime' \
  "$test_build/implicit_domain_specialization_message.rs" >/dev/null ||
  fail 'implicit floatWorker specialization did not get a concrete Rust layout'

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
  "$(printf 'read aliases: 7 7\nprimitive projection: 7\nafter message copy: 7 7\ntransferred payload: original')"

# Synchronous message expressions and terminating replies.
run_case phase106_sync_smoke tests/phase106_sync_smoke.moss 'set
next
7'
run_case phase106_reply_control tests/phase106_reply_control.moss 'after'
run_case message_unit_result tests/message_unit_result.moss 'notified'
run_case payload_reply_example examples/message_payload_reply.moss '7 7 9'
reject_case message_unknown_receiver \
  "message target 'missing' is not a static domain instance or declared route"
reject_case message_unknown_handler "domain Worker has no message handler 'Missing'"
reject_case message_wrong_arity "message Worker.Work expects 1 arguments, got 0"
reject_case message_argument_type "argument 1 to message Worker.Work has type 'string', expected 'int'"
reject_source example_domain_route_cycle examples/errors/domain_route_cycle.moss \
  "concrete domain route cycle detected"

# Retired syntax diagnostics: these are the only source migration fixtures.
reject_case await_retired "await is retired: message is synchronous"
reject_case spawn_retired "'spawn' is retired"


# Incoming payloads remain immutable semantic values. Phase 15.1 may represent
# an internal synchronous boundary as a stable borrow; replies and exported
# bridges remain independently owned boundaries.
run_optimized_case phase26_direct_payload tests/phase26_direct_payload.moss '7 8'
grep -F "fn __moss_body_Worker_Process(state: &mut WorkerProcessState<'_>, payload: &impl MossAccess_Payload)" \
  "$test_build/phase26_direct_payload.rs" >/dev/null ||
  fail 'direct handler did not receive the internal borrowed payload ABI'
grep -F 'let __moss_root_arg_1 = (data).clone(); runtime_invoke(move || __moss_root_domain_0.Process_shared(&__moss_root_arg_1)' \
  "$test_build/phase26_direct_payload.rs" >/dev/null ||
  fail 'main Root ingress did not snapshot and borrow its non-Copy payload inside runtime_invoke'
run_optimized_case phase26_repeated_payload tests/phase26_repeated_payload.moss '7 8'
run_case phase26_message_payload tests/phase26_message_payload.moss '9'
grep -F 'runtime_invoke(move || __moss_root_domain_0.Process_shared(&__moss_root_arg_1))' \
  "$test_build/phase26_message_payload.rs" >/dev/null ||
  fail 'one-way main message did not use runtime_invoke with an owned payload snapshot'
run_case phase26_payload_forward tests/phase26_payload_forward.moss '11'
run_optimized_case phase26_copy_payloads tests/phase26_copy_payloads.moss \
  "forwarded: 10
11 8"

reject_case phase26_payload_write \
  "cannot WRITE incoming message payload 'payload'"
reject_case phase26_payload_transitive_write \
  "cannot WRITE incoming message payload 'payload'"
reject_case phase26_payload_consume \
  "cannot CONSUME incoming message payload 'payload'"
reject_case phase26_payload_state_store \
  "cannot CONSUME incoming message payload 'payload'"
run_case phase26_payload_reply tests/payload_reply_record.moss '1'
reject_case phase26_payload_alias_reply \
  "cannot CONSUME incoming message payload 'payload'"
run_case phase26_payload_reply_copy tests/payload_reply_primitive.moss '1'
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
grep -F 'warning: message payload materializes 1088 bytes at an owned domain boundary' \
  "$warning_stderr" >/dev/null || fail "large payload did not report its copy cost"

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
