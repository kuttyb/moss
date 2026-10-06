#!/usr/bin/env python3
"""
Phase 20 Agent C — Executor / root runtime tests
tests/tooling/check_phase20c_executor.py [compiler]

Each Rust test program is the compiler-emitted runtime (handler runtime plus
executor runtime, extracted from a generated Moss program) followed by a test
`main`.  Ordering proofs use runtime state/ticket instrumentation
(`#[cfg(moss_perf)]` accessors, events, and the pre-wait hook), never sleeps.
`wait_until` polls an observable runtime state with a generous deadline; a
deadline miss is a failure (hang detector), not a timing assumption.

Coverage:
  ingress     1-10, 21-27, 31, 32, 44, 45, 50, 51, 52  (INLINE/ACTIVE/DRAINING, fairness, tickets, reentrancy)
  branches    11-15, 30, 38, 39, 41, 42, 46, 47 (publish/join, helping, ABI ownership)
  solo        17-20, 28, 29, 33, 36, 48 (real-worker compensation, T_max)
  seams       16, 34, 35, 37, 40, 43, 49 (config, linear handle, symbols, TLS, roles)
"""

import concurrent.futures
import json
import os
import shutil
import subprocess
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[2]
TMP_DIR = REPO_ROOT / "tmp" / "phase20c_executor_tests"
RUSTC = os.environ.get("RUSTC", "rustc")
COMPILER = Path(sys.argv[1]).resolve() if len(sys.argv) > 1 else REPO_ROOT / "moss"
RUN_TIMEOUT = 60

PASS = []
FAIL = []


def generate(name: str, source: str) -> str:
    if not COMPILER.exists():
        print("ERROR: Moss compiler not found; run make first", file=sys.stderr)
        sys.exit(1)
    src = TMP_DIR / f"_probe_{name}.moss"
    out = TMP_DIR / f"_probe_{name}.rs"
    src.write_text(source)
    r = subprocess.run([str(COMPILER), str(src), "-o", str(out)], capture_output=True, text=True)
    if r.returncode != 0:
        print("ERROR: moss codegen failed:", r.stderr[:800], file=sys.stderr)
        sys.exit(1)
    return out.read_text()


def runtime_preamble(generated_rs: str) -> str:
    """Handler runtime + executor runtime, without the generated program."""
    # Agent B's process-wide FileIO root runtime is emitted alongside the C
    # runtime in integrated programs. C symbol-ownership tests inspect the
    # executor block itself, so remove B's independent hook table before
    # constructing their runtime harness.
    fileio_root_start = generated_rs.find(
        "// Moss Phase 20 process-wide root coordination runtime.")
    if fileio_root_start >= 0:
        fileio_root_end_marker = (
            "pub use moss_root_runtime::{moss_set_solo_hooks, moss_clear_solo_hooks};")
        fileio_root_end = generated_rs.find(fileio_root_end_marker, fileio_root_start)
        if fileio_root_end >= 0:
            fileio_root_end = generated_rs.find("\n", fileio_root_end)
            generated_rs = (generated_rs[:fileio_root_start] +
                            generated_rs[fileio_root_end + 1:])
    start = generated_rs.find("struct MossAbortOnUnwind")
    end_marker = "// ─── End Phase 20 Executor Runtime"
    end = generated_rs.find(end_marker)
    if start < 0 or end < 0:
        print("ERROR: runtime markers not found in generated Rust", file=sys.stderr)
        sys.exit(1)
    return generated_rs[start:generated_rs.index("\n", end) + 1]


def executor_runtime_block(generated_rs: str) -> str:
    """Return C's delimited runtime block, excluding Agent B's root runtime."""
    start_marker = "// Phase 20 Executor Runtime  ("
    end_marker = "// ─── End Phase 20 Executor Runtime"
    start = generated_rs.find(start_marker)
    end = generated_rs.find(end_marker, start)
    if start < 0 or end < 0:
        raise AssertionError("executor runtime block markers not found")
    return generated_rs[start:generated_rs.index("\n", end) + 1]


TEST_PRELUDE = r"""
#[allow(dead_code)]
fn wait_until(what: &str, mut cond: impl FnMut() -> bool) {
    let start = std::time::Instant::now();
    while !cond() {
        if start.elapsed() > std::time::Duration::from_secs(20) {
            eprintln!("timed out waiting for {}", what);
            std::process::exit(3);
        }
        std::thread::sleep(std::time::Duration::from_micros(100));
    }
}

#[allow(dead_code)]
mod evlog {
    use super::MossRtEvent;
    pub static EVENTS: std::sync::Mutex<Vec<(MossRtEvent, u64, u64, usize)>> = std::sync::Mutex::new(Vec::new());
    pub static LIVE_WORKERS: std::sync::atomic::AtomicIsize = std::sync::atomic::AtomicIsize::new(0);
    pub static PEAK_WORKERS: std::sync::atomic::AtomicIsize = std::sync::atomic::AtomicIsize::new(0);
    pub fn hook(ev: MossRtEvent, a: u64, b: u64, w: usize) {
        use std::sync::atomic::Ordering::SeqCst;
        match ev {
            MossRtEvent::WorkerSpawned => {
                let live = LIVE_WORKERS.fetch_add(1, SeqCst) + 1;
                PEAK_WORKERS.fetch_max(live, SeqCst);
            }
            MossRtEvent::WorkerExited => { LIVE_WORKERS.fetch_sub(1, SeqCst); }
            _ => {}
        }
        EVENTS.lock().unwrap_or_else(|e| e.into_inner()).push((ev, a, b, w));
    }
    pub fn install() { super::moss_rt_set_hook(hook); }
    pub fn count(ev: MossRtEvent) -> usize {
        EVENTS.lock().unwrap().iter().filter(|e| e.0 == ev).count()
    }
    pub fn snapshot() -> Vec<(MossRtEvent, u64, u64, usize)> { EVENTS.lock().unwrap().clone() }
    pub fn live() -> isize { LIVE_WORKERS.load(SeqCst) }
    pub fn peak() -> isize { PEAK_WORKERS.load(SeqCst) }
    use std::sync::atomic::Ordering::SeqCst;
}

/// Exact reference counts of one Branch scope via a Weak, which keeps the
/// allocation (not the scope) alive, so reading counts is always defined.
#[allow(dead_code)]
mod scope_track {
    static WEAKS: std::sync::Mutex<Vec<std::sync::Weak<super::MossBranchScopeState>>> = std::sync::Mutex::new(Vec::new());
    pub fn track(raw: *const ()) -> usize {
        unsafe {
            super::MossBranchScope::with_raw_borrowed(raw, |s| {
                let mut w = WEAKS.lock().unwrap();
                w.push(std::sync::Arc::downgrade(&s.state));
                w.len() - 1
            })
        }
    }
    pub fn strong(id: usize) -> usize { WEAKS.lock().unwrap()[id].strong_count() }
    pub fn all_released() -> bool { WEAKS.lock().unwrap().iter().all(|w| w.strong_count() == 0) }
}
"""


def write_and_compile(name: str, rust_src: str, extra_flags=None, crate_type=None):
    src_path = TMP_DIR / f"{name}.rs"
    out_path = TMP_DIR / (f"lib{name}.rlib" if crate_type == "rlib" else name)
    src_path.write_text(rust_src)
    flags = [RUSTC, "--edition", "2021", "--cfg", "moss_perf", "-C", "opt-level=0",
             str(src_path), "-o", str(out_path)]
    if crate_type:
        flags[1:1] = [f"--crate-type={crate_type}"]
    if extra_flags:
        flags.extend(extra_flags)
    return subprocess.run(flags, capture_output=True, text=True), out_path


def report(test_name: str, ok: bool, detail: str = ""):
    if ok:
        PASS.append(test_name)
        print(f"  PASS: {test_name}")
    else:
        FAIL.append(test_name)
        print(f"  FAIL: {test_name}")
        if detail:
            print("    " + detail[:1500].replace("\n", "\n    "))


class RustTest:
    def __init__(self, name, title, body, expect="ok", expect_abort=None, extra_flags=None, prelude=None):
        self.name, self.title, self.body = name, title, body
        self.expect, self.expect_abort = expect, expect_abort
        self.extra_flags = extra_flags
        self.prelude = prelude


def run_rust_tests(tests, preamble):
    def compile_one(t):
        source = (t.prelude if t.prelude is not None else preamble) + TEST_PRELUDE + t.body
        return write_and_compile(f"t_{t.name.split('-')[0]}", source, t.extra_flags)

    with concurrent.futures.ThreadPoolExecutor(max_workers=os.cpu_count() or 4) as pool:
        compiled = list(pool.map(compile_one, tests))
    for t, (cr, binary) in zip(tests, compiled):
        print(f"Test {t.name}: {t.title}")
        if cr.returncode != 0:
            report(t.name, False, "compile failed:\n" + cr.stderr)
            continue
        try:
            r = subprocess.run([str(binary)], capture_output=True, text=True, timeout=RUN_TIMEOUT)
        except subprocess.TimeoutExpired:
            report(t.name, False, f"timed out after {RUN_TIMEOUT}s (hang)")
            continue
        if t.expect_abort is not None:
            ok = r.returncode != 0 and t.expect_abort in r.stderr
            report(t.name, ok, f"exit={r.returncode}\nstderr: {r.stderr}\nstdout: {r.stdout}")
            continue
        ok = r.returncode == 0 and t.expect in r.stdout
        report(t.name, ok, f"exit={r.returncode}\nstderr: {r.stderr}\nstdout: {r.stdout}")


# ════════════════════════════════════════════════════════════════════════════
# Rust test bodies
# ════════════════════════════════════════════════════════════════════════════

T = []

T.append(RustTest("1-inline-one-at-a-time", "INLINE admits one Root at a time", r"""
fn main() {
    use std::sync::{Arc, Barrier};
    use std::sync::atomic::{AtomicUsize, Ordering};
    let concurrent = Arc::new(AtomicUsize::new(0));
    let max_seen = Arc::new(AtomicUsize::new(0));
    let start = Arc::new(Barrier::new(6));
    let threads: Vec<_> = (0..6).map(|_| {
        let (c, m, s) = (Arc::clone(&concurrent), Arc::clone(&max_seen), Arc::clone(&start));
        std::thread::spawn(move || {
            s.wait();
            runtime_invoke(move || {
                let n = c.fetch_add(1, Ordering::SeqCst) + 1;
                m.fetch_max(n, Ordering::SeqCst);
                for _ in 0..200 { std::thread::yield_now(); }
                c.fetch_sub(1, Ordering::SeqCst);
            });
        })
    }).collect();
    for t in threads { t.join().unwrap(); }
    assert_eq!(max_seen.load(Ordering::SeqCst), 1);
    assert_eq!(moss_runtime_inline_tickets(), (6, 6));
    println!("ok");
}
"""))

T.append(RustTest("2-inline-fifo-fairness", "INLINE admission is FIFO by ingress ticket", r"""
fn main() {
    use std::sync::{Arc, Barrier, Mutex};
    let held = Arc::new(Barrier::new(2));
    let release = Arc::new(Barrier::new(2));
    let (h2, r2) = (Arc::clone(&held), Arc::clone(&release));
    let blocker = std::thread::spawn(move || runtime_invoke(move || { h2.wait(); r2.wait(); }));
    held.wait();
    let order = Arc::new(Mutex::new(Vec::new()));
    let mut waiters = Vec::new();
    for i in 0..6u64 {
        let o = Arc::clone(&order);
        waiters.push(std::thread::spawn(move || runtime_invoke(move || o.lock().unwrap().push(i))));
        wait_until("waiter ticket", || moss_runtime_inline_tickets().0 == i + 2);
    }
    release.wait();
    blocker.join().unwrap();
    for w in waiters { w.join().unwrap(); }
    assert_eq!(*order.lock().unwrap(), (0..6).collect::<Vec<u64>>());
    println!("ok");
}
"""))

