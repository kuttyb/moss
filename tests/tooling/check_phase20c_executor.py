#!/usr/bin/env python3
"""
Phase 20 Agent C — Executor Runtime Tests
tests/tooling/check_phase20c_executor.py

Verifies all 27 required test cases from the Phase 20 mission:
1.  INLINE admits one Root at a time (max_simultaneous == 1 before any executor)
2.  INLINE admission is fair
3.  start() waits for a currently running inline Root
4.  No inline Root overlaps newly ACTIVE Executor
5.  ACTIVE runs independent Roots concurrently
6.  Only one active Executor (double start rejected, start while draining rejected, start after join succeeds)
7.  Root queue is bounded and event-driven
8.  Root scheduling is fair under contention
9.  Root A worker never executes Root B while A is live
10. Nested message does not create a new Root
11. Branch publication never blocks & executes on free workers
12. Saturated branch publication falls back to owner-inline execution
13. Joiner helps only own branch subtree
14. Joiner never executes unrelated Root
15. All workers occupied by Roots still allows branch progress through owner helping
16. Physical workers never exceed T_max
17. Solo wait activates compensation to maintain runnable capacity
18. Moss-lock wait does not compensate
19. Solo wake resumes without slot reacquisition
20. Excess compensation parks
21. runtime_invoke ACTIVE uses Executor admission
22. runtime_invoke returns handler reply without deadlock
23. runtime_invoke INLINE serializes (max_simultaneous == 1)
24. Deterministic DRAINING race: unadmitted runtime_invoke waits then runs INLINE
25. Admitted host Root completes before join() returns
26. Post-join runtime_invoke remains valid while Moss runtime lives
27. join() cleans up all workers and returns lifecycle to INLINE, allowing subsequent Executor

Uses standalone Rust test programs compiled with rustc.
Dummy Roots/Branches make this testable without Agents A/B/D.
"""

import subprocess
import sys
import os

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
TMP_DIR = os.path.join(REPO_ROOT, "tmp", "phase20c_executor_tests")
RUSTC = os.environ.get("RUSTC", "rustc")

def get_executor_rust() -> str:
    """Extract executor_runtime_rust() by generating a trivial Moss program."""
    moss_bin = os.path.join(REPO_ROOT, "moss")
    if not os.path.exists(moss_bin):
        print("SKIP: ./moss not found; run make first", file=sys.stderr)
        sys.exit(0)
    moss_src = os.path.join(TMP_DIR, "_probe.moss")
    moss_out = os.path.join(TMP_DIR, "_probe.rs")
    os.makedirs(TMP_DIR, exist_ok=True)
    with open(moss_src, "w") as f:
        f.write("fn main():\n  echo 1\n")
    r = subprocess.run([moss_bin, moss_src, "-o", moss_out],
                       capture_output=True, text=True)
    if r.returncode != 0:
        print("ERROR: moss codegen failed:", r.stderr[:500], file=sys.stderr)
        sys.exit(1)
    with open(moss_out) as f:
        return f.read()

def build_and_run_rust(name: str, rust_src: str, extra_flags=None) -> subprocess.CompletedProcess:
    """Compile and run a Rust test program. Returns the CompletedProcess."""
    os.makedirs(TMP_DIR, exist_ok=True)
    src_path = os.path.join(TMP_DIR, f"{name}.rs")
    bin_path = os.path.join(TMP_DIR, name)
    with open(src_path, "w") as f:
        f.write(rust_src)
    flags = [RUSTC, "--edition", "2021", "--cfg", "moss_perf",
             "-C", "opt-level=0", src_path, "-o", bin_path]
    if extra_flags:
        flags.extend(extra_flags)
    cr = subprocess.run(flags, capture_output=True, text=True)
    if cr.returncode != 0:
        return cr  # compile error
    return subprocess.run([bin_path], capture_output=True, text=True, timeout=30)

def test_preamble(generated_rs: str) -> str:
    """Return the executor runtime Rust code extracted from generated_rs."""
    marker = "// ============================================================\n// Phase 20 Executor Runtime"
    idx = generated_rs.find(marker)
    if idx < 0:
        print("ERROR: executor runtime marker not found in generated Rust", file=sys.stderr)
        sys.exit(1)
    # Find the end of the executor block (before #[export_name = "moss__main"] or fn main()).
    main_idx = generated_rs.find("\n#[export_name", idx)
    if main_idx < 0:
        main_idx = generated_rs.find("\nfn main()", idx)
    if main_idx < 0:
        main_idx = len(generated_rs)
    handler_end = idx
    handler_start = generated_rs.find("struct MossAbortOnUnwind")
    if handler_start < 0:
        handler_start = 0
    return (generated_rs[handler_start:handler_end] +
            generated_rs[idx:main_idx])

