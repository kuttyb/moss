#!/usr/bin/env python3
"""Phase 20 Agent D corrective pass: executor.invoke RootSubmissionPlan
lifecycle/argument legality, and FileIO/Range chunk-pipeline ChunkParallelPlan
(bounded-K Branch lowering with inline fallback, and the plain sequential
fallback for an ineligible pipeline).

Architecture under test (see src/executor_invoke_lowering.inc,
src/executor_invoke_codegen.inc, src/file_chunk_lowering.inc,
src/file_chunk_codegen.inc, src/branch_runtime.hpp, moss.cpp's Phase 20
capability adapter):

  - Agent D owns no production FileIO or Executor/Branch-worker-pool
    runtime. `executor.invoke` never lowers to a synchronous handler call;
    it lowers to calls against three not-yet-defined Agent-C names
    (moss_root_start/moss_root_submit/moss_root_join). A standalone native
    build of a program using executor.invoke or FileIO is therefore
    *expected* to fail to link without Agent C/B's crates -- these tests
    supply tests/tooling/fixtures/*.rs TEST-ONLY shims (never emitted by the
    compiler, never documented as Phase 20 runtime behavior) so Agent D's
    own compiler output can be exercised end to end.
  - `branch_publish`/`branch_join` (src/branch_runtime.hpp) *are* real,
    normative Agent-D-owned production code: the Branch abstraction's
    inline-execution fallback is itself Phase 20 semantics (sec. 7.3, E4),
    not a substitute for Agent C's eventual worker-pool implementation.
  - Fast Debug does not implement a competing Executor/FileIO runtime
    either: `executor.invoke` and `FileIO.open` both throw a clear "pending
    Agent C/B integration" RuntimeError in Fast Debug. Chunk-pipeline
    algorithm correctness (ordering, termination, K-independence) is
    exercised through native codegen + the test-only Rust shims instead.

Not covered here (deliberately; see the .inc files' header comments for
the scope rationale): domain-field FileIO, Range/RangeBatch borrow scoping,
batch reads, a real Agent-C worker pool, and "map/combine makes a FileIO
call" (FileIO cannot be passed into an ordinary helper as anything other
than a ordinary borrowed parameter, so a chunk pipeline's map/combine
callables -- resolved as plain functions of the element/accumulator types --
cannot reach one under this scope).
"""
import os
from pathlib import Path
import re
import subprocess
import sys

repo = Path(__file__).resolve().parents[2]
compiler = Path(sys.argv[1]).resolve()
out = Path(sys.argv[2] if len(sys.argv) > 2 else repo / 'build/tests').resolve()
out.mkdir(parents=True, exist_ok=True)

ROOT_QUEUE_SHIM = repo / 'tests/tooling/fixtures/phase20_root_queue_shim.rs'
FILEIO_SHIM = repo / 'tests/tooling/fixtures/phase20_fileio_range_shim.rs'


def run(args, *, expected=0):
    p = subprocess.run(list(map(str, args)), text=True, capture_output=True, timeout=90)
    assert p.returncode == expected, (args, p.returncode, p.stdout, p.stderr)
    return p


def expect_rejected(source, needle, *, code=None):
    p = run([compiler, '--check', repo / source], expected=1)
    assert needle in p.stderr, (source, p.stderr)
    if code:
        assert code in p.stderr, (source, p.stderr)


def expect_interp_pending(source, needle):
    p = run([compiler, 'run', '--interp', repo / source], expected=1)
    assert needle in p.stderr, (source, p.stderr)


def native_build(name, source, *, shims=()):
    """Compiles `source` to Rust, concatenates it with the given test-only
    shim(s) (generated file FIRST: Rust crate-level `#![...]` attributes
    must lead the file), and links a runnable binary. Returns the combined
    .rs path and the binary path."""
    rust = out / (name + '.rs')
    run([compiler, repo / source, '-o', rust])
    text = rust.read_text()
    assert '#![allow(dead_code)]' in text, (source, 'missing expected crate preamble')
    combined = out / (name + '_combined.rs')
    combined.write_text(text + ''.join(Path(shim).read_text() for shim in shims))
    binary = out / (name + '_bin')
    run(['rustc', '-D', 'warnings', combined, '-o', binary])
    return rust, binary


def run_binary(binary, *, env=None):
    full_env = dict(os.environ)
    if env:
        full_env.update(env)
    p = subprocess.run([str(binary)], text=True, capture_output=True, timeout=30, env=full_env)
    assert p.returncode == 0, (binary, p.returncode, p.stdout, p.stderr)
    return p.stdout


# ---------------------------------------------------------------------------
# executor.invoke / Executor lifecycle: required negative legality matrix.
# ---------------------------------------------------------------------------
expect_rejected('tests/negative/phase20_executor_invoke_outside_main.moss',
                'may only be configured and started directly in', code='EXECUTOR_CONSTRUCT_OUTSIDE_MAIN')
expect_rejected('tests/negative/phase20_executor_unknown_handler.moss',
                'unknown handler', code='EXECUTOR_INVOKE_UNKNOWN_HANDLER')
