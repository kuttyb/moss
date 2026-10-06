#!/usr/bin/env python3
"""Phase 20 FileIO source legality, effects, and synchronization regression."""
import json
from pathlib import Path
import subprocess
import sys

compiler = Path(sys.argv[1]).resolve()
out = Path(sys.argv[2]).resolve()
out.mkdir(parents=True, exist_ok=True)


def check(name, source, code=None):
    path = out / (name + '.moss')
    path.write_text(source)
    process = subprocess.run([str(compiler), 'check', str(path), '--json'],
                             text=True, capture_output=True, timeout=30)
    result = json.loads(process.stdout)
    diagnostics = result['result']['diagnostics'] if result['ok'] else result.get('diagnostics', [])
    if code is None:
        assert process.returncode == 0 and result['ok'], (name, process.stdout, process.stderr)
    else:
        assert process.returncode != 0 and code in process.stdout, (name, code, process.stdout)
    return path, diagnostics


def main(body):
    return 'fn main():\n' + ''.join('  ' + line + '\n' for line in body.splitlines())


def proc_main(body):
    return 'proc main():\n' + ''.join('  ' + line + '\n' for line in body.splitlines())


read, _ = check('root_read', main('file = FileIO.open("input", ro)\n'
    'data = file.read(0, 4)\necho data.length()\nfile.close()'))
check('root_write', main('file = FileIO.open("output", create)\n'
    'file.write(0, "ok")\nfile.sync()\nfile.sync(dataonly)\nfile.close()'))
check('byte_vector_write_rejected', main('file = FileIO.open("output", create)\n'
    'file.write(0, [65, 66, 67, 68])\nfile.close()'),
    'FILEIO_INVALID_PAYLOAD')
check('range_write', main('src = FileIO.open("input", ro)\n'
    'dst = FileIO.open("output", create)\ndata = src.read(0, 4096)\n'
    'dst.write(0, data)\nsrc.close()\ndst.close()'))
check('range_byte_surface', main('file = FileIO.open("input", ro)\n'
    'data = file.read(0, 16)\nfirst = data[0]\n'
    'for byte in data:\n  echo byte\n'
    'part = data.slice(1, 3)\necho first\necho part.length()\nfile.close()'))
check('borrow', 'fn use(file: FileIO) -> Int:\n  data = file.read(0, 4)\n'
    '  return data.length()\n\n' + main('file = FileIO.open("input", rw)\n'
    'echo use(file)\nfile.close()'))
check('range_borrow', 'fn size(data: Range) -> Int:\n  return data.length()\n\n' +
    main('file = FileIO.open("input", ro)\ndata = file.read(0, 4)\n'
         'echo size(data)\nfile.close()'))
check('batch_index', main('file = FileIO.open("input", ro)\n'
    'batch = file.read([(0, 4), (4, 4)])\nfirst = batch[0]\n'
    'echo first.length()\nfile.close()'))
check('batch_iteration', main('file = FileIO.open("input", ro)\n'
    'batch = file.read([(0, 4)])\nfor item in batch:\n'
    '  echo item.length()\nfile.close()'))
check('branch_closed', main('file = FileIO.open("input", ro)\n'
    'if true:\n  file.close()\nelse:\n  file.close()'))
check('main_unclosed', main('file = FileIO.open("input", ro)\necho 1'),
      'FILEIO_MUST_CLOSE')
check('main_partial_close', main('file = FileIO.open("input", ro)\n'
    'if true:\n  file.close()'), 'FILEIO_MUST_CLOSE')
check('main_all_branches_close', main('file = FileIO.open("input", ro)\n'
    'if true:\n  file.close()\nelse:\n  file.close()'))
check('proc_main_unclosed', proc_main('file = FileIO.open("input", ro)\necho 1'),
      'FILEIO_MUST_CLOSE')
check('proc_main_partial_close', proc_main('file = FileIO.open("input", ro)\n'
    'if true:\n  file.close()'), 'FILEIO_MUST_CLOSE')
check('proc_main_all_branches_close', proc_main('file = FileIO.open("input", ro)\n'
    'if true:\n  file.close()\nelse:\n  file.close()'))

