#!/usr/bin/env python3
"""Phase 20 Agent D: compiler concurrency seams.

Covers executor.invoke (ExecutorStartPlan / RootSubmissionPlan legality and
lowering against Agent C's root-admission ABI), FileIO/Range typing aligned
with Agent B's runtime ABI, and FileIO chunk pipelines (structured
ChunkParallelPlan; bounded-K read/map Branches published into one Agent-C
join scope per window, compiler-owned result slots, owned FileIO read-borrow
tokens, ordered parent commit; sequential lowering for ineligible stages).

Agent D owns no production FileIO, Executor, or Branch runtime. Generated
native code names Agent B/C's physical ABI and therefore needs their crates
to link. These tests append TEST-ONLY shims from tests/tooling/fixtures/
(never emitted by the compiler) whose signatures mirror Agent B/C closely
enough to surface integration mismatches -- in particular, Branch and Root
work must satisfy `FnOnce() + Send + 'static`.

Fast Debug models executor.invoke as a deterministic sequential schedule of
deferred roots (compared against native output below). FileIO execution in
Fast Debug awaits Agent A/B integration; Fast Debug raises a clear error.
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

FIXTURES = repo / 'tests/tooling/fixtures'
EXECUTOR_SHIM = FIXTURES / 'phase20_executor_runtime_shim.rs'
BRANCH_SHIM = FIXTURES / 'phase20_branch_runtime_shim.rs'
FILEIO_SHIM = FIXTURES / 'phase20_fileio_range_shim.rs'
# Compile-time contract for Agent B's reserved read-borrow seam: the token a
# Branch carries must be an owned `Send + 'static` value.
READ_BORROW_CONTRACT = FIXTURES / 'phase20_read_borrow_contract.rs'


def run(args, *, expected=0, env=None):
    p = subprocess.run(list(map(str, args)), text=True, capture_output=True,
                       timeout=120, env=env)
    assert p.returncode == expected, (args, p.returncode, p.stdout, p.stderr)
    return p


def rel(path):
    return os.path.relpath(path, repo)


def write_source(name, text):
    path = out / (name + '.moss')
    path.write_text(text)
    return rel(path)


def expect_rejected(source, needle, *, code=None):
    p = run([compiler, '--check', repo / source], expected=1)
    assert needle in p.stderr, (source, p.stderr)
    if code:
        assert code in p.stderr, (source, p.stderr)


def link_with_shims(name, rust, shims, extra=()):
    """Append test-only shims to generated `rust` (generated file first: its
    crate-level #![...] attributes must lead) and link."""
    combined = out / (name + '_combined.rs')
    combined.write_text(rust.read_text() + ''.join(Path(s).read_text() for s in shims))
    binary = out / (name + '_bin')
    run(['rustc', '-D', 'warnings', *extra, combined, '-o', binary])
    return binary


def native_build(name, source, *, shims=()):
    """Compile `source` and link it with test-only shims. Returns (rust, binary)."""
    rust = out / (name + '.rs')
    run([compiler, repo / source, '-o', rust])
    return rust, link_with_shims(name, rust, shims)


def run_binary(binary):
    return run([binary])


def fast_debug(source, *, expected=0):
    return run([compiler, 'run', '--interp', repo / source], expected=expected)


def functional_ir(source):
    return run([compiler, '--dump-functional-ir', repo / source, '-o',
                out / 'phase20_ir_scratch.rs']).stdout


# ---------------------------------------------------------------------------
# Generated code must not define any sibling agent's production runtime.
# ---------------------------------------------------------------------------
def code_only(rust_text):
    return '\n'.join(line for line in rust_text.splitlines() if not line.lstrip().startswith('//'))


def assert_no_d_runtime(rust_text, label):
    for forbidden in (r'\bfn branch_publish\b', r'\bfn branch_join\b', r'\bfn branch_scope_new\b',
                      r'\bstruct MossExecutor\b', r'\bstruct FileIO\b', r'\bstruct Range\b',
                      r'\bfn moss_fileio_branch_read_borrow\b', r'MOSS_CHUNK_WINDOW_K'):
        assert not re.search(forbidden, rust_text), (label, forbidden)


# ---------------------------------------------------------------------------
# executor.invoke / Executor lifecycle: negative legality matrix.
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
expect_rejected('tests/negative/phase20_executor_return_before_join.moss',
                "'return' leaves Executor", code='EXECUTOR_JOIN_MISSING')
expect_rejected('tests/negative/phase20_executor_branch_join_use_after.moss',
                'already joined and consumed', code='EXECUTOR_USE_AFTER_JOIN')
expect_rejected('tests/negative/phase20_executor_invoke_argument_type.moss',
                "has type 'string', expected 'int'",
                code='EXECUTOR_INVOKE_ARGUMENT_TYPE_MISMATCH')

CONFIG_PROGRAM = '''domain Worker:
  fn Process(amount: Int):
    pass

fn main():
  worker = Worker()
  executor = {chain}
  executor.join()
'''
for chain, needle, code in (
        ('Executor().threads("eight").start()', 'Executor.threads requires an Int', 'EXECUTOR_CONFIG_TYPE'),
        ('Executor().start(4)', 'Executor.start() takes no arguments', 'EXECUTOR_START_ARITY'),
        ('Executor().threads(0).start()', 'must be positive', 'EXECUTOR_CONFIG_RANGE'),
        ('Executor().threads(8).max_threads(4).start()', 'cannot be less than threads', 'EXECUTOR_CONFIG_RANGE'),
        ('Executor().affinity(3).start()', 'Vector[Int] of core ids', 'EXECUTOR_CONFIG_TYPE'),
        ('Executor().priority("high").start()', 'Executor.priority requires an Int', 'EXECUTOR_CONFIG_TYPE'),
        ('Executor().workers(2).start()', "unknown Executor configuration method 'workers'",
         'EXECUTOR_UNKNOWN_CONFIG_METHOD')):
    expect_rejected(write_source('phase20_executor_config_negative', CONFIG_PROGRAM.format(chain=chain)),
                    needle, code=code)

# ---------------------------------------------------------------------------
# executor.invoke positive: native (Agent-C-shaped test shim) vs Fast Debug.
# ---------------------------------------------------------------------------
def executor_parity(name, source, expected_stdout):
    rust, binary = native_build(name, source, shims=[EXECUTOR_SHIM])
    text = rust.read_text()
    assert_no_d_runtime(text, name)
    assert 'MossExecutor::new()' in text and '.enqueue_root(MossRootDescriptor::with_target(' in text
    assert re.search(r'\.join\(\);', text)
    # Never a synchronous production fallback: the handler is only called
    # inside the submitted root thunk.
    native = run_binary(binary)
    interpreted = fast_debug(source)
    assert native.stdout == interpreted.stdout == expected_stdout, (name, native.stdout, interpreted.stdout)
    return text, native


executor_parity('phase20_executor_basic', 'tests/phase20_executor_basic.moss', '12\n')
executor_parity('phase20_executor_multi_root', 'tests/phase20_executor_multi_root.moss', '10\n')
# executor.invoke is submission, not a synchronous call: under these
# deterministic schedules the root runs at join, after "submitted".
executor_parity('phase20_executor_deferred', 'tests/phase20_executor_deferred.moss',
                'submitted\nroot ran\n5\n')
# An untyped handler parameter reachable only through executor.invoke is
# inferred exactly like a message call site (no explicit-type requirement).
executor_parity('phase20_executor_untyped_parameter', 'tests/phase20_executor_untyped_parameter.moss', '10\n')

config_source = write_source('phase20_executor_config', '''fn pick(n: Int) -> Int:
  echo n
  return n

domain Worker:
  total = 0

  fn Note(label: String):
    total = total + label.length()

  fn Read() -> Int:
    reply total

fn main():
  worker = Worker()
  executor = Executor().threads(pick(2)).max_threads(pick(8)).queue_capacity(pick(16)).affinity([0, 1]).priority(pick(3)).start()
  label = "abcd"
  executor.invoke(worker.Note(label))
  executor.join()
  echo message worker.Read()
''')
text, native = executor_parity('phase20_executor_config', config_source, '2\n8\n16\n3\n4\n')
# Evaluated exactly once each, in source order, and passed to start().
assert 'moss-executor start threads=Some(2) max_threads=Some(8) queue_capacity=Some(16) '\
       'affinity=Some([0, 1]) priority=Some(3)' in native.stderr, native.stderr
assert native.stderr.index('moss-executor start') < native.stderr.index('moss-executor enqueue') \
    < native.stderr.index('moss-executor join'), native.stderr
assert text.count('__moss_executor_cfg_') >= 10  # 5 temps, each bound then used once
# Configuration values are converted with checked conversions, never `as`:
# a value that passes the static checks but is out of range at run time
# fails closed before any worker starts.
assert not re.search(r'__moss_executor_cfg_\d+ as (usize|i32)', text), text
for label, chain, needle in (
        ('threads', 'Executor().threads(pick(0 - 2)).start()', 'invalid threads'),
        ('queue', 'Executor().queue_capacity(pick(0 - 1)).start()', 'invalid queue_capacity'),
        ('priority', 'Executor().priority(pick(4294967296)).start()', 'invalid priority'),
        ('affinity', 'Executor().affinity([0, pick(0 - 1)]).start()', 'invalid affinity')):
    name = 'phase20_executor_config_dynamic_' + label
    source = write_source(name, """fn pick(n: Int) -> Int:
  return n

domain Worker:
  fn Process(amount: Int):
    pass

fn main():
  worker = Worker()
  executor = %s
  executor.invoke(worker.Process(1))
  executor.join()
""" % chain)
    _, binary = native_build(name, source, shims=[EXECUTOR_SHIM])
    p = subprocess.run([str(binary)], text=True, capture_output=True, timeout=120)
    assert p.returncode != 0, (name, p.returncode, p.stderr)
    assert needle in p.stderr and 'moss-executor start' not in p.stderr, (name, p.stderr)

# The test shim's handle is linear like Agent C's: no Clone.
assert not re.search(r'derive\([^)]*Clone[^)]*\)\s*pub struct MossExecutorHandle',
                     EXECUTOR_SHIM.read_text())

# ---------------------------------------------------------------------------
# Top-level `message` from main is root ingress: a synchronous Root through
# runtime_invoke, in INLINE (before start), ACTIVE (alongside enqueue_root),
# and again INLINE after join. A nested message inside a Root stays a direct
# synchronous call within that Root.
# ---------------------------------------------------------------------------
root_source = write_source('phase20_root_message', """fn label() -> String:
  echo "label evaluated"
  return "abcd"

domain Ledger:
  total = 0

  fn Add(amount: Int):
    total = total + amount

  fn Get() -> Int:
    reply total

domain Worker:
  domainroutes(ledger: Ledger)

  fn Work(amount: Int):
    message ledger.Add(amount)

  fn Get(text: String) -> Int:
    reply text.length()

fn main():
  ledger = Ledger()
  worker = Worker(ledger: ledger)
  before = message worker.Get("abc")
  echo before
  executor = Executor().threads(2).start()
  executor.invoke(worker.Work(5))
  during = message worker.Get(label())
  echo during
  executor.join()
  message ledger.Add(1)
  after = message ledger.Get()
  echo after
""")
rust, binary = native_build('phase20_root_message', root_source, shims=[EXECUTOR_SHIM])
text = code_only(rust.read_text())
assert text.count('runtime_invoke(') == 4, text
assert '.enqueue_root(MossRootDescriptor::with_target(' in text
work_body = re.search(r'fn __moss_body_Worker_Work\b.*?\n}\n', text, re.S)
assert work_body and 'runtime_invoke' not in work_body.group(0), work_body
native = run_binary(binary)
assert native.stdout == fast_debug(root_source).stdout == '3\nlabel evaluated\n4\n6\n', native.stdout
events = [line for line in native.stderr.splitlines() if line.startswith('moss-executor ')]
assert [e.split()[1] for e in events] == ['runtime_invoke', 'start', 'enqueue', 'runtime_invoke',
                                         'join', 'runtime_invoke', 'runtime_invoke'], events

# ---------------------------------------------------------------------------
# FileIO: Agent-B-aligned statement lowering and ordinary helper borrowing.
# ---------------------------------------------------------------------------
read_data = out / 'phase20_fileio_read.data'
read_data.write_bytes(b'hello world')
write_target = out / 'phase20_fileio_write.data'
if write_target.exists():
    write_target.unlink()
fileio_source = write_source('phase20_fileio_basic', '''fn main():
  file = FileIO.open("%s", ro)
  data = file.read(0, 5)
  echo data.length()
  file.close()

  out = FileIO.open("%s", create)
  out.write(0, "written bytes")
  out.sync()
  out.sync(dataonly)
  out.close()
''' % (read_data, write_target))
rust, binary = native_build('phase20_fileio_basic', fileio_source, shims=[FILEIO_SHIM])
text = code_only(rust.read_text())
assert_no_d_runtime(text, 'phase20_fileio_basic')
assert '.sync();' in text and '.sync_dataonly();' in text and 'sync(true' not in text and 'sync(false' not in text
assert '.len()' in text and '.length()' not in text
assert run_binary(binary).stdout == '5\n'
assert write_target.read_text() == 'written bytes', write_target.read_text()
p = fast_debug(fileio_source, expected=1)
assert "FileIO requires Agent B's" in p.stderr, p.stderr

_, binary = native_build('phase20_fileio_helper_parameter', 'tests/phase20_fileio_helper_parameter.moss',
                         shims=[FILEIO_SHIM])
assert run_binary(binary).stdout == '64\n'

# ---------------------------------------------------------------------------
# FileIO chunk pipelines.
# ---------------------------------------------------------------------------
CHUNK_SOURCE = '''fn chunk_length(chunk: Range) -> Int:
  return chunk.length()

fn positional(acc: Int, len: Int) -> Int:
  return acc * 10 + len

fn start_acc() -> Int:
  echo "initializer"
  return 0

fn main():
  file = FileIO.open("{path}", ro)
  total = file.chunks({size}) |> map(chunk_length) |> reduce(start_acc(), positional)
  file.close()
  echo total
'''


def split_trace(stdout):
    lines = stdout.splitlines()
    trace = [line for line in lines if line.startswith('moss-branch ')]
    program = [line for line in lines if not line.startswith('moss-branch ')]
    return lines, trace, program


def chunk_case(name, content, size, expected):
    data = out / (name + '.data')
    data.write_bytes(content)
    source = write_source(name, CHUNK_SOURCE.format(path=data, size=size))
    ir = functional_ir(source)
    assert 'Source seq[range]' in ir and 'vector[range]' not in ir, ir
    assert 'window_k=4 eligible=yes' in ir, ir
    rust, binary = native_build(name, source, shims=[BRANCH_SHIM, FILEIO_SHIM, READ_BORROW_CONTRACT])
    text = rust.read_text()
    assert_no_d_runtime(text, name)
    # The runtime supplies the current Root's identity; no placeholder owner.
    assert 'branch_scope_new_current()' in text and 'branch_scope_new(' not in text
    assert 'moss_fileio_branch_read_borrow(&' in text
    lines, trace, program = split_trace(run_binary(binary).stdout)
    assert program == ['initializer', expected], (name, program)
    # The initializer is evaluated before any Branch is published.
    assert lines[0] == 'initializer', (name, lines)
    # Every window publishes exactly K=4 Branches before its single join --
    # never publish/join/publish/join.
    assert trace and len(trace) % 5 == 0, (name, trace)
    for window in range(0, len(trace), 5):
        assert trace[window:window + 5] == ['moss-branch publish'] * 4 + ['moss-branch join'], (name, trace)
    return trace


# Zero-length EOF at index 0: contributes nothing; indices 1..3 speculative.
chunk_case('phase20_chunk_empty', b'', 4, '0')
# One short nonempty chunk: folded once; later speculative slots discarded.
chunk_case('phase20_chunk_smaller_than_one', b'ab', 4, '2')
chunk_case('phase20_chunk_exact_one_full', b'abcd', 4, '4')
chunk_case('phase20_chunk_exact_multiple', b'123456', 3, '33')
chunk_case('phase20_chunk_short_final', b'1234567', 3, '331')
# Spans two K=4 windows; zero-length EOF at index 8 terminates the second.
trace = chunk_case('phase20_chunk_many', b'0123456789abcdef', 2, '22222222')
assert len(trace) == 15, trace
# Short final chunk inside the second window, with an already-produced
# speculative outcome after it (index 7) that must be discarded.
trace = chunk_case('phase20_chunk_short_mid_window', b'0123456789abc', 2, '2222221')
assert len(trace) == 10, trace


def ineligible_case(name, source, expected, reason):
    ir = functional_ir(source)
    assert 'eligible=no reason=' + reason in ir, (name, ir)
    rust, binary = native_build(name, source, shims=[FILEIO_SHIM])
    text = rust.read_text()
    assert not re.search(r'\bbranch_publish\(', text), name
    assert run_binary(binary).stdout == expected, name


side = out / 'phase20_side.data'
side.write_bytes(b'xy')
chunk_data = out / 'phase20_chunk_ineligible.data'
chunk_data.write_bytes(b'0123456789abc')

# FileIO reached (transitively, through a helper) from map, and from combine:
# both remain valid Moss and lower sequentially.
ineligible_case('phase20_chunk_fileio_map', write_source('phase20_chunk_fileio_map', '''fn chunk_len_io(chunk: Range) -> Int:
  side = FileIO.open("%s", ro)
  extra = side.read(0, 1).length()
  side.close()
  return chunk.length() + extra * 0

fn positional(acc: Int, len: Int) -> Int:
  return acc * 10 + len

fn main():
  file = FileIO.open("%s", ro)
  total = file.chunks(2) |> map(chunk_len_io) |> reduce(0, positional)
  file.close()
  echo total
''' % (side, chunk_data)), '2222221\n', 'map stage reaches a FileIO operation')

ineligible_case('phase20_chunk_fileio_combine', write_source('phase20_chunk_fileio_combine', '''fn chunk_length(chunk: Range) -> Int:
  return chunk.length()

fn positional_io(acc: Int, len: Int) -> Int:
  side = FileIO.open("%s", ro)
  extra = side.read(0, 1).length()
  side.close()
  return acc * 10 + len + extra * 0

fn main():
  file = FileIO.open("%s", ro)
  total = file.chunks(2) |> map(chunk_length) |> reduce(0, positional_io)
  file.close()
  echo total
''' % (side, chunk_data)), '2222221\n', 'combine stage reaches a FileIO operation')

# Inside a handler: a capture-free placeholder map is eligible (and its
# Branch closure satisfies Send + 'static); a map closing over domain state
# is ineligible and lowers sequentially, still producing the right result.
handler_data = out / 'phase20_chunk_handler.data'
handler_data.write_bytes(b'123456')
handler_source = write_source('phase20_chunk_handler', '''fn positional(acc: Int, len: Int) -> Int:
  return acc * 10 + len

domain Counter:
  bonus = 1

  fn Plain(path: String) -> Int:
    file = FileIO.open(path, ro)
    total = file.chunks(3) |> map(_.length()) |> reduce(0, positional)
    file.close()
    reply total

  fn Bonus(path: String) -> Int:
    file = FileIO.open(path, ro)
    total = file.chunks(3) |> map(_.length() + bonus) |> reduce(0, positional)
    file.close()
    reply total

fn main():
  counter = Counter()
  echo message counter.Plain("%s")
  echo message counter.Bonus("%s")
''' % (handler_data, handler_data))
ir = functional_ir(handler_source)
assert 'window_k=4 eligible=yes' in ir and 'eligible=no reason=map stage is not pure: observable domain READ' in ir, ir
rust, binary = native_build('phase20_chunk_handler', handler_source, shims=[BRANCH_SHIM, FILEIO_SHIM])
_, trace, program = split_trace(run_binary(binary).stdout)
assert program == ['33', '44'], program
assert trace == ['moss-branch publish'] * 4 + ['moss-branch join'], trace

# A source-free provider callable carries no FileIO fact in its .mossi
# interface, so it must not be assumed FileIO-free: a pipeline whose combine
# reaches one lowers sequentially, while a local combine stays eligible.
provider_root = out / 'phase20_provider'
provider = provider_root / 'provider'
consumer = provider_root / 'consumer'
for directory in (provider / 'src', consumer / 'src'):
    directory.mkdir(parents=True, exist_ok=True)
for directory in (provider, consumer):
    run(['rm', '-rf', directory / 'build'])
(provider / 'Moss.toml').write_text('[project]\nname = "foldlib"\nversion = "0.1.0"\n\n[build]\nsource = "src"\n')
(provider / 'src/folds.moss').write_text("""module folds

export fn positional(acc: Int, len: Int) -> Int:
  return acc * 10 + len
""")
provider_data = provider_root / 'data.txt'
provider_data.write_bytes(b'123456')
(consumer / 'Moss.toml').write_text('[project]\nname = "p20consumer"\nversion = "0.1.0"\n\n[build]\nsource = "src"\n')
(consumer / 'src/main.moss').write_text("""module app
import folds

fn chunk_length(chunk: Range) -> Int:
  return chunk.length()

fn local_positional(acc: Int, len: Int) -> Int:
  return acc * 10 + len

fn provider_positional(acc: Int, len: Int) -> Int:
  return folds.positional(acc, len)

fn main():
  file = FileIO.open("%s", ro)
  local_total = file.chunks(3) |> map(chunk_length) |> reduce(0, local_positional)
  provider_total = file.chunks(3) |> map(chunk_length) |> reduce(0, provider_positional)
  file.close()
  echo local_total
  echo provider_total
""" % provider_data)
subprocess.run([str(compiler), 'build'], cwd=provider, text=True, capture_output=True, timeout=120, check=True)
provider_artifacts = provider / 'build/debug'
# The consumer's own rustc link needs Agent B/C's crates and fails here; the
# generated Rust is still written and is linked below against the shims.
subprocess.run([str(compiler), 'build'], cwd=consumer, text=True, capture_output=True, timeout=120,
               env=dict(os.environ, MOSS_MODULE_PATH=str(provider_artifacts)))
consumer_rust = consumer / 'build/debug/app.rs'
text = consumer_rust.read_text()
assert text.count('branch_publish(') == 1, text
assert 'ineligible for Branch lowering: combine callable FileIO effect unavailable for compiled provider' in text, text
binary = link_with_shims('phase20_provider_chunks', consumer_rust, [BRANCH_SHIM, FILEIO_SHIM],
                         extra=['-L', provider_artifacts])
_, trace, program = split_trace(run_binary(binary).stdout)
assert program == ['33', '33'], program
assert trace == ['moss-branch publish'] * 4 + ['moss-branch join'], trace

# The chunk source is a scoped seq[Range], not a materializable collection.
expect_rejected(write_source('phase20_chunks_materialize', '''fn main():
  file = FileIO.open("%s", ro)
  c = file.chunks(4)
  file.close()
''' % chunk_data), 'not a materializable collection', code='FILEIO_CHUNKS_NOT_MATERIALIZABLE')
expect_rejected(write_source('phase20_chunks_shape', '''fn main():
  file = FileIO.open("%s", ro)
  n = file.chunks(4) |> count
  file.close()
''' % chunk_data), "must be exactly 'file.chunks(size) |> map(f) |> reduce(initial, combine)'",
    code='FILEIO_CHUNK_PIPELINE_SHAPE')

# Production compiler sources define no Branch scheduler or FileIO runtime.
for path in (repo / 'src').iterdir():
    if path.suffix in ('.hpp', '.inc', '.cpp'):
        source_text = path.read_text()
        assert 'fn branch_publish' not in source_text, path
        assert 'fn moss_fileio_branch_read_borrow' not in source_text, path
assert not (repo / 'src/branch_runtime.hpp').exists()
assert not (repo / 'src/fileio_runtime.hpp').exists()

print('phase20 executor/fileio checks passed')
