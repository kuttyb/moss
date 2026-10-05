#!/usr/bin/env python3
"""Phase 20 Agent D: executor.invoke lifecycle and FileIO chunk pipelines.

Covers the required executor.invoke positive/negative legality matrix and
the required differential sequential-vs-eligible chunk-pipeline coverage
from the Phase 20 Agent D task. See:
  - docs/MOSS_PHASE_20_FILE_IO_AND_EXECUTORS.md
  - src/executor_invoke_lowering.inc / executor_invoke_codegen.inc
  - src/file_chunk_lowering.inc / file_chunk_codegen.inc

Deliberately not covered here (see those .inc files' header comments for
the scope rationale): domain-field FileIO, Range/RangeBatch borrow
scoping, batch reads, and any real parallel branch execution (K=1, K
greater than available workers, root/branch contention) -- there is no
Agent-C root-admission/worker-pool runtime in this tree, so every chunk
pipeline (eligible or not) executes the same sequential reference loop on
both backends. "map/combine makes a FileIO call" is also not exercised:
FileIO is a pinned capability that cannot be passed into an ordinary
helper function, so a valid Moss program cannot construct that shape
under this scope.
"""
import os
from pathlib import Path
import subprocess
import sys

repo = Path(__file__).resolve().parents[2]
compiler = Path(sys.argv[1]).resolve()
out = Path(sys.argv[2] if len(sys.argv) > 2 else repo / 'build/tests').resolve()
out.mkdir(parents=True, exist_ok=True)


def run(args, *, expected=0):
    p = subprocess.run(list(map(str, args)), text=True, capture_output=True, timeout=90)
    assert p.returncode == expected, (args, p.returncode, p.stdout, p.stderr)
    return p


def expect_rejected(source, needle, *, code=None):
    p = run([compiler, '--check', repo / source], expected=1)
    assert needle in p.stderr, (source, p.stderr)
    if code:
        assert code in p.stderr, (source, p.stderr)


def native_output(name, source):
    rust = out / (name + '.rs')
    run([compiler, repo / source, '-o', rust])
    binary = rust.with_suffix('')
    run(['rustc', '-D', 'warnings', rust, '-o', binary])
    return run([binary]).stdout


def interp_output(source):
    return run([compiler, 'run', '--interp', repo / source]).stdout


def differential(name, source, expected):
    native = native_output(name, source)
    interp = interp_output(source)
    assert native == interp == expected, (source, native, interp, expected)


# --- executor.invoke / Executor lifecycle: required negative legality cases ---
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

# --- executor.invoke: required positive cases ---
differential('phase20_executor_basic', 'tests/phase20_executor_basic.moss', '12\n')
differential('phase20_executor_multi_root', 'tests/phase20_executor_multi_root.moss', '10\n')

# --- FileIO chunk pipelines: differential sequential-vs-optimized coverage ---
moss_src = '''fn tag(chunk: String) -> String:
  return "[" + chunk + "]"

fn concat(a: String, b: String) -> String:
  return a + b

fn main():
  file = FileIO.open("{path}", ro)
  total = file.chunks({size}) |> map(tag) |> reduce("", concat)
  file.close()
  echo total
'''


def chunk_case(name, content, size, expected):
    data_path = out / (name + '.data')
    data_path.write_bytes(content)
    source_path = out / (name + '.moss')
    source_path.write_text(moss_src.format(path=data_path, size=size))
    differential(name, os.path.relpath(source_path, repo), expected)


chunk_case('phase20_chunk_empty', b'', 4, '\n')
chunk_case('phase20_chunk_smaller_than_one', b'ab', 4, '[ab]\n')
chunk_case('phase20_chunk_exact_one_full', b'abcd', 4, '[abcd]\n')
chunk_case('phase20_chunk_exact_multiple', b'123456', 3, '[123][456]\n')
chunk_case('phase20_chunk_short_final', b'1234567', 3, '[123][456][7]\n')
chunk_case('phase20_chunk_many', b'0123456789abcdef', 2, '[01][23][45][67][89][ab][cd][ef]\n')
chunk_case('phase20_chunk_word_boundary', b'wor' b'd ' b'spa' b'nning', 3, '[wor][d s][pan][nin][g]\n')

# K=1 is meaningless without a real branch runtime (sec. "scope boundary" in
# file_chunk_codegen.inc); the loop above is already exactly that sequential
# execution regardless of a would-be K. Zero-length EOF contributing nothing
# is covered by phase20_chunk_empty (accumulator stays "").

# Ordered fold with an observable parent-root/domain-state effect, and a map
# stage that captures domain state (making the pipeline ineligible for the
# would-be parallel branch lowering, per file_chunk_lowering.inc), still
# executing correctly and sequentially either way.
domain_data = out / 'phase20_chunk_domain.data'
domain_data.write_bytes(b'123456')
domain_source = out / 'phase20_chunk_domain.moss'
domain_source.write_text('''fn add(a: Int, b: Int) -> Int:
  return a + b

domain Counter:
  bonus = 100

  fn Summarize(path: String) -> Int:
    file = FileIO.open(path, ro)
    total = file.chunks(3) |> map(_.length() + bonus) |> reduce(0, add)
    file.close()
    reply total

fn main():
  counter = Counter()
  result = message counter.Summarize("%s")
  echo result
''' % domain_data)
differential('phase20_chunk_domain', os.path.relpath(domain_source, repo), '206\n')

# Handler-scoped root-local FileIO + chunk pipeline (the canonical shape from
# the word-count example in docs/MOSS_PHASE_20_FILE_IO_AND_EXECUTORS.md).
wc_data = out / 'phase20_chunk_handler.data'
wc_data.write_bytes(b'123456')
wc_source = out / 'phase20_chunk_handler.moss'
wc_source.write_text('''fn chunk_length(chunk: String) -> Int:
  return chunk.length()

fn add(a: Int, b: Int) -> Int:
  return a + b

domain WordCount:
  fn Count(path: String) -> Int:
    file = FileIO.open(path, ro)
    total = file.chunks(3) |> map(chunk_length) |> reduce(0, add)
    file.close()
    reply total

fn main():
  counter = WordCount()
  result = message counter.Count("%s")
  echo result
''' % wc_data)
differential('phase20_chunk_handler', os.path.relpath(wc_source, repo), '6\n')

# --- FileIO: root-local open/read/write/sync/close ---
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
  echo data

  out = FileIO.open("%s", create)
  out.write(0, "written bytes")
  out.sync()
  out.close()
''' % (read_data, write_target))
differential('phase20_fileio_basic', os.path.relpath(fileio_source, repo), 'hello\n')
assert write_target.read_text() == 'written bytes', write_target.read_text()

print('phase20 executor/fileio checks passed')