T.append(RustTest("3-start-closes-inline-gate", "start() closes the INLINE gate; queued callers enter ACTIVE", r"""
fn main() {
    use std::sync::{Arc, Barrier};
    use std::sync::atomic::{AtomicBool, Ordering};
    let held = Arc::new(Barrier::new(2));
    let release = Arc::new(Barrier::new(2));
    let (h2, r2) = (Arc::clone(&held), Arc::clone(&release));
    let root1 = std::thread::spawn(move || runtime_invoke(move || { h2.wait(); r2.wait(); }));
    held.wait();
    let queued_on_worker = Arc::new(AtomicBool::new(false));
    let q = Arc::clone(&queued_on_worker);
    let queued = std::thread::spawn(move || runtime_invoke(move || {
        if moss_executor_state() == "ACTIVE" && current_worker_id().is_some() { q.store(true, Ordering::SeqCst); }
    }));
    wait_until("queued caller ticket", || moss_runtime_inline_tickets().0 == 2);
    let started = Arc::new(AtomicBool::new(false));
    let s2 = Arc::clone(&started);
    let starter = std::thread::spawn(move || { let e = MossExecutor::new().threads(2).start(); s2.store(true, Ordering::SeqCst); e });
    wait_until("start_pending", || moss_runtime_start_pending());
    // start() cannot complete while the inline Root holds the gate.
    assert!(!started.load(Ordering::SeqCst));
    assert_eq!(moss_executor_state(), "INLINE");
    release.wait();
    root1.join().unwrap();
    let exec = starter.join().unwrap();
    queued.join().unwrap();
    assert!(queued_on_worker.load(Ordering::SeqCst), "queued caller did not use ACTIVE admission");
    exec.join();
    println!("ok");
}
"""))

T.append(RustTest("4-no-inline-active-overlap", "No inline Root overlaps the newly ACTIVE Executor", r"""
fn main() {
    use std::sync::{Arc, Barrier};
    use std::sync::atomic::{AtomicBool, Ordering};
    let inline_running = Arc::new(AtomicBool::new(false));
    let entered = Arc::new(Barrier::new(2));
    let (ir, e2) = (Arc::clone(&inline_running), Arc::clone(&entered));
    let t = std::thread::spawn(move || runtime_invoke(move || {
        ir.store(true, Ordering::SeqCst);
        e2.wait();
        wait_until("start_pending observed by inline root", || moss_runtime_start_pending());
        ir.store(false, Ordering::SeqCst);
    }));
    entered.wait();
    let exec = MossExecutor::new().threads(2).start();
    assert!(!inline_running.load(Ordering::SeqCst), "start() returned while inline root was running");
    t.join().unwrap();
    exec.join();
    println!("ok");
}
"""))

T.append(RustTest("5-active-concurrent-roots", "ACTIVE runs independent Roots concurrently", r"""
fn main() {
    use std::sync::{Arc, Barrier};
    let n = 4usize;
    let exec = MossExecutor::new().threads(n).queue_capacity(16).start();
    // Deadlocks (and times out) unless all n Roots run at once.
    let barrier = Arc::new(Barrier::new(n));
    for _ in 0..n {
        let b = Arc::clone(&barrier);
        exec.enqueue_root(MossRootDescriptor::one_way(next_root_id(), move || { b.wait(); }));
    }
    exec.join();
    println!("ok");
}
"""))

T.append(RustTest("6-one-active-executor", "Only one active Executor; restart after join", r"""
fn main() {
    let exec1 = MossExecutor::new().threads(2).start();
    let second = std::panic::catch_unwind(|| { MossExecutor::new().threads(2).start(); });
    assert!(second.is_err(), "second start() during ACTIVE did not fail closed");
    exec1.join();
    let exec2 = MossExecutor::new().threads(2).start();
    exec2.join();
    println!("ok");
}
"""))

T.append(RustTest("7-root-queue-bounded", "Root queue is bounded by queue_capacity", r"""
fn main() {
    use std::sync::{Arc, Barrier};
    use std::sync::atomic::{AtomicBool, Ordering};
    let exec = MossExecutor::new().threads(1).queue_capacity(2).start();
    let running = Arc::new(AtomicBool::new(false));
    let release = Arc::new(Barrier::new(2));
    let (r1, rel) = (Arc::clone(&running), Arc::clone(&release));
    exec.enqueue_root(MossRootDescriptor::one_way(next_root_id(), move || { r1.store(true, Ordering::SeqCst); rel.wait(); }));
    wait_until("blocker running", || running.load(Ordering::SeqCst));
    for _ in 0..2 { exec.enqueue_root(MossRootDescriptor::one_way(next_root_id(), || {})); }
    let extra_admitted = AtomicBool::new(false);
    std::thread::scope(|s| {
        s.spawn(|| {
            exec.enqueue_root(MossRootDescriptor::one_way(next_root_id(), || {}));
            extra_admitted.store(true, Ordering::SeqCst);
        });
        wait_until("producer waiting for capacity", || moss_executor_admission_tickets() == (4, 3));
        assert_eq!(moss_executor_queue_len(), 2);
        assert!(!extra_admitted.load(Ordering::SeqCst), "producer was admitted past queue_capacity");
        release.wait();
    });
    assert!(extra_admitted.load(Ordering::SeqCst));
    exec.join();
    println!("ok");
}
"""))

T.append(RustTest("8-fair-root-admission", "ACTIVE queue-capacity admission is FIFO by ticket", r"""
fn main() {
    use std::sync::{Arc, Barrier, Mutex};
    use std::sync::atomic::{AtomicBool, Ordering};
    let exec = MossExecutor::new().threads(1).queue_capacity(1).start();
    let order = Arc::new(Mutex::new(Vec::new()));
    let running = Arc::new(AtomicBool::new(false));
    let release = Arc::new(Barrier::new(2));
    let (o0, r0, rel) = (Arc::clone(&order), Arc::clone(&running), Arc::clone(&release));
    exec.enqueue_root(MossRootDescriptor::one_way(next_root_id(), move || {
        r0.store(true, Ordering::SeqCst); rel.wait(); o0.lock().unwrap().push(0);
    }));
    wait_until("root 0 running", || running.load(Ordering::SeqCst));
    let o1 = Arc::clone(&order);
    exec.enqueue_root(MossRootDescriptor::one_way(next_root_id(), move || o1.lock().unwrap().push(1)));
    std::thread::scope(|s| {
        for i in 2..=4u64 {
            let o = Arc::clone(&order);
            let ex = &exec;
            s.spawn(move || ex.enqueue_root(MossRootDescriptor::one_way(next_root_id(), move || o.lock().unwrap().push(i))));
            wait_until("producer ticket", || moss_executor_admission_tickets().0 == i + 1);
        }
        release.wait();
    });
    exec.join();
    assert_eq!(*order.lock().unwrap(), vec![0, 1, 2, 3, 4]);
    println!("ok");
}
"""))

T.append(RustTest("9-root-a-not-interrupted-by-b", "A worker never starts Root B while Root A is live", r"""
fn main() {
    use std::sync::{Arc, Mutex};
    use std::sync::atomic::{AtomicBool, Ordering};
    let exec = MossExecutor::new().threads(1).max_threads(1).queue_capacity(8).start();
    let seq = Arc::new(Mutex::new(Vec::<&str>::new()));
    let b_enqueued = Arc::new(AtomicBool::new(false));
    let (s1, be) = (Arc::clone(&seq), Arc::clone(&b_enqueued));
    exec.enqueue_root(MossRootDescriptor::one_way(next_root_id(), move || {
        s1.lock().unwrap().push("A-start");
        wait_until("B enqueued", || be.load(Ordering::SeqCst));
        s1.lock().unwrap().push("A-end");
    }));
    let s2 = Arc::clone(&seq);
    exec.enqueue_root(MossRootDescriptor::one_way(next_root_id(), move || s2.lock().unwrap().push("B-start")));
    b_enqueued.store(true, Ordering::SeqCst);
    exec.join();
    assert_eq!(*seq.lock().unwrap(), vec!["A-start", "A-end", "B-start"]);
    println!("ok");
}
"""))

T.append(RustTest("10-no-root-inside-root", "Nested work stays in its Root; Root-in-Root ingress fails closed", r"""
fn main() {
    let (outer, nested, rejected) = runtime_invoke(|| {
        let outer = current_root_id();
        // A nested synchronous message is an ordinary call in the same Root.
        let nested = (|| current_root_id())();
        let rejected = std::panic::catch_unwind(|| runtime_invoke(|| 1u8)).is_err();
        (outer, nested, rejected)
    });
    assert!(outer.is_some());
    assert_eq!(outer, nested);
    assert!(rejected, "runtime_invoke inside a Root was not rejected");
    assert_eq!(current_root_id(), None);
    println!("ok");
}
"""))

T.append(RustTest("11-branch-publish-free-worker", "Branch publication is nonblocking and free workers execute Branches", r"""
fn main() {
    use std::sync::Arc;
    use std::sync::atomic::{AtomicBool, AtomicUsize, Ordering};
    let exec = MossExecutor::new().threads(4).start();
    let (count, on_worker) = runtime_invoke(|| {
        let root_thread = std::thread::current().id();
        let go = Arc::new(AtomicBool::new(false));
        let count = Arc::new(AtomicUsize::new(0));
        let on_worker = Arc::new(AtomicUsize::new(0));
        let scope = branch_scope_new_current();
        for _ in 0..20 {
            let (g, c, w) = (Arc::clone(&go), Arc::clone(&count), Arc::clone(&on_worker));
            branch_publish(&scope, move || {
                if std::thread::current().id() != root_thread { w.fetch_add(1, Ordering::SeqCst); }
                wait_until("go", || g.load(Ordering::SeqCst));
                c.fetch_add(1, Ordering::SeqCst);
            });
        }
        // Every publish returned although no Branch may complete before `go`.
        assert_eq!(count.load(Ordering::SeqCst), 0);
        wait_until("a free worker starts a Branch", || on_worker.load(Ordering::SeqCst) > 0);
        go.store(true, Ordering::SeqCst);
        branch_join(scope);
        (count.load(Ordering::SeqCst), on_worker.load(Ordering::SeqCst))
    });
    exec.join();
    assert_eq!(count, 20);
    assert!(on_worker > 0);
    println!("ok on_worker={}", on_worker);
}
"""))

T.append(RustTest("12-branch-window-overflow-inline", "Overflowing Branch publication runs inline; INLINE mode runs inline", r"""
fn main() {
    use std::sync::Arc;
    use std::sync::atomic::{AtomicUsize, Ordering};
    evlog::install();
    // INLINE mode: no executor, each Branch runs at publication.
    let inline_ok = runtime_invoke(|| {
        let n = Arc::new(AtomicUsize::new(0));
        let scope = branch_scope_new_current();
        for i in 0..5 {
            let c = Arc::clone(&n);
            branch_publish(&scope, move || { c.fetch_add(1, Ordering::SeqCst); });
            assert_eq!(n.load(Ordering::SeqCst), i + 1);
        }
        branch_join(scope);
        n.load(Ordering::SeqCst)
    });
    assert_eq!(inline_ok, 5);
    assert_eq!(evlog::count(MossRtEvent::BranchInline), 5);
    // ACTIVE with the only worker occupied by the Root: no one drains the
    // scope, so the window (512) overflows deterministically.
    let exec = MossExecutor::new().threads(1).max_threads(1).start();
    let total = runtime_invoke(|| {
        let n = Arc::new(AtomicUsize::new(0));
        let scope = branch_scope_new_current();
        for _ in 0..520 {
            let c = Arc::clone(&n);
            branch_publish(&scope, move || { c.fetch_add(1, Ordering::SeqCst); });
        }
        assert_eq!(n.load(Ordering::SeqCst), 8, "overflow Branches must run inline at publication");
        branch_join(scope);
        n.load(Ordering::SeqCst)
    });
    exec.join();
    assert_eq!(total, 520);
    assert_eq!(evlog::count(MossRtEvent::BranchInline), 5 + 8);
    assert_eq!(evlog::count(MossRtEvent::BranchPublished), 512);
    assert_eq!(evlog::count(MossRtEvent::BranchHelped), 512);
    println!("ok");
}
"""))

