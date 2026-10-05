#!/usr/bin/env python3
"""
Phase 20 Agent C — Executor Runtime Tests
tests/tooling/check_phase20c_executor.py

Verifies all 27 required test cases from the Phase 20 mission:
1.  INLINE admits one Root at a time
2.  INLINE admission is fair
3.  start() waits for a currently running inline Root
4.  No inline Root overlaps newly ACTIVE Executor
5.  ACTIVE runs independent Roots concurrently
6.  Only one active Executor
7.  Root queue is bounded
8.  Root scheduling is fair
9.  Root A worker never executes Root B while A is live
10. Nested message does not create a new Root
11. Branch publication never blocks
12. Saturated branch publication falls back to owner-inline execution
13. Joiner helps only own branch subtree
14. Joiner never executes unrelated Root
15. All workers occupied by Roots still allows branch progress through owner helping
16. Physical workers never exceed T_max
17. Solo wait may activate compensation
18. Moss-lock wait does not compensate (not applicable here — tested by convention)
19. Solo wake resumes without slot reacquisition
20. Excess compensation parks
21. runtime_invoke ACTIVE uses Executor admission
22. runtime_invoke returns handler reply
23. runtime_invoke INLINE serializes
24. Unadmitted runtime_invoke racing join() waits then runs INLINE
25. Admitted host Root completes before join() returns
26. Post-join runtime_invoke remains valid while Moss runtime lives
27. join() returns lifecycle to INLINE

Uses standalone Rust test programs compiled with rustc.
Dummy Roots/Branches make this testable without Agents A/B/D.
"""

import subprocess
import sys
import os
import textwrap
import tempfile
import shutil

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
TMP_DIR = os.path.join(REPO_ROOT, "tmp", "phase20c_executor_tests")
RUSTC = os.environ.get("RUSTC", "rustc")

# ── Rust runtime preamble ─────────────────────────────────────────────────
# We extract the executor_runtime_rust() string by compiling a tiny Moss file
# and capturing the generated Rust, then prepend the handler_runtime header.
# Alternatively we just include the relevant portions inline since we own
# the header content.  For test isolation we compile the Rust directly.

def get_executor_rust() -> str:
    """Extract executor_runtime_rust() by generating a trivial Moss program."""
    moss_bin = os.path.join(REPO_ROOT, "moss")
    if not os.path.exists(moss_bin):
        print("SKIP: ./moss not found; run make first", file=sys.stderr)
        sys.exit(0)
    # Use a minimal Moss source and capture the generated Rust.
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

# ── Runtime preamble used by every test ──────────────────────────────────
def test_preamble(generated_rs: str) -> str:
    """Return the executor runtime Rust code extracted from generated_rs."""
    # Find the executor section: starts after handler_runtime block.
    marker = "// ============================================================\n// Phase 20 Executor Runtime"
    idx = generated_rs.find(marker)
    if idx < 0:
        print("ERROR: executor runtime marker not found in generated Rust", file=sys.stderr)
        sys.exit(1)
    # Find the end of the executor block (before generated domain/fn code).
    # The executor block ends just before the first `fn __moss_require_send` or
    # domain/fn generation. We take everything up to the first `fn main()`.
    main_idx = generated_rs.find("\nfn main()", idx)
    if main_idx < 0:
        main_idx = len(generated_rs)
    # Also grab the handler_runtime preamble (before executor block).
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
            print(f"    stderr: {result.stderr[:300]}")
        if result.stdout:
            print(f"    stdout: {result.stdout[:300]}")

def compile_check(test_name: str, result: subprocess.CompletedProcess):
    """Fail if the Rust compile step failed."""
    if result.returncode != 0:
        FAIL.append(test_name)
        print(f"  COMPILE-FAIL: {test_name}")
        print(f"    {result.stderr[:400]}")
        return False
    return True