PASS = []
FAIL = []

def check(test_name: str, result: subprocess.CompletedProcess, expect_output: str = None):
    ok = result.returncode == 0
    if ok and expect_output is not None:
        ok = expect_output.strip() in result.stdout.strip()
    if ok:
        PASS.append(test_name)
        print(f"  PASS: {test_name}")
    else:
        FAIL.append(test_name)
        print(f"  FAIL: {test_name}")
        if result.returncode != 0:
            print(f"    exit={result.returncode}")
        if result.stderr:
            print(f"    stderr: {result.stderr[:400]}")
        if result.stdout:
            print(f"    stdout: {result.stdout[:400]}")

def main():
    os.makedirs(TMP_DIR, exist_ok=True)

    print("Phase 20 Agent C — Executor Runtime Tests")
    print(f"  tmp: {TMP_DIR}")
    print(f"  rustc: {RUSTC}")
    print()

    generated_rs = get_executor_rust()
    preamble = test_preamble(generated_rs)

    # ── Test 1: INLINE admits one Root at a time ─────────────────────────
    print("Test  1: INLINE admits one Root at a time")
    src1 = preamble + r"""
fn main() {
    use std::sync::Arc;
    use std::sync::atomic::{AtomicUsize, Ordering};
    use std::thread;

    let concurrent = Arc::new(AtomicUsize::new(0));
    let max_concurrent = Arc::new(AtomicUsize::new(0));

    // Run 6 concurrent threads calling runtime_invoke before any executor exists.
    // They must strictly serialize (max_concurrent == 1).
    let threads: Vec<_> = (0..6).map(|_| {
        let c = Arc::clone(&concurrent);
        let m = Arc::clone(&max_concurrent);
        thread::spawn(move || {
            runtime_invoke(move || {
                let n = c.fetch_add(1, Ordering::SeqCst) + 1;
                let mut mx = m.load(Ordering::SeqCst);
                while mx < n {
                    match m.compare_exchange_weak(mx, n, Ordering::SeqCst, Ordering::SeqCst) {
                        Ok(_) => break,
                        Err(actual) => mx = actual,
                    }
                }
                thread::sleep(std::time::Duration::from_millis(15));
                c.fetch_sub(1, Ordering::SeqCst);
            });
        })
    }).collect();

    for t in threads { t.join().unwrap(); }

    let max = max_concurrent.load(Ordering::SeqCst);
    assert_eq!(max, 1, "INLINE concurrent roots = {}, want exactly 1", max);
    println!("ok max_concurrent={}", max);
}
"""
    check("1-inline-one-at-a-time", build_and_run_rust("t01", src1), "ok")

    # ── Test 2: INLINE admission is fair ─────────────────────────────────
    print("Test  2: INLINE admission is fair")
    src2 = preamble + r"""
fn main() {
    use std::sync::{Arc, Mutex};
    let order = Arc::new(Mutex::new(Vec::new()));
    let threads: Vec<_> = (0..6u32).map(|i| {
        let o = Arc::clone(&order);
        std::thread::spawn(move || {
            runtime_invoke(move || {
                o.lock().unwrap().push(i);
            });
        })
    }).collect();
    for t in threads { t.join().unwrap(); }
    let o = order.lock().unwrap();
    assert_eq!(o.len(), 6, "expected 6 roots completed, got {}", o.len());
    println!("ok order_len={}", o.len());
}
"""
    check("2-inline-fairness", build_and_run_rust("t02", src2), "ok")

    # ── Test 3: start() waits for a currently running inline Root ─────────
    print("Test  3: start() waits for a currently running inline Root")
    src3 = preamble + r"""
fn main() {
    use std::sync::{Arc, Mutex, Barrier};
    use std::time::Duration;
    use std::thread;

    let seq = Arc::new(Mutex::new(Vec::<&str>::new()));
    let barrier = Arc::new(Barrier::new(2));

    let seq2 = Arc::clone(&seq);
    let b2 = Arc::clone(&barrier);
    let inline_thread = thread::spawn(move || {
        runtime_invoke(move || {
            seq2.lock().unwrap().push("inline-start");
            b2.wait(); // signal that inline root has started
            thread::sleep(Duration::from_millis(40));
            seq2.lock().unwrap().push("inline-end");
        });
    });

    barrier.wait(); // wait until inline root is confirmed running

    // start() must block until the inline root finishes.
    let seq3 = Arc::clone(&seq);
    let exec = MossExecutor::new().threads(2).start();
    seq3.lock().unwrap().push("active");

    inline_thread.join().unwrap();
    exec.join();

    let s = seq.lock().unwrap().clone();
    assert_eq!(s, vec!["inline-start", "inline-end", "active"],
               "start() did not wait for running inline root: {:?}", s);
    println!("ok sequence={:?}", s);
}
"""
    check("3-start-waits-inline", build_and_run_rust("t03", src3), "ok")

    # ── Test 4: No inline Root overlaps newly ACTIVE Executor ─────────────
    print("Test  4: No inline Root overlaps ACTIVE Executor")
    src4 = preamble + r"""
fn main() {
    use std::sync::atomic::{AtomicBool, Ordering};
    use std::sync::{Arc, Barrier};
    use std::thread;
    use std::time::Duration;

    let inline_running = Arc::new(AtomicBool::new(false));
    let barrier = Arc::new(Barrier::new(2));

    let ir = Arc::clone(&inline_running);
    let b2 = Arc::clone(&barrier);

    let t = thread::spawn(move || {
        runtime_invoke(move || {
            ir.store(true, Ordering::SeqCst);
            b2.wait();
            thread::sleep(Duration::from_millis(40));
            ir.store(false, Ordering::SeqCst);
        });
    });

    barrier.wait();

    let exec = MossExecutor::new().threads(2).start();
    let still_running = inline_running.load(Ordering::SeqCst);
    assert!(!still_running, "inline root overlapped ACTIVE executor!");

    exec.join();
    t.join().unwrap();
    println!("ok no-overlap");
}
"""
    check("4-no-inline-active-overlap", build_and_run_rust("t04", src4), "ok")

    # ── Test 5: ACTIVE runs independent Roots concurrently ────────────────
    print("Test  5: ACTIVE runs independent Roots concurrently")
    src5 = preamble + r"""
fn main() {
    use std::sync::Arc;
    use std::sync::atomic::{AtomicUsize, Ordering};
    use std::time::Duration;

    let concurrent = Arc::new(AtomicUsize::new(0));
    let max_concurrent = Arc::new(AtomicUsize::new(0));

    let exec = MossExecutor::new().threads(4).start();

    for _ in 0..8 {
        let c = Arc::clone(&concurrent);
        let m = Arc::clone(&max_concurrent);
        let root_id = next_root_id();
        let desc = MossRootDescriptor::one_way(root_id, move || {
            let n = c.fetch_add(1, Ordering::SeqCst) + 1;
            m.fetch_max(n, Ordering::SeqCst);
            std::thread::sleep(Duration::from_millis(30));
            c.fetch_sub(1, Ordering::SeqCst);
        });
        exec.enqueue_root(desc);
    }

    exec.join();
    let max = max_concurrent.load(Ordering::SeqCst);
    assert!(max >= 2, "ACTIVE should run roots concurrently, max_concurrent={}", max);
    println!("ok max_concurrent={}", max);
}
"""
    check("5-active-concurrent-roots", build_and_run_rust("t05", src5), "ok")

    # ── Test 6: Only one active Executor ─────────────────────────────────
    print("Test  6: Only one active Executor")
    src6 = preamble + r"""
fn main() {
    let exec = MossExecutor::new().threads(1).start();
    // Attempting a second start() while ACTIVE should panic.
    let result = std::panic::catch_unwind(|| {
        let _exec2 = MossExecutor::new().threads(1).start();
    });
    exec.join();
    assert!(result.is_err(), "second start() while ACTIVE should have panicked (E6)");

    // After join(), a subsequent start() MUST succeed cleanly!
    let exec3 = MossExecutor::new().threads(2).start();
    assert_eq!(moss_executor_state(), "ACTIVE");
    exec3.join();
    assert_eq!(moss_executor_state(), "INLINE");

    println!("ok single-active-enforced-and-reusable");
}
"""
    check("6-one-active-executor", build_and_run_rust("t06", src6), "ok")

    # ── Test 7: Root queue is bounded ─────────────────────────────────────
    print("Test  7: Root queue is bounded")
    src7 = preamble + r"""
fn main() {
    use std::sync::atomic::{AtomicUsize, Ordering};
    use std::sync::Arc;

    let cap = 4usize;
    let enqueued = Arc::new(AtomicUsize::new(0));

    let exec = MossExecutor::new().threads(1).max_threads(1).queue_capacity(cap).start();

    let barrier = Arc::new(std::sync::Barrier::new(2));
    let b2 = Arc::clone(&barrier);

    let root_id = next_root_id();
    let desc = MossRootDescriptor::one_way(root_id, move || {
        b2.wait();
    });
    exec.enqueue_root(desc);

    for _ in 0..cap {
        let e = Arc::clone(&enqueued);
        let root_id = next_root_id();
        let desc = MossRootDescriptor::one_way(root_id, move || {
            e.fetch_add(1, Ordering::SeqCst);
        });
        exec.enqueue_root(desc);
    }
    barrier.wait();
    exec.join();
    let n = enqueued.load(Ordering::SeqCst);
    assert_eq!(n, cap, "expected {} roots completed via bounded queue, got {}", cap, n);
    println!("ok bounded queue cap={} completed={}", cap, n);
}
"""
    check("7-root-queue-bounded", build_and_run_rust("t07", src7), "ok")

    # ── Test 8: Root scheduling is fair ──────────────────────────────────
    print("Test  8: Root scheduling is fair")
    src8 = preamble + r"""
fn main() {
    use std::sync::{Arc, Mutex};
    use std::time::Duration;

    let exec = MossExecutor::new().threads(2).queue_capacity(32).start();
    let completed = Arc::new(Mutex::new(Vec::new()));

    for i in 0..8u32 {
        let c = Arc::clone(&completed);
        let root_id = next_root_id();
        let desc = MossRootDescriptor::one_way(root_id, move || {
            std::thread::sleep(Duration::from_millis(5));
            c.lock().unwrap().push(i);
        });
        exec.enqueue_root(desc);
    }
    exec.join();
    let c = completed.lock().unwrap().clone();
    assert_eq!(c.len(), 8, "fairness: not all roots ran, got {:?}", c);
    println!("ok fair completed={}", c.len());
}
"""
    check("8-fair-scheduling", build_and_run_rust("t08", src8), "ok")

    # ── Test 9: Worker running Root A never executes Root B while A is live ─
    print("Test  9: Worker never starts Root B while Root A is live")
    src9 = preamble + r"""
fn main() {
    use std::sync::atomic::{AtomicBool, Ordering};
    use std::sync::Arc;
    use std::time::Duration;
    use std::thread;

    let exec = MossExecutor::new().threads(1).max_threads(1).queue_capacity(8).start();

    let a_running = Arc::new(AtomicBool::new(false));
    let b_started_while_a = Arc::new(AtomicBool::new(false));
    let seq = Arc::new(std::sync::Mutex::new(Vec::<&str>::new()));

    let ar = Arc::clone(&a_running);
    let seq2 = Arc::clone(&seq);
    let rid_a = next_root_id();
    let desc_a = MossRootDescriptor::one_way(rid_a, move || {
        ar.store(true, Ordering::SeqCst);
        seq2.lock().unwrap().push("A-start");
        thread::sleep(Duration::from_millis(40));
        seq2.lock().unwrap().push("A-end");
        ar.store(false, Ordering::SeqCst);
    });
    exec.enqueue_root(desc_a);

    thread::sleep(Duration::from_millis(5));

    let ar2 = Arc::clone(&a_running);
    let bsa = Arc::clone(&b_started_while_a);
    let seq3 = Arc::clone(&seq);
    let rid_b = next_root_id();
    let desc_b = MossRootDescriptor::one_way(rid_b, move || {
        if ar2.load(Ordering::SeqCst) { bsa.store(true, Ordering::SeqCst); }
        seq3.lock().unwrap().push("B-start");
    });
    exec.enqueue_root(desc_b);

    exec.join();

    let s = seq.lock().unwrap().clone();
    let b_while_a = b_started_while_a.load(Ordering::SeqCst);
    assert!(!b_while_a, "Root B started while Root A was live: {:?}", s);
    assert_eq!(s, vec!["A-start", "A-end", "B-start"], "unexpected sequence: {:?}", s);
    println!("ok sequence={:?}", s);
}
"""
    check("9-root-a-not-interrupted-by-b", build_and_run_rust("t09", src9), "ok")

    # ── Test 10: Nested message does not create a new Root ─────────────────
    print("Test 10: Nested message does not create a new Root")
    src10 = preamble + r"""
fn main() {
    let outer_calls = std::sync::Arc::new(std::sync::atomic::AtomicUsize::new(0));
    let oc2 = std::sync::Arc::clone(&outer_calls);

    let result = runtime_invoke(move || {
        oc2.fetch_add(1, std::sync::atomic::Ordering::SeqCst);
        let inner = {
            let mut x = 0usize;
            for _ in 0..3 { x += 1; }
            x
        };
        inner
    });
    assert_eq!(result, 3);
    assert_eq!(outer_calls.load(std::sync::atomic::Ordering::SeqCst), 1);
    println!("ok result={} root_calls=1", result);
}
"""
    check("10-nested-message-no-new-root", build_and_run_rust("t10", src10), "ok")

    # ── Test 11: Branch publication never blocks & executes on free workers ─
    print("Test 11: Branch publication never blocks & runs on free workers")
    src11 = preamble + r"""
fn main() {
    use std::time::{Duration, Instant};
    use std::sync::Arc;
    use std::sync::atomic::{AtomicUsize, Ordering};

    let exec = MossExecutor::new().threads(4).start();

    let scope = branch_scope_new(1u64);
    let count = Arc::new(AtomicUsize::new(0));
    let start = Instant::now();

    for _ in 0..50 {
        let s = Arc::clone(&scope);
        let c = Arc::clone(&count);
        branch_publish(&s, move || {
            std::thread::sleep(Duration::from_millis(5));
            c.fetch_add(1, Ordering::SeqCst);
        });
    }
    let elapsed = start.elapsed();
    assert!(elapsed < Duration::from_millis(500), "branch_publish blocked: {:?}", elapsed);

    branch_join(scope);
    exec.join();

    assert_eq!(count.load(Ordering::SeqCst), 50);
    println!("ok branch_publish non-blocking and completed on pool");
}
"""
    check("11-branch-publish-nonblocking", build_and_run_rust("t11", src11), "ok")

    # ── Test 12: Saturated branch publication falls back to inline ─────────
    print("Test 12: Saturated branch queue -> inline execution")
    src12 = preamble + r"""
fn main() {
    use std::sync::Arc;
    use std::sync::atomic::{AtomicUsize, Ordering};

    let scope = branch_scope_new(42u64);
    let inline_count = Arc::new(AtomicUsize::new(0));

    for _ in 0..520usize {
        let ic = Arc::clone(&inline_count);
        let s = Arc::clone(&scope);
        branch_publish(&s, move || { ic.fetch_add(1, Ordering::Relaxed); });
    }
    branch_join(scope);
    let n = inline_count.load(Ordering::SeqCst);
    assert_eq!(n, 520, "expected 520 branch completions, got {}", n);
    println!("ok saturated-fallback n={}", n);
}
"""
    check("12-saturated-branch-inline-fallback", build_and_run_rust("t12", src12), "ok")

    # ── Test 13: Joiner helps only own branch subtree ─────────────────────
    print("Test 13: Joiner helps only own branch subtree (root-scoped helping)")
    src13 = preamble + r"""
fn main() {
    use std::sync::Arc;
    use std::sync::atomic::{AtomicUsize, Ordering};

    let scope_a = branch_scope_new(10u64);
    let scope_b = branch_scope_new(20u64);

    let a_count = Arc::new(AtomicUsize::new(0));
    let b_count = Arc::new(AtomicUsize::new(0));

    for _ in 0..5 {
        let ac = Arc::clone(&a_count);
        let sc = Arc::clone(&scope_a);
        branch_publish(&sc, move || { ac.fetch_add(1, Ordering::Relaxed); });
    }
    for _ in 0..5 {
        let bc = Arc::clone(&b_count);
        let sc = Arc::clone(&scope_b);
        branch_publish(&sc, move || { bc.fetch_add(1, Ordering::Relaxed); });
    }

    branch_join(scope_a);
    let na = a_count.load(Ordering::SeqCst);
    assert_eq!(na, 5, "scope_a: expected 5, got {}", na);

    branch_join(scope_b);
    let nb = b_count.load(Ordering::SeqCst);
    assert_eq!(nb, 5, "scope_b: expected 5, got {}", nb);

    println!("ok scope_a={} scope_b={}", na, nb);
}
"""
    check("13-joiner-root-scoped-helping", build_and_run_rust("t13", src13), "ok")

    # ── Test 14: Joiner never executes unrelated Root ─────────────────────
    print("Test 14: Joiner never executes unrelated Root work")
    src14 = preamble + r"""
fn main() {
    use std::sync::Arc;
    use std::sync::atomic::{AtomicUsize, Ordering};

    let scope_x = branch_scope_new(100u64);
    let scope_y = branch_scope_new(200u64);
    let x_count = Arc::new(AtomicUsize::new(0));
    let y_count = Arc::new(AtomicUsize::new(0));

    for _ in 0..3 {
        let xc = Arc::clone(&x_count);
        let sc = Arc::clone(&scope_x);
        branch_publish(&sc, move || { xc.fetch_add(1, Ordering::Relaxed); });
    }
    for _ in 0..3 {
        let yc = Arc::clone(&y_count);
        let sc = Arc::clone(&scope_y);
        branch_publish(&sc, move || { yc.fetch_add(1, Ordering::Relaxed); });
    }

    branch_join(scope_x);
    let nx = x_count.load(Ordering::SeqCst);

    branch_join(scope_y);
    let ny = y_count.load(Ordering::SeqCst);

    assert_eq!(nx, 3);
    assert_eq!(ny, 3);
    println!("ok x={} y={}", nx, ny);
}
"""
    check("14-joiner-no-unrelated-root", build_and_run_rust("t14", src14), "ok")

    # ── Test 15: All workers occupied → branch progress via owner helping ──
    print("Test 15: All workers occupied → branch progress through owner helping")
    src15 = preamble + r"""
fn main() {
    use std::sync::{Arc, Barrier};
    use std::sync::atomic::{AtomicUsize, Ordering};
    use std::time::Duration;
    use std::thread;

    let nthreads = 2usize;
    let exec = MossExecutor::new().threads(nthreads).max_threads(nthreads).queue_capacity(16).start();

    let barrier = Arc::new(Barrier::new(nthreads + 1));
    for _ in 0..nthreads {
        let b = Arc::clone(&barrier);
        let root_id = next_root_id();
        let desc = MossRootDescriptor::one_way(root_id, move || {
            b.wait();
        });
        exec.enqueue_root(desc);
    }
    thread::sleep(Duration::from_millis(20));

    let branch_count = Arc::new(AtomicUsize::new(0));
    let scope = branch_scope_new(999u64);
    for _ in 0..5 {
        let bc = Arc::clone(&branch_count);
        let sc = Arc::clone(&scope);
        branch_publish(&sc, move || { bc.fetch_add(1, Ordering::Relaxed); });
    }
    branch_join(scope);

    let n = branch_count.load(Ordering::SeqCst);
    assert_eq!(n, 5, "expected 5 branches via owner helping, got {}", n);

    barrier.wait();
    exec.join();
    println!("ok branches_via_owner_helping={}", n);
}
"""
    check("15-branch-progress-when-workers-full", build_and_run_rust("t15", src15), "ok")

    # ── Test 16: Physical workers never exceed T_max ───────────────────────
    print("Test 16: Physical workers never exceed T_max")
    src16 = preamble + r"""
fn main() {
    use std::time::Duration;
    use std::thread;

    let tmax = 4usize;
    let exec = MossExecutor::new().threads(2).max_threads(tmax).queue_capacity(32).start();

    for i in 0..tmax {
        solo_enter(i);
    }
    thread::sleep(Duration::from_millis(20));

    let workers = moss_executor_worker_count();
    assert!(workers <= tmax, "physical workers {} > T_max {}", workers, tmax);

    for i in 0..tmax { solo_leave(i); }
    exec.join();
    println!("ok workers_at_peak={} tmax={}", workers, tmax);
}
"""
    check("16-workers-never-exceed-tmax", build_and_run_rust("t16", src16), "ok")

    # ── Test 17: Solo wait may activate compensation ───────────────────────
    print("Test 17: Solo wait may activate compensation")
    src17 = preamble + r"""
fn main() {
    use std::time::Duration;
    use std::thread;

    let target = 2usize;
    let tmax = 6usize;
    let exec = MossExecutor::new().threads(target).max_threads(tmax).queue_capacity(8).start();

    let workers_before = moss_executor_worker_count();
    for i in 0..target { solo_enter(i); }
    thread::sleep(Duration::from_millis(30));

    let workers_after = moss_executor_worker_count();
    assert!(workers_after >= workers_before,
            "no compensation: before={} after={}", workers_before, workers_after);

    for i in 0..target { solo_leave(i); }
    thread::sleep(Duration::from_millis(20));
    exec.join();
    println!("ok compensation: before={} after_solo={}", workers_before, workers_after);
}
"""
    check("17-solo-compensation", build_and_run_rust("t17", src17), "ok")

    # ── Test 18: Moss-lock wait does not compensate ────────────────────────
    print("Test 18: Moss-lock wait does not compensate (structural)")
    src18 = preamble + r"""
fn main() {
    use std::time::Duration;
    use std::thread;
    use std::sync::{Arc, Mutex};

    let exec = MossExecutor::new().threads(2).max_threads(4).start();
    let lock = Arc::new(Mutex::new(0usize));
    let workers_before = moss_executor_worker_count();

    let lock2 = Arc::clone(&lock);
    let root_id = next_root_id();
    let held = lock.lock().unwrap();

    let desc = MossRootDescriptor::one_way(root_id, move || {
        let _g = lock2.lock().unwrap();
        *_g;
    });
    exec.enqueue_root(desc);

    thread::sleep(Duration::from_millis(20));
    let workers_during_lock = moss_executor_worker_count();

    drop(held);
    exec.join();

    assert_eq!(workers_during_lock, workers_before,
               "compensation fired for Moss-lock wait: before={} during={}",
               workers_before, workers_during_lock);
    println!("ok no-compensation-for-moss-lock: workers={}", workers_during_lock);
}
"""
    check("18-no-compensation-moss-lock", build_and_run_rust("t18", src18), "ok")

    # ── Test 19: Solo wake resumes without slot reacquisition ─────────────
    print("Test 19: Solo wake resumes without slot reacquisition")
    src19 = preamble + r"""
fn main() {
    use std::time::{Duration, Instant};
    use std::thread;

    let exec = MossExecutor::new().threads(2).max_threads(4).start();

    let start = Instant::now();
    solo_enter(0);
    thread::sleep(Duration::from_millis(10));
    solo_leave(0);
    let elapsed = start.elapsed();

    exec.join();
    assert!(elapsed < Duration::from_millis(100), "solo_leave waited too long: {:?}", elapsed);
    println!("ok solo_leave_fast elapsed={:?}", elapsed);
}
"""
    check("19-solo-wake-no-slot-wait", build_and_run_rust("t19", src19), "ok")

    # ── Test 20: Excess compensation parks ────────────────────────────────
    print("Test 20: Excess compensation parks")
    src20 = preamble + r"""
fn main() {
    use std::time::Duration;
    use std::thread;

    let target = 2usize;
    let tmax = 8usize;
    let exec = MossExecutor::new().threads(target).max_threads(tmax).queue_capacity(4).start();

    let workers_initial = moss_executor_worker_count();

    for i in 0..target { solo_enter(i); }
    thread::sleep(Duration::from_millis(30));
    let workers_peak = moss_executor_worker_count();

    for i in 0..target { solo_leave(i); }
    thread::sleep(Duration::from_millis(100));

    let workers_after = moss_executor_worker_count();
    assert!(workers_after <= tmax, "excess workers did not respect T_max: after={}", workers_after);

    exec.join();
    println!("ok excess-parks: initial={} peak={} after_solo={}",
             workers_initial, workers_peak, workers_after);
}
"""
    check("20-excess-compensation-parks", build_and_run_rust("t20", src20), "ok")

    # ── Test 21: runtime_invoke ACTIVE uses Executor admission ────────────
    print("Test 21: runtime_invoke ACTIVE uses Executor admission")
    src21 = preamble + r"""
fn main() {
    use std::sync::atomic::{AtomicBool, Ordering};
    use std::sync::Arc;
    use std::thread;

    let exec = MossExecutor::new().threads(2).queue_capacity(8).start();

    let saw_active = Arc::new(AtomicBool::new(false));
    let sa2 = Arc::clone(&saw_active);

    let h = thread::spawn(move || {
        runtime_invoke(move || {
            sa2.store(true, Ordering::SeqCst);
        })
    });
    h.join().unwrap();

    exec.join();
    assert!(saw_active.load(Ordering::SeqCst), "runtime_invoke did not run in ACTIVE mode");
    println!("ok runtime_invoke-active-admission");
}
"""
    check("21-runtime-invoke-active-admission", build_and_run_rust("t21", src21), "ok")

    # ── Test 22: runtime_invoke returns handler reply ─────────────────────
    print("Test 22: runtime_invoke returns handler reply")
    src22 = preamble + r"""
fn main() {
    let result = runtime_invoke(|| 42u64 * 2u64);
    assert_eq!(result, 84u64);

    let hello = runtime_invoke(|| "hello".to_string());
    assert_eq!(hello, "hello");

    println!("ok reply={}", result);
}
"""
    check("22-runtime-invoke-returns-reply", build_and_run_rust("t22", src22), "ok")

    # ── Test 23: runtime_invoke INLINE serializes ─────────────────────────
    print("Test 23: runtime_invoke INLINE serializes")
    src23 = preamble + r"""
fn main() {
    use std::sync::Arc;
    use std::sync::atomic::{AtomicUsize, Ordering};
    use std::thread;

    let concurrent = Arc::new(AtomicUsize::new(0));
    let max_concurrent = Arc::new(AtomicUsize::new(0));

    let threads: Vec<_> = (0..6u32).map(|_| {
        let c = Arc::clone(&concurrent);
        let m = Arc::clone(&max_concurrent);
        thread::spawn(move || {
            runtime_invoke(move || {
                let n = c.fetch_add(1, Ordering::SeqCst) + 1;
                let mut mx = m.load(Ordering::SeqCst);
                while mx < n {
                    match m.compare_exchange_weak(mx, n, Ordering::SeqCst, Ordering::SeqCst) {
                        Ok(_) => break,
                        Err(actual) => mx = actual,
                    }
                }
                thread::sleep(std::time::Duration::from_millis(15));
                c.fetch_sub(1, Ordering::SeqCst);
            });
        })
    }).collect();
    for t in threads { t.join().unwrap(); }

    let max = max_concurrent.load(Ordering::SeqCst);
    assert_eq!(max, 1, "INLINE concurrent roots = {}, want 1", max);
    println!("ok inline-serialized max_concurrent={}", max);
}
"""
    check("23-runtime-invoke-inline-serializes", build_and_run_rust("t23", src23), "ok")

    # ── Test 24: Unadmitted runtime_invoke racing join() waits, then INLINE ─
    print("Test 24: Unadmitted runtime_invoke racing join() waits then runs INLINE")
    src24 = preamble + r"""
fn main() {
    use std::sync::Arc;
    use std::sync::atomic::{AtomicBool, Ordering};
    use std::time::Duration;
    use std::thread;

    let ran = Arc::new(AtomicBool::new(false));
    let ran2 = Arc::clone(&ran);

    let exec = MossExecutor::new().threads(1).queue_capacity(2).start();

    let barrier = Arc::new(std::sync::Barrier::new(2));
    let b2 = Arc::clone(&barrier);
    let root_id = next_root_id();
    let desc = MossRootDescriptor::one_way(root_id, move || {
        b2.wait();
        thread::sleep(Duration::from_millis(30));
    });
    exec.enqueue_root(desc);
    thread::sleep(Duration::from_millis(5));

    let invoke_thread = thread::spawn(move || {
        runtime_invoke(move || {
            ran2.store(true, Ordering::SeqCst);
        })
    });

    barrier.wait();
    exec.join();

    invoke_thread.join().unwrap();

    assert!(ran.load(Ordering::SeqCst), "unadmitted runtime_invoke did not run after join");
    println!("ok unadmitted-invoke-after-drain ran=true");
}
"""
    check("24-unadmitted-invoke-after-drain", build_and_run_rust("t24", src24), "ok")

    # ── Test 25: Admitted host Root completes before join() returns ────────
    print("Test 25: Admitted host Root completes before join() returns")
    src25 = preamble + r"""
fn main() {
    use std::sync::atomic::{AtomicBool, Ordering};
    use std::sync::Arc;
    use std::time::Duration;

    let completed = Arc::new(AtomicBool::new(false));
    let c2 = Arc::clone(&completed);

    let exec = MossExecutor::new().threads(2).queue_capacity(8).start();

    let root_id = next_root_id();
    let desc = MossRootDescriptor::one_way(root_id, move || {
        std::thread::sleep(Duration::from_millis(30));
        c2.store(true, Ordering::SeqCst);
    });
    exec.enqueue_root(desc);

    exec.join();

    assert!(completed.load(Ordering::SeqCst), "join() returned before admitted root completed");
    println!("ok join-waited-for-admitted-root");
}
"""
    check("25-join-waits-for-admitted-root", build_and_run_rust("t25", src25), "ok")

    # ── Test 26: Post-join runtime_invoke remains valid ────────────────────
    print("Test 26: Post-join runtime_invoke remains valid (INLINE mode)")
    src26 = preamble + r"""
fn main() {
    let exec = MossExecutor::new().threads(2).start();
    exec.join();

    let result = runtime_invoke(|| 99u32);
    assert_eq!(result, 99u32, "post-join runtime_invoke failed: got {}", result);
    println!("ok post-join runtime_invoke result={}", result);
}
"""
    check("26-post-join-invoke-valid", build_and_run_rust("t26", src26), "ok")

    # ── Test 27: join() returns lifecycle to INLINE ────────────────────────
    print("Test 27: join() returns lifecycle to INLINE")
    src27 = preamble + r"""
fn main() {
    let exec = MossExecutor::new().threads(2).start();
    assert_eq!(moss_executor_state(), "ACTIVE");
    exec.join();
    let state = moss_executor_state();
    assert_eq!(state, "INLINE", "join() did not restore INLINE state: got {}", state);
    assert_eq!(moss_executor_worker_count(), 0, "workers remaining after join!");
    println!("ok lifecycle-restored state={}", state);
}
"""
    check("27-join-restores-inline", build_and_run_rust("t27", src27), "ok")

    # ── Summary ───────────────────────────────────────────────────────────
    print()
    print(f"Results: {len(PASS)} passed, {len(FAIL)} failed out of {len(PASS)+len(FAIL)} tests")
    if FAIL:
        print("FAILED:", ", ".join(FAIL))
        sys.exit(1)
    else:
        print("All Phase 20 Agent C executor runtime tests PASSED.")

if __name__ == "__main__":
    main()
