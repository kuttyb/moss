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


read, _ = check('root_read', main('file = FileIO.open("input", ro)\n'
    'data = file.read(0, 4)\necho data.length()\nfile.close()'))
check('root_write', main('file = FileIO.open("output", create)\n'
    'file.write(0, "ok")\nfile.sync()\nfile.sync(dataonly)\nfile.close()'))
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

domain = '''fn borrowed_read(file: FileIO) -> Int:
  data = file.read(0, 4)
  return data.length()

fn borrowed_write(file: FileIO):
  file.write(0, "ok")

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
query = subprocess.run([str(compiler), 'inspect', 'main', '--source', str(domain_path), '--json'],
                       text=True, capture_output=True, check=True)
plan = json.loads(query.stdout)['result']['synchronization_plan']['domains'][0]
handlers = {handler['name']: handler for handler in plan['handlers']}
for name in ('Read', 'Read2', 'Batch', 'Chunks'):
    assert handlers[name]['normalized_effects'] == {'file': 'READ'}, (name, handlers[name])
    assert handlers[name]['read_only']
for name in ('Open', 'Write', 'WriteViaHelper', 'Sync', 'Close'):
    assert handlers[name]['normalized_effects'] == {'file': 'WRITE'}, (name, handlers[name])
assert handlers['Read']['class_modes'] == handlers['Read2']['class_modes']
assert not any({c['left'].split('.')[-1], c['right'].split('.')[-1]} == {'Read', 'Read2'}
               for c in plan['conflicts'])

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
native = subprocess.run([str(compiler), '--check', str(out / 'file_copy.moss')],
                        text=True, capture_output=True)
fast_debug = subprocess.run([str(compiler), 'debug', str(out / 'file_copy.moss')],
                            text=True, capture_output=True)
assert native.returncode == fast_debug.returncode == 1
assert '[FILEIO_PINNED_OWNERSHIP]' in native.stderr
assert '[FILEIO_PINNED_OWNERSHIP]' in fast_debug.stderr
print('Phase 20 FileIO semantics: positive, negative, effects, and warning checks passed')