T.append(RustTest("13-root-scoped-helping", "A joining Root helps only its own Branches", r"""
fn main() {
    use std::sync::{Arc, Barrier, Mutex};
    let exec = MossExecutor::new().threads(2).max_threads(2).queue_capacity(4).start();
    let both = Arc::new(Barrier::new(2));
    let joined = Arc::new(Barrier::new(2));
    let records = Arc::new(Mutex::new(Vec::new()));
    for _ in 0..2 {
        let (b, j, rec) = (Arc::clone(&both), Arc::clone(&joined), Arc::clone(&records));
        exec.enqueue_root(MossRootDescriptor::one_way(next_root_id(), move || {
            b.wait(); // both workers now run Roots: no free worker exists
            let me = current_root_id().unwrap();
            let my_thread = std::thread::current().id();
            let scope = branch_scope_new_current();
            for _ in 0..10 {
                let r = Arc::clone(&rec);
                branch_publish(&scope, move || {
                    r.lock().unwrap().push((me, current_root_id().unwrap(), std::thread::current().id() == my_thread));
                });
            }
            branch_join(scope);
            // Stay live until both Roots joined, so no worker becomes free.
            j.wait();
        }));
    }
    exec.join();
    let recs = records.lock().unwrap();
    assert_eq!(recs.len(), 20);
    for (owner, ctx, same_thread) in recs.iter() {
        assert_eq!(owner, ctx, "Branch ran under a different Root context");
        assert!(same_thread, "Branch of Root {} ran on another Root's thread", owner);
    }
    println!("ok");
}
"""))

T.append(RustTest("14-joiner-never-runs-unrelated-root", "Join helping never starts an unrelated queued Root", r"""
fn main() {
    use std::sync::Arc;
    use std::sync::atomic::{AtomicBool, Ordering};
    evlog::install();
    let exec = MossExecutor::new().threads(1).max_threads(1).queue_capacity(4).start();
    let b_queued = Arc::new(AtomicBool::new(false));
    let bq = Arc::clone(&b_queued);
    let a = next_root_id();
    exec.enqueue_root(MossRootDescriptor::one_way(a, move || {
        wait_until("B queued", || bq.load(Ordering::SeqCst));
        let scope = branch_scope_new_current();
        for _ in 0..8 { branch_publish(&scope, || {}); }
        branch_join(scope);
    }));
    let b = next_root_id();
    exec.enqueue_root(MossRootDescriptor::one_way(b, || {}));
    b_queued.store(true, Ordering::SeqCst);
    exec.join();
    let ev = evlog::snapshot();
    let pos = |e: MossRtEvent, id: u64| ev.iter().position(|x| x.0 == e && x.1 == id).unwrap();
    assert!(pos(MossRtEvent::RootStarted, b) > pos(MossRtEvent::RootCompleted, a));
    assert_eq!(evlog::count(MossRtEvent::BranchHelped), 8);
    println!("ok");
}
"""))

T.append(RustTest("15-branch-progress-all-workers-busy", "All workers occupied by Roots: Branches progress by owner helping", r"""
fn main() {
    use std::sync::{Arc, Barrier};
    use std::sync::atomic::{AtomicUsize, Ordering};
    let n = 2usize;
    let exec = MossExecutor::new().threads(n).max_threads(n).queue_capacity(8).start();
    let all = Arc::new(Barrier::new(n));
    let done = Arc::new(AtomicUsize::new(0));
    for _ in 0..n {
        let (b, d) = (Arc::clone(&all), Arc::clone(&done));
        exec.enqueue_root(MossRootDescriptor::one_way(next_root_id(), move || {
            b.wait();
            let scope = branch_scope_new_current();
            let c = Arc::new(AtomicUsize::new(0));
            for _ in 0..5 { let c2 = Arc::clone(&c); branch_publish(&scope, move || { c2.fetch_add(1, Ordering::SeqCst); }); }
            branch_join(scope);
            assert_eq!(c.load(Ordering::SeqCst), 5);
            d.fetch_add(1, Ordering::SeqCst);
        }));
    }
    exec.join();
    assert_eq!(done.load(Ordering::SeqCst), n);
    println!("ok");
}
"""))

T.append(RustTest("16-config-validation", "Invalid configurations fail closed before activation", r"""
fn main() {
    std::panic::set_hook(Box::new(|_| {}));
    assert!(std::panic::catch_unwind(|| { MossExecutor::new().threads(8).max_threads(4).start(); }).is_err());
    assert!(std::panic::catch_unwind(|| { MossExecutor::new().threads(0); }).is_err());
    assert!(std::panic::catch_unwind(|| { MossExecutor::new().max_threads(0); }).is_err());
    assert!(std::panic::catch_unwind(|| { MossExecutor::new().queue_capacity(0); }).is_err());
    let bad = MossExecutorConfig { threads: 4, max_threads: 2, queue_capacity: 8, affinity: None, priority: None };
    assert!(std::panic::catch_unwind(move || { moss_root_start_with_config(bad); }).is_err());
    let zero_q = MossExecutorConfig { queue_capacity: 0, ..MossExecutorConfig::default() };
    assert!(std::panic::catch_unwind(move || { moss_root_start_with_config(zero_q); }).is_err());
    // Rejected configurations never reserved activation.
    assert_eq!(moss_executor_state(), "INLINE");
    assert!(!moss_runtime_start_pending());
    MossExecutor::new().threads(2).max_threads(2).start().join();
    println!("ok");
}
"""))

T.append(RustTest("17-solo-compensation-real-workers", "Solo waits on real worker Roots activate compensation", r"""
fn main() {
    use std::sync::{Arc, Barrier};
    use std::sync::atomic::{AtomicBool, Ordering};
    evlog::install();
    let (target, tmax) = (2usize, 4usize);
    let exec = MossExecutor::new().threads(target).max_threads(tmax).queue_capacity(8).start();
    let blocked = Arc::new(Barrier::new(target + 1));
    let release = Arc::new(Barrier::new(target + 1));
    for _ in 0..target {
        let (b, r) = (Arc::clone(&blocked), Arc::clone(&release));
        exec.enqueue_root(MossRootDescriptor::one_way(next_root_id(), move || {
            assert!(solo_enter_current("test-read"));
            b.wait();
            r.wait();
            assert!(solo_leave_current("test-read"));
        }));
    }
    blocked.wait();
    assert_eq!(moss_executor_solo_blocked(), target);
    assert_eq!(moss_executor_worker_count(), tmax);
    // Both target workers are blocked; this Root needs a compensation worker.
    let ran = Arc::new(AtomicBool::new(false));
    let r2 = Arc::clone(&ran);
    exec.enqueue_root(MossRootDescriptor::one_way(next_root_id(), move || r2.store(true, Ordering::SeqCst)));
    wait_until("compensation worker runs a Root", || ran.load(Ordering::SeqCst));
    assert!(evlog::peak() <= tmax as isize, "physical workers {} exceeded T_max", evlog::peak());
    release.wait();
    exec.join();
    println!("ok");
}
"""))

T.append(RustTest("18-no-compensation-for-moss-lock", "Moss-lock waits are not compensated", r"""
fn main() {
    use std::sync::{Arc, Mutex};
    use std::sync::atomic::{AtomicBool, Ordering};
    evlog::install();
    let exec = MossExecutor::new().threads(2).max_threads(4).start();
    let lock = Arc::new(Mutex::new(0usize));
    let held = lock.lock().unwrap();
    let about_to_lock = Arc::new(AtomicBool::new(false));
    let (l2, a2) = (Arc::clone(&lock), Arc::clone(&about_to_lock));
    exec.enqueue_root(MossRootDescriptor::one_way(next_root_id(), move || {
        a2.store(true, Ordering::SeqCst);
        let _g = l2.lock().unwrap();
    }));
    wait_until("root reaches lock", || about_to_lock.load(Ordering::SeqCst));
    // Both worker threads must be running before the peak is meaningful.
    wait_until("both workers started", || evlog::live() == 2);
    assert_eq!(moss_executor_worker_count(), 2);
    drop(held);
    exec.join();
    assert_eq!(evlog::count(MossRtEvent::SoloEnter), 0);
    assert_eq!(evlog::peak(), 2);
    println!("ok");
}
"""))

T.append(RustTest("19-solo-wake-resumes-immediately", "A worker leaving Solo resumes without waiting for a slot", r"""
fn main() {
    use std::sync::Arc;
    use std::sync::atomic::{AtomicBool, Ordering};
    let exec = MossExecutor::new().threads(1).max_threads(2).queue_capacity(4).start();
    let r2_running = Arc::new(AtomicBool::new(false));
    let r1_after_leave = Arc::new(AtomicBool::new(false));
    let (a, b) = (Arc::clone(&r2_running), Arc::clone(&r1_after_leave));
    exec.enqueue_root(MossRootDescriptor::one_way(next_root_id(), move || {
        assert!(solo_enter_current("read"));
        wait_until("R2 running on compensation worker", || a.load(Ordering::SeqCst));
        // Runnable workers now equal target; leaving must not wait for a slot.
        assert!(solo_leave_current("read"));
        b.store(true, Ordering::SeqCst);
    }));
    let (c, d) = (Arc::clone(&r2_running), Arc::clone(&r1_after_leave));
    exec.enqueue_root(MossRootDescriptor::one_way(next_root_id(), move || {
        c.store(true, Ordering::SeqCst);
        // Holds the compensation worker until R1 has resumed.
        wait_until("R1 resumed after Solo", || d.load(Ordering::SeqCst));
    }));
    exec.join();
    println!("ok");
}
"""))

T.append(RustTest("20-excess-compensation-exits", "Excess compensation workers exit once Solo waits end", r"""
fn main() {
    use std::sync::{Arc, Barrier};
    evlog::install();
    let exec = MossExecutor::new().threads(2).max_threads(6).queue_capacity(8).start();
    let blocked = Arc::new(Barrier::new(3));
    let release = Arc::new(Barrier::new(3));
    for _ in 0..2 {
        let (b, r) = (Arc::clone(&blocked), Arc::clone(&release));
        exec.enqueue_root(MossRootDescriptor::one_way(next_root_id(), move || {
            solo_enter_current("fsync"); b.wait(); r.wait(); solo_leave_current("fsync");
        }));
    }
    blocked.wait();
    wait_until("compensation spawned", || evlog::live() == 4);
    assert_eq!(moss_executor_worker_count(), 4);
    release.wait();
    wait_until("excess workers exit", || moss_executor_worker_count() == 2 && evlog::live() == 2);
    exec.join();
    assert_eq!(evlog::live(), 0);
    assert!(evlog::peak() <= 6);
    println!("ok");
}
"""))

T.append(RustTest("21-runtime-invoke-active-admission", "runtime_invoke while ACTIVE uses Executor admission", r"""
fn main() {
    let exec = MossExecutor::new().threads(2).queue_capacity(8).start();
    let (worker, root) = std::thread::spawn(|| runtime_invoke(|| (current_worker_id(), current_root_id()))).join().unwrap();
    assert!(worker.is_some(), "ACTIVE runtime_invoke did not run on an Executor worker");
    assert!(root.is_some());
    assert_eq!(moss_executor_admission_tickets(), (1, 1));
    exec.join();
    println!("ok");
}
"""))

T.append(RustTest("22-runtime-invoke-returns-reply", "runtime_invoke returns the handler reply", r"""
fn main() {
    assert_eq!(runtime_invoke(|| 42u64 * 2), 84);
    assert_eq!(runtime_invoke(|| "hello".to_string()), "hello");
    let exec = MossExecutor::new().threads(2).start();
    assert_eq!(runtime_invoke(|| vec![1, 2, 3]), vec![1, 2, 3]);
    exec.join();
    println!("ok");
}
"""))

