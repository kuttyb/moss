#!/bin/sh
# Common shell test helper functions for Moss regression suites.
# Sourced by tests/run.sh and tests/shards/*.sh.

fail() {
  echo "test failure: $*" >&2
  exit 1
}

# Generated Rust minus the emitted Phase 20 executor runtime, whose work
# queue, threads, and C Branch ABI are runtime internals rather than lowered
# Moss code. The pinned Float formatter is also compiler runtime code.
# Negative backend-shape checks inspect only lowered code. Unless exactly one
# of each delimited block is present, the whole file is returned.
lowered_rust() {
  runtime_start='^// Phase 20 Executor Runtime  (moss executor_runtime_rust: '
  runtime_end='^// ─── End Phase 20 Executor Runtime '
  float_start='^// ─── Begin Moss Float Format Runtime '
  float_end='^// ─── End Moss Float Format Runtime '
  string_start='^// ─── Begin Moss String Runtime '
  string_end='^// ─── End Moss String Runtime '
  if [ "$(grep -c "$runtime_start" "$1")" -eq 1 ] &&
     [ "$(grep -c "$runtime_end" "$1")" -eq 1 ] &&
     [ "$(grep -c "$float_start" "$1")" -eq 1 ] &&
     [ "$(grep -c "$float_end" "$1")" -eq 1 ] &&
     [ "$(grep -c "$string_start" "$1")" -eq 1 ] &&
     [ "$(grep -c "$string_end" "$1")" -eq 1 ]; then
    sed -e "\#$runtime_start#,\#$runtime_end#d" \
        -e "\#$float_start#,\#$float_end#d" \
        -e "\#$string_start#,\#$string_end#d" "$1"
  else
    cat "$1"
  fi
}

# All functional optimization flags must converge on the authoritative 2PL path.
assert_class_lowering() {
  grep -F 'fn moss_read_or_abort<T>' "$1" >/dev/null ||
    fail "$1 omitted synchronization-class storage"
  grep -F 'let result = __moss_body_' "$1" >/dev/null ||
    fail "$1 bypassed synchronized handler entry"
  if lowered_rust "$1" | grep -E 'Arc<(Mutex|RwLock)<[^>]*State|AtomicI64|AtomicBool|MossCluster[0-9]+Runtime|thread::spawn' >/dev/null; then
    fail "$1 activated legacy coarse/atomic/cluster synchronization"
  fi
}

compile_case() {
  name=$1
  source=$2
  "$compiler" --check "$source"
  "$compiler" "$source" -o "$test_build/$name.rs"
  if grep -Eq 'std::sync::mpsc|mpsc::channel' "$test_build/$name.rs"; then
    fail "$name emitted forbidden Rust message-passing transport"
  fi
  grep -F 'fn moss_read_or_abort<T>' "$test_build/$name.rs" >/dev/null ||
    fail "$name did not emit synchronization-class storage"
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

compile_shared_memory_case() {
  name=$1
  source=$2
  compile_optimized_case "$name" "$source"
  grep -F 'Synchronous domain calls enter compiler-planned handler-level 2PL.' \
    "$test_build/$name.rs" >/dev/null ||
    fail "$name did not select direct shared-memory dispatch"
  grep -E 'Moss backend plan: .* = Handler2PL' \
    "$test_build/$name.rs" >/dev/null ||
    fail "$name did not emit a planned direct domain lowering"
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

run_fast_debug_case() {
  name=$1
  source=$2
  expected=$3
  actual=$("$compiler" run --interp "$source")
  if [ "$actual" != "$expected" ]; then
    printf 'test failure: %s Fast Debug output\nexpected:\n%s\nactual:\n%s\n' \
      "$name" "$expected" "$actual" >&2
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
  grep -Eq '^(moss:[0-9]+: error:|error\[[A-Z0-9_]+\]:)' "$stderr" ||
    fail "$name did not produce a line-numbered diagnostic"
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

tooling_process_tracing_denied() {
  grep -E \
    'Connection shut down by remote side while waiting for reply to initial handshake packet|Operation not permitted|ptrace' \
    "$@" >/dev/null
}
