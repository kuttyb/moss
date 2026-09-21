#!/usr/bin/env python3
"""10.6D.1: runtime borrow lifetimes, no clones, and actual generated object views."""
import json
from pathlib import Path
import re
import shutil
import subprocess
import sys

repo = Path(__file__).resolve().parents[2]
compiler = Path(sys.argv[1]).resolve()
out = Path(sys.argv[2]).resolve()
out.mkdir(parents=True, exist_ok=True)


def run(args, *, cwd=None, expected=0):
    p = subprocess.run(list(map(str, args)), cwd=cwd, capture_output=True, text=True, timeout=90)
    assert p.returncode == expected, (args, p.returncode, p.stdout, p.stderr)
    return p.stdout


runtime = (repo / 'src/handler_runtime.hpp').read_text().split('R"RUST(', 1)[1].split(')RUST"', 1)[0]
assert 'V: Clone' not in runtime and '.clone()' not in runtime
assert 'moss_read_or_abort<T>' in runtime and 'MossClassRuntime' not in runtime
preamble = '#![allow(dead_code, unused_variables)]\n'
harness = r'''
use std::sync::atomic::{AtomicUsize, Ordering};
static CLONES: AtomicUsize = AtomicUsize::new(0);
struct NonClone { text: String }
#[derive(Debug)]
struct Probe { text: String }
impl Clone for Probe {
    fn clone(&self) -> Self {
        CLONES.fetch_add(1, Ordering::SeqCst);
        Self { text: self.text.clone() }
    }
}
impl Probe { fn score(&self) -> usize { self.text.len() } }
fn inspect(value: &Probe) -> usize { value.score() }
fn main() {
    let protected = std::sync::RwLock::new(NonClone { text: "original".into() });
    let immutable = NonClone { text: "immutable".into() };
    {
        let guard = moss_read_or_abort(&protected);
        let value = MossRead(&guard.text);
        assert_eq!(&**value, "original");
        assert_eq!(immutable.text, "immutable");
        assert!(protected.try_write().is_err());
    }
    let protected = std::sync::RwLock::new(Probe { text: "x".repeat(100_000) });
    let fixed = Probe { text: "y".repeat(100_000) };
    let guard = moss_read_or_abort(&protected);
    for _ in 0..20 {
        let value = MossRead(&*guard);
        assert_eq!(value.text.len(), 100_000);
        assert_eq!(value.score(), 100_000);
        assert_eq!(inspect(&value), 100_000);
        assert_eq!(fixed.score(), 100_000);
    }
    assert_eq!(CLONES.load(Ordering::SeqCst), 0);
    let reply = guard.clone();
    assert_eq!(CLONES.load(Ordering::SeqCst), 1);
    assert_ne!(reply.text.as_ptr(), guard.text.as_ptr());
    drop(guard);
    assert!(protected.try_write().is_ok());
}
'''
(out / 'runtime.rs').write_text(preamble + runtime + harness)
run(['rustc', '-D', 'warnings', out / 'runtime.rs', '-o', out / 'runtime'])
run([out / 'runtime'])
escape = r'''
fn main() {
    let runtime = std::sync::RwLock::new(String::from("value"));
    let borrowed;
    { let guard = moss_read_or_abort(&runtime); borrowed = &*guard; }
    println!("{:?}", borrowed);
}
'''
(out / 'escape.rs').write_text(preamble + runtime + escape)
p = subprocess.run(['rustc', '-D', 'warnings', str(out / 'escape.rs'), '-o', str(out / 'escape')], text=True, capture_output=True)
assert p.returncode != 0 and 'does not live long enough' in p.stderr, p.stderr

source = repo / 'tests/phase106d1_borrowed_reads.moss'
rust = out / 'reads.rs'
run([compiler, source, '-o', rust])
text = rust.read_text()
source_map = json.loads(rust.with_suffix('.mossmap').read_text())
identities = [entry['semantic_identity'] for entry in source_map['entries']]
assert len(identities) == len(set(identities))
helper_map = next(entry for entry in source_map['entries'] if entry['generated_symbol'] == 'inspect_record')
mapped = helper_map['line_mappings']
assert len(mapped) > len({item['moss_line'] for item in mapped})