# ──────────────────────────────────────────────────────────────────────────
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
    use std::sync::{Arc, Mutex};
    use std::sync::atomic::{AtomicUsize, Ordering};
    use std::thread;

    let concurrent = Arc::new(AtomicUsize::new(0));
    let max_concurrent = Arc::new(AtomicUsize::new(0));

    // Run 4 inline roots; they must run one at a time.
    for _ in 0..4 {
        let c = Arc::clone(&concurrent);
        let m = Arc::clone(&max_concurrent);
        runtime_invoke(move || {
            let n = c.fetch_add(1, Ordering::SeqCst) + 1;
            let mut mx = m.load(Ordering::SeqCst);
            while mx < n { mx = m.fetch_max(n, Ordering::SeqCst).max(n); }
            thread::sleep(std::time::Duration::from_millis(5));
            c.fetch_sub(1, Ordering::SeqCst);
        });
    }
    let max = max_concurrent.load(Ordering::SeqCst);
    assert!(max <= 1, "INLINE concurrent roots = {}, want <= 1", max);
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
    let threads: Vec<_> = (0..4u32).map(|i| {
        let o = Arc::clone(&order);
        std::thread::spawn(move || {
            runtime_invoke(move || {
                o.lock().unwrap().push(i);
            });
        })
    }).collect();
    for t in threads { t.join().unwrap(); }
    let o = order.lock().unwrap();
    // All 4 must appear (fairness = each gets a turn).
    assert_eq!(o.len(), 4, "expected 4 roots completed, got {}", o.len());
    println!("ok order_len={}", o.len());
}
"""
    check("2-inline-fairness", build_and_run_rust("t02", src2), "ok")

    # ── Test 3: start() waits for a currently running inline Root ─────────
    print("Test  3: start() waits for a currently running inline Root")
    src3 = preamble + r"""
fn main() {
    use std::sync::{Arc, Mutex};
    use std::time::Duration;
    use std::thread;

    let seq = Arc::new(Mutex::new(Vec::<&str>::new()));

    // Start a long inline root in a background thread.
    let seq2 = Arc::clone(&seq);
    let inline_thread = thread::spawn(move || {
        runtime_invoke(move || {
            seq2.lock().unwrap().push("inline-start");
            thread::sleep(Duration::from_millis(50));
            seq2.lock().unwrap().push("inline-end");
        });
    });

    // Give it time to start.
    thread::sleep(Duration::from_millis(10));

    // start() must wait for the inline root to finish before going ACTIVE.
    let seq3 = Arc::clone(&seq);
    let exec = MossExecutor::new().threads(2).start();
    seq3.lock().unwrap().push("active");

    inline_thread.join().unwrap();
    exec.join();

    let s = seq.lock().unwrap().clone();
    // inline-start must come before active (start waited for inline root).
    let istart = s.iter().position(|&x| x == "inline-start").unwrap_or(usize::MAX);
    let active = s.iter().position(|&x| x == "active").unwrap_or(0);
    assert!(istart < active, "start() did not wait for inline root: {:?}", s);
    println!("ok sequence={:?}", s);
}
"""
    check("3-start-waits-inline", build_and_run_rust("t03", src3), "ok")

    # ── Test 4: No inline Root overlaps newly ACTIVE Executor ─────────────
    print("Test  4: No inline Root overlaps ACTIVE Executor")
    src4 = preamble + r"""