domain = '''fn borrowed_read(file: FileIO) -> Int:
  data = file.read(0, 4)
  return data.length()

fn borrowed_write(file: FileIO):
  file.write(0, "ok")

fn nested_read(file: FileIO) -> Int:
  return borrowed_read(file)

domain Store:
  file: FileIO
  table = 0

  fn Open():
    file.open("input", rw)

  fn Read() -> Int:
    reply borrowed_read(file)

  fn Read2() -> Int:
    data = file.read(0, 4)
    reply data.length()

  fn ReadViaNested() -> Int:
    reply nested_read(file)

  fn Batch() -> Int:
    batch = file.read([(0, 4)])
    reply batch[0].length()

  fn Chunks():
    for chunk in file.chunks(4):
      echo chunk.length()

  fn Write():
    file.write(0, "ok")

  fn WriteViaHelper():
    borrowed_write(file)

  fn Sync():
    file.sync(dataonly)

  fn Close():
    file.close()

  fn Block():
    table = table + 1
    data = file.read(0, 4)
    echo data.length()

  fn BlockViaHelper() -> Int:
    table = table + 1
    reply borrowed_read(file)

fn main():
  store = Store()
  echo message store.Read()
'''
domain_path, diagnostics = check('domain_effects', domain)
assert any('FILEIO_BLOCKING_WITH_SHARED_WRITE' in str(d) and 'table' in str(d)
           for d in diagnostics), diagnostics
assert sum('FILEIO_BLOCKING_WITH_SHARED_WRITE' in str(d) for d in diagnostics) == 2
assert any('via borrowed_read' in str(d) for d in diagnostics), diagnostics

nested_message_source = '''domain Storage:
  file: FileIO

  fn Read():
    data = file.read(0, 4)

domain Proxy:
  counter = 0
  domainroutes(storage: Storage)

  fn Block():
    counter = counter + 1
    message storage.Read()

fn main():
  storage = Storage()
  proxy = Proxy(storage: storage)
'''
_, nested_message_diagnostics = check('nested_message_blocking_warning',
                                      nested_message_source)
assert any('FILEIO_BLOCKING_WITH_SHARED_WRITE' in str(d) and
           'counter' in str(d) and 'message' in str(d)
           for d in nested_message_diagnostics), nested_message_diagnostics
query = subprocess.run([str(compiler), 'inspect', 'main', '--source', str(domain_path), '--json'],
                       text=True, capture_output=True, check=True)
plan = json.loads(query.stdout)['result']['synchronization_plan']['domains'][0]
handlers = {handler['name']: handler for handler in plan['handlers']}
for name in ('Read', 'Read2', 'ReadViaNested', 'Batch', 'Chunks'):
    assert handlers[name]['normalized_effects'] == {'file': 'READ'}, (name, handlers[name])
    assert handlers[name]['read_only']
for name in ('Open', 'Write', 'WriteViaHelper', 'Sync', 'Close'):
    assert handlers[name]['normalized_effects'] == {'file': 'WRITE'}, (name, handlers[name])
assert handlers['Read']['class_modes'] == handlers['Read2']['class_modes']
handler_name = lambda identity: identity.split('::handler:')[-1]
conflicts = [{handler_name(c['left']), handler_name(c['right'])}
             for c in plan['conflicts']]
assert {'Read', 'Read2'} not in conflicts
for mutating in ('Open', 'Write', 'Sync', 'Close'):
    assert {mutating, 'Read'} in conflicts, (mutating, conflicts)

inferred_path, inferred_diagnostics = check('inferred_helper_effect',
    'fn leaf(file):\n  data = file.read(0, 4)\n'
    '  echo data.length()\n\n'
    'domain Store:\n  file: FileIO\n  counter: Int\n\n'
    '  fn Read():\n    leaf(file)\n\n'
    '  fn Block():\n    counter = counter + 1\n'
    '    leaf(file)\n\n' + main('store = Store()'))
assert any('FILEIO_BLOCKING_WITH_SHARED_WRITE' in str(d) and
           'counter' in str(d) for d in inferred_diagnostics), inferred_diagnostics
inferred_plan = json.loads(subprocess.run(
    [str(compiler), 'inspect', 'main', '--source', str(inferred_path), '--json'],
    text=True, capture_output=True, check=True).stdout)['result'][
        'synchronization_plan']['domains'][0]