T.append(RustTest("23-runtime-invoke-many-hosts-active", "Concurrent host callers each receive their own reply while ACTIVE", r"""
fn main() {
    let exec = MossExecutor::new().threads(3).queue_capacity(2).start();
    let hosts: Vec<_> = (0..16u64).map(|i| std::thread::spawn(move || runtime_invoke(move || i * 10))).collect();
    for (i, h) in hosts.into_iter().enumerate() { assert_eq!(h.join().unwrap(), i as u64 * 10); }
    exec.join();
    println!("ok");
}
"""))

T.append(RustTest("24-unadmitted-invoke-during-draining", "runtime_invoke arriving in DRAINING waits, then runs INLINE", r"""
fn main() {
    use std::sync::{Arc, Barrier};
    use std::sync::atomic::{AtomicBool, Ordering};
    let exec = MossExecutor::new().threads(1).queue_capacity(2).start();
    let running = Arc::new(AtomicBool::new(false));
    let release = Arc::new(Barrier::new(2));
    let (r1, rel) = (Arc::clone(&running), Arc::clone(&release));
    exec.enqueue_root(MossRootDescriptor::one_way(next_root_id(), move || { r1.store(true, Ordering::SeqCst); rel.wait(); }));
    wait_until("root running", || running.load(Ordering::SeqCst));
    let joiner = std::thread::spawn(move || exec.join());
    wait_until("DRAINING", || moss_executor_state() == "DRAINING");
    let ran = Arc::new(AtomicBool::new(false));
    let ran2 = Arc::clone(&ran);
    let host = std::thread::spawn(move || runtime_invoke(move || {
        ran2.store(true, Ordering::SeqCst);
        (moss_executor_state(), current_worker_id())
    }));
    wait_until("host ticket issued", || moss_runtime_inline_tickets().0 == 1);
    assert!(!ran.load(Ordering::SeqCst), "unadmitted caller ran during DRAINING");
    release.wait();
    joiner.join().unwrap();
    let (state, worker) = host.join().unwrap();
    assert_eq!(state, "INLINE");
    assert_eq!(worker, None);
    println!("ok");
}
"""))

T.append(RustTest("25-join-waits-for-admitted-root", "join() waits for every admitted Root", r"""
fn main() {
    use std::sync::Arc;
    use std::sync::atomic::{AtomicBool, Ordering};
    let exec = MossExecutor::new().threads(2).queue_capacity(8).start();
    let completed = Arc::new(AtomicBool::new(false));
    let c2 = Arc::clone(&completed);
    exec.enqueue_root(MossRootDescriptor::one_way(next_root_id(), move || {
        wait_until("join() entered DRAINING", || moss_executor_state() == "DRAINING");
        c2.store(true, Ordering::SeqCst);
    }));
    exec.join();
    assert!(completed.load(Ordering::SeqCst));
    println!("ok");
}
"""))

T.append(RustTest("26-post-join-invoke-valid", "runtime_invoke stays valid after join (INLINE)", r"""
fn main() {
    MossExecutor::new().threads(2).start().join();
    assert_eq!(runtime_invoke(|| (99u32, current_worker_id())), (99, None));
    println!("ok");
}
"""))

T.append(RustTest("27-join-joins-all-workers", "join() joins every physical worker before returning INLINE", r"""
fn main() {
    use std::sync::{Arc, Barrier};
    evlog::install();
    let exec = MossExecutor::new().threads(2).max_threads(5).start();
    assert_eq!(moss_executor_state(), "ACTIVE");
    let b = Arc::new(Barrier::new(3));
    for _ in 0..2 {
        let b2 = Arc::clone(&b);
        exec.enqueue_root(MossRootDescriptor::one_way(next_root_id(), move || { solo_enter_current("x"); b2.wait(); solo_leave_current("x"); }));
    }
    b.wait();
    exec.join();
    assert_eq!(moss_executor_state(), "INLINE");
    assert_eq!(evlog::live(), 0, "a worker was still running after join()");
    assert_eq!(evlog::count(MossRtEvent::WorkerSpawned), evlog::count(MossRtEvent::WorkerExited));
    println!("ok");
}
"""))

T.append(RustTest("28-solo-scheduler-stress", "Real-worker Solo + Branch + host ingress stress respects T_max", r"""
fn main() {
    use std::sync::Arc;
    use std::sync::atomic::{AtomicUsize, Ordering};
    evlog::install();
    let tmax = 4usize;
    let exec = MossExecutor::new().threads(2).max_threads(tmax).queue_capacity(8).start();
    let branches = Arc::new(AtomicUsize::new(0));
    std::thread::scope(|s| {
        for _ in 0..2 {
            s.spawn(|| for i in 0..60u64 { assert_eq!(runtime_invoke(move || i + 1), i + 1); });
        }
        for _ in 0..80 {
            let br = Arc::clone(&branches);
            exec.enqueue_root(MossRootDescriptor::one_way(next_root_id(), move || {
                solo_enter_current("read");
                std::thread::yield_now();
                solo_leave_current("read");
                let scope = branch_scope_new_current();
                for _ in 0..4 {
                    let b2 = Arc::clone(&br);
                    branch_publish(&scope, move || {
                        solo_enter_current("chunk-read");
                        std::thread::yield_now();
                        solo_leave_current("chunk-read");
                        b2.fetch_add(1, Ordering::SeqCst);
                    });
                }
                branch_join(scope);
            }));
        }
    });
    exec.join();
    assert_eq!(branches.load(Ordering::SeqCst), 320);
    assert!(evlog::peak() <= tmax as isize, "peak workers {} > T_max {}", evlog::peak(), tmax);
    assert_eq!(evlog::live(), 0);
    println!("ok peak={}", evlog::peak());
}
"""))

T.append(RustTest("29-solo-bridge-worker-identity", "Solo bridge is a no-op off-worker and active on workers", r"""
fn main() {
    assert!(!solo_enter_current("host"));
    assert!(!solo_leave_current("host"));
    executor_solo_enter_from_fileio("host-read");
    executor_solo_leave_from_fileio("host-read");
    // INLINE Roots run on the caller thread: not a worker, no compensation.
    assert!(!runtime_invoke(|| solo_enter_current("inline-root")));
    let exec = MossExecutor::new().threads(1).max_threads(3).start();
    let (entered, workers, left) = runtime_invoke(|| {
        let e = solo_enter_current("fileio-fsync");
        let w = moss_executor_worker_count();
        let l = solo_leave_current("fileio-fsync");
        (e, w, l)
    });
    exec.join();
    assert!(entered && left);
    assert_eq!(workers, 2);
    println!("ok");
}
"""))

T.append(RustTest("31-concurrent-start-race", "A second start() while one is pending fails closed", r"""
fn main() {
    use std::sync::{Arc, Barrier, Mutex};
    std::panic::set_hook(Box::new(|_| {}));
    let held = Arc::new(Barrier::new(2));
    let release = Arc::new(Barrier::new(2));
    let (h2, r2) = (Arc::clone(&held), Arc::clone(&release));
    let inline_root = std::thread::spawn(move || runtime_invoke(move || { h2.wait(); r2.wait(); }));
    held.wait();
    let slot = Arc::new(Mutex::new(None));
    let s2 = Arc::clone(&slot);
    let starter = std::thread::spawn(move || { *s2.lock().unwrap() = Some(MossExecutor::new().threads(2).start()); });
    wait_until("start_pending", || moss_runtime_start_pending());
    assert!(std::panic::catch_unwind(|| { MossExecutor::new().threads(2).start(); }).is_err());
    release.wait();
    inline_root.join().unwrap();
    starter.join().unwrap();
    let exec = slot.lock().unwrap().take().unwrap();
    assert_eq!(moss_executor_state(), "ACTIVE");
    exec.join();
    assert_eq!(moss_executor_state(), "INLINE");
    println!("ok");
}
"""))

T.append(RustTest("32-ticket-transfer-inline-to-active", "Waiting INLINE tickets transfer to ACTIVE admission in order", r"""
fn main() {
    use std::sync::{Arc, Barrier, Mutex};
    let held = Arc::new(Barrier::new(2));
    let release = Arc::new(Barrier::new(2));
    let (h2, r2) = (Arc::clone(&held), Arc::clone(&release));
    let blocker = std::thread::spawn(move || runtime_invoke(move || { h2.wait(); r2.wait(); }));
    held.wait();
    let order = Arc::new(Mutex::new(Vec::new()));
    let mut hosts = Vec::new();
    for i in 1..=3u64 {
        let o = Arc::clone(&order);
        hosts.push(std::thread::spawn(move || runtime_invoke(move || { o.lock().unwrap().push(i); i * 100 })));
        wait_until("host ticket", || moss_runtime_inline_tickets().0 == i + 1);
    }
    let starter = std::thread::spawn(|| MossExecutor::new().threads(1).max_threads(1).start());
    wait_until("start_pending", || moss_runtime_start_pending());
    release.wait();
    blocker.join().unwrap();
    let exec = starter.join().unwrap();
    for (i, h) in hosts.into_iter().enumerate() { assert_eq!(h.join().unwrap(), (i as u64 + 1) * 100); }
    assert_eq!(*order.lock().unwrap(), vec![1, 2, 3], "single worker must observe FIFO admission");
    exec.join();
    assert_eq!(runtime_invoke(|| 300u32), 300);
    assert_eq!(moss_runtime_inline_tickets(), (5, 5));
    println!("ok");
}
"""))

T.append(RustTest("33-tmax-compensation-cap", "Compensation never exceeds T_max; excess Root waits for a worker", r"""
fn main() {
    use std::sync::{Arc, Barrier};
    use std::sync::atomic::{AtomicBool, Ordering};
    evlog::install();
    let (target, tmax) = (2usize, 4usize);
    let exec = MossExecutor::new().threads(target).max_threads(tmax).queue_capacity(16).start();
    let all_blocked = Arc::new(Barrier::new(tmax + 1));
    let release = Arc::new(Barrier::new(tmax + 1));
    for _ in 0..tmax {
        let (b, r) = (Arc::clone(&all_blocked), Arc::clone(&release));
        exec.enqueue_root(MossRootDescriptor::one_way(next_root_id(), move || {
            solo_enter_current("t-max"); b.wait(); r.wait(); solo_leave_current("t-max");
        }));
    }
    all_blocked.wait(); // only possible with tmax concurrent workers
    let fifth = Arc::new(AtomicBool::new(false));
    let f2 = Arc::clone(&fifth);
    exec.enqueue_root(MossRootDescriptor::one_way(next_root_id(), move || f2.store(true, Ordering::SeqCst)));
    assert_eq!(moss_executor_worker_count(), tmax);
    assert_eq!(moss_executor_queue_len(), 1, "a fifth worker was activated beyond T_max");
    assert!(!fifth.load(Ordering::SeqCst));
    release.wait();
    exec.join();
    assert!(fifth.load(Ordering::SeqCst));
    assert_eq!(evlog::peak(), tmax as isize);
    println!("ok");
}
"""))