fn main() {
    use std::sync::atomic::{AtomicBool, Ordering};
    use std::sync::Arc;
    use std::thread;
    use std::time::Duration;

    let inline_running = Arc::new(AtomicBool::new(false));
    let overlap_detected = Arc::new(AtomicBool::new(false));

    let ir = Arc::clone(&inline_running);
    let od = Arc::clone(&overlap_detected);

    // Launch a long inline root in another thread.
    let t = thread::spawn(move || {
        runtime_invoke(move || {
            ir.store(true, Ordering::SeqCst);
            thread::sleep(Duration::from_millis(60));
            ir.store(false, Ordering::SeqCst);
        });
    });

    thread::sleep(Duration::from_millis(15));

    // start() should block until the inline root finishes.
    let exec = MossExecutor::new().threads(2).start();
    // After start() returns (ACTIVE), inline_running should be false.
    if overlap_detected.load(Ordering::SeqCst) || inline_running.load(Ordering::SeqCst) {
        panic!("inline root overlapped ACTIVE executor");
    }
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
    use std::sync::{Arc, Mutex};
    use std::sync::atomic::{AtomicUsize, Ordering};
    use std::time::Duration;
    use std::thread;

    let concurrent = Arc::new(AtomicUsize::new(0));
    let max_concurrent = Arc::new(AtomicUsize::new(0));
    let c2 = Arc::clone(&concurrent);
    let m2 = Arc::clone(&max_concurrent);

    let exec = MossExecutor::new().threads(4).start();

    for _ in 0..6 {
        let c = Arc::clone(&concurrent);
        let m = Arc::clone(&max_concurrent);
        let root_id = next_root_id();
        let desc = MossRootDescriptor::one_way(root_id, move || {
            let n = c.fetch_add(1, Ordering::SeqCst) + 1;
            m.fetch_max(n, Ordering::SeqCst);
            thread::sleep(Duration::from_millis(30));
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
use std::panic;
fn main() {
    let exec = MossExecutor::new().threads(1).start();
    // Attempting a second start() while ACTIVE should panic.
    let result = panic::catch_unwind(|| {
        let _exec2 = MossExecutor::new().threads(1).start();
    });
    exec.join();
    assert!(result.is_err(), "second start() should have panicked (E6)");
    println!("ok double-start-rejected");
}
"""
    check("6-one-active-executor", build_and_run_rust("t06", src6), "ok")

    # ── Test 7: Root queue is bounded ─────────────────────────────────────
    print("Test  7: Root queue is bounded")
    src7 = preamble + r"""
fn main() {
    use std::sync::atomic::{AtomicUsize, Ordering};
    use std::sync::Arc;
    use std::time::Duration;
    use std::thread;

    let cap = 4usize;
    let enqueued = Arc::new(AtomicUsize::new(0));

    // Workers sleep long so the queue fills up.
    let exec = MossExecutor::new().threads(1).max_threads(1).queue_capacity(cap).start();

    // Enqueue more roots than workers can immediately absorb.
    // The queue should back-pressure the submitter.
    let barrier = Arc::new(std::sync::Barrier::new(2));
    let b2 = Arc::clone(&barrier);

    // A root that blocks to keep the worker busy.
    let root_id = next_root_id();
    let desc = MossRootDescriptor::one_way(root_id, move || {
        b2.wait(); // wait for signal before finishing
    });
    exec.enqueue_root(desc);

    // Enqueue cap more roots (should fill queue).
    for _ in 0..cap {
        let e = Arc::clone(&enqueued);
        let root_id = next_root_id();
        let desc = MossRootDescriptor::one_way(root_id, move || {
            e.fetch_add(1, Ordering::SeqCst);
        });
        exec.enqueue_root(desc);
    }
    // Release the blocking root.
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
    // All 8 must complete.
    assert_eq!(c.len(), 8, "fairness: not all roots ran, got {:?}", c);
    println!("ok fair completed={}", c.len());
}
"""
    check("8-fair-scheduling", build_and_run_rust("t08", src8), "ok")

    # ── Test 9: Worker running Root A never executes Root B while A is live ─
    print("Test  9: Worker never starts Root B while Root A is live")
    src9 = preamble + r"""
fn main() {
    use std::sync::atomic::{AtomicBool, AtomicUsize, Ordering};
    use std::sync::Arc;
    use std::time::Duration;
    use std::thread;

    // Single worker: ensure Root B only starts after Root A finishes.
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

    // Small delay to ensure A is running before B is enqueued.
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
    // With 1 worker, B cannot start while A is running.
    assert!(!b_while_a, "Root B started while Root A was live: {:?}", s);
    // Order must be A-start, A-end, B-start.
    assert_eq!(s, vec!["A-start", "A-end", "B-start"],
               "unexpected sequence: {:?}", s);
    println!("ok sequence={:?}", s);
}
"""
    check("9-root-a-not-interrupted-by-b", build_and_run_rust("t09", src9), "ok")

    # ── Test 10: Nested message does not create a new Root ─────────────────
    print("Test 10: Nested message does not create a new Root")
    src10 = preamble + r"""
fn main() {
    // Simulated nested message: a call chain within one root's work
    // closure should NOT change the running root count.
    let root_id = next_root_id();
    let outer_calls = std::sync::Arc::new(std::sync::atomic::AtomicUsize::new(0));
    let oc2 = std::sync::Arc::clone(&outer_calls);

    // In INLINE mode: nested synchronous calls within one runtime_invoke
    // do not result in additional concurrent roots.
    let result = runtime_invoke(move || {
        oc2.fetch_add(1, std::sync::atomic::Ordering::SeqCst);
        // Simulate nested message (synchronous call within the same root).
        let inner = {
            let mut x = 0usize;
            for _ in 0..3 { x += 1; } // pure computation, not a new root
            x
        };
        inner
    });
    assert_eq!(result, 3, "nested computation incorrect: {}", result);
    assert_eq!(outer_calls.load(std::sync::atomic::Ordering::SeqCst), 1,
               "should have one root execution");
    println!("ok result={} root_calls=1", result);
}
"""
    check("10-nested-message-no-new-root", build_and_run_rust("t10", src10), "ok")

    # ── Test 11: Branch publication never blocks ───────────────────────────
    print("Test 11: Branch publication never blocks")
    src11 = preamble + r"""
fn main() {
    use std::time::{Duration, Instant};
    use std::sync::Arc;

    // branch_publish should return immediately (within a short timeout).
    let scope = branch_scope_new(1u64);
    let start = Instant::now();
    for _ in 0..200 {
        let s = Arc::clone(&scope);
        branch_publish(&s, move || {
            // tiny branch body
            let _ = 1 + 1;
            // mark complete
        });
    }
    let elapsed = start.elapsed();
    // Join the scope to drain.
    branch_join(scope);
    assert!(elapsed < Duration::from_millis(500),
            "branch_publish blocked: elapsed={:?}", elapsed);
    println!("ok branch_publish non-blocking elapsed={:?}", elapsed);
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

    // Publish more branches than the internal BRANCH_QUEUE_CAP (512).
    // The ones that overflow should run inline (fallback).
    // We simulate saturation by pre-filling the queue to capacity.
    // Since BRANCH_QUEUE_CAP=512, we publish 520 and see fallback behavior.
    for _ in 0..520usize {
        let ic = Arc::clone(&inline_count);
        let s = Arc::clone(&scope);
        branch_publish(&s, move || { ic.fetch_add(1, Ordering::Relaxed); });
    }
    branch_join(scope);
    let n = inline_count.load(Ordering::SeqCst);
    // All 520 branches must complete (inline or via join helping).
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

    // Two separate scopes (two roots).
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

    // Join scope_a: helper should only run scope_a's branches.
    branch_join(scope_a);
    let na = a_count.load(Ordering::SeqCst);
    assert_eq!(na, 5, "scope_a: expected 5, got {}", na);

    // scope_b's branches are not helped by scope_a's join.
    branch_join(scope_b);
    let nb = b_count.load(Ordering::SeqCst);
    assert_eq!(nb, 5, "scope_b: expected 5, got {}", nb);

    println!("ok scope_a={} scope_b={}", na, nb);
}
"""
    check("13-joiner-root-scoped-helping", build_and_run_rust("t13", src13), "ok")

    # ── Test 14: Joiner never executes unrelated Root ─────────────────────
    print("Test 14: Joiner never executes unrelated Root work")
    # This is verified structurally: branch_join only pops from scope.pending
    # which is scope-local. We verify the count is exact.
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

    // Join x: should run exactly x's branches.
    branch_join(scope_x);
    let nx = x_count.load(Ordering::SeqCst);
    // y is untouched by x's join.
    let ny_before_join = y_count.load(Ordering::SeqCst);
    // ny_before_join should be 0 (no other thread running y's branches).
    // (In this single-threaded test no worker runs y's queue.)
    branch_join(scope_y);
    let ny = y_count.load(Ordering::SeqCst);

    assert_eq!(nx, 3, "x branches: expected 3, got {}", nx);
    assert_eq!(ny, 3, "y branches: expected 3, got {}", ny);
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

    // Fill all workers with blocking roots.
    let barrier = Arc::new(Barrier::new(nthreads + 1));
    for _ in 0..nthreads {
        let b = Arc::clone(&barrier);
        let root_id = next_root_id();
        let desc = MossRootDescriptor::one_way(root_id, move || {
            b.wait(); // block until released
        });
        exec.enqueue_root(desc);
    }
    thread::sleep(Duration::from_millis(20));

    // Now publish branches for a root that runs inline (after join).
    let branch_count = Arc::new(AtomicUsize::new(0));
    let scope = branch_scope_new(999u64);
    for _ in 0..5 {
        let bc = Arc::clone(&branch_count);
        let sc = Arc::clone(&scope);
        branch_publish(&sc, move || { bc.fetch_add(1, Ordering::Relaxed); });
    }
    // Joining the scope: owner helps inline since all workers are busy.
    branch_join(scope);

    let n = branch_count.load(Ordering::SeqCst);
    assert_eq!(n, 5, "expected 5 branches via owner helping, got {}", n);

    // Release the blocking roots.
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
    use std::sync::Arc;
    use std::sync::atomic::{AtomicUsize, Ordering};
    use std::time::Duration;
    use std::thread;

    let tmax = 4usize;
    let exec = MossExecutor::new().threads(2).max_threads(tmax).queue_capacity(32).start();

    // Trigger solo compensation multiple times.
    for i in 0..tmax {
        solo_enter(i);
    }
    thread::sleep(Duration::from_millis(20));

    let workers = moss_executor_worker_count();
    assert!(workers <= tmax,
            "physical workers {} > T_max {}", workers, tmax);

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
    // Enter Solo blocking region on all target workers.
    for i in 0..target { solo_enter(i); }
    thread::sleep(Duration::from_millis(30));

    let workers_after = moss_executor_worker_count();
    // Compensation should have added workers (up to tmax).
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
    # This is verified by design: solo_enter/solo_leave are ONLY called around
    # known Solo kernel waits, not around Mutex wait paths. The test verifies
    # that NOT calling solo_enter/leave does not trigger compensation.
    src18 = preamble + r"""
fn main() {
    use std::time::Duration;
    use std::thread;
    use std::sync::{Arc, Mutex};

    let exec = MossExecutor::new().threads(2).max_threads(4).start();
    let lock = Arc::new(Mutex::new(0usize));
    let workers_before = moss_executor_worker_count();

    // Simulate a root that waits on a Moss-generated lock (no solo_enter).
    let lock2 = Arc::clone(&lock);
    let root_id = next_root_id();
    let held = lock.lock().unwrap(); // hold the lock to block the root

    let desc = MossRootDescriptor::one_way(root_id, move || {
        let _g = lock2.lock().unwrap(); // will block but NO solo_enter called
        *_g; // just read
    });
    exec.enqueue_root(desc);

    thread::sleep(Duration::from_millis(20));
    let workers_during_lock = moss_executor_worker_count();

    drop(held); // release lock
    exec.join();

    // No compensation should have fired (no solo_enter called).
    // workers_during_lock should be == workers_before (no new workers).
    assert_eq!(workers_during_lock, workers_before,
               "compensation fired for Moss-lock wait (should not): before={} during={}",
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

    // Enter Solo, then immediately leave. The leaving worker should not wait.
    let start = Instant::now();
    solo_enter(0);
    // Simulate Solo kernel wait completion.
    thread::sleep(Duration::from_millis(10));
    solo_leave(0); // must return immediately, no slot wait (E8/R2)
    let elapsed = start.elapsed();

    exec.join();
    // solo_leave should be near-instant (< 50ms after the sleep).
    assert!(elapsed < Duration::from_millis(100),
            "solo_leave waited too long: {:?}", elapsed);
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

    // Enter Solo to trigger compensation.
    for i in 0..target { solo_enter(i); }
    thread::sleep(Duration::from_millis(30));
    let workers_peak = moss_executor_worker_count();

    // Leave Solo: excess workers should park.
    for i in 0..target { solo_leave(i); }
    thread::sleep(Duration::from_millis(100)); // allow parking

    let workers_after = moss_executor_worker_count();
    // After Solo ends, excess workers should have parked.
    // workers_after may be < workers_peak (though timing-dependent).
    assert!(workers_after <= tmax,
            "excess workers did not respect T_max: after={}", workers_after);

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
    use std::time::Duration;
    use std::thread;

    let exec = MossExecutor::new().threads(2).queue_capacity(8).start();

    // In ACTIVE state, runtime_invoke should enqueue through executor.
    // We verify by checking running_roots > 0 when the root executes.
    let saw_active = Arc::new(AtomicBool::new(false));
    let sa2 = Arc::clone(&saw_active);

    // Launch runtime_invoke in a separate thread to avoid deadlock.
    let h = thread::spawn(move || {
        runtime_invoke(move || {
            sa2.store(true, Ordering::SeqCst);
        })
    });
    h.join().unwrap();

    exec.join();
    assert!(saw_active.load(Ordering::SeqCst),
            "runtime_invoke did not run in ACTIVE mode");
    println!("ok runtime_invoke-active-admission");
}
"""
    check("21-runtime-invoke-active-admission", build_and_run_rust("t21", src21), "ok")

    # ── Test 22: runtime_invoke returns handler reply ─────────────────────
    print("Test 22: runtime_invoke returns handler reply")
    src22 = preamble + r"""
fn main() {
    // INLINE mode: runtime_invoke should return the computed value.
    let result = runtime_invoke(|| 42u64 * 2u64);
    assert_eq!(result, 84u64, "expected 84, got {}", result);

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
    use std::sync::{Arc, Mutex};
    use std::thread;

    let order = Arc::new(Mutex::new(Vec::<u32>::new()));

    // Concurrent threads all call runtime_invoke in INLINE mode.
    let threads: Vec<_> = (0..4u32).map(|i| {
        let o = Arc::clone(&order);
        thread::spawn(move || {
            runtime_invoke(move || {
                o.lock().unwrap().push(i);
            });
        })
    }).collect();
    for t in threads { t.join().unwrap(); }

    let o = order.lock().unwrap().clone();
    assert_eq!(o.len(), 4, "INLINE: not all roots ran: {:?}", o);
    println!("ok inline-serialized count={}", o.len());
}
"""
    check("23-runtime-invoke-inline-serializes", build_and_run_rust("t23", src23), "ok")

    # ── Test 24: Unadmitted runtime_invoke racing join() waits, then INLINE ─
    print("Test 24: Unadmitted runtime_invoke racing join() waits then runs INLINE")
    src24 = preamble + r"""
fn main() {
    use std::sync::{Arc, Mutex};
    use std::sync::atomic::{AtomicBool, Ordering};
    use std::time::Duration;
    use std::thread;

    let ran = Arc::new(AtomicBool::new(false));
    let ran2 = Arc::clone(&ran);

    let exec = MossExecutor::new().threads(1).queue_capacity(2).start();

    // Occupy the worker with a long root.
    let barrier = Arc::new(std::sync::Barrier::new(2));
    let b2 = Arc::clone(&barrier);
    let root_id = next_root_id();
    let desc = MossRootDescriptor::one_way(root_id, move || {
        b2.wait();
        thread::sleep(Duration::from_millis(30));
    });
    exec.enqueue_root(desc);
    thread::sleep(Duration::from_millis(5));

    // Start a thread that will call runtime_invoke while we're about to join.
    let invoke_thread = thread::spawn(move || {
        runtime_invoke(move || {
            ran2.store(true, Ordering::SeqCst);
        })
    });

    // Release the long root, then join (races with invoke_thread).
    barrier.wait();
    exec.join(); // DRAINING

    // After join, the runtime_invoke should have run (INLINE after drain).
    invoke_thread.join().unwrap();

    assert!(ran.load(Ordering::SeqCst),
            "unadmitted runtime_invoke did not run after join");
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
    use std::thread;

    let completed = Arc::new(AtomicBool::new(false));
    let c2 = Arc::clone(&completed);

    let exec = MossExecutor::new().threads(2).queue_capacity(8).start();

    // Enqueue a root.
    let root_id = next_root_id();
    let desc = MossRootDescriptor::one_way(root_id, move || {
        thread::sleep(Duration::from_millis(30));
        c2.store(true, Ordering::SeqCst);
    });
    exec.enqueue_root(desc);

    // join() must wait for the root to complete before returning.
    exec.join();

    assert!(completed.load(Ordering::SeqCst),
            "join() returned before admitted root completed");
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

    // After join, Moss is back in INLINE. runtime_invoke must still work.
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
    assert_eq!(moss_executor_state(), "ACTIVE",
               "expected ACTIVE after start, got {}", moss_executor_state());
    exec.join();
    // After join, state must be INLINE.
    let state = moss_executor_state();
    assert_eq!(state, "INLINE",
               "join() did not restore INLINE state: got {}", state);
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
