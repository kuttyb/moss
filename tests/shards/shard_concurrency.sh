#!/bin/sh
set -eu

compiler=${1:-./moss}
test_build=${2:-build/tests/concurrency}
mkdir -p "$test_build"

. "$(dirname "$0")/../lib_test.sh"

run_case message_main tests/message_main.moss 'main: true'
run_case main_helper_message tests/main_helper_message.moss '9'
run_case branch_same_domain_type tests/branch_same_domain_type.moss '7'
run_case phase106b_topology tests/phase106b_topology.moss '7'
python3 tests/tooling/check_domain_handle_closure.py "$compiler"
run_case phase106c_account tests/phase106c_account.moss "$(printf '7\n1')"
python3 tests/tooling/check_synchronization_plan.py "$compiler"
${CXX:-c++} -std=c++17 -Wall -Wextra -pedantic -Werror -Isrc \
  tests/tooling/check_synchronization_plan.cpp -o "$test_build/check_synchronization_plan"
${CXX:-c++} -std=c++17 -O2 -Wall -Wextra -pedantic -Wno-missing-field-initializers -Werror -Isrc \
  tests/tooling/check_synchronization_lowering.cpp -o "$test_build/check_synchronization_lowering"
"$test_build/check_synchronization_lowering"
"$test_build/check_synchronization_plan"
PYTHONDONTWRITEBYTECODE=1 python3 tests/tooling/check_phase153_path_placement.py "$compiler"

"$compiler" inspect main --source tests/phase106b_topology.moss --json > "$test_build/phase106b_topology.inspect.json"
python3 - "$test_build/phase106b_topology.inspect.json" <<'PY'
import json
import sys

with open(sys.argv[1], encoding="utf-8") as stream:
    graph = json.load(stream)["result"]["concrete_domain_graph"]
instances = {item["identity"]: item for item in graph["instances"]}
if set(instances) != {"main::ledger", "main::inventory", "main::app"}:
    raise SystemExit("phase106b topology introspection lost concrete instances")
if any(item["domain_rank"] is None for item in instances.values()):
    raise SystemExit("phase106b topology introspection omitted domain rank")
if len(graph["edges"]) != 3:
    raise SystemExit("phase106b topology introspection lost route edges")
PY
run_optimized_case branch_same_domain_type_optimized \
  tests/branch_same_domain_type.moss '7'
run_case sequential_messages tests/sequential_messages.moss 'sequential: true'
run_case ignored_reply tests/ignored_reply.moss 'ignored reply completed'
run_case primitive_assignment tests/primitive_assignment.moss 'primitive: 1 1'
run_case domain_ref_message tests/domain_ref_message.moss 'ping
ping'
run_case fresh_message_payload tests/fresh_message_payload.moss 'fresh: 7'
run_case fresh_reply_payload tests/fresh_reply_payload.moss 'fresh reply: 9'
compile_case synchronous_call_order tests/shared_memory_contention.moss
iteration=1
while [ "$iteration" -le 20 ]; do
  actual=$("$test_build/synchronous_call_order")
  [ "$actual" = 'shared total: 2000 true true' ] ||
    fail "synchronous execution changed source ordering on iteration $iteration"
  iteration=$((iteration + 1))
done
run_shared_memory_case shared_memory_checkout examples/checkout.moss 'charged: 75
order completed
order rejected: insufficient inventory'
assert_class_lowering "$test_build/shared_memory_checkout.rs"
if grep -F 'tx: MossSender<CheckoutMsg>' "$test_build/shared_memory_checkout.rs" >/dev/null; then
  fail "synchronous checkout retained removed asynchronous transport"
fi
run_shared_memory_case shared_memory_example_optimized examples/shared_memory.moss \
  'shared total: 10 42'
assert_class_lowering "$test_build/shared_memory_example_optimized.rs"
if grep -F 'tx: MossSender<ClientMsg>' "$test_build/shared_memory_example_optimized.rs" >/dev/null; then
  fail "shared-memory example retained removed asynchronous Client transport"
fi
shared_memory_call_line=$(awk '/^    first = message counter.Add\(10\)/ { print NR; exit }' examples/shared_memory.moss)
grep -F "// Moss line $shared_memory_call_line: first = message counter.Add(10)" \
  "$test_build/shared_memory_example_optimized.rs" >/dev/null ||
  fail "shared-memory optimization omitted its Moss message annotation"
assert_class_lowering "$test_build/shared_memory_example_optimized.rs"
run_optimized_case shared_memory_object_pipeline examples/object_pipeline.moss \
  'created: widget 1 false
observed: widget 1 false
revised: widget 2 true
verified: widget 2 true
archive: widget 2 true'
grep -F 'let result = __moss_body_' "$test_build/shared_memory_object_pipeline.rs" >/dev/null ||
  fail "object pipeline did not use synchronous shared-memory transport"
run_optimized_case shared_memory_ignored_reply tests/ignored_reply.moss \
  'ignored reply completed'
if grep -F 'tx: MossSender<WorkerMsg>' "$test_build/shared_memory_ignored_reply.rs" >/dev/null; then
  fail "ignored reply call retained removed asynchronous transport"
fi
run_optimized_case shared_memory_domain_ref tests/domain_ref_message.moss 'ping
ping'
run_shared_memory_case shared_memory_contention tests/shared_memory_contention.moss \
  'shared total: 2000 true true'
assert_class_lowering "$test_build/shared_memory_contention.rs"
if grep -F 'tx: MossSender<ProducerMsg>' "$test_build/shared_memory_contention.rs" >/dev/null; then
  fail "contention case retained removed asynchronous Producer transport"
fi
iteration=1
while [ "$iteration" -le 20 ]; do
  actual=$("$test_build/shared_memory_contention")
  [ "$actual" = 'shared total: 2000 true true' ] ||
    fail "shared-memory contention lost or reordered work on iteration $iteration"
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