T.append(RustTest("34-config-aware-root-start-seam", "moss_root_start_with_config propagates every configuration field", r"""
fn main() {
    use std::sync::Arc;
    use std::sync::atomic::{AtomicUsize, Ordering};
    evlog::install();
    let cfg = MossExecutorConfig {
        threads: 3, max_threads: 7, queue_capacity: 5,
        affinity: Some(vec![0]), priority: Some(0),
    };
    let exec = moss_root_start_with_config(cfg.clone());
    assert_eq!(exec.config(), &cfg);
    assert_eq!(moss_executor_active_config(), Some(cfg.clone()));
    wait_until("target workers started", || evlog::live() == 3);
    assert_eq!(moss_executor_worker_count(), 3);
    assert_eq!(moss_executor_max_threads(), 7);
    let n = Arc::new(AtomicUsize::new(0));
    for _ in 0..10 { let c = Arc::clone(&n); moss_root_submit(&exec, move || { c.fetch_add(1, Ordering::SeqCst); }); }
    moss_root_submit_with_target(&exec, "entity-v1:handler:Worker.Process", || {});
    moss_root_join(exec);
    assert_eq!(n.load(Ordering::SeqCst), 10);
    // Builder and seam resolve identically; omitted fields get defaults.
    let built = MossExecutor::new().threads(3).max_threads(7).queue_capacity(5).affinity(vec![0]).priority(0).config();
    assert_eq!(built, cfg);
    let resolved = MossExecutorConfig::resolved(Some(64), None, None, None, None);
    assert!(resolved.max_threads >= 64, "default T_max below threads");
    assert_eq!(resolved.queue_capacity, MossExecutorConfig::DEFAULT_QUEUE_CAPACITY);
    let d = moss_root_start();
    assert_eq!(d.config(), &MossExecutorConfig::default());
    moss_root_join(d);
    std::panic::set_hook(Box::new(|_| {}));
    let bad = MossExecutorConfig { threads: 8, max_threads: 2, ..MossExecutorConfig::default() };
    assert!(std::panic::catch_unwind(move || { moss_root_start_with_config(bad); }).is_err());
    assert_eq!(moss_executor_state(), "INLINE");
    println!("ok");
}
"""))

T.append(RustTest("35-linear-handle-worker-enqueue-rejected", "A running Root cannot enqueue Roots or join the Executor", r"""
fn main() {
    use std::sync::Arc;
    use std::sync::atomic::{AtomicBool, Ordering};
    let exec = MossExecutor::new().threads(2).start();
    let rejected = Arc::new(AtomicBool::new(false));
    let r2 = Arc::clone(&rejected);
    exec.enqueue_root(MossRootDescriptor::one_way(next_root_id(), move || {
        let gx = process_rt().active_executor().unwrap();
        let handle = std::mem::ManuallyDrop::new(MossExecutorHandle { gx });
        let bad = std::panic::catch_unwind(std::panic::AssertUnwindSafe(|| {
            handle.enqueue_root(MossRootDescriptor::one_way(next_root_id(), || {}));
        }));
        r2.store(bad.is_err(), Ordering::SeqCst);
    }));
    exec.join();
    assert!(rejected.load(Ordering::SeqCst));
    println!("ok");
}
"""))

T.append(RustTest("36-b-style-hook-table-bridge", "B-style FileIO hook table forwards Solo into C compensation", r"""
// TEST-ONLY fixture with the FileIO root runtime's hook-table shape.  The
// production table and the process-wide moss_solo_enter/moss_solo_leave
// symbols belong to the FileIO runtime, not to the Executor runtime.
mod fileio_hook_fixture {
    static ENTER: std::sync::Mutex<Option<fn(&str)>> = std::sync::Mutex::new(None);
    static LEAVE: std::sync::Mutex<Option<fn(&str)>> = std::sync::Mutex::new(None);
    pub fn moss_set_solo_hooks(enter: fn(&str), leave: fn(&str)) {
        *ENTER.lock().unwrap() = Some(enter);
        *LEAVE.lock().unwrap() = Some(leave);
    }
    #[no_mangle]
    pub extern "Rust" fn moss_solo_enter(reason: &str) { let h = *ENTER.lock().unwrap(); if let Some(f) = h { f(reason) } }
    #[no_mangle]
    pub extern "Rust" fn moss_solo_leave(reason: &str) { let h = *LEAVE.lock().unwrap(); if let Some(f) = h { f(reason) } }
}

fn main() {
    use fileio_hook_fixture::*;
    evlog::install();
    moss_set_solo_hooks(executor_fileio_solo_enter_callback(), executor_fileio_solo_leave_callback());
    // Off-worker FileIO: hook runs, no compensation, no manufactured worker.
    moss_solo_enter("host-open");
    moss_solo_leave("host-open");
    assert_eq!(evlog::count(MossRtEvent::SoloEnter), 0);
    let exec = MossExecutor::new().threads(1).max_threads(3).start();
    let (during, blocked, after) = runtime_invoke(|| {
        moss_solo_enter("fileio-pread");
        let during = moss_executor_worker_count();
        let blocked = moss_executor_solo_blocked();
        moss_solo_leave("fileio-pread");
        (during, blocked, moss_executor_solo_blocked())
    });
    exec.join();
    assert_eq!((during, blocked, after), (2, 1, 0));
    assert_eq!(evlog::count(MossRtEvent::SoloEnter), 1);
    assert_eq!(evlog::count(MossRtEvent::SoloLeave), 1);
    println!("ok");
}
"""))

T.append(RustTest("38-branch-lost-wakeup", "Publication between an idle worker's predicate check and wait is not lost", r"""
use std::sync::atomic::{AtomicBool, AtomicUsize, Ordering};
static ARMED: AtomicBool = AtomicBool::new(false);
static AT_PREWAIT: AtomicUsize = AtomicUsize::new(usize::MAX);
static SIGNALLED: AtomicBool = AtomicBool::new(false);

fn on_event(ev: MossRtEvent, _a: u64, _b: u64, _w: usize) {
    if ev == MossRtEvent::BranchWakeSignal { SIGNALLED.store(true, Ordering::SeqCst); }
}

// Runs with the idle worker holding the executor lock, after its predicate
// check and before its Condvar wait: the exact lost-wakeup window.
fn prewait(worker: usize) {
    if ARMED.swap(false, Ordering::SeqCst) {
        AT_PREWAIT.store(worker, Ordering::SeqCst);
        // Let the publisher push the Branch and bump the generation now.
        wait_until("publisher signalled", || SIGNALLED.load(Ordering::SeqCst));
    }
}

fn main() {
    use std::sync::Arc;
    moss_rt_set_hook(on_event);
    moss_rt_set_prewait_hook(prewait);
    let exec = MossExecutor::new().threads(2).max_threads(2).start();
    let ran_on = runtime_invoke(|| {
        let me = current_worker_id().unwrap();
        ARMED.store(true, Ordering::SeqCst);
        // Make the idle worker re-run its predicate so it reaches the hook.
        let gx = process_rt().active_executor().unwrap();
        let inner = gx.lock_inner();
        gx.work_cv.notify_all(&inner);
        drop(inner);
        wait_until("idle worker in pre-wait window", || AT_PREWAIT.load(Ordering::SeqCst) != usize::MAX);
        let idle = AT_PREWAIT.load(Ordering::SeqCst);
        assert_ne!(idle, me);
        let ran_on = Arc::new(AtomicUsize::new(usize::MAX));
        let r2 = Arc::clone(&ran_on);
        let scope = branch_scope_new_current();
        branch_publish(&scope, move || r2.store(current_worker_id().unwrap(), Ordering::SeqCst));
        // No further publication or Root: only the original wake can run it.
        wait_until("idle worker woke and ran the Branch", || ran_on.load(Ordering::SeqCst) != usize::MAX);
        branch_join(scope);
        let worker = ran_on.load(Ordering::SeqCst);
        assert_eq!(worker, idle);
        worker
    });
    exec.join();
    println!("ok ran_on={}", ran_on);
}
"""))

T.append(RustTest("39-branch-scope-abi-ownership", "Raw Branch ABI transfers each reference count exactly once", r"""
use std::sync::Arc;
use std::sync::atomic::{AtomicUsize, Ordering};
unsafe extern "C" fn invoke(data: *mut ()) { let c = unsafe { Box::from_raw(data as *mut Arc<AtomicUsize>) }; c.fetch_add(1, Ordering::SeqCst); }
unsafe extern "C" fn discard(data: *mut ()) { drop(unsafe { Box::from_raw(data as *mut Arc<AtomicUsize>) }); }

// `queued` is the deterministic ready-queue reference: 0 in INLINE mode, 1
// when the Root occupies the only worker so no worker pops the scope.  Join
// releases that reference along with the joiner's own count.
fn exact_cycle(queued: usize) {
    let n = Arc::new(AtomicUsize::new(0));
    let owned = __moss_branch_scope_new_current();
    let id = scope_track::track(owned);
    assert_eq!(scope_track::strong(id), 1);
    let clone = unsafe { __moss_branch_scope_clone(owned) };
    assert_eq!(clone, owned, "clone must name the same scope");
    assert_eq!(scope_track::strong(id), 2);
    for _ in 0..3 {
        let data = Box::into_raw(Box::new(Arc::clone(&n))) as *mut ();
        unsafe { __moss_branch_publish_trampoline(clone, data, invoke, discard) };
    }
    assert_eq!(scope_track::strong(id), 2 + queued, "publish must only borrow the scope");
    unsafe { __moss_branch_join(owned) };
    assert_eq!(scope_track::strong(id), 1, "join must consume one count and release the ready-queue entry");
    assert_eq!(moss_executor_ready_scopes(), 0, "a joined scope stayed in the ready queue");
    unsafe { __moss_branch_scope_drop(clone) };
    assert_eq!(scope_track::strong(id), 0, "drop must consume exactly one count");
    assert_eq!(n.load(Ordering::SeqCst), 3);
}

fn main() {
    runtime_invoke(|| exact_cycle(0));
    let exec = MossExecutor::new().threads(1).max_threads(1).start();
    runtime_invoke(|| for _ in 0..50 { exact_cycle(1) });
    exec.join();
    // Free-worker stress: counts race with workers, but the clone must keep
    // the scope alive after join, and nothing may leak.
    let exec = MossExecutor::new().threads(3).start();
    let total = runtime_invoke(|| {
        let n = Arc::new(AtomicUsize::new(0));
        for _ in 0..500 {
            let owned = __moss_branch_scope_new_current();
            let id = scope_track::track(owned);
            let clone = unsafe { __moss_branch_scope_clone(owned) };
            for _ in 0..3 {
                let data = Box::into_raw(Box::new(Arc::clone(&n))) as *mut ();
                unsafe { __moss_branch_publish_trampoline(clone, data, invoke, discard) };
            }
            unsafe { __moss_branch_join(owned) };
            assert!(scope_track::strong(id) >= 1, "join released the clone's count");
            unsafe { __moss_branch_scope_drop(clone) };
        }
        n.load(Ordering::SeqCst)
    });
    exec.join();
    assert_eq!(total, 1500);
    assert!(scope_track::all_released(), "a scope leaked");
    assert_eq!(moss_live_branch_scopes(), 0, "scope leaked or double-dropped");
    println!("ok");
}
"""))

T.append(RustTest("40-current-root-identity", "Current Root identity is set on every ingress path and for Branches", r"""
fn main() {
    use std::sync::{Arc, Barrier, Mutex};
    use std::sync::atomic::{AtomicBool, Ordering};
    std::panic::set_hook(Box::new(|_| {}));
    assert_eq!(current_root_id(), None);
    assert!(std::panic::catch_unwind(|| branch_scope_new_current()).is_err(), "scope outside a Root must fail closed");
    // INLINE runtime_invoke.
    let inline_id = runtime_invoke(|| current_root_id());
    assert!(inline_id.is_some());
    assert_eq!(current_root_id(), None, "Root identity leaked to the host thread");
    // Worker Root and its Branches (on free workers and helped).
    let exec = MossExecutor::new().threads(3).start();
    let seen = Arc::new(Mutex::new(Vec::new()));
    let s2 = Arc::clone(&seen);
    let rid = next_root_id();
    exec.enqueue_root(MossRootDescriptor::one_way(rid, move || {
        let me = current_root_id().unwrap();
        s2.lock().unwrap().push(("root", me));
        let scope = branch_scope_new_current();
        assert_eq!(scope.owner_root_id(), me);
        for _ in 0..6 { let s3 = Arc::clone(&s2); branch_publish(&scope, move || s3.lock().unwrap().push(("branch", current_root_id().unwrap()))); }
        branch_join(scope);
    }));
    // A host Root on the Executor cannot join a scope owned by another Root.
    let other_scope = Arc::new(Mutex::new(None));
    let os2 = Arc::clone(&other_scope);
    runtime_invoke(move || { *os2.lock().unwrap() = Some(branch_scope_new_current()); });
    let foreign = other_scope.lock().unwrap().take().unwrap();
    let rejected = runtime_invoke(move || std::panic::catch_unwind(std::panic::AssertUnwindSafe(|| branch_join(foreign))).is_err());
    assert!(rejected, "a Root joined another Root's scope");
    // DRAINING fallback Root.
    let running = Arc::new(AtomicBool::new(false));
    let release = Arc::new(Barrier::new(2));
    let (r1, rel) = (Arc::clone(&running), Arc::clone(&release));
    exec.enqueue_root(MossRootDescriptor::one_way(next_root_id(), move || { r1.store(true, Ordering::SeqCst); rel.wait(); }));
    wait_until("blocker running", || running.load(Ordering::SeqCst));
    let joiner = std::thread::spawn(move || exec.join());
    wait_until("DRAINING", || moss_executor_state() == "DRAINING");
    let fallback = std::thread::spawn(|| runtime_invoke(|| current_root_id()));
    wait_until("fallback waiting", || moss_runtime_inline_tickets().0 == 4);
    release.wait();
    joiner.join().unwrap();
    assert!(fallback.join().unwrap().is_some());
    let seen = seen.lock().unwrap();
    assert_eq!(seen.len(), 7);
    assert!(seen.iter().all(|(_, id)| *id == rid), "Branch ran outside its owner's Root context: {:?}", *seen);
    println!("ok");
}
"""))