expect_rejected('tests/negative/phase20_executor_value_returning.moss',
                'requires a one-way handler', code='EXECUTOR_INVOKE_VALUE_RETURNING')
expect_rejected('tests/negative/phase20_executor_use_after_join.moss',
                'already joined and consumed', code='EXECUTOR_USE_AFTER_JOIN')
expect_rejected('tests/negative/phase20_executor_missing_join.moss',
                'must reach Executor.join()', code='EXECUTOR_JOIN_MISSING')
expect_rejected('tests/negative/phase20_executor_two_active.moss',
                'at most one Executor may be active', code='EXECUTOR_MULTIPLE_ACTIVE')
expect_rejected('tests/negative/phase20_executor_domain_state.moss',
                "unknown state type 'Executor'", code='UNKNOWN_SYMBOL_OR_TYPE')
expect_rejected('tests/negative/phase20_executor_domain_boundary.moss',
                "unknown parameter type 'executor'", code='UNKNOWN_SYMBOL_OR_TYPE')
expect_rejected('tests/negative/phase20_executor_invoke_capability_argument.moss',
                'cannot be passed as an executor.invoke argument',
                code='EXECUTOR_INVOKE_CAPABILITY_ARGUMENT')
# Corrective-pass item 12: an early `return` with an Executor still active
# is itself rejected, not merely "falling off the end" of the function.
expect_rejected('tests/negative/phase20_executor_return_before_join.moss',
                "'return' leaves Executor", code='EXECUTOR_JOIN_MISSING')
# Corrective-pass item 13: joining inside both branches of an `if` must be
# recognized as consuming the Executor on every path, so a later invoke is
# "already joined", not merely "not active".
expect_rejected('tests/negative/phase20_executor_branch_join_use_after.moss',
                'already joined and consumed', code='EXECUTOR_USE_AFTER_JOIN')
# Corrective-pass item 8: argument type compatibility is checked like an
# ordinary handler call, not left for rustc to discover.
expect_rejected('tests/negative/phase20_executor_invoke_argument_type.moss',
                "has type 'string', expected 'int'",
                code='EXECUTOR_INVOKE_ARGUMENT_TYPE_MISMATCH')
# An untyped handler parameter reachable only through executor.invoke (never
# `message`) never gets an inferred type at all -- still a real rejection,
# just via the general TYPE_INFERENCE_FAILED diagnostic rather than a
# Phase-20-specific one.
expect_rejected('tests/negative/phase20_executor_invoke_untyped_parameter.moss',
                "cannot infer type for parameter 'amount'", code='TYPE_INFERENCE_FAILED')

# ---------------------------------------------------------------------------
# executor.invoke: required positive cases, native (+ test-only root-queue
# shim) and Fast Debug (expected clean "pending Agent C" rejection).
# ---------------------------------------------------------------------------
_, basic_bin = native_build('phase20_executor_basic', 'tests/phase20_executor_basic.moss',
                            shims=[ROOT_QUEUE_SHIM])
assert run_binary(basic_bin) == '12\n'
expect_interp_pending('tests/phase20_executor_basic.moss',
                      "executor.invoke requires Agent C's")

_, multi_bin = native_build('phase20_executor_multi_root', 'tests/phase20_executor_multi_root.moss',
                            shims=[ROOT_QUEUE_SHIM])
assert run_binary(multi_bin) == '10\n'

# ---------------------------------------------------------------------------
# FileIO: root-local open/read/write/sync/close, and the ordinary-helper
# borrow explicitly required by corrective-pass item 4.
# ---------------------------------------------------------------------------
read_data = out / 'phase20_fileio_read.data'
read_data.write_bytes(b'hello world')
write_target = out / 'phase20_fileio_write.data'
if write_target.exists():
    write_target.unlink()
fileio_source = out / 'phase20_fileio_basic.moss'
fileio_source.write_text('''fn main():
  file = FileIO.open("%s", ro)
  data = file.read(0, 5)
  file.close()
  echo data.length()

  out = FileIO.open("%s", create)
  out.write(0, "written bytes")
  out.sync()
  out.close()
''' % (read_data, write_target))
_, fileio_bin = native_build('phase20_fileio_basic', os.path.relpath(fileio_source, repo),
                             shims=[FILEIO_SHIM])
assert run_binary(fileio_bin) == '5\n'
assert write_target.read_text() == 'written bytes', write_target.read_text()
expect_interp_pending(os.path.relpath(fileio_source, repo),
                      "FileIO requires Agent B's")

# Ordinary synchronous helper borrowing a FileIO parameter must be legal
# (corrective-pass item 4) -- this was Agent D's own earlier bug.
_, helper_bin = native_build('phase20_fileio_helper_parameter',
                             'tests/phase20_fileio_helper_parameter.moss',
                             shims=[FILEIO_SHIM])
assert run_binary(helper_bin) == '64\n'

