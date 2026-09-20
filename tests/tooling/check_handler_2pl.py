#!/usr/bin/env python3
"""Production 2PL: execute generated wrappers concurrently, not a runtime mock."""
import json
from pathlib import Path
import re
import resource
import shutil
import subprocess
import sys

repository = Path(__file__).resolve().parents[2]
compiler = Path(sys.argv[1]).resolve()
out = Path(sys.argv[2] if len(sys.argv) > 2 else repository / 'tmp/phase106d').resolve()
out.mkdir(parents=True, exist_ok=True)
resource.setrlimit(resource.RLIMIT_CORE, (0, 0))


def run(args, *, expected=0, cwd=None):
    result = subprocess.run(list(map(str, args)), cwd=cwd, text=True, capture_output=True, timeout=90)
    assert result.returncode == expected, (args, result.returncode, result.stdout, result.stderr)
    return result.stdout


def compile_fixture(source, flags=(), name='fixture'):
    rust = out / (name + '.rs')
    run([compiler, *flags, source, '-o', rust])
    text = rust.read_text()
    assert 'struct MossClassRuntime' in text and 'self.state.enter(' in text
    assert not re.search(r'Arc<(Mutex|RwLock)<\w+State|AtomicI64|AtomicBool|thread::spawn', text)
    assert 'unsafe {' not in text and 'unsafe impl' not in text
    return rust, text


def constructors(text):
    main = text[text.index('fn main() {'):]
    return '\n'.join(line for line in main.splitlines() if ' = construct_' in line)


source = repository / 'tests/phase106d_handler_2pl.moss'
plan = json.loads(run([compiler, 'inspect', 'main', '--source', source, '--json']))['result']['synchronization_plan']
domain = next(d for d in plan['domains'] if d['domain'] == 'Store')
classes = domain['sync_classes']
assert len(classes) == 3, classes
assert any(c['member_leaves'] == ['pair.x', 'pair.y'] for c in classes)
handlers = {h['name']: h for h in domain['handlers']}
assert handlers['Fixed']['class_set'] == []
assert handlers['Left']['write_set'] == ['left']
assert len(handlers['Both']['class_set']) == 2
assert not set(handlers['Left']['class_set']) & set(handlers['Right']['class_set'])

rust, text = compile_fixture(source)
run(['rustc', '-D', 'warnings', rust, '-o', out / 'fixture'])
assert run([out / 'fixture']).strip() == '11 1 4 2 7'
for n, flags in enumerate([['-O0'], ['-Oshared-memory']]):
    other, generated = compile_fixture(source, flags, 'route' + str(n))
    # Functional optimization levels preserve the same synchronized entry.
    assert constructors(generated) == constructors(text)
    run(['rustc', '-D', 'warnings', other, '-o', out / ('route' + str(n))])
    assert run([out / ('route' + str(n))]).strip() == '11 1 4 2 7'
repeat, repeat_text = compile_fixture(source, name='deterministic')
assert repeat_text == text