T.append(RustTest("41-branch-scope-one-shot", "Branch scopes are one-shot: no publish after join, no second join", r"""
fn main() {
    std::panic::set_hook(Box::new(|_| {}));
    let exec = MossExecutor::new().threads(2).start();
    let (late_publish, double_join) = runtime_invoke(|| {
        let scope = branch_scope_new_current();
        let keep = scope.clone();
        branch_publish(&scope, || {});
        branch_join(scope);
        let late = std::panic::catch_unwind(std::panic::AssertUnwindSafe(|| branch_publish(&keep, || {}))).is_err();
        let again = keep.clone();
        let twice = std::panic::catch_unwind(std::panic::AssertUnwindSafe(|| branch_join(again))).is_err();
        (late, twice)
    });
    exec.join();
    assert!(late_publish && double_join);
    println!("ok");
}
"""))

T.append(RustTest("42-ready-scope-queue-bounded", "Each scope occupies at most one ready-queue entry", r"""
fn main() {
    let exec = MossExecutor::new().threads(1).max_threads(1).start();
    let peak = runtime_invoke(|| {
        let scopes: Vec<_> = (0..3).map(|_| branch_scope_new_current()).collect();
        let mut peak = 0;
        for i in 0..300 {
            branch_publish(&scopes[i % 3], || {});
            peak = peak.max(moss_executor_ready_scopes());
        }
        for s in scopes { branch_join(s); }
        peak
    });
    exec.join();
    assert!(peak <= 3, "ready-scope queue grew to {} for 3 scopes", peak);
    println!("ok peak={}", peak);
}
"""))

T.append(RustTest("46-joined-scopes-leave-ready-queue", "Joined scopes leave the ready queue even when every worker is busy", r"""
fn main() {
    // threads=1: the Root occupies the only worker, so no free worker ever
    // pops the ready queue.  Joined scopes must not accumulate there.
    let exec = MossExecutor::new().threads(1).max_threads(1).start();
    let (peak, ready, live) = runtime_invoke(|| {
        let mut peak = 0;
        for _ in 0..5000 {
            let s = branch_scope_new_current();
            for _ in 0..3 { branch_publish(&s, || {}); }
            peak = peak.max(moss_executor_ready_scopes());
            branch_join(s);
        }
        (peak, moss_executor_ready_scopes(), moss_live_branch_scopes())
    });
    exec.join();
    assert_eq!(peak, 1, "one live scope must occupy at most one entry");
    assert_eq!(ready, 0, "joined scopes stayed in the ready queue");
    assert_eq!(live, 0, "joined scope states were retained by the ready queue");
    assert_eq!(moss_live_branch_scopes(), 0);
    println!("ok");
}
"""))

T.append(RustTest("47-joined-scope-in-transit-race", "A ready-queue entry re-pushed while its scope joins is released", r"""
fn main() {
    use std::sync::Arc;
    use std::sync::atomic::{AtomicUsize, Ordering};
    // Free workers pop, run, and re-push scope entries while the owner joins
    // the same scope, exercising the in-transit entry path.
    let exec = MossExecutor::new().threads(4).max_threads(4).start();
    let total = Arc::new(AtomicUsize::new(0));
    let t2 = Arc::clone(&total);
    let roots: Vec<_> = (0..3).map(|_| {
        let t = Arc::clone(&t2);
        std::thread::spawn(move || runtime_invoke(move || {
            for _ in 0..2000 {
                let s = branch_scope_new_current();
                for _ in 0..4 {
                    let t = Arc::clone(&t);
                    branch_publish(&s, move || { t.fetch_add(1, Ordering::Relaxed); });
                }
                branch_join(s);
            }
        }))
    }).collect();
    for r in roots { r.join().unwrap(); }
    // An in-transit pusher releases its stale entry right after re-pushing.
    wait_until("ready queue empty", || moss_executor_ready_scopes() == 0);
    wait_until("no live scope", || moss_live_branch_scopes() == 0);
    exec.join();
    assert_eq!(total.load(Ordering::SeqCst), 3 * 2000 * 4);
    println!("ok");
}
"""))

T.append(RustTest("48-tmax-physical-thread-slots", "A retiring compensation worker holds its T_max slot until its thread body ends", r"""
fn main() {
    // Every leave makes the compensation worker excess; every enter wants a
    // replacement.  The replacement may only reuse a slot after the retiring
    // thread body has returned, so live worker threads never exceed T_max.
    let exec = MossExecutor::new().threads(1).max_threads(2).start();
    let (worst, spawned) = runtime_invoke(|| {
        evlog::install();
        let mut worst = 0;
        for _ in 0..3000 {
            solo_enter_current("stress");
            worst = worst.max(moss_executor_live_threads());
            solo_leave_current("stress");
            worst = worst.max(moss_executor_live_threads());
        }
        (worst, evlog::count(MossRtEvent::WorkerSpawned))
    });
    wait_until("compensation retired", || moss_executor_live_threads() == 1);
    exec.join();
    assert!(worst <= 2, "live worker threads reached {} with max_threads=2", worst);
    assert!(spawned >= 1, "no compensation worker was ever activated");
    assert_eq!(moss_executor_live_threads(), 0);
    println!("ok worst={} spawned={}", worst, spawned);
}
"""))

T.append(RustTest("44-draining-denied-caller-keeps-fifo", "A caller denied by DRAINING keeps its FIFO place for the inline run", r"""
fn main() {
    use std::sync::{Arc, Barrier, Mutex};
    use std::sync::atomic::{AtomicBool, Ordering};
    let exec = MossExecutor::new().threads(1).max_threads(1).queue_capacity(1).start();
    let running = Arc::new(AtomicBool::new(false));
    let release = Arc::new(Barrier::new(2));
    let (r1, rel) = (Arc::clone(&running), Arc::clone(&release));
    exec.enqueue_root(MossRootDescriptor::one_way(next_root_id(), move || { r1.store(true, Ordering::SeqCst); rel.wait(); }));
    wait_until("blocker running", || running.load(Ordering::SeqCst));
    exec.enqueue_root(MossRootDescriptor::one_way(next_root_id(), || {}));  // queue now full
    let order = Arc::new(Mutex::new(Vec::new()));
    let o1 = Arc::clone(&order);
    let h1 = std::thread::spawn(move || runtime_invoke(move || o1.lock().unwrap().push((1, moss_executor_state()))));
    wait_until("H1 waiting in admission", || moss_executor_admission_tickets() == (3, 2));
    let o2 = Arc::clone(&order);
    let h2 = std::thread::spawn(move || runtime_invoke(move || o2.lock().unwrap().push((2, moss_executor_state()))));
    wait_until("H2 ticket", || moss_runtime_inline_tickets().0 == 2);
    let joiner = std::thread::spawn(move || exec.join());
    wait_until("DRAINING", || moss_executor_state() == "DRAINING");
    release.wait();
    joiner.join().unwrap();
    h1.join().unwrap();
    h2.join().unwrap();
    assert_eq!(*order.lock().unwrap(), vec![(1, "INLINE"), (2, "INLINE")]);
    assert_eq!(moss_runtime_inline_tickets(), (2, 2));
    println!("ok");
}
"""))

T.append(RustTest("45-draining-denied-caller-survives-restart", "A DRAINING-denied caller re-admits through a restarted Executor", r"""
fn main() {
    use std::sync::{Arc, Barrier};
    use std::sync::atomic::{AtomicBool, Ordering};
    let exec = MossExecutor::new().threads(1).max_threads(1).queue_capacity(1).start();
    let running = Arc::new(AtomicBool::new(false));
    let release = Arc::new(Barrier::new(2));
    let (r1, rel) = (Arc::clone(&running), Arc::clone(&release));
    exec.enqueue_root(MossRootDescriptor::one_way(next_root_id(), move || { r1.store(true, Ordering::SeqCst); rel.wait(); }));
    wait_until("blocker running", || running.load(Ordering::SeqCst));
    exec.enqueue_root(MossRootDescriptor::one_way(next_root_id(), || {}));
    // Deny H1 during DRAINING; the joining thread restarts immediately.
    let h1 = std::thread::spawn(|| runtime_invoke(|| (current_worker_id().is_some(), 7u8)));
    wait_until("H1 waiting in admission", || moss_executor_admission_tickets() == (3, 2));
    // Restart from the joining thread as soon as INLINE returns.
    let restarter = std::thread::spawn(move || { exec.join(); MossExecutor::new().threads(1).start() });
    wait_until("DRAINING", || moss_executor_state() == "DRAINING");
    release.wait();
    let exec2 = restarter.join().unwrap();
    let (on_worker, v) = h1.join().unwrap();
    assert_eq!(v, 7);
    // Either path is correct (INLINE before the restart, or re-admitted to
    // the new Executor); before the fix a restart stranded H1 forever.
    println!("path={}", if on_worker { "readmitted" } else { "inline" });
    exec2.join();
    assert_eq!(runtime_invoke(|| 1u8), 1);
    println!("ok");
}
"""))

T.append(RustTest("50-root-admission-full-queue-reentrancy-rejected", "Root A on single-worker single-slot-queue attempting Root admission while queue is full is rejected immediately without deadlocking", r"""
fn main() {
    use std::sync::{Arc, Barrier};
    use std::sync::atomic::{AtomicBool, Ordering};
    std::panic::set_hook(Box::new(|_| {}));
    // 1 worker, 1 queue slot: the exact topology where reentrant admission would deadlock.
    let exec = MossExecutor::new().threads(1).max_threads(1).queue_capacity(1).start();
    let running = Arc::new(AtomicBool::new(false));
    let queue_full_barrier = Arc::new(Barrier::new(2));
    let b_ran = Arc::new(AtomicBool::new(false));
    let reentrancy_rejected = Arc::new(AtomicBool::new(false));
    let tickets_unmodified = Arc::new(AtomicBool::new(false));
    let (r1, qb, b_flag, rej, t_ok) = (
        Arc::clone(&running),
        Arc::clone(&queue_full_barrier),
        Arc::clone(&b_ran),
        Arc::clone(&reentrancy_rejected),
        Arc::clone(&tickets_unmodified),
    );

    // 1. Root A starts on the only worker
    exec.enqueue_root(MossRootDescriptor::one_way(next_root_id(), move || {
        r1.store(true, Ordering::SeqCst);
        // 3. Root A waits for main to enqueue Root B and fill the queue
        qb.wait();

        // 7. Root B now definitely occupies the 1 available queue slot.
        let before_tickets = moss_executor_admission_tickets();

        // Root A attempts to submit Root C directly via the executor handle
        let gx = process_rt().active_executor().unwrap();
        let handle = std::mem::ManuallyDrop::new(MossExecutorHandle { gx });
        let result = std::panic::catch_unwind(std::panic::AssertUnwindSafe(|| {
            handle.enqueue_root(MossRootDescriptor::one_way(next_root_id(), || {}));
        }));
        if result.is_err() {
            rej.store(true, Ordering::SeqCst);
        }

        let after_tickets = moss_executor_admission_tickets();
        if before_tickets == after_tickets {
            t_ok.store(true, Ordering::SeqCst);
        }
        // 9. Root A completes
    }));

    // 2. Wait until Root A starts on the worker
    wait_until("Root A running", || running.load(Ordering::SeqCst));

    // 4. main enqueues Root B
    exec.enqueue_root(MossRootDescriptor::one_way(next_root_id(), move || {
        b_flag.store(true, Ordering::SeqCst);
    }));

    // 5. Verify B is queued and queue is full
    assert_eq!(moss_executor_queue_len(), 1, "Root B must be queued");

    // 6. Signal Root A that B has filled the queue
    queue_full_barrier.wait();

    // 10. Root B runs after Root A completes; 11. join completes
    exec.join();

    assert!(reentrancy_rejected.load(Ordering::SeqCst), "Root A reentrant admission attempt did not fail closed");
    assert!(tickets_unmodified.load(Ordering::SeqCst), "Root A admission attempt allocated admission ticket despite being rejected");
    assert!(b_ran.load(Ordering::SeqCst), "Root B did not run after Root A completed");
    println!("ok");
}
"""))