# ---------------------------------------------------------------------------
# FileIO chunk pipelines: structured ChunkParallelPlan, bounded-K Branch
# lowering for an eligible pipeline, plain sequential lowering for an
# ineligible one, and K-independence of the correct result.
#
# Chunk element type is `Range` (not `String`); the only Moss-level Range
# accessor Agent D recognizes is `.length()` (see file_chunk_lowering.inc).
# Ordering is verified by positional-encoding the committed sequence of
# chunk lengths into one base-10 integer (acc*10+len): a reordering or
# double-commit bug changes the digit sequence even though same-size full
# chunks are individually indistinguishable by length alone.
# ---------------------------------------------------------------------------
CHUNK_MOSS = '''fn chunk_length(chunk: Range) -> Int:
  return chunk.length()

fn positional(acc: Int, len: Int) -> Int:
  return acc * 10 + len

fn main():
  file = FileIO.open("{path}", ro)
  total = file.chunks({size}) |> map(chunk_length) |> reduce(0, positional)
  file.close()
  echo total
'''


def chunk_case(name, content, size, expected, *, check_branch_publish=True):
    data_path = out / (name + '.data')
    data_path.write_bytes(content)
    source_path = out / (name + '.moss')
    source_path.write_text(CHUNK_MOSS.format(path=data_path, size=size))
    rust, binary = native_build(name, os.path.relpath(source_path, repo), shims=[FILEIO_SHIM])
    actual = run_binary(binary)
    assert actual == expected, (name, actual, expected)
    text = rust.read_text()
    # The preamble *definition* is `fn branch_publish<T>(...)` (generic
    # params before the parenthesis), so this only matches call sites.
    call_sites = len(re.findall(r'\bbranch_publish\(', text))
    if check_branch_publish:
        assert call_sites >= 1, (name, 'expected the eligible bounded-K Branch lowering')
    else:
        assert call_sites == 0, (name, 'ineligible pipeline must not use branch_publish')
    return binary


chunk_case('phase20_chunk_empty', b'', 4, '0\n')
chunk_case('phase20_chunk_smaller_than_one', b'ab', 4, '2\n')
chunk_case('phase20_chunk_exact_one_full', b'abcd', 4, '4\n')
chunk_case('phase20_chunk_exact_multiple', b'123456', 3, '33\n')
chunk_case('phase20_chunk_short_final', b'1234567', 3, '331\n')
many_binary = chunk_case('phase20_chunk_many', b'0123456789abcdef', 2, '22222222\n')

# K is just a lowering-window bound, never an observable correctness
# property (corrective-pass item 23): the same eligible binary must produce
# the identical correct result for K=1, a small K, and a large K.
for k in (1, 2, 4, 1000):
    assert run_binary(many_binary, env={'MOSS_CHUNK_WINDOW_K': str(k)}) == '22222222\n', k

# A map stage that captures domain state (closing over `bonus` inside a
# handler) is ineligible for the Branch lowering and must fall back to the
# plain sequential loop -- while still producing the correct result.
domain_data = out / 'phase20_chunk_domain.data'
domain_data.write_bytes(b'123456')
domain_source = out / 'phase20_chunk_domain.moss'
domain_source.write_text('''fn positional(acc: Int, len: Int) -> Int:
  return acc * 10 + len

domain Counter:
  bonus = 1

  fn Summarize(path: String) -> Int:
    file = FileIO.open(path, ro)
    total = file.chunks(3) |> map(_.length() + bonus) |> reduce(0, positional)
    file.close()
    reply total

fn main():
  counter = Counter()
  result = message counter.Summarize("%s")
  echo result
''' % domain_data)
rust, binary = native_build('phase20_chunk_domain', os.path.relpath(domain_source, repo),
                            shims=[FILEIO_SHIM])
assert run_binary(binary) == '44\n'
assert not re.search(r'\bbranch_publish\(', rust.read_text()), \
    'ineligible (domain-capturing) pipeline must not use the Branch abstraction'

# The canonical handler-scoped word-count shape from
# docs/MOSS_PHASE_20_FILE_IO_AND_EXECUTORS.md (root-local FileIO opened and
# closed inside a handler, chunk pipeline with a pure map).
wc_data = out / 'phase20_chunk_handler.data'
wc_data.write_bytes(b'123456')
wc_source = out / 'phase20_chunk_handler.moss'
wc_source.write_text('''fn chunk_length(chunk: Range) -> Int:
  return chunk.length()

fn positional(acc: Int, len: Int) -> Int:
  return acc * 10 + len

domain WordCount:
  fn Count(path: String) -> Int:
    file = FileIO.open(path, ro)
    total = file.chunks(3) |> map(chunk_length) |> reduce(0, positional)
    file.close()
    reply total

fn main():
  counter = WordCount()
  result = message counter.Count("%s")
  echo result
''' % wc_data)
_, handler_binary = native_build('phase20_chunk_handler', os.path.relpath(wc_source, repo),
                                 shims=[FILEIO_SHIM])
assert run_binary(handler_binary) == '33\n'

print('phase20 executor/fileio checks passed')