run(['rustc', '-D', 'warnings', rust, '-o', out / 'reads'])
assert run([out / 'reads']).strip() == '31 13 31 12 39\n13 31'
assert 'V: Clone' not in text and 'Default::default()' not in text
assert not re.search(r'#\[derive\(Clone\)\]\s*enum \w+Leaf', text)
assert 'moss_read_or_abort(&self.state.class' in text and 'MossClassRuntime' not in text
assert 'value: &impl MossAccess_Record' in text
# Phase 15.1 lends the decomposed state view to the internal synchronous
# handler instead of reconstructing an owned Record at this message boundary.
assert 'Accept_shared(&(state.record))' in text
assert 'Some((state.record).__moss_value())' in text
assert 'unsafe' not in text and 'dyn ' not in text
repeat = out / 'repeat.rs'
run([compiler, source, '-o', repeat])
assert repeat.read_text() == text
plan_data = json.loads(run([compiler, 'inspect', 'main', '--source', source, '--json']))['result']['synchronization_plan']
store_plan = next(d for d in plan_data['domains'] if d['domain'] == 'Store')
handlers = {h['name']: h for h in store_plan['handlers']}
assert handlers['Immutable']['class_set'] == []
assert handlers['Mixed']['write_set'] == ['total']
# Observe pointer identity in the actual generated wrapper, its helper, and its
# static method body. A handler-entry String/Vector snapshot fails these checks.
probe = r'''
thread_local! { static EXPECTED_READ_POINTERS: std::cell::RefCell<Option<(usize,usize)>> = const { std::cell::RefCell::new(None) }; }
fn check_read_pointers(name: &String, values: &Vec<i64>) {
    EXPECTED_READ_POINTERS.with(|expected| {
        if let Some((n,v)) = *expected.borrow() {
            assert_eq!(name.as_ptr() as usize, n);
            assert_eq!(values.as_ptr() as usize, v);
        }
    });
}
'''
needle = next(line for line in text.splitlines() if line.startswith('fn __moss_body_Store_Read('))
assert needle in text
instrumented = text.replace(needle, needle + '\ncheck_read_pointers(state.record.__moss_field_profile().__moss_field_name(), state.record.__moss_field_profile().__moss_field_values());', 1)
needle = next(line for line in text.splitlines() if line.startswith('fn __moss_body_Reader_Accept('))
assert needle in instrumented
instrumented = instrumented.replace(needle, needle + "\nEXPECTED_READ_POINTERS.with(|expected| { if let Some((n,v)) = *expected.borrow() { assert_eq!(value.__moss_field_profile().__moss_field_name().as_ptr() as usize, n); assert_eq!(value.__moss_field_profile().__moss_field_values().as_ptr() as usize, v); } });", 1)
needle = 'fn __moss_view_inspect_record(value: &impl MossAccess_Record) -> i64 {'
assert needle in instrumented
instrumented = instrumented.replace(needle, needle + '\ncheck_read_pointers(value.__moss_field_profile().__moss_field_name(), value.__moss_field_profile().__moss_field_values());')
start = instrumented.index('trait MossAccess_Record')
method = instrumented.index('fn score(&self) -> i64 {', start) + len('fn score(&self) -> i64 {')
instrumented = instrumented[:method] + '\ncheck_read_pointers(self.__moss_field_profile().__moss_field_name(), self.__moss_field_profile().__moss_field_values());' + instrumented[method:]
constructors = '\n'.join(line for line in text[text.index('fn main() {'):].splitlines() if ' = construct_' in line)
# Inflate actual domain state, without a benchmark or new Moss syntax.
constructors = constructors.replace('"large".to_string()', '"x".repeat(100_000)').replace('vec![1_i64, 2_i64, 3_i64]', 'vec![1_i64; 100_000]')
# Resolve physical fields from the authoritative plan in the test generator.
def stored(path):
    cls = next(c['class_rank'] for c in store_plan['sync_classes'] if path in c['member_leaves'])
    field = '__moss_storage_' + ''.join(str(len(part)) + '_' + part for part in path.split('.'))
    return f'store.state.class{cls}.read().unwrap().{field}'