T.append(RustTest("51-branch-context-root-ingress-rejected", "A compiler Branch inherits the Root context and rejects root admission while Branch publish/join succeeds", r"""
fn main() {
    use std::sync::Arc;
    use std::sync::atomic::{AtomicBool, Ordering};
    let exec = MossExecutor::new().threads(2).start();
    let (branch_ran, ingress_rejected) = runtime_invoke(|| {
        let ran = Arc::new(AtomicBool::new(false));
        let rej = Arc::new(AtomicBool::new(false));
        let scope = branch_scope_new_current();
        let (r, j) = (Arc::clone(&ran), Arc::clone(&rej));
        branch_publish(&scope, move || {
            // Check that we have a root context in the branch
            assert!(current_root_id().is_some(), "Branch should inherit root ID");
            let ingress_res = std::panic::catch_unwind(std::panic::AssertUnwindSafe(|| {
                runtime_invoke(|| 99u8);
            }));
            if ingress_res.is_err() {
                j.store(true, Ordering::SeqCst);
            }
            r.store(true, Ordering::SeqCst);
        });
        branch_join(scope);
        (ran.load(Ordering::SeqCst), rej.load(Ordering::SeqCst))
    });
    exec.join();
    assert!(branch_ran, "Branch did not run");
    assert!(ingress_rejected, "Root ingress from Branch context was not rejected");
    println!("ok");
}
"""))

T.append(RustTest("52-active-recursive-runtime-invoke-rejected", "Recursive runtime_invoke from inside an active Root fails closed before admission", r"""
fn main() {
    use std::sync::Arc;
    use std::sync::atomic::{AtomicBool, Ordering};
    let exec = MossExecutor::new().threads(2).start();
    let rejected = Arc::new(AtomicBool::new(false));
    let r = Arc::clone(&rejected);
    let val = runtime_invoke(move || {
        let nested = std::panic::catch_unwind(std::panic::AssertUnwindSafe(|| {
            runtime_invoke(|| 100u8);
        }));
        if nested.is_err() {
            r.store(true, Ordering::SeqCst);
        }
        50u8
    });
    exec.join();
    assert_eq!(val, 50);
    assert!(rejected.load(Ordering::SeqCst), "Recursive runtime_invoke was not rejected");
    println!("ok");
}
"""))


# ════════════════════════════════════════════════════════════════════════════
# Static, cross-crate, and build-pipeline tests
# ════════════════════════════════════════════════════════════════════════════

def test_symbol_ownership(root_rs: str, module_root_rs: str, provider_rs: str, selected_rs: str):
    name = "37-symbol-ownership"
    print(f"Test {name}: Executor owns no FileIO Solo symbols; roles emit the right ABI side")
    problems = []
    for label, text in (("root", root_rs), ("module-root", module_root_rs), ("provider", provider_rs),
                        ("selected", selected_rs)):
        if text.count("// Phase 20 Executor Runtime  (") != 1 or text.count("// ─── End Phase 20 Executor Runtime") != 1:
            problems.append(f"{label} lacks exactly one delimited runtime block")
        runtime = executor_runtime_block(text)
        for forbidden in ("fn moss_solo_enter", "fn moss_solo_leave", "__moss_process_runtime_raw",
                          "__moss_next_root_id", "__moss_next_branch_id", "fn __moss_branch_scope_new("):
            if forbidden in runtime:
                problems.append(f"{label} emits {forbidden}")
    for label, text in (("root", root_rs), ("module-root", module_root_rs)):
        runtime = executor_runtime_block(text)
        for required in ("pub extern \"C\" fn __moss_branch_scope_new_current", "struct MossProcessRuntime",
                         "pub fn executor_fileio_solo_enter_callback", "pub fn moss_root_start_with_config"):
            if required not in runtime:
                problems.append(f"{label} lacks {required}")
    provider_runtime = executor_runtime_block(provider_rs)
    for forbidden in ("struct MossProcessRuntime", "struct MossGlobalExec", "fn runtime_invoke",
                      "#[no_mangle]\npub extern \"C\" fn __moss_branch"):
        if forbidden in provider_runtime:
            problems.append(f"provider emits {forbidden}")
    if "fn __moss_branch_scope_new_current() -> *const ();" not in provider_runtime:
        problems.append("provider lacks the opaque Branch ABI import")
    selected_runtime = executor_runtime_block(selected_rs)
    # A unit without main: the executable build alone selects the root role.
    for required in ("(moss executor_runtime_rust: process root when built as the executable)",
                     "#[cfg(moss_process_root)]\nmod __moss_executor_runtime {",
                     "#[cfg(not(moss_process_root))]\nmod __moss_executor_runtime {",
                     "pub use __moss_executor_runtime::*;"):
        if required not in selected_runtime:
            problems.append(f"selected-role crate lacks {required!r}")
    report(name, not problems, "\n".join(problems))


def test_handle_not_clone(preamble: str):
    name = "35b-handle-not-clone"
    print(f"Test {name}: MossExecutorHandle is linear (does not implement Clone)")
    src = preamble + "fn main() { let e = MossExecutor::new().start(); let f = e.clone(); f.join(); }\n"
    cr, _ = write_and_compile("t_35b", src)
    ok = cr.returncode != 0 and "clone" in cr.stderr and "MossExecutorHandle" in cr.stderr
    report(name, ok, cr.stderr)


PROVIDER_FUNCTIONS = r"""
/// Publish one Branch and wait (without helping) until a worker runs it.
pub fn provider_branch_runs_on_worker() -> bool {
    let me = std::thread::current().id();
    let ran = std::sync::Arc::new(std::sync::atomic::AtomicU8::new(0));
    let r2 = std::sync::Arc::clone(&ran);
    let scope = branch_scope_new_current();
    branch_publish(&scope, move || {
        let other = std::thread::current().id() != me;
        r2.store(if other { 2 } else { 1 }, std::sync::atomic::Ordering::SeqCst);
    });
    let start = std::time::Instant::now();
    while ran.load(std::sync::atomic::Ordering::SeqCst) == 0 {
        if start.elapsed() > std::time::Duration::from_secs(20) { return false; }
        std::thread::yield_now();
    }
    branch_join(scope);
    ran.load(std::sync::atomic::Ordering::SeqCst) == 2
}

/// The provider wrapper's clone / publish / join / drop must each move
/// exactly the reference counts documented by the ABI.  `queued` is the
/// deterministic ready-queue reference (see the root-side test), which the
/// root releases when the scope is joined.
pub fn provider_refcount_exact(queued: usize, track: fn(*const ()) -> usize, strong: fn(usize) -> usize) -> usize {
    let count = std::sync::Arc::new(std::sync::atomic::AtomicUsize::new(0));
    let scope = branch_scope_new_current();
    let id = track(scope.as_raw_borrowed());
    assert_eq!(strong(id), 1);
    let clone = scope.clone();
    assert_eq!(strong(id), 2);
    for _ in 0..3 {
        let c = std::sync::Arc::clone(&count);
        branch_publish(&clone, move || { c.fetch_add(1, std::sync::atomic::Ordering::SeqCst); });
    }
    assert_eq!(strong(id), 2 + queued);
    branch_join(scope);
    assert_eq!(strong(id), 1, "provider branch_join must transfer exactly one count and release the ready-queue entry");
    drop(clone);
    assert_eq!(strong(id), 0, "provider drop must consume exactly one count");
    count.load(std::sync::atomic::Ordering::SeqCst)
}

/// clone / publish via clone / join original / drop clone, `n` times.
pub fn provider_refcount_stress(n: usize, track: fn(*const ()) -> usize, strong: fn(usize) -> usize) -> usize {
    let count = std::sync::Arc::new(std::sync::atomic::AtomicUsize::new(0));
    for _ in 0..n {
        let scope = branch_scope_new_current();
        let id = track(scope.as_raw_borrowed());
        let clone = scope.clone();
        for _ in 0..3 {
            let c = std::sync::Arc::clone(&count);
            branch_publish(&clone, move || { c.fetch_add(1, std::sync::atomic::Ordering::SeqCst); });
        }
        branch_join(scope);
        assert!(strong(id) >= 1, "join released the clone's reference count");
        drop(clone);
    }
    count.load(std::sync::atomic::Ordering::SeqCst)
}

pub fn provider_scope_outside_root() {
    let _ = branch_scope_new_current();
}
"""

ROOT_PROVIDER_MAIN = r"""
extern crate {crate};
use {crate}::*;

fn main() {
    if std::env::args().nth(1).as_deref() == Some("outside-root") {
        provider_scope_outside_root();
        return;
    }
    let (track, strong) = (scope_track::track as fn(*const ()) -> usize, scope_track::strong as fn(usize) -> usize);
    // INLINE mode: no executor; Branches run inline through the root ABI.
    assert_eq!(runtime_invoke(move || provider_refcount_exact(0, track, strong)), 3);
    // ACTIVE, Root occupying the only worker: exact counts incl. ready queue.
    let exec = MossExecutor::new().threads(1).max_threads(1).start();
    assert_eq!(runtime_invoke(move || (0..50).map(|_| provider_refcount_exact(1, track, strong)).sum::<usize>()), 150);
    exec.join();
    // ACTIVE with free workers: provider Branches run on root workers.
    let exec = MossExecutor::new().threads(3).start();
    assert!(runtime_invoke(provider_branch_runs_on_worker), "provider Branch did not run on a root Executor worker");
    let n = runtime_invoke(move || provider_refcount_stress(2000, track, strong));
    exec.join();
    assert_eq!(n, 6000);
    assert!(scope_track::all_released(), "provider scope leaked");
    assert_eq!(moss_live_branch_scopes(), 0, "provider scopes leaked or were double-dropped");
    println!("ok");
}
"""


def test_cross_crate_provider(module_root_preamble: str, provider_preamble: str):
    name = "30-provider-branches-on-root-executor"
    print(f"Test {name}: provider .rlib publishes Branches executed by the explicit-module root Executor")
    lib_cr, rlib = write_and_compile("p20c_provider", provider_preamble + PROVIDER_FUNCTIONS, crate_type="rlib")
    if lib_cr.returncode != 0:
        report(name, False, "provider rlib compile failed:\n" + lib_cr.stderr)
        return
    main_src = module_root_preamble + TEST_PRELUDE + ROOT_PROVIDER_MAIN.replace("{crate}", "p20c_provider")
    cr, binary = write_and_compile("t_30", main_src, ["--extern", f"p20c_provider={rlib}"])
    if cr.returncode != 0:
        report(name, False, "root compile failed:\n" + cr.stderr)
        return
    r = subprocess.run([str(binary)], capture_output=True, text=True, timeout=RUN_TIMEOUT)
    report(name, r.returncode == 0 and "ok" in r.stdout, f"exit={r.returncode}\n{r.stderr}\n{r.stdout}")
    name = "30b-provider-scope-outside-root-fails-closed"
    print(f"Test {name}: provider scope creation outside a Root fails closed")
    r = subprocess.run([str(binary), "outside-root"], capture_output=True, text=True, timeout=RUN_TIMEOUT)
    report(name, r.returncode != 0 and "outside a running Root" in r.stderr, f"exit={r.returncode}\n{r.stderr}")