inferred_handlers = {h['name']: h for h in inferred_plan['handlers']}
assert inferred_handlers['Read']['normalized_effects'] == {'file': 'READ'}
assert inferred_handlers['Read']['read_only']

check('file_copy', main('file = FileIO.open("input", ro)\nother = file\nfile.close()'), 'FILEIO')
check('file_move', main('file = FileIO.open("input", ro)\nother = FileIO.open("other", ro)\n'
                         'other = file\nfile.close()\nother.close()'), 'FILEIO')
check('file_return', 'fn escaped() -> FileIO:\n  file = FileIO.open("input", ro)\n'
    '  return file\n\nfn main():\n  echo 1\n', 'FILEIO')
check('file_message', 'domain Sink:\n  fn Take(file: FileIO):\n    echo 1\n\n' +
    main('sink = Sink()\nfile = FileIO.open("input", ro)\n'
         'message sink.Take(file)\nfile.close()'), 'FILEIO')
check('file_domain_store', 'domain Store:\n  file: FileIO\n'
    '  fn Bad():\n    file = FileIO.open("input", ro)\n\n' +
    main('store = Store()'), 'FILEIO')
check('file_collection', main('file = FileIO.open("input", ro)\n'
    'files = [file]\nfile.close()'), 'FILEIO')
check('file_unclosed_function', 'fn opened():\n  file = FileIO.open("input", ro)\n'
    '  echo 1\n\n' + main('opened()'), 'FILEIO_MUST_CLOSE')
check('file_unclosed_handler', 'domain Store:\n  fn Open():\n'
    '    file = FileIO.open("input", ro)\n\n' +
    main('store = Store()\nmessage store.Open()'), 'FILEIO_MUST_CLOSE')
check('range_return', 'fn escaped(file: FileIO) -> Range:\n'
    '  return file.read(0, 4)\n\n' + main('echo 1'), 'FILEIO_PINNED_OWNERSHIP')
check('range_reply', 'domain Store:\n  file: FileIO\n'
    '  fn Read() -> Range:\n    reply file.read(0, 4)\n\n' +
    main('store = Store()'), 'FILEIO_BOUNDARY_ESCAPE')
check('range_message', 'domain Sink:\n  fn Take(data: Range):\n    echo 1\n\n' +
    main('sink = Sink()\nfile = FileIO.open("input", ro)\n'
         'data = file.read(0, 4)\nmessage sink.Take(data)\nfile.close()'), 'FILEIO_BOUNDARY_ESCAPE')
check('range_domain_store', 'domain Store:\n  saved: Range\n\n' +
    main('store = Store()'), 'RANGE_STATE_ESCAPE')
check('range_collection', main('file = FileIO.open("input", ro)\n'
    'data = file.read(0, 4)\nsaved = [data]\nfile.close()'), 'FILEIO')
check('rangefinder_collection', 'type RangeFinder:\n  value: Int\n\n'
    'fn accept(items: Vector[RangeFinder]):\n  echo 1\n\n' +
    main('echo 1'))
check('fileiostats_collection', 'type FileIOStats:\n  value: Int\n\n'
    'fn accept(items: Vector[FileIOStats]):\n  echo 1\n\n' +
    main('echo 1'))
check('batch_escape', main('file = FileIO.open("input", ro)\n'
    'batch = file.read([(0, 4)])\ndata = batch[0]\n'
    'batch = file.read([(4, 4)])\necho data.length()\nfile.close()'), 'RANGE_BATCH_LIFETIME')
for operation in ('read(0, 4)', 'write(0, "ok")', 'sync()', 'close()'):
    check('closed_' + operation.split('(')[0], main('file = FileIO.open("input", rw)\n'
        'file.close()\nfile.' + operation), 'FILEIO_CLOSED_OPERATION')
check('double_open', main('file = FileIO.open("input", ro)\n'
    'file.open("again", ro)\nfile.close()'), 'FILEIO_ALREADY_OPEN')
check('domain_double_open', 'domain Store:\n  file: FileIO\n'
    '  fn Open():\n    file.open("input", rw)\n'
    '    file.open("again", rw)\n\n' + main('store = Store()'), 'FILEIO_ALREADY_OPEN')