harness = r'''
#[cfg(test)]
mod phase106d {
    use super::*;
    use std::sync::{Barrier, Mutex, OnceLock};
    static SECOND_CALLER: OnceLock<Barrier> = OnceLock::new();
    static ACQUISITIONS: std::sync::atomic::AtomicUsize = std::sync::atomic::AtomicUsize::new(0);
    static COMPLETION: OnceLock<Mutex<Option<Arc<Barrier>>>> = OnceLock::new();
    static OVERLAP: OnceLock<Mutex<Option<Arc<Barrier>>>> = OnceLock::new();
    thread_local! { static HELD: std::cell::RefCell<Vec<(usize, usize)>> = const { std::cell::RefCell::new(Vec::new()) }; }
    fn hook(event: &str, _instance: &str, handler: &str, domain: usize, class: usize, _exclusive: bool) {
        if event == "lock_acquire" {
            ACQUISITIONS.fetch_add(1, std::sync::atomic::Ordering::SeqCst);
            if std::thread::current().name() == Some("blocked-reader") { SECOND_CALLER.get().unwrap().wait(); }
        }
        HELD.with(|held| {
            let mut held = held.borrow_mut();
            if event == "lock_acquired" {
                assert!(held.iter().all(|rank| *rank < (domain, class)), "non-increasing current lock stack");
                held.push((domain, class));
            } else if event == "lock_release" { assert_eq!(held.pop(), Some((domain, class))); }
        });
        if event == "handler_complete" && handler.contains(":Store:") && handler.ends_with(":Both") {
            let gate = COMPLETION.get().unwrap().lock().unwrap().clone();
            if let Some(gate) = gate { gate.wait(); gate.wait(); }
        }
        if event == "lock_acquired" && std::thread::current().name().unwrap_or("").starts_with("overlap") {
            let gate = OVERLAP.get().unwrap().lock().unwrap().clone();
            if let Some(gate) = gate { gate.wait(); }
        }
    }
    fn pair(a: StoreRef, b: StoreRef, left: fn(&StoreRef), right: fn(&StoreRef)) {
        *OVERLAP.get().unwrap().lock().unwrap() = Some(Arc::new(Barrier::new(2)));
        let a = std::thread::Builder::new().name("overlap-a".into()).spawn(move || left(&a)).unwrap();
        let b = std::thread::Builder::new().name("overlap-b".into()).spawn(move || right(&b)).unwrap();
        a.join().unwrap(); b.join().unwrap();
        *OVERLAP.get().unwrap().lock().unwrap() = None;
    }
    #[test]
    fn generated_concurrency() {
        OVERLAP.set(Mutex::new(None)).unwrap();
        COMPLETION.set(Mutex::new(None)).unwrap();
        MOSS_LOCK_HOOK.set(hook).unwrap();
        CONSTRUCTORS
        assert_eq!(store.state.classes.len(), 3); // pair.x/y share ONE physical lock
        let left_rank = *store.state.leaf_classes.get("left").unwrap();
        let right_rank = *store.state.leaf_classes.get("right").unwrap();
        assert_ne!(left_rank, right_rank);
        assert!(!store.state.leaf_classes.contains_key("fixed"));
        for _ in 0..20 {
            // Barrier deadlocks if a hidden whole-domain lock serializes disjoint entries.
            pair(store.clone(), store.clone(), |s| s.Left_shared(), |s| s.Right_shared());
            // Same-class shared readers must ALSO reach the barrier together.
            pair(store.clone(), store.clone(), |s| { s.ReadLeft_shared(); }, |s| { s.ReadLeftAgain_shared(); });
        }
        assert_eq!(split.state.classes.len(), 2);
        for _ in 0..20 {
            *OVERLAP.get().unwrap().lock().unwrap() = Some(Arc::new(Barrier::new(2)));
            let x = split.clone(); let y = split.clone();
            let x = std::thread::Builder::new().name("overlap-x".into()).spawn(move || x.X_shared()).unwrap();
            let y = std::thread::Builder::new().name("overlap-y".into()).spawn(move || y.Y_shared()).unwrap();
            x.join().unwrap(); y.join().unwrap();
            *OVERLAP.get().unwrap().lock().unwrap() = None;
        }
        split.Both_shared();
        let mut independent = split.Read_shared().unwrap();
        assert_eq!((independent.x, independent.y), (21, 21));
        independent.x = 999;
        assert_eq!(independent.x, 999);
        assert_eq!(split.Read_shared().unwrap().x, 21);
        let mut threads = Vec::new();
        for i in 0..8 {
            let target = store.clone();
            threads.push(std::thread::spawn(move || {
                for _ in 0..100 { if i % 2 == 0 { target.Left_shared(); } else { target.LeftAgain_shared(); } }
            }));
        }
        for thread in threads { thread.join().unwrap(); }
        assert_eq!(store.ReadLeft_shared(), Some(820));
        // Explicit compatibility matrix while guards are retained, no sleeps or fairness assumptions.
        {
            let reader = store.state.enter("ReadLeft");
            assert!(store.state.classes[left_rank].try_write().is_err());
            assert!(store.state.classes[left_rank].try_read().is_ok());
            drop(reader);
            let writer = store.state.enter("Left");
            assert!(store.state.classes[left_rank].try_write().is_err());
            assert!(store.state.classes[left_rank].try_read().is_err());
            assert!(store.state.classes[right_rank].try_write().is_ok());
            drop(writer);
        }
        assert!(store.state.classes[left_rank].try_write().is_ok());
        let acquisitions = ACQUISITIONS.load(std::sync::atomic::Ordering::SeqCst);
        assert_eq!(store.Fixed_shared(), Some(7));
        assert_eq!(ACQUISITIONS.load(std::sync::atomic::Ordering::SeqCst), acquisitions);
        let gate = Arc::new(Barrier::new(2));
        *COMPLETION.get().unwrap().lock().unwrap() = Some(gate.clone());
        let returned = Arc::new(std::sync::atomic::AtomicBool::new(false));
        let flag = returned.clone();
        let target = store.clone();
        let caller = std::thread::spawn(move || {
            let reply = target.Both_shared();
            flag.store(true, std::sync::atomic::Ordering::SeqCst);
            reply
        });
        gate.wait(); // reply evaluated and leaves restored, but guards still held
        assert!(!returned.load(std::sync::atomic::Ordering::SeqCst));
        assert!(store.state.classes[left_rank].try_read().is_err());
        assert!(store.state.classes[right_rank].try_write().is_err());
        SECOND_CALLER.set(Barrier::new(2)).unwrap();
        let second_returned = Arc::new(std::sync::atomic::AtomicBool::new(false));
        let second_flag = second_returned.clone();
        let target = store.clone();
        let second = std::thread::Builder::new().name("blocked-reader".into()).spawn(move || {
            let result = target.ReadLeft_shared();
            second_flag.store(true, std::sync::atomic::Ordering::SeqCst);
            result
        }).unwrap();
        SECOND_CALLER.get().unwrap().wait(); // second entry is attempting the held class
        assert!(!second_returned.load(std::sync::atomic::Ordering::SeqCst));
        gate.wait();
        assert_eq!(caller.join().unwrap(), Some(842));
        assert_eq!(second.join().unwrap(), Some(821));
        assert!(second_returned.load(std::sync::atomic::Ordering::SeqCst));
        *COMPLETION.get().unwrap().lock().unwrap() = None;
        assert!(returned.load(std::sync::atomic::Ordering::SeqCst));
        assert_eq!(store.Pair_shared(), Some(2));
        assert_eq!(store.ReadPair_shared(), Some(2));
        HELD.with(|held| assert!(held.borrow().is_empty()));
        // Normal reply restores all evacuated leaves before wrapper return/unlock.
        assert!(store.state.classes.iter().all(|c| c.try_write().is_ok()));
    }
}
'''.replace('CONSTRUCTORS', constructors(text))
rust.write_text(text + harness)
run(['rustc', '--test', '-D', 'warnings', rust, '-o', out / 'concurrency'])
for _ in range(5):
    run([out / 'concurrency', '--test-threads=1'])

