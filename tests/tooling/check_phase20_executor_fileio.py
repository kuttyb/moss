#!/usr/bin/env python3
"""Phase 20 Agent D: compiler concurrency seams.

Covers executor.invoke (ExecutorStartPlan / RootSubmissionPlan legality and
lowering against Agent C's root-admission ABI), FileIO/Range typing aligned
with Agent B's runtime ABI, and FileIO chunk pipelines (structured
ChunkParallelPlan; bounded-K read/map Branches published into one Agent-C
join scope per window, compiler-owned result slots, owned FileIO read-borrow
tokens, ordered parent commit; sequential lowering for ineligible stages).

Agent D owns no production FileIO, Executor, or Branch runtime. Generated
native code links against the real Agent B (src/fileio_runtime.hpp) and
Agent C (src/executor_runtime.hpp) runtimes emitted by the compiler. Branch
traces come from a TEST-ONLY observer (tests/tooling/fixtures/
phase20_runtime_observer.rs, never emitted by the compiler) that installs
Agent C's `moss_perf` instrumentation hook.

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
RUNTIME_OBSERVER = FIXTURES / 'phase20_runtime_observer.rs'


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


def native_build(name, source, *, observe=False):
    """Compile `source` and link it against the emitted B/C runtimes. With
    `observe`, append the test-only Branch observer (generated file first: its
    crate-level #![...] attributes must lead) and enable `moss_perf`.
    Returns (rust, binary)."""
    rust = out / (name + '.rs')
    run([compiler, repo / source, '-o', rust])
    binary = out / (name + '_bin')
    if observe:
        combined = out / (name + '_observed.rs')
        combined.write_text(rust.read_text() + RUNTIME_OBSERVER.read_text())
        run(['rustc', '-D', 'warnings', '--cfg', 'moss_perf', combined, '-o', binary])
    else:
        run(['rustc', '-D', 'warnings', rust, '-o', binary])
    return rust, binary


def run_binary(binary):
    return run([binary])


def fast_debug(source, *, expected=0):
    return run([compiler, 'run', '--interp', repo / source], expected=expected)


def functional_ir(source):
    return run([compiler, '--dump-functional-ir', repo / source, '-o',
                out / 'phase20_ir_scratch.rs']).stdout


# ---------------------------------------------------------------------------
# Runtime items come only from their owning agent's runtime module, once.
# ---------------------------------------------------------------------------
def code_only(rust_text):
    return '\n'.join(line for line in rust_text.splitlines() if not line.lstrip().startswith('//'))


def assert_owned_runtime(rust_text, label, *, fileio):
    for item in (r'\bpub fn branch_publish\b', r'\bpub fn branch_join\b', r'\bpub fn branch_scope_new\b',
                 r'\bpub struct MossExecutor\b'):
        assert len(re.findall(item, rust_text)) == 1, (label, item)
    for item in (r'\bpub struct FileIO\b', r'\bpub struct Range\b',
                 r'\bpub fn moss_fileio_branch_read_borrow\b'):
        assert len(re.findall(item, rust_text)) == (1 if fileio else 0), (label, item)
    assert 'MOSS_CHUNK_WINDOW_K' not in rust_text, label


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
# Untyped handler parameter inferred from the invoke site: Agent A's
# boundary rule rejects the inferred FileIO handler parameter.
expect_rejected('tests/negative/phase20_fileio_executor_invoke.moss',
                'handler parameters cannot contain FileIO', code='FILEIO_BOUNDARY_ESCAPE')
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
# executor.invoke positive: native (Agent C runtime) vs Fast Debug.
# ---------------------------------------------------------------------------
def executor_parity(name, source, expected_stdout):
    rust, binary = native_build(name, source)
    text = rust.read_text()
    assert_owned_runtime(text, name, fileio=False)
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
# Evaluated exactly once each, in source order (stdout above), and passed
# to Agent C's builder in that order before start().
assert 'std::convert::TryFrom<i64>' in text and '.try_from(__moss_executor_cfg_' in text, text
assert not re.search(r'__moss_executor_cfg_\d+ as (usize|i32)', text), text
assert text.index('.start()') < text.index('.enqueue_root(') < text.rindex('executor.join()')
assert text.count('__moss_executor_cfg_') >= 10  # 5 temps, each bound then used once

# Dynamic Moss Int values must convert with checked TryFrom, rejecting
# negative and out-of-range values before Executor.start().
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
    rust, binary = native_build(name, source)
    text = rust.read_text()
    assert 'std::convert::TryFrom<i64>' in text, text
    p = subprocess.run([str(binary)], text=True, capture_output=True, timeout=120)
    assert p.returncode != 0 and needle in p.stderr, (name, p.returncode, p.stderr)

# Main-originated messages use runtime_invoke before start, while ACTIVE,
# and after join. Their arguments are evaluated once, and handler-to-handler
# messages stay within the current Root.
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
root_rust, root_binary = native_build('phase20_root_message', root_source, observe=True)
root_text = code_only(root_rust.read_text())
assert root_text.count('runtime_invoke(') == 4, root_text
work_body = re.search(r'fn __moss_body_Worker_Work\\b.*?\\n}\\n', root_text, re.S)
assert work_body and 'runtime_invoke' not in work_body.group(0), work_body
assert run_binary(root_binary).stdout == '3\\nlabel evaluated\\n4\\n6\\n'

# Root ingress marking covers nested main control-flow blocks.
nested_root_source = write_source('phase20_root_message_nested_control', """enum MainChoice:
  Run
  Skip

domain Counter:
  total = 0
  fn Add(amount: Int):
    total = total + amount
  fn Read() -> Int:
    reply total

fn main():
  counter = Counter()
  executor = Executor().threads(2).start()
  if true:
    message counter.Add(1)
  match MainChoice.Run:
    case Run:
      message counter.Add(2)
    case Skip:
      pass
  for item in range(0, 2):
    message counter.Add(item + 3)
  executor.join()
  echo message counter.Read()
""")
nested_rust, nested_binary = native_build(
    'phase20_root_message_nested_control', nested_root_source, observe=True)
nested_text = code_only(nested_rust.read_text())
assert nested_text.count('runtime_invoke(') == 4, nested_text
assert run_binary(nested_binary).stdout == '10\\n'

exported_root_source = write_source('phase20_root_message_exported_domain', """export domain Boundary:
  fn Read() -> Int:
    reply 23

fn main():
  boundary = Boundary()
  executor = Executor().threads(2).start()
  value = message boundary.Read()
  executor.join()
  echo value
""")
exported_rust, exported_binary = native_build(
    'phase20_root_message_exported_domain', exported_root_source, observe=True)
exported_text = code_only(exported_rust.read_text())
assert 'runtime_invoke(move ||' in exported_text and '.__moss_message_Read()' in exported_text
assert run_binary(exported_binary).stdout == '23\\n'

# runtime_invoke is emitted for executor-free main messages as well; the
# normal executable root includes Agent C's runtime in INLINE mode.
free_root_source = write_source('phase20_root_message_executor_free', """domain Counter:
  fn Read() -> Int:
    reply 17

fn main():
  counter = Counter()
  echo message counter.Read()
""")
free_rust, free_binary = native_build('phase20_root_message_executor_free', free_root_source)
assert 'runtime_invoke(move ||' in code_only(free_rust.read_text())
assert run_binary(free_binary).stdout == '17\\n'

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
rust, binary = native_build('phase20_fileio_basic', fileio_source)
text = code_only(rust.read_text())
assert_owned_runtime(text, 'phase20_fileio_basic', fileio=True)
assert 'out.sync();' in text and 'out.sync_dataonly();' in text
assert 'data.len()' in text and 'data.length()' not in text
assert run_binary(binary).stdout == '5\n'
assert write_target.read_text() == 'written bytes', write_target.read_text()
p = fast_debug(fileio_source, expected=1)
assert "FileIO requires Agent B's" in p.stderr, p.stderr

_, binary = native_build('phase20_fileio_helper_parameter', 'tests/phase20_fileio_helper_parameter.moss')
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


WINDOW_TRACE = ['moss-branch publish'] * 4 + ['moss-branch run'] * 4
WINDOW = len(WINDOW_TRACE)


def split_trace(stdout):
    lines = [line for line in stdout.splitlines() if not line.startswith('moss-solo ')]
    trace = [line for line in lines if line.startswith('moss-branch ')]
    program = [line for line in lines if not line.startswith('moss-branch ')]
    return lines, trace, program


def chunk_case(name, content, size, expected):
    data = out / (name + '.data')
    data.write_bytes(content)
    source = write_source(name, CHUNK_SOURCE.format(path=data, size=size))
    ir = functional_ir(source)
    assert 'Source seq[Range]' in ir and 'vector[Range]' not in ir, ir
    assert 'window_k=4 eligible=yes' in ir, ir
    rust, binary = native_build(name, source, observe=True)
    text = rust.read_text()
    assert_owned_runtime(text, name, fileio=True)
    assert 'branch_scope_new(' in text and 'moss_fileio_branch_read_borrow(&' in text
    lines, trace, program = split_trace(run_binary(binary).stdout)
    assert program == ['initializer', expected], (name, program)
    # The initializer is evaluated before any Branch is published.
    assert lines[0] == 'initializer', (name, lines)
    # Every window publishes exactly K=4 Branches before its single join runs
    # them (inline: no active Executor) -- never publish/run/publish/run.
    assert trace and len(trace) % WINDOW == 0, (name, trace)
    for window in range(0, len(trace), WINDOW):
        assert trace[window:window + WINDOW] == WINDOW_TRACE, (name, trace)
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
assert len(trace) == 3 * WINDOW, trace
# Short final chunk inside the second window, with an already-produced
# speculative outcome after it (index 7) that must be discarded.
trace = chunk_case('phase20_chunk_short_mid_window', b'0123456789abc', 2, '2222221')
assert len(trace) == 2 * WINDOW, trace


def ineligible_case(name, source, expected, reason):
    ir = functional_ir(source)
    assert 'eligible=no reason=' + reason in ir, (name, ir)
    rust, binary = native_build(name, source)
    text = code_only(rust.read_text())
    assert 'branch_publish(&__moss_chunk_scope' not in text and '__moss_chunk_borrow' not in text, name
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
rust, binary = native_build('phase20_chunk_handler', handler_source, observe=True)
_, trace, program = split_trace(run_binary(binary).stdout)
assert program == ['33', '44'], program
assert trace == WINDOW_TRACE, trace

# Integrated A+B+C+D: with an active Executor, invoked Roots perform blocking
# FileIO (Agent B Solo hooks -> Agent C compensation) while a value-returning
# handler, called synchronously by message, lowers its chunk pipeline to
# Branches that run on executor workers. Each Root opens its own inode
# (spec sec. 10: one live FileIO per inode).
e2e_data = out / 'phase20_e2e.data'
e2e_data.write_bytes(bytes(range(256)) * 400 + b'tail')
for suffix in ('a', 'b'):
    (out / ('phase20_e2e.data.' + suffix)).write_bytes(e2e_data.read_bytes())
e2e_source = write_source('phase20_e2e_executor_fileio', '''fn add_length(acc: Int, len: Int) -> Int:
  return acc + len

domain Count:
  fn Total(path: String) -> Int:
    file = FileIO.open(path, ro)
    total = file.chunks(4096) |> map(_.length()) |> reduce(0, add_length)
    file.close()
    reply total

domain Reader:
  seen = 0

  fn Scan(path: String):
    file = FileIO.open(path, ro)
    data = file.read(0, 64)
    seen = seen + data.length()
    file.close()

  fn Seen() -> Int:
    reply seen

fn main():
  count = Count()
  reader = Reader()
  executor = Executor().threads(2).max_threads(4).start()
  path = "%s"
  executor.invoke(reader.Scan(path + ".a"))
  executor.invoke(reader.Scan(path + ".b"))
  echo message count.Total(path)
  executor.join()
  echo message reader.Seen()
''' % e2e_data)
_, binary = native_build('phase20_e2e_executor_fileio', e2e_source, observe=True)
for _ in range(5):
    stdout = run_binary(binary).stdout
    _, trace, program = split_trace(stdout)
    assert program == [str(len(e2e_data.read_bytes())), '128'], program
    publishes = trace.count('moss-branch publish')
    assert publishes and publishes % 4 == 0 and trace.count('moss-branch run') == publishes, trace
    assert 'moss-solo enter' in stdout.splitlines(), stdout

# Sequential `for` over the scoped chunk source iterates Agent B's MossChunks.
for_chunks_source = write_source('phase20_for_chunks', '''fn main():
  file = FileIO.open("%s", ro)
  for chunk in file.chunks(3):
    echo chunk.length()
  file.close()
''' % (out / 'phase20_chunk_short_final.data'))
_, binary = native_build('phase20_for_chunks', for_chunks_source)
assert run_binary(binary).stdout == '3\n3\n1\n'

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

# Branch scheduling and FileIO runtime live only in their owners' modules
# (Agent C: executor_runtime.hpp; Agent B: fileio_runtime.hpp), never in
# Agent D's lowering/codegen.
for path in (repo / 'src').iterdir():
    if path.suffix in ('.hpp', '.inc', '.cpp'):
        source_text = path.read_text()
        if path.name != 'executor_runtime.hpp':
            assert 'fn branch_publish' not in source_text, path
        if path.name != 'fileio_runtime.hpp':
            assert 'fn moss_fileio_branch_read_borrow' not in source_text, path
assert not (repo / 'src/branch_runtime.hpp').exists()

print('phase20 executor/fileio checks passed')