check('domain_closed_read', 'domain Store:\n  file: FileIO\n'
    '  fn Read():\n    file.open("input", ro)\n'
    '    file.close()\n    data = file.read(0, 4)\n\n' +
    main('store = Store()'), 'FILEIO_CLOSED_OPERATION')
check('invalid_mode', main('file = FileIO.open("input", bad)\nfile.close()'),
      'FILEIO_INVALID_OPERATION')
check('negative_size', main('file = FileIO.open("input", ro)\n'
    'data = file.read(0, -1)\nfile.close()'), 'FILEIO_INVALID_BOUND')
check('zero_chunk', main('file = FileIO.open("input", ro)\n'
    'chunks = file.chunks(0)\nfile.close()'), 'FILEIO_INVALID_BOUND')
check('negative_batch_size', main('file = FileIO.open("input", ro)\n'
    'batch = file.read([(0, -1)])\nfile.close()'), 'FILEIO_INVALID_BOUND')
check('read_only_write', main('file = FileIO.open("input", ro)\n'
    'file.write(0, "ok")\nfile.close()'), 'FILEIO_READ_ONLY')
check('branch_unclosed', main('file = FileIO.open("input", ro)\n'
    'if true:\n  file.close()'), 'FILEIO_MUST_CLOSE')
check('unbounded_read', 'fn run(size: Int):\n'
    '  file = FileIO.open("input", ro)\n  data = file.read(0, size)\n'
    '  file.close()\n\n' + main('echo 1'),
    'FILEIO_UNBOUNDED_REQUEST')
check('unbounded_chunk', 'fn run(size: Int):\n'
    '  file = FileIO.open("input", ro)\n  chunks = file.chunks(size)\n'
    '  file.close()\n\n' + main('echo 1'), 'FILEIO_UNBOUNDED_REQUEST')
check('unbounded_batch_total', 'fn run(size: Int):\n'
    '  file = FileIO.open("input", ro)\n'
    '  batch = file.read([(0, size), (4096, size)])\n  file.close()\n\n' +
    main('echo 1'),
    'FILEIO_UNBOUNDED_REQUEST')
check('scalar_batch_rejected', main('file = FileIO.open("input", ro)\n'
    'batch = file.read(123)\nfile.close()'), 'FILEIO_INVALID_OPERATION')
check('malformed_batch_pair', main('file = FileIO.open("input", ro)\n'
    'batch = file.read([(0, 4, 8)])\nfile.close()'), 'FILEIO_INVALID_OPERATION')
check('string_batch_coordinate', main('file = FileIO.open("input", ro)\n'
    'batch = file.read([("zero", 4)])\nfile.close()'), 'FILEIO_INVALID_OPERATION')
check('unbounded_write_payload', 'fn run(payload: String):\n'
    '  file = FileIO.open("input", rw)\n  file.write(0, payload)\n'
    '  file.close()\n\n' + main('echo 1'), 'FILEIO_UNBOUNDED_REQUEST')
check('handler_fileio_parameter', 'domain Sink:\n  fn Take(file: FileIO):\n'
    '    echo 1\n\n' + main('sink = Sink()'), 'FILEIO_BOUNDARY_ESCAPE')
check('helper_close_propagates', 'fn finish(file: FileIO):\n  file.close()\n\n' +
    main('file = FileIO.open("input", ro)\nfinish(file)'))
check('helper_close_use_rejected', 'fn finish(file: FileIO):\n  file.close()\n\n' +
    main('file = FileIO.open("input", ro)\nfinish(file)\nfile.read(0, 4)'),
    'FILEIO_CLOSED_OPERATION')
check('handler_range_reply', 'domain Source:\n  file: FileIO\n'
    '  fn Get() -> Range:\n    reply file.read(0, 4)\n\n' +
    main('source = Source()'), 'FILEIO_BOUNDARY_ESCAPE')
_, chain_diagnostics = check('helper_chain_block_warning', 'fn leaf(file: FileIO):\n'
    '  data = file.read(0, 4)\n\nfn middle(file: FileIO):\n  leaf(file)\n\n'
    'domain Store:\n  file: FileIO\n  counter: Int\n\n'
    '  fn Block():\n    counter = counter + 1\n    middle(file)\n\n' +
    main('store = Store()'))
