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
  grep -F 'state: Arc<Mutex<' "$test_build/$name.rs" >/dev/null ||
    fail "$name did not emit lock-protected domain state"
  grep -F 'fn __moss_require_send<T: Send>()' "$test_build/$name.rs" >/dev/null ||
    fail "$name did not emit the Rust Send boundary check"
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
run_case shared_memory_example examples/shared_memory.moss 'shared total: 10 42'
grep -F '// Moss line 21: client.Run(counter)' "$test_build/shared_memory_example.rs" >/dev/null ||
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
grep -F 'state: Arc<Mutex<CounterState>>' "$test_build/shared_memory_example_optimized.rs" >/dev/null ||
  fail "shared-memory example did not lower Counter state to a lock"
grep -F 'tx: MossSender<ClientMsg>' "$test_build/shared_memory_example_optimized.rs" >/dev/null ||
  fail "shared-memory example did not retain the asynchronous Client mailbox"
grep -F '// Moss line 14: let first = await counter.Add(10)' \
  "$test_build/shared_memory_example_optimized.rs" >/dev/null ||
  fail "shared-memory optimization omitted its Moss await annotation"
grep -F '// Moss backend: SHARED-MEMORY DIRECT version: lock the target state and invoke the handler without a request message' \
  "$test_build/shared_memory_example_optimized.rs" >/dev/null ||
  fail "shared-memory optimization omitted its direct lowering annotation"
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
grep -F 'state: Arc<Mutex<CounterState>>' "$test_build/shared_memory_contention.rs" >/dev/null ||
  fail "contention case did not promote Counter state"
grep -F 'tx: MossSender<ProducerMsg>' "$test_build/shared_memory_contention.rs" >/dev/null ||
  fail "contention case incorrectly promoted asynchronous Producer messages"
iteration=1
while [ "$iteration" -le 20 ]; do
  actual=$("$test_build/shared_memory_contention")
  [ "$actual" = 'shared total: 2000 true true' ] ||
    fail "shared-memory contention lost or reordered work on iteration $iteration"
  iteration=$((iteration + 1))
done

reject_source use_after_transfer examples/use_after_transfer.moss \
  "value 'original' was transferred to 'destination' at line 10"
reject_case message_transfer "owned non-primitive value 'payload' cannot cross a domain boundary"
reject_case domain_state_transfer "domain state 'dataset' cannot be transferred by message"
reject_case await_object_transfer "owned non-primitive value 'payload' cannot cross a domain boundary"
reject_case object_reply_transfer "owned non-primitive value 'payload' cannot cross a domain boundary in a reply"
reject_case nested_object_transfer "owned non-primitive value 'envelope' cannot cross a domain boundary"
reject_case string_transfer "owned non-primitive value 'text' cannot cross a domain boundary"

reject_cluster cluster_duplicate_spawn tests/shared_memory_contention.moss 'Counter,Producer' \
  "clustered domain type 'Producer' must be spawned exactly once in main (found 2)"
reject_cluster cluster_unknown_domain examples/counter.moss 'Counter,Missing' \
  'unknown domain in cluster: Missing'
reject_cluster cluster_await_cycle tests/cluster_await_cycle.moss 'Left,Right' \
  "clustered await cycle involving 'Left' cannot use direct same-thread dispatch"

compile_cluster_case clustered_checkout examples/checkout.moss 'Checkout,Inventory,Payments'
clustered_checkout_output=$("$test_build/clustered_checkout")
[ "$clustered_checkout_output" = 'charged: 75
order completed
order rejected: insufficient inventory' ] || fail "clustered checkout output differed"
grep -F 'Domain cluster 0: static same-thread dispatch for Checkout, Inventory, Payments.' \
  "$test_build/clustered_checkout.rs" >/dev/null || fail "cluster plan was not recorded"
grep -F 'self.Inventory_Reserve_local(quantity)' "$test_build/clustered_checkout.rs" >/dev/null ||
  fail "clustered await did not select the local call version"
grep -F 'self.Payments_Charge_local(quantity * price)' "$test_build/clustered_checkout.rs" >/dev/null ||
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
reject_case reply_one_way "reply is only valid in a handler declaring '-> Type'"
reject_case reply_main "reply is only valid in a handler declaring '-> Type'"
reject_case await_unknown_receiver "unknown message receiver 'missing'"
reject_case await_unknown_handler "domain Worker has no message handler 'Missing'"
reject_case await_wrong_arity 'message Worker.Work expects 1 arguments, got 0'
reject_case self_await 'a domain cannot await itself because handlers are non-reentrant'

echo 'all Moss v0.2 tests passed'
