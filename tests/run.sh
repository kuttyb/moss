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

run_case counter examples/counter.moss 'counter: 41
counter: 42'
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

reject_source use_after_transfer examples/use_after_transfer.moss \
  "value 'original' was transferred to 'destination' at line 10"
reject_case message_transfer "value 'payload' was transferred to 'Worker.Process' at line 11"
reject_case domain_state_transfer "domain state 'dataset' cannot be transferred by message"

compile_case non_reentrant tests/non_reentrant.moss
iteration=1
while [ "$iteration" -le 20 ]; do
  actual=$("$test_build/non_reentrant")
  [ "$actual" = 'start
finish
second' ] || fail "non_reentrant output was out of order on iteration $iteration"
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

reject_case await_one_way "cannot await one-way handler 'Worker.Notify'"
reject_case reply_one_way "reply is only valid in a handler declaring '-> Type'"
reject_case reply_main "reply is only valid in a handler declaring '-> Type'"
reject_case await_unknown_receiver "unknown message receiver 'missing'"
reject_case await_unknown_handler "domain Worker has no message handler 'Missing'"
reject_case await_wrong_arity 'message Worker.Work expects 1 arguments, got 0'
reject_case self_await 'a domain cannot await itself because handlers are non-reentrant'

echo 'all Moss v0.2 tests passed'