assert any('FILEIO_BLOCKING_WITH_SHARED_WRITE' in str(d) and
           'counter' in str(d) and 'via middle' in str(d)
           for d in chain_diagnostics), chain_diagnostics
_, echo_diagnostics = check('helper_echo_block_warning',
    'fn read_len(file: FileIO) -> Int:\n'
    '  data = file.read(0, 4)\n  return data.length()\n\n'
    'domain Store:\n  file: FileIO\n  counter: Int\n'
    '  fn Block():\n    counter = counter + 1\n'
    '    echo read_len(file)\n\n' + main('store = Store()'))
assert any('FILEIO_BLOCKING_WITH_SHARED_WRITE' in str(d) and
           'counter' in str(d) for d in echo_diagnostics), echo_diagnostics
_, second_echo_diagnostics = check('second_echo_block_warning',
    'domain Store:\n  file: FileIO\n  counter: Int\n'
    '  fn Block():\n    counter = counter + 1\n'
    '    echo 1, file.read(0, 4).length()\n\n' + main('store = Store()'))
assert any('FILEIO_BLOCKING_WITH_SHARED_WRITE' in str(d) and
           'counter' in str(d) for d in second_echo_diagnostics), second_echo_diagnostics
check('negative_batch_offset', main('file = FileIO.open("input", ro)\n'
    'batch = file.read([(-1, 4)])\nfile.close()'), 'FILEIO_INVALID_BOUND')
check('negative_chunk', main('file = FileIO.open("input", ro)\n'
    'chunks = file.chunks(-4)\nfile.close()'), 'FILEIO_INVALID_BOUND')

check('dynamic_read_offset', 'fn read_at(file: FileIO, offset: Int):\n'
    '  data = file.read(offset, 4096)\n  echo data.length()\n\n' +
    main('file = FileIO.open("input", ro)\nread_at(file, 20)\nfile.close()'))
check('dynamic_write_offset', 'fn write_at(file: FileIO, offset: Int):\n'
    '  file.write(offset, "abcd")\n\n' +
    main('file = FileIO.open("input", rw)\nwrite_at(file, 20)\nfile.close()'))
check('constant_expression_size', main('file = FileIO.open("input", ro)\n'
    'data = file.read(0, 1024 * 1024)\nfile.close()'))
check('bound_local_size_and_payload', main('file = FileIO.open("input", rw)\n'
    'size = 1024 * 2\npayload = "abcd"\n'
    'data = file.read(0, size)\nfile.write(1, payload)\nfile.close()'))
check('computed_bounded_string_payload', main(
    'payload = "ab" + "cd"\nfile = FileIO.open("output", create)\n'
    'file.write(0, payload)\nfile.close()'))
check('bound_helper_final_expression', 'fn writer(file: FileIO):\n'
    '  payload = "abcd"\n  file.write(0, payload)\n\n' +
    main('file = FileIO.open("input", rw)\nwriter(file)\nfile.close()'))
check('negative_dynamic_expression_offset', main('file = FileIO.open("input", ro)\n'
    'data = file.read(0 - 1, 4)\nfile.close()'), 'FILEIO_INVALID_BOUND')
check('anonymous_fileio', 'fn consume(file: FileIO):\n  file.close()\n\n' +
    main('consume(FileIO.open("input", ro))'), 'FILEIO')
check('parenthesized_fileio_owner', main('file = (FileIO.open("input", ro))\n'
    'data = file.read(0, 4)\nfile.close()'))
check('parenthesized_fileio_owner_static_write_bound', main(
    'payload = "abcd"\nfile = (FileIO.open("output", create))\n'
    'file.write(0, payload)\nfile.close()'))
check('parenthesized_fileio_owner_chunk_bound', main(
    'file = ((FileIO.open("input", ro)))\n'
    'for chunk in file.chunks(4):\n  echo chunk.length()\n'
    'file.close()'))
check('anonymous_fileio_in_echo', 'fn wrapper(file: FileIO) -> Int:\n'
    '  file.close()\n  return 1\n\n' +
    main('echo wrapper((FileIO.open("input", ro)))'), 'FILEIO_PINNED_OWNERSHIP')
check('nested_helper_close', 'fn finish(file: FileIO):\n  file.close()\n\n'
    'fn finish2(file: FileIO):\n  finish(file)\n\n' +
    main('file = FileIO.open("input", ro)\nfinish2(file)'))