test = r'''
#[test]
fn original_storage() {
    CONSTRUCTORS
    let name_pointer = NAME.as_ptr() as usize;
    let values_pointer = VALUES.as_ptr() as usize;
    EXPECTED_READ_POINTERS.with(|expected| *expected.borrow_mut() = Some((name_pointer, values_pointer)));
    assert_eq!(store.Read_shared(), Some(200011));
    assert_eq!(store.Mixed_shared(), Some(100003));
    assert_eq!(store.Immutable_shared(), Some(31));
    assert_eq!(store.Forward_shared(), Some(300009));
    EXPECTED_READ_POINTERS.with(|expected| *expected.borrow_mut() = None);
    let snapshot = store.Snapshot_shared().unwrap();
    assert_ne!(snapshot.profile.name.as_ptr(), NAME.as_ptr());
    store.Change_shared();
    assert_eq!(snapshot.profile.name.len(), 100_000);
}
'''.replace('CONSTRUCTORS', constructors).replace('NAME', stored('record.profile.name')).replace('VALUES', stored('record.profile.values'))
rust.write_text(instrumented + probe + test)
run(['rustc', '--test', '-D', 'warnings', rust, '-o', out / 'original-storage'])
run([out / 'original-storage'])
print('10.6D.1 non-Clone runtime, clone counter, borrow lifetime, original String/Vector storage, whole/nested object helper/method, mixed/immutable/pipeline and explicit boundary checks passed.')

projection = out / 'projection.rs'
run([compiler, repo / 'tests/phase106d1_read_projection.moss', '-o', projection])
run(['rustc', '-D', 'warnings', projection, '-o', out / 'projection'])
assert run([out / 'projection']).strip() == 'original\noriginal\nindexed original\nindexed original\n24\noriginal\n8\n5\n17\nindexed original'
projected = projection.read_text()
assert '__moss_view___moss_specialize_generic_score_' in projected
assert 'reader.__moss_view_inspect(' in projected
indexed_read = next(line for line in projected.splitlines() if 'echo_text(&(' in line)
assert '.clone()' not in indexed_read
assert not re.search(r'println!.*state.strings.*clone', projected)
print('10.6D.1 generic/method-argument views, indexed READ/WRITE, and captured filter/map/reduce checks passed.')

project = out / 'modules'
if project.exists():
    shutil.rmtree(project)
(project / 'src').mkdir(parents=True)
(project / 'moss.toml').write_text('[project]\nname = "borrowed"\nversion = "0.1.0"\n')
provider = project / 'src/provider.moss'
provider.write_text('''module provider
export type Record:
  title: String
  values: Vector[Int]
  fn score() -> Int:
    echo title
    return values |> sum
export fn make() -> Record:
  return Record(title: "provider", values: [2, 3])
export fn inspect(value: Record) -> Int:
  return value.score()
export domain Bank:
  record: Record
  fn Read() -> Int:
    reply inspect(record)
  fn Change():
    record.title = "changed"
    record.values = [11]
''')
application = project / 'src/main.moss'
application.write_text('''module app
import provider
domain Client:
  record: provider.Record
  fn Read() -> Int:
    reply provider.inspect(record)
fn main():
  bank = provider.Bank(record: provider.make())
  client = Client(record: provider.make())
  echo message bank.Read(), message client.Read()
  message bank.Change()
  echo message bank.Read()
''')
for source_free in (False, True):
    if source_free:
        provider.rename(provider.with_suffix('.hidden'))
        application.write_text(application.read_text().replace('echo message', 'echo 1\n  echo message', 1))
    result = json.loads(run([compiler, 'build', '--json'], cwd=project))['result']
    expected = ('1\n' if source_free else '') + 'provider\nprovider\n5 5\nchanged\n11'
    assert run([result['artifacts']['executable']]).strip() == expected
    interface = (project / 'build/debug/provider.mossi').read_text()
    assert all(word not in interface for word in ('MossSlot', 'MossAccess', 'View<', 'class_rank', 'MossPhysicalPlan'))
print('10.6D.1 source/source-free provider helpers, methods, and domain views passed; no physical borrow metadata in .mossi.')