# Nested calls and a descending sibling AFTER return. Rank checking is a test
# observer of actual retained acquisitions, not the production correctness mechanism.
nested, nested_text = compile_fixture(repository / 'tests/phase106d_nested.moss', name='nested')
run(['rustc', '-D', 'warnings', nested, '-o', out / 'nested'])
assert run([out / 'nested']).strip() == '4\n8'
nested_harness = r'''
#[cfg(test)]
mod nested_test {
    use super::*;
    thread_local! { static HELD: std::cell::RefCell<Vec<(usize,usize)>> = const { std::cell::RefCell::new(Vec::new()) }; }
    static EVENTS: std::sync::Mutex<Vec<(String,usize,usize)>> = std::sync::Mutex::new(Vec::new());
    fn hook(event: &str, _: &str, _: &str, d: usize, c: usize, _: bool) {
        HELD.with(|held| {
            let mut held = held.borrow_mut();
            if event == "lock_acquired" { assert!(held.iter().all(|r| *r < (d,c))); held.push((d,c)); }
            if event == "lock_release" { assert_eq!(held.pop(), Some((d,c))); }
        });
        EVENTS.lock().unwrap().push((event.into(), d, c));
    }
    #[test]
    fn nesting() {
        MOSS_LOCK_HOOK.set(hook).unwrap();
        CONSTRUCTORS
        assert_eq!(root.Work_shared(), Some(4));
        HELD.with(|held| assert!(held.borrow().is_empty()));
        let events = EVENTS.lock().unwrap();
        let acquired: Vec<_> = events.iter().filter(|e| e.0 == "lock_acquired").map(|e| e.1).collect();
        assert_eq!(acquired, vec![0, 3, 1, 2]);
    }
}
'''.replace('CONSTRUCTORS', constructors(nested_text))
nested.write_text(nested_text + nested_harness)
run(['rustc', '--test', '-D', 'warnings', nested, '-o', out / 'nested-test'])
run([out / 'nested-test'])

# Failure must abort even if an outer caller attempts catch_unwind. Inject a
# backend hook into an ordinary generated body; no Moss failure syntax added.
failure = text.replace('fn main() {', 'fn saved_main() {', 1)
assert 'increment(&mut (*state.left));' in failure
failure = failure.replace('increment(&mut (*state.left));', 'increment(&mut (*state.left)); panic!("injected handler failure");', 1)
failure += '\nfn main() {\n' + constructors(text) + '\nlet _ = std::panic::catch_unwind(std::panic::AssertUnwindSafe(|| store.Left_shared())); println!("RESUMED");\n}\n'
(out / 'failure.rs').write_text(failure)
run(['rustc', '-D', 'warnings', '-A', 'unreachable_code', out / 'failure.rs', '-o', out / 'failure'])
r = subprocess.run([str(out / 'failure')], capture_output=True, text=True, timeout=10)
assert r.returncode != 0 and 'RESUMED' not in r.stdout, r
print('Phase 10.6D runtime/codegen checks passed: disjoint writers, shared readers, conflicts, exact classes, class sharing, empty footprint, nested/sibling ordering, WRITE-through, release, failure, deterministic flag convergence.')