check('inferred_helper_close', 'fn finish(file):\n  file.close()\n\n' +
    main('file = FileIO.open("input", ro)\nfinish(file)'))
check('inferred_helper_use_after_close', 'fn finish(file):\n  file.close()\n\n' +
    main('file = FileIO.open("input", ro)\nfinish(file)\n'
         'data = file.read(0, 4)'), 'FILEIO_CLOSED_OPERATION')
check('helper_close_in_echo', 'fn finish(file: FileIO) -> Int:\n'
    '  file.close()\n  return 1\n\n' +
    main('file = FileIO.open("input", ro)\necho finish(file)'))
check('helper_close_in_nested_call', 'fn finish(file: FileIO) -> Int:\n'
    '  file.close()\n  return 1\n\n'
    'fn identity(value: Int) -> Int:\n  return value\n\n' +
    main('file = FileIO.open("input", ro)\necho identity(finish(file))'))
check('helper_close_in_condition', 'fn finish(file: FileIO) -> Int:\n'
    '  file.close()\n  return 1\n\n' +
    main('file = FileIO.open("input", ro)\n'
         'if finish(file) == 1:\n  echo 1'))
check('closed_file_nested_view', main('file = FileIO.open("input", ro)\n'
    'file.close()\necho file.read(0, 4).length()'),
    'FILEIO_CLOSED_OPERATION')
check('nested_helper_use_after_close', 'fn finish(file: FileIO):\n'
    '  file.close()\n\nfn finish2(file: FileIO):\n  finish(file)\n\n' +
    main('file = FileIO.open("input", ro)\nfinish2(file)\n'
         'data = file.read(0, 4)'), 'FILEIO_CLOSED_OPERATION')
check('helper_reopens', 'fn reopen(file: FileIO):\n'
    '  file.close()\n  file.open("other", ro)\n\n' +
    main('file = FileIO.open("input", ro)\nreopen(file)\n'
         'data = file.read(0, 4)\nfile.close()'))
check('helper_reopen_mode', 'fn reopen(file: FileIO):\n'
    '  file.close()\n  file.open("other", ro)\n\n' +
    main('file = FileIO.open("input", rw)\nreopen(file)\n'
         'file.write(0, "a")\nfile.close()'), 'FILEIO_READ_ONLY')
check('helper_read_preserves_open', 'fn inspect(file: FileIO):\n'
    '  data = file.read(0, 4)\n  echo data.length()\n\n' +
    main('file = FileIO.open("input", ro)\ninspect(file)\n'
         'data = file.read(0, 4)\nfile.close()'))
check('helper_conditional_close', 'fn maybe_finish(file: FileIO, flag: Bool):\n'
    '  if flag:\n    file.close()\n\n' +
    main('file = FileIO.open("input", ro)\nmaybe_finish(file, true)\n'
         'data = file.read(0, 4)\nfile.close()'), 'FILEIO_UNKNOWN_STATE')
check('helper_conditional_close_exit', 'fn maybe_finish(file: FileIO, flag: Bool):\n'
    '  if flag:\n    file.close()\n\n' +
    main('file = FileIO.open("input", ro)\nmaybe_finish(file, true)'),
    'FILEIO_MUST_CLOSE')
check('conditional_domain_open', 'domain Store:\n  file: FileIO\n'
    '  fn Maybe(flag: Bool):\n'
    '    if flag:\n      file.open("input", ro)\n'
    '    data = file.read(0, 4)\n\n' +
    main('store = Store()'), 'FILEIO_UNKNOWN_STATE')
check('conditional_mode_join', main('file = FileIO.open("input", rw)\n'
    'file.close()\nif true:\n  file.open("input", ro)\n'
    'else:\n  file.open("input", rw)\n'
    'file.write(0, "a")\nfile.close()'), 'FILEIO_READ_ONLY')

request_type = 'type Request:\n  offset: Int\n  size: Int\n\n'
check('computed_bounded_batch', request_type + 'fn read_at(offset: Int):\n'
    '  file = FileIO.open("input", ro)\n'
    '  requests = [Request(offset: offset, size: 4), '
    'Request(offset: offset + 4, size: 8)]\n'
    '  batch = file.read(requests)\n  echo batch[0].length()\n'
    '  file.close()\n\n' + main('read_at(0)'))