def build_role_project():
    """A real two-module unit with main: app is the process root and pricing
    a Provider-role crate.  Returns (app.rs, pricing.rs)."""
    project = TMP_DIR / "role_project"
    shutil.rmtree(project, ignore_errors=True)
    (project / "src").mkdir(parents=True)
    (project / "moss.toml").write_text('[project]\nname = "p20c_role"\nversion = "0.1.0"\n[build]\nsource = "src"\n')
    (project / "src" / "pricing.moss").write_text(
        "module pricing\n\nexport fn notional(value: Int) -> Int:\n  value * 2\n")
    (project / "src" / "main.moss").write_text(
        "module app\nimport pricing\n\nfn main():\n  echo pricing.notional(21)\n")
    p = subprocess.run([str(COMPILER), "build", "--json"], cwd=project, capture_output=True, text=True)
    if p.returncode != 0:
        print("ERROR: role project build failed:", (p.stdout + p.stderr)[:800], file=sys.stderr)
        sys.exit(1)
    out = project / "build" / "debug"
    return (out / "app.rs").read_text(), (out / "pricing.rs").read_text()


LIBRARY_EXECUTABLE_MAIN = r"""fn main() {
    let exec = MossExecutor::new().threads(2).start();
    let n = runtime_invoke(|| {
        let hits = std::sync::Arc::new(std::sync::atomic::AtomicUsize::new(0));
        let scope = branch_scope_new_current();
        for _ in 0..16 {
            let h = std::sync::Arc::clone(&hits);
            branch_publish(&scope, move || { h.fetch_add(1, std::sync::atomic::Ordering::SeqCst); });
        }
        branch_join(scope);
        hits.load(std::sync::atomic::Ordering::SeqCst)
    });
    exec.join();
    assert_eq!(n, 16);
    println!("ok");
}"""


def test_library_only_unit(preamble: str):
    name = "49-library-only-unit-executable-owns-runtime"
    print(f"Test {name}: a unit without main gives its executable build, not its rlib, the runtime")
    project = TMP_DIR / "library_only_project"
    shutil.rmtree(project, ignore_errors=True)
    (project / "src").mkdir(parents=True)
    (project / "tests").mkdir()
    (project / "moss.toml").write_text('[project]\nname = "p20c_lib"\nversion = "0.1.0"\n[build]\nsource = "src"\n')
    (project / "src" / "pricing.moss").write_text(
        "module pricing\n\nexport fn notional(value: Int) -> Int:\n  value * 2\n")
    (project / "tests" / "pricing_test.moss").write_text(
        'module pricing_tests\nimport pricing\n\ntest "notional":\n  assertEqual(pricing.notional(21), 42)\n')
    problems = []
    try:
        p = subprocess.run([str(COMPILER), "build", "--json"], cwd=project, capture_output=True, text=True)
        if p.returncode != 0:
            raise AssertionError(p.stdout + p.stderr)
        artifacts = json.loads(p.stdout)["result"]["artifacts"]
        subprocess.run([artifacts["executable"]], check=True, timeout=RUN_TIMEOUT)
        t = subprocess.run([str(COMPILER), "test"], cwd=project, capture_output=True, text=True, timeout=600)
        if t.returncode != 0:
            problems.append("library-only unit tests failed:\n" + t.stdout + t.stderr)
        out = project / "build" / "debug"
        pricing_rs = (out / "pricing.rs").read_text()
        if "(moss executor_runtime_rust: process root when built as the executable)" not in pricing_rs:
            problems.append("library-only crate does not select its role at executable build")
        # Executable build (as Moss links it): this crate owns a working runtime.
        if pricing_rs.count("fn main() {}") != 1:
            raise AssertionError("library executable stub main not found")
        exe_src = pricing_rs.replace("fn main() {}", LIBRARY_EXECUTABLE_MAIN)
        cr, binary = write_and_compile("t_49_exe", exe_src,
                                       ["--cfg", "moss_process_root", "-D", "warnings", "-L", f"dependency={out}"])
        if cr.returncode != 0:
            problems.append("executable build of the library crate did not link a process runtime:\n" + cr.stderr)
        else:
            r = subprocess.run([str(binary)], capture_output=True, text=True, timeout=RUN_TIMEOUT)
            if r.returncode != 0 or "ok" not in r.stdout:
                problems.append(f"library executable Branch run failed: exit={r.returncode}\n{r.stderr}")
        # Provider build: the published rlib is a wrapper, so a separate root
        # links against it without duplicate runtime symbols.
        rlib = next((Path(x) for x in artifacts["module_rlibs"] if Path(x).name.startswith("libpricing")), None)
        if rlib is None:
            raise AssertionError("no pricing rlib in artifacts")
        main_src = preamble + TEST_PRELUDE + "extern crate moss_pricing;\n" + LIBRARY_EXECUTABLE_MAIN.replace(
            "branch_scope_new_current()", "moss_pricing::branch_scope_new_current()").replace(
            "branch_publish(", "moss_pricing::branch_publish(").replace("branch_join(", "moss_pricing::branch_join(")
        cr, binary = write_and_compile("t_49_root", main_src, ["--extern", f"moss_pricing={rlib}", "-L", f"dependency={out}"])
        if cr.returncode != 0:
            problems.append("root failed to link the library's provider rlib:\n" + cr.stderr)
        else:
            r = subprocess.run([str(binary)], capture_output=True, text=True, timeout=RUN_TIMEOUT)
            if r.returncode != 0 or "ok" not in r.stdout:
                problems.append(f"root + library rlib Branch run failed: exit={r.returncode}\n{r.stderr}")
        report(name, not problems, "\n".join(problems))
    except (AssertionError, subprocess.SubprocessError, KeyError, OSError) as error:
        report(name, False, str(error))


def test_explicit_module_pipeline():
    name = "43-explicit-module-root-and-source-free-provider"
    print(f"Test {name}: moss build gives the explicit-module main crate the runtime; provider crates wrappers")
    project = TMP_DIR / "explicit_module_project"
    shutil.rmtree(project, ignore_errors=True)
    (project / "src").mkdir(parents=True)
    (project / "moss.toml").write_text('[project]\nname = "p20c_roles"\nversion = "0.1.0"\n[build]\nsource = "src"\n')
    (project / "src" / "pricing.moss").write_text(
        "module pricing\n\nexport fn notional(value: Int) -> Int:\n  value * 2\n")
    (project / "src" / "main.moss").write_text(
        "module app\nimport pricing\n\nfn main():\n  echo pricing.notional(21)\n")

    def build():
        p = subprocess.run([str(COMPILER), "build", "--json"], cwd=project, capture_output=True, text=True)
        if p.returncode != 0:
            raise AssertionError(p.stdout + p.stderr)
        return json.loads(p.stdout)["result"]["artifacts"]

    try:
        first = build()
        out = project / "build" / "debug"
        rlib = next((Path(p) for p in first["module_rlibs"] if Path(p).name.startswith("libpricing")), None)
        app_rs, pricing_rs = (out / "app.rs").read_text(), (out / "pricing.rs").read_text()
        problems = []
        if "pub extern \"C\" fn __moss_branch_scope_new_current" not in app_rs:
            problems.append("explicit-module executable app.rs does not own the process runtime")
        if "struct MossProcessRuntime" in pricing_rs or "fn __moss_branch_scope_new_current() -> *const ();" not in pricing_rs:
            problems.append("provider pricing.rs is not a wrapper-only crate")
        # Source-free provider: hide pricing source and rebuild the root.
        shutil.move(project / "src" / "pricing.moss", project / "pricing.moss.hidden")
        artifacts = build()
        output = subprocess.check_output([artifacts["executable"]], text=True, timeout=RUN_TIMEOUT)
        if output != "42\n":
            problems.append(f"source-free provider program printed {output!r}")
        if rlib is None or not rlib.exists():
            problems.append("no pricing rlib in artifacts")
        else:
            # Link a Rust root built from the real app.rs runtime against the
            # real source-free provider rlib and run Branch work through the
            # provider's generated wrapper.
            main_src = runtime_preamble(app_rs) + TEST_PRELUDE + r"""
extern crate moss_pricing;
fn main() {
    let exec = MossExecutor::new().threads(2).start();
    let (n, worker) = runtime_invoke(|| {
        let me = std::thread::current().id();
        let hits = std::sync::Arc::new(std::sync::atomic::AtomicUsize::new(0));
        let other = std::sync::Arc::new(std::sync::atomic::AtomicBool::new(false));
        let scope = moss_pricing::branch_scope_new_current();
        for _ in 0..8 {
            let (h, o) = (std::sync::Arc::clone(&hits), std::sync::Arc::clone(&other));
            moss_pricing::branch_publish(&scope, move || {
                if std::thread::current().id() != me { o.store(true, std::sync::atomic::Ordering::SeqCst); }
                h.fetch_add(1, std::sync::atomic::Ordering::SeqCst);
            });
        }
        wait_until("worker runs a provider Branch", || other.load(std::sync::atomic::Ordering::SeqCst));
        moss_pricing::branch_join(scope);
        (hits.load(std::sync::atomic::Ordering::SeqCst), other.load(std::sync::atomic::Ordering::SeqCst))
    });
    exec.join();
    assert_eq!(n, 8);
    assert!(worker);
    assert_eq!(moss_live_branch_scopes(), 0);
    println!("ok");
}
"""
            cr, binary = write_and_compile("t_43", main_src, ["--extern", f"moss_pricing={rlib}", "-L", f"dependency={out}"])
            if cr.returncode != 0:
                problems.append("root/provider link failed:\n" + cr.stderr)
            else:
                r = subprocess.run([str(binary)], capture_output=True, text=True, timeout=RUN_TIMEOUT)
                if r.returncode != 0 or "ok" not in r.stdout:
                    problems.append(f"cross-crate Branch run failed: exit={r.returncode}\n{r.stderr}")
        report(name, not problems, "\n".join(problems))
    except (AssertionError, subprocess.SubprocessError, KeyError) as error:
        report(name, False, str(error))


def main():
    TMP_DIR.mkdir(parents=True, exist_ok=True)
    print("Phase 20 Agent C — Executor runtime tests")
    print(f"  compiler: {COMPILER}\n  tmp: {TMP_DIR}\n  rustc: {RUSTC}\n")

    root_rs = generate("root", "fn main():\n  echo 1\n")
    module_root_rs = generate("module_root", "module app\nfn main():\n  echo 1\n")
    # A single-file explicit module without main is a unit without main.
    selected_rs = generate("library_unit", "module probelib\nexport fn foo() -> Int:\n  return 1\n")
    _, provider_rs = build_role_project()
    preamble = runtime_preamble(root_rs)

    test_symbol_ownership(root_rs, module_root_rs, provider_rs, selected_rs)
    run_rust_tests(T, preamble)
    test_handle_not_clone(preamble)
    test_cross_crate_provider(runtime_preamble(module_root_rs), runtime_preamble(provider_rs))
    test_explicit_module_pipeline()
    test_library_only_unit(preamble)

    print()
    print(f"Results: {len(PASS)} passed, {len(FAIL)} failed out of {len(PASS) + len(FAIL)} tests")
    if FAIL:
        print("FAILED:", ", ".join(FAIL))
        sys.exit(1)
    print("All Phase 20 Agent C executor runtime tests PASSED.")


if __name__ == "__main__":
    main()