# Source-free provider: physical descriptor belongs to the consumer graph;
# inferred primitive WRITE ABI survives source removal with its semantic effects.
project = out / 'modules'
if project.exists():
    shutil.rmtree(project)
(project / 'src').mkdir(parents=True, exist_ok=True)
(project / 'moss.toml').write_text('[project]\nname = "physical"\nversion = "0.1.0"\n')
provider = project / 'src/provider.moss'
provider.write_text('''module provider
export fn increment(value: Int) -> Int:
  value = value + 1
  return value
export domain Counter:
  value: Int
  fn Bump():
    increment(value)
  fn Read() -> Int:
    reply value
''')
application = project / 'src/main.moss'
application.write_text('''module app
import provider
fn main():
  left = provider.Counter(value: 4)
  right = provider.Counter(value: 8)
  local = 10
  ignored = provider.increment(local)
  message left.Bump()
  message right.Bump()
  echo local, message left.Read(), message right.Read()
''')
for source_free in (False, True):
    if source_free:
        provider.rename(provider.with_suffix('.hidden'))
        # Force a fresh consumer graph/descriptor rather than cached execution.
        application.write_text(application.read_text().replace('value: 8', 'value: 18'))
    built = json.loads(run([compiler, 'build', '--json'], cwd=project))['result']
    assert run([built['artifacts']['executable']]).strip() == ('11 5 19' if source_free else '11 5 9')
    interface = (project / 'build/debug/provider.mossi').read_text()
    for physical in ('class_rank', 'class_set', 'sync_class', 'LockRank', 'MossPhysicalPlan'):
        assert physical not in interface
interface_path = project / 'build/debug/provider.mossi'
contents = interface_path.read_text()
interface_path.write_text(contents.replace('native_abi 4\n', ''))
old = run([compiler, 'inspect', 'main', '--source', application, '--json'], expected=1, cwd=project)
assert 'rebuild' in old and 'native calling convention' in old
interface_path.write_text(contents)
print('Phase 10.6D source/source-free domain and primitive WRITE ABI checks passed.')

special_source = repository / 'tests/phase106d_specializations.moss'
special_plan = json.loads(run([compiler, 'inspect', 'main', '--source', special_source, '--json']))['result']['synchronization_plan']
by_instance = {d['concrete_instance_id']: d for d in special_plan['domains']}
assert len(by_instance['main::left']['sync_classes']) == 2
assert len(by_instance['main::right']['sync_classes']) == 1
special, special_text = compile_fixture(special_source, name='specialized')
run(['rustc', '-D', 'warnings', special, '-o', out / 'specialized'])
assert run([out / 'specialized']).strip() == '11 22'
special.write_text(special_text + '\n#[test] fn exact_physical_layout() {\n' + constructors(special_text) +
                   '\nassert_eq!(left.state.classes.len(), 2);\nassert_eq!(right.state.classes.len(), 1);\n}\n')
run(['rustc', '--test', '-D', 'warnings', special, '-o', out / 'specialized-test'])
run([out / 'specialized-test'])
print('Phase 10.6D exact-specialization and whole-object restoration checks passed.')

parameter_rust = out / 'parameters.rs'
run([compiler, repository / 'tests/phase106d_parameter_write.moss', '-o', parameter_rust])
run(['rustc', '-D', 'warnings', parameter_rust, '-o', out / 'parameters'])
assert run([out / 'parameters']).strip() == '5 true 1.5 11\n7 8'
print('Phase 10.6D primitive/generic/method/projection WRITE and aggregate reassignment checks passed.')

# Poisoning is fatal even if created by a backend harness outside Moss entry.
poison = text.replace('fn main() {', 'fn saved_main() {', 1)
poison += '\nfn main() {\n' + constructors(text) + r"""
    let target = store.clone();
    let rank = *store.state.leaf_classes.get("left").unwrap();
    let _ = std::thread::spawn(move || {
        let _guard = target.state.classes[rank].write().unwrap();
        panic!("inject poison outside handler entry");
    }).join();
    store.Left_shared();
    println!("RESUMED");
}
"""
(out / 'poison.rs').write_text(poison)
run(['rustc', '-D', 'warnings', out / 'poison.rs', '-o', out / 'poison'])
r = subprocess.run([str(out / 'poison')], capture_output=True, text=True, timeout=10)
assert r.returncode != 0 and 'RESUMED' not in r.stdout
print('Phase 10.6D fatal lock-poisoning check passed.')