check('unbounded_batch_count_record', request_type +
    'fn run(requests: Vector[Request]):\n'
    '  file = FileIO.open("input", ro)\n'
    '  batch = file.read(requests)\n  file.close()\n\n' + main('echo 1'),
    'FILEIO_UNBOUNDED_REQUEST')
check('unbounded_batch_entry_record', request_type +
    'fn run(size: Int):\n  file = FileIO.open("input", ro)\n'
    '  requests = [Request(offset: 0, size: size)]\n'
    '  batch = file.read(requests)\n  file.close()\n\n' + main('echo 1'),
    'FILEIO_UNBOUNDED_REQUEST')
check('mutated_batch_loses_bound', request_type +
    'fn add(requests: Vector[Request]):\n'
    '  requests.push(Request(offset: 4, size: 4))\n\n' +
    main('file = FileIO.open("input", ro)\n'
         'requests = [Request(offset: 0, size: 4)]\n'
    'ignored = add(requests)\nbatch = file.read(requests)\n'
    'file.close()'), 'FILEIO_UNBOUNDED_REQUEST')
check('echo_second_arg_mutates_batch', request_type +
    'fn grow(requests: Vector[Request], count: Int) -> Int:\n'
    '  i = 0\n  while i < count:\n'
    '    requests.push(Request(offset: i * 4, size: 4))\n'
    '    i = i + 1\n  return 0\n\n' +
    main('file = FileIO.open("input", ro)\n'
         'requests = [Request(offset: 0, size: 4)]\n'
         'echo 1, grow(requests, 2)\nbatch = file.read(requests)\n'
         'file.close()'), 'FILEIO_UNBOUNDED_REQUEST')
check('echo_second_arg_nonmutating_preserves_bound', request_type +
    'fn value() -> Int:\n  return 2\n\n' +
    main('file = FileIO.open("input", ro)\n'
         'requests = [Request(offset: 0, size: 4)]\n'
         'echo 1, value()\nbatch = file.read(requests)\nfile.close()'))
check('echo_mutation_through_local_alias_loses_bound', request_type +
    'fn grow(requests: Vector[Request]):\n'
    '  requests.push(Request(offset: 4, size: 4))\n\n' +
    main('file = FileIO.open("input", ro)\n'
         'requests = [Request(offset: 0, size: 4)]\n'
         'alias = requests\necho 1, grow(alias)\n'
    'batch = file.read(requests)\nfile.close()'),
    'FILEIO_UNBOUNDED_REQUEST')
check('echo_mutation_through_transitive_local_alias_loses_bound', request_type +
    'fn grow(requests: Vector[Request]):\n'
    '  requests.push(Request(offset: 4, size: 4))\n\n' +
    main('file = FileIO.open("input", ro)\n'
         'requests = [Request(offset: 0, size: 4)]\n'
         'a = requests\nb = a\necho 1, grow(b)\n'
         'batch = file.read(requests)\nfile.close()'),
    'FILEIO_UNBOUNDED_REQUEST')

check('fileio_duplicate_close_read_alias_rejected',
    'fn finish(a: FileIO, b: FileIO):\n'
    '  a.close()\n  data = b.read(0, 4)\n\n' +
    main('file = FileIO.open("input", ro)\nfinish(file, file)'),
    'OWNERSHIP_CONFLICTING_ACCESS')
check('fileio_duplicate_write_read_alias_rejected',
    'fn change(a: FileIO, b: FileIO):\n'
    '  a.write(0, "x")\n  data = b.read(0, 4)\n\n' +
    main('file = FileIO.open("input", rw)\nchange(file, file)\nfile.close()'),
    'OWNERSHIP_CONFLICTING_ACCESS')
check('fileio_duplicate_read_alias_allowed',
    'fn read_both(a: FileIO, b: FileIO):\n'
    '  left = a.read(0, 4)\n  right = b.read(4, 4)\n\n' +
    main('file = FileIO.open("input", ro)\nread_both(file, file)\nfile.close()'))
