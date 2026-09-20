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
assert 'fn read(&self,' in runtime and 'Option<&V>' in runtime
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
fn plan() -> MossPhysicalPlan {
    ("instance", 0, vec![vec!["protected"]], vec![
        ("read", vec![(0,false)], vec!["protected","fixed"], vec![], "read"),
        ("write", vec![(0,true)], vec![], vec!["protected"], "write"),
    ])
}
fn main() {
    let mut values = MossLeaves::new();
    values.insert("protected", NonClone { text: "original".into() });
    values.insert("fixed", NonClone { text: "immutable".into() });
    let state = MossClassRuntime::new(plan(), values);
    {
        let frame = state.enter("read");
        assert_eq!(frame.read("protected").unwrap().text, "original");
        assert!(std::ptr::eq(frame.read("fixed").unwrap(), &state.immutable["fixed"]));
        assert!(frame.read("unused").is_none());
        assert!(state.classes[0].try_write().is_err());
    }
    let mut values = MossLeaves::new();
    values.insert("protected", Probe { text: "x".repeat(100_000) });
    values.insert("fixed", Probe { text: "y".repeat(100_000) });
    let state = MossClassRuntime::new(plan(), values);
    let frame = state.enter("read");
    for _ in 0..20 {
        let value = MossSlot::Read(frame.read("protected").unwrap());
        assert_eq!(value.text.len(), 100_000);
        assert_eq!(value.score(), 100_000);
        assert_eq!(inspect(&value), 100_000);
        assert_eq!(frame.read("fixed").unwrap().score(), 100_000);
    }
    assert_eq!(CLONES.load(Ordering::SeqCst), 0);
    // An explicit independent-value boundary can clone; ordinary READ never did.
    let reply = frame.read("protected").unwrap().clone();
    assert_eq!(CLONES.load(Ordering::SeqCst), 1);
    assert_ne!(reply.text.as_ptr(), frame.read("protected").unwrap().text.as_ptr());
    drop(frame);
    assert!(state.classes[0].try_write().is_ok());
}
'''
(out / 'runtime.rs').write_text(preamble + runtime + harness)
run(['rustc', '-D', 'warnings', out / 'runtime.rs', '-o', out / 'runtime'])
run([out / 'runtime'])
escape = r'''
fn main() {
    let values = MossLeaves::<String>::new();
    let runtime = MossClassRuntime::new(("i",0,vec![],vec![("h",vec![],vec![],vec![],"h")]), values);
    let borrowed;
    { let frame = runtime.enter("h"); borrowed = frame.read("x"); }
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
assert 'self.state.enter(' in text and '__frame.read(' in text
assert 'value: &impl MossAccess_Record' in text
assert 'Accept_shared((state.record).__moss_value())' in text
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
needle = "fn Read_body(&self, state: &mut StoreState<'_>) -> Option<i64> {"
assert needle in text
instrumented = text.replace(needle, needle + '\ncheck_read_pointers(&state.record.profile.name, &state.record.profile.values);', 1)
needle = "fn Accept_body(&self, state: &mut ReaderState<'_>, value: Record) -> Option<i64> {"
assert needle in instrumented
instrumented = instrumented.replace(needle, needle + "\nEXPECTED_READ_POINTERS.with(|expected| { if let Some((n,v)) = *expected.borrow() { assert_ne!(value.profile.name.as_ptr() as usize, n); assert_ne!(value.profile.values.as_ptr() as usize, v); } });", 1)
needle = 'fn __moss_view_inspect_record(value: &impl MossAccess_Record) -> i64 {'
assert needle in instrumented
instrumented = instrumented.replace(needle, needle + '\ncheck_read_pointers(value.__moss_field_profile().__moss_field_name(), value.__moss_field_profile().__moss_field_values());')
start = instrumented.index('trait MossAccess_Record')
method = instrumented.index('fn score(&self) -> i64 {', start) + len('fn score(&self) -> i64 {')
instrumented = instrumented[:method] + '\ncheck_read_pointers(self.__moss_field_profile().__moss_field_name(), self.__moss_field_profile().__moss_field_values());' + instrumented[method:]
constructors = '\n'.join(line for line in text[text.index('fn main() {'):].splitlines() if ' = construct_' in line)
# Inflate actual domain state, without a benchmark or new Moss syntax.
constructors = constructors.replace('"large".to_string()', '"x".repeat(100_000)').replace('vec![1_i64, 2_i64, 3_i64]', 'vec![1_i64; 100_000]')
# Identify enum variants by the generated typed insertion, without assuming leaf indices.
name_variant = re.search(r'leaves.insert\("record.profile.name", (StoreLeaf::L\d+)', text)[1]
values_variant = re.search(r'leaves.insert\("record.profile.values", (StoreLeaf::L\d+)', text)[1]
test = r'''
#[test]
fn original_storage() {
    CONSTRUCTORS
    let frame = store.state.enter("Read");
    let name = match frame.read("record.profile.name").unwrap() { NAME(value) => value, _ => panic!() };
    let values = match frame.read("record.profile.values").unwrap() { VALUES(value) => value, _ => panic!() };
    EXPECTED_READ_POINTERS.with(|expected| *expected.borrow_mut() = Some((name.as_ptr() as usize, values.as_ptr() as usize)));
    drop(frame);
    assert_eq!(store.Read_shared(), Some(200011));
    assert_eq!(store.Mixed_shared(), Some(100003));
    assert_eq!(store.Immutable_shared(), Some(31));
    assert_eq!(store.Forward_shared(), Some(300009));
    EXPECTED_READ_POINTERS.with(|expected| *expected.borrow_mut() = None);
    let snapshot = store.Snapshot_shared().unwrap();
    let frame = store.state.enter("Read");
    let stored = match frame.read("record.profile.name").unwrap() { NAME(value) => value, _ => panic!() };
    assert_ne!(snapshot.profile.name.as_ptr(), stored.as_ptr());
    drop(frame);
    store.Change_shared();
    assert_eq!(snapshot.profile.name.len(), 100_000);
}
'''.replace('CONSTRUCTORS', constructors).replace('NAME(value)', name_variant + '(value)').replace('VALUES(value)', values_variant + '(value)')
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
