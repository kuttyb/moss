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
run_case frontend_syntax examples/frontend_syntax.moss 'note: 4'
run_case concrete_method tests/concrete_method.moss '12.56636'
run_case concrete_method_body tests/concrete_method_body.moss "$(printf '12.566368\n0')"
run_case duck_typed_methods tests/duck_typed_methods.moss "$(printf '6\n9')"
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
  'first second third'
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
  tests/phase25_atomic_copy_boundary.moss 'original original'
grep -F '// Moss backend plan: Gate = DirectAtomic.' \
  "$test_build/phase25_atomic_copy_boundary.rs" >/dev/null ||
  fail "copy-boundary fixture did not select atomic lowering"
[ "$(grep -c '(token).clone()' "$test_build/phase25_atomic_copy_boundary.rs")" -ge 2 ] ||
  fail "atomic request/reply path did not retain both semantic copy boundaries"

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
run_case read_alias tests/read_alias.moss "$(printf '7 7\n7')"
run_case primitive_field_projection tests/primitive_field_projection.moss \
  "$(printf '42\n7')"
run_case method_receiver_effects tests/method_receiver_effects.moss \
  "$(printf '42\n7')"
run_case generic_method_effects tests/generic_method_effects.moss '12 5'
run_case phase2_safety examples/phase2_safety.moss \
  "$(printf 'read aliases: 7 7\nprimitive projection: 7\nafter await copy: 7 7\ntransferred payload: original')"
grep -F 'fn read_code(&self)' "$test_build/method_receiver_effects.rs" >/dev/null ||
  fail "copyable field read did not retain a READ receiver"
grep -F 'fn take_payload(self)' "$test_build/method_receiver_effects.rs" >/dev/null ||
  fail "nontrivial field movement did not lower a CONSUME receiver"
warning_stdout="$test_build/large_payload_warning.stdout"
warning_stderr="$test_build/large_payload_warning.stderr"
"$compiler" --check tests/large_payload_warning.moss >"$warning_stdout" 2>"$warning_stderr"
grep -F 'warning: message payload copies 1088 bytes across a domain boundary' \
  "$warning_stderr" >/dev/null || fail "large payload did not report its copy cost"

reject_case direct_await_cycle "await dependency cycle: Left -> Right -> Left"
reject_case transitive_await_cycle "await dependency cycle: First -> Second -> Third -> First"
reject_case function_await_cycle "await dependency cycle: Left -> Right -> Left"
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
  "await dependency cycle: Coordinator -> Worker -> Coordinator"
reject_source example_recursive_call examples/errors/recursive_call.moss \
  "recursive local call cycle: countdown -> countdown"
reject_source example_conflicting_access examples/errors/conflicting_access.moss \
  "conflicting accesses to value 'counter' in call to 'increment_from': mutation overlaps with read"

reject_cluster cluster_duplicate_spawn tests/shared_memory_contention.moss 'Counter,Producer' \
  "clustered domain type 'Producer' must be spawned exactly once in main (found 2)"
reject_cluster cluster_unknown_domain examples/counter.moss 'Counter,Missing' \
  'unknown domain in cluster: Missing'
reject_cluster cluster_await_cycle tests/cluster_await_cycle.moss 'Left,Right' \
  'await dependency cycle: Left -> Right -> Left'

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
reject_case await_unknown_receiver "unknown message receiver 'missing'"
reject_case await_unknown_handler "domain Worker has no message handler 'Missing'"
reject_case await_wrong_arity 'message Worker.Work expects 1 arguments, got 0'
reject_case self_await 'a domain cannot await itself because handlers are non-reentrant'
reject_case duck_missing_method "missing required method 'describe'"
reject_case duck_wrong_method_arity "wrong arity for required method 'describe'"
reject_case duck_incompatible_method_argument "method 'draw' is incompatible with argument types (string)"
reject_case trait_missing_method "missing required trait method 'area'"
reject_case trait_incompatible_method_signature "trait method 'draw' has an incompatible parameter signature"
reject_case conflicting_method_results "conflicting result expectations for required method 'current'"
reject_case unresolved_collection_type "heterogeneous or unresolved collection element type"
reject_case heterogeneous_collection "heterogeneous or unresolved collection element type"

echo 'all Moss v0.2 tests passed'