check('batch_total_overflow', main('file = FileIO.open("input", ro)\n'
    'batch = file.read([(0, 9223372036854775807), '
    '(0, 9223372036854775807), (0, 9223372036854775807)])\n'
    'file.close()'), 'FILEIO_UNBOUNDED_REQUEST')
for name, field_type in (('vector_range', 'Vector[Range]'),
                         ('map_range', 'Map[Int, Range]'),
                         ('vector_batch', 'Vector[RangeBatch]'),
                         ('option_range', 'Option[Range]')):
    check('nested_object_' + name,
          'type Box:\n  value: ' + field_type + '\n\n' + main('echo 1'),
          'FILEIO_COLLECTION_ESCAPE')
check('object_containing_range_domain', 'type Box:\n  value: Range\n\n'
    'domain Store:\n  box: Box\n\n' + main('store = Store()'),
    'FILEIO_COLLECTION_ESCAPE')
check('multiple_batch_views', main('file = FileIO.open("input", ro)\n'
    'batch = file.read([(0, 4), (4, 4)])\n'
    'left = batch[0]\nright = batch[1]\n'
    'echo left.length()\necho right.length()\nfile.close()'))
check('batch_index_return', 'fn escape(file: FileIO) -> Range:\n'
    '  batch = file.read([(0, 4)])\n  return batch[0]\n\n' +
    main('echo 1'), 'FILEIO_PINNED_OWNERSHIP')
check('batch_index_collection', main('file = FileIO.open("input", ro)\n'
    'batch = file.read([(0, 4)])\nitems = [batch[0]]\nfile.close()'),
    'FILEIO_COLLECTION_ESCAPE')
check('batch_index_reply', 'domain Store:\n  file: FileIO\n'
    '  fn Bad() -> Range:\n'
    '    batch = file.read([(0, 4)])\n    reply batch[0]\n\n' +
    main('store = Store()'), 'FILEIO_BOUNDARY_ESCAPE')

native = subprocess.run([str(compiler), '--check', str(out / 'file_copy.moss')],
                        text=True, capture_output=True)
fast_debug = subprocess.run([str(compiler), 'debug', str(out / 'file_copy.moss')],
                            text=True, capture_output=True)
assert native.returncode == fast_debug.returncode == 1
assert '[FILEIO_PINNED_OWNERSHIP]' in native.stderr
assert '[FILEIO_PINNED_OWNERSHIP]' in fast_debug.stderr
for name, code in (('anonymous_fileio', 'FILEIO_PINNED_OWNERSHIP'),
                   ('anonymous_fileio_in_echo', 'FILEIO_PINNED_OWNERSHIP'),
                   ('unbounded_read', 'FILEIO_UNBOUNDED_REQUEST'),
                   ('echo_second_arg_mutates_batch', 'FILEIO_UNBOUNDED_REQUEST'),
                   ('helper_close_use_rejected', 'FILEIO_CLOSED_OPERATION'),
                   ('batch_index_collection', 'FILEIO_COLLECTION_ESCAPE'),
                   ('conditional_domain_open', 'FILEIO_UNKNOWN_STATE'),
                   ('fileio_duplicate_close_read_alias_rejected',
                    'OWNERSHIP_CONFLICTING_ACCESS'),
                   ('fileio_duplicate_write_read_alias_rejected',
                    'OWNERSHIP_CONFLICTING_ACCESS'),
                   ('byte_vector_write_rejected', 'FILEIO_INVALID_PAYLOAD'),
                   ('echo_second_arg_mutates_batch',
                    'FILEIO_UNBOUNDED_REQUEST'),
                   ('echo_mutation_through_local_alias_loses_bound',
                    'FILEIO_UNBOUNDED_REQUEST'),
                   ('echo_mutation_through_transitive_local_alias_loses_bound',
                    'FILEIO_UNBOUNDED_REQUEST')):
    source = str(out / (name + '.moss'))
    native = subprocess.run([str(compiler), '--check', source],
                            text=True, capture_output=True)
    fast_debug = subprocess.run([str(compiler), 'debug', source],
                                text=True, capture_output=True)
    assert native.returncode == fast_debug.returncode == 1, name
    assert code in native.stderr and code in fast_debug.stderr, (
        name, native.stderr, fast_debug.stderr)
print('Phase 20 FileIO semantics: positive, negative, effects, and warning checks passed')
