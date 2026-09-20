#!/usr/bin/env python3
"""Synchronous interpreter equivalence, semantic traces, and retired runtime absence."""
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys

repo = Path(__file__).resolve().parents[2]
compiler = Path(sys.argv[1]).resolve()
out = Path(sys.argv[2]).resolve()
out.mkdir(parents=True, exist_ok=True)


def run(args, *, cwd=None, native=True, expected=0):
    env = dict(os.environ)
    if not native:
        env['RUSTC'] = '/no-native-moss-in-fast-debug'
    p = subprocess.run(list(map(str, args)), cwd=cwd, env=env, text=True,
                       capture_output=True, timeout=90)
    assert p.returncode == expected, (args, p.returncode, p.stdout, p.stderr)
    return p


def equivalent(source, expected):
    source = repo / source
    rust = out / (source.stem + '.rs')
    run([compiler, source, '-o', rust])
    text = rust.read_text()
    # Inspect implementation constructs, not historical documentation/comments.
    implementation = '\n'.join(line for line in text.splitlines() if not line.lstrip().startswith('//'))
    assert not re.search(r'Moss(?:Sender|Receiver|Channel|Tracker|Cluster)|thread::spawn|Condvar|mpsc::|_locked\(|_local\(|AtomicI64|AtomicBool|Arc<(?:Mutex|RwLock)<\w+State', implementation)
    assert 'self.state.enter(' in text and '__frame.read(' in text
    binary = rust.with_suffix('')
    run(['rustc', '-D', 'warnings', rust, '-o', binary])
    production = run([binary]).stdout
    interpreted = run([compiler, 'run', '--interp', '--trace', source], native=False)
    assert production == interpreted.stdout == expected, (source, production, interpreted.stdout)
    repeated = run([compiler, 'run', '--interp', '--trace', source], native=False)
    assert interpreted.stderr == repeated.stderr
    trace = [json.loads(line) for line in interpreted.stderr.splitlines()]
    stack = []
    for event in trace:
        assert event['source_file'] and event['semantic_identity']
        assert not any(key in event for key in ('class_rank', 'class_set', 'lock', 'thread', 'address'))
        if event['event'] in ('message_call', 'handler_enter', 'handler_exit', 'message_return', 'reply'):
            assert event['instance'] and event['specialization'] and event['handler']
        if event['event'] == 'handler_enter':
            stack.append((event['instance'], event['handler']))
        elif event['event'] == 'handler_exit':
            assert stack.pop() == (event['instance'], event['handler'])
    assert not stack
    return trace


trace = equivalent('tests/phase106e_domains.moss', '2\n16\ninitial 15\n7 99\n51\n')
assert len([e for e in trace if e['event'] == 'domain_instance']) == 2
writes = [e for e in trace if e['event'] == 'state_write']
assert any(e['path'] == 'value' and e['before'] == '0' and e['after'] == '1' for e in writes)
assert any(e['path'] == 'record.score' and e['before'] == '12' and e['after'] == '15' for e in writes)
assert not any(e.get('after') == '999' for e in trace)
assert any(e['event'] == 'state_read' and e['path'] == 'record.score' for e in trace)
assert len([e for e in trace if e['event'] == 'reply' and e['handler'] == 'Inc']) == 1
nested = equivalent('tests/phase106d_nested.moss', '4\n8\n')
entries = [e['instance'] for e in nested if e['event'] == 'handler_enter']
assert entries == ['main::root', 'main::sibling', 'main::branch', 'main::child'] * 2
# Concrete generic instances and ordinary WRITE helpers use checked semantics.
equivalent('tests/phase106d_specializations.moss', '11 22\n')
write = run([compiler, 'run', '--interp', repo / 'tests/phase106d_parameter_write.moss'], native=False)
assert write.stdout == '5 true 1.5 11\n7 8\n'
# Rejection belongs to the checker, before interpreter execution.
invalid = run([compiler, 'run', '--interp', repo / 'tests/negative/phase26_payload_write.moss'], native=False, expected=1)
assert 'cannot WRITE incoming message payload' in invalid.stderr
for flag in ('--cluster=Store,Other', '--no-await-error-handling'):
    p = subprocess.run([str(compiler), flag, str(repo / 'tests/phase106e_domains.moss')], capture_output=True, text=True)
    assert p.returncode != 0 and 'unknown option' in p.stderr, p.stderr
# No remaining alternate backend/await IR or analysis implementation.
implementation = '\n'.join(path.read_text() for path in (repo / 'src').iterdir() if path.suffix in ('.hpp', '.inc', '.cpp'))
assert not re.search(r'\b(?:DomainLowering|BackendOptimizer|AwaitBoundary|SemanticAwaitSite|SemanticAwaitEdge|BatchedSendRegion|CoalescedLockRegion|AtomicHandlerPlan)\b|Stmt::Kind::AwaitMessage|check_global_await_cycles|gen_shared_channel|gen_direct_domain|gen_atomic_domain|gen_cluster', implementation)

project = out / 'modules'
if project.exists():
    shutil.rmtree(project)
(project / 'src').mkdir(parents=True)
(project / 'moss.toml').write_text('[project]\nname = "domain_debug"\nversion = "0.1.0"\n')
(project / 'src/detail.moss').write_text('''module detail
export fn bump(value: Int):
  value = value + 1
export domain Counter:
  value: Int
  fn Next() -> Int:
    bump(value)
    reply value
''')
(project / 'src/service.moss').write_text('''module service
import detail
export domain App:
  domainroutes(counter: detail.Counter)
  fn Run() -> Int:
    reply message counter.Next()
''')
(project / 'src/main.moss').write_text('''module app
import service
import detail
fn main():
  client = service.App(counter: counter)
  counter = detail.Counter(value: 4)
  echo message client.Run()
  echo message client.Run()
''')
for command in (['debug', 'app'], ['run', '--interp', '--trace', 'src/main.moss']):
    result = run([compiler, *command], cwd=project, native=False)
    assert result.stdout == '5\n6\n', result.stdout
result = json.loads(run([compiler, 'build', '--json'], cwd=project).stdout)['result']
assert run([result['artifacts']['executable']]).stdout == '5\n6\n'
(project / 'src/detail.moss').rename(project / 'src/detail.hidden')
rejected = run([compiler, 'run', '--interp', 'src/main.moss'], cwd=project, expected=1)
assert 'source' in rejected.stderr and ('compiled' in rejected.stderr or '.mossi' in rejected.stderr), rejected.stderr
print('10.6E direct production/interpreter equivalence, exact routes/specializations, nested ordering, value independence, state WRITE-through, deterministic semantic trace, source-module closure, source-free rejection, and legacy retirement checks passed.')
