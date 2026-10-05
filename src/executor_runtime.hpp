#pragma once
// Phase 20 Agent C — Executor and Root Runtime
//
// Returns the Rust source implementing the Phase 20 execution substrate.
// Emitted into every generated Moss program by the code generator, after
// handler_runtime_rust() (see src/moss.cpp line ~11659).
//
// Implements:
//   - MossExecutor: INLINE → ACTIVE → DRAINING → INLINE lifecycle (§5.4)
//   - MossRootDescriptor: internal root representation (ROOT_RUNTIME_ABI §3)
//   - MossRoot / MossBranch: distinct scheduler classes (E1)
//   - branch_publish / branch_join: bounded non-blocking branch API (§7.3)
//   - solo_enter / solo_leave: managed Solo blocking compensation (§15)
//   - runtime_invoke: synchronous host → Moss ingress (ROOT_RUNTIME_ABI §6)
//   - Test instrumentation under #[cfg(any(test, moss_perf))]
//
// Module ownership: new src/executor_runtime.hpp.
// src/moss.cpp includes this and emits it alongside handler_runtime_rust().
// See docs/ROOT_RUNTIME_ABI.md and docs/MOSS_PHASE_20_FILE_IO_AND_EXECUTORS.md.

namespace moss {

inline const char* executor_runtime_rust() {
  return R"EXECUTOR_RUST(
// ============================================================
// Phase 20 Executor Runtime  (moss executor_runtime_rust)
// INLINE ─start()→ ACTIVE ─join()→ DRAINING ─drained→ INLINE
// ============================================================
use std::sync::{
    Arc, Mutex, Condvar,
    atomic::{AtomicBool, AtomicU64, AtomicUsize, Ordering},
};
use std::collections::VecDeque;
use std::thread;
use std::time::Duration;

// ─── Lifecycle ───────────────────────────────────────────────────────────
/// Ingress lifecycle. §5.4 / ROOT_RUNTIME_ABI §"Exact ingress lifecycle".
#[derive(Clone, Copy, PartialEq, Eq, Debug)]
#[allow(dead_code)]
enum MossIngressState {
    /// No active executor; roots run one-at-a-time on the caller thread.
    Inline,
    /// Executor active; every new root enters through executor admission.
    Active,
    /// join() called; no new admission; admitted roots run to completion.
    Draining,
}

// ─── Test / perf instrumentation ────────────────────────────────────────
/// Event kinds for the test/perf hook (§"Test instrumentation").
#[cfg(any(test, moss_perf))]
#[derive(Clone, Copy, PartialEq, Eq, Debug)]
pub enum MossRtEvent {
    RootEnqueued,
    RootStarted,
    RootCompleted,
    BranchPublished,
    BranchHelped,
    BranchCompleted,
    SoloEnter,
    SoloLeave,
    WorkerSpawned,
    WorkerParked,
    IngressStateChange,
    AdmissionQueued,
}

/// Install the test/perf event hook.
/// Signature: fn(event, root_id, branch_id, worker_id)
#[cfg(any(test, moss_perf))]
static MOSS_RT_HOOK: std::sync::OnceLock<
    fn(MossRtEvent, u64, u64, usize),
> = std::sync::OnceLock::new();

#[cfg(any(test, moss_perf))]
pub fn moss_rt_set_hook(f: fn(MossRtEvent, u64, u64, usize)) {
    let _ = MOSS_RT_HOOK.set(f);
}

#[cfg(any(test, moss_perf))]
#[inline(always)]
fn moss_rt_ev(ev: MossRtEvent, root_id: u64, branch_id: u64, worker_id: usize) {
    if let Some(hook) = MOSS_RT_HOOK.get() { hook(ev, root_id, branch_id, worker_id); }
}
#[cfg(not(any(test, moss_perf)))]
#[inline(always)]
fn moss_rt_ev(_ev: u8, _r: u64, _b: u64, _w: usize) {}

// ─── ID counters ─────────────────────────────────────────────────────────
static MOSS_NEXT_ROOT_ID: AtomicU64 = AtomicU64::new(1);
static MOSS_NEXT_BRANCH_ID: AtomicU64 = AtomicU64::new(1);
#[allow(dead_code)]
fn next_root_id() -> u64 { MOSS_NEXT_ROOT_ID.fetch_add(1, Ordering::Relaxed) }
fn next_branch_id() -> u64 { MOSS_NEXT_BRANCH_ID.fetch_add(1, Ordering::Relaxed) }

// ─── RootDescriptor ──────────────────────────────────────────────────────
/// Internal submission record for one statically resolved handler invocation.
/// Not a first-class Moss value or dynamic dispatch target (ROOT_RUNTIME_ABI §3).
/// All three ingress paths share this representation.
pub struct MossRootDescriptor {
    /// Unique root identity (for instrumentation).
    pub root_id: u64,
    /// Type-erased root body; holds the domain ref and evaluated arg snapshot.
    pub work: Box<dyn FnOnce() + Send + 'static>,
    /// Completion signal for synchronous callers (None for one-way roots).
    pub completion_tx: Option<std::sync::mpsc::SyncSender<()>>,
}

impl MossRootDescriptor {
    /// One-way root (executor.invoke semantics).
    pub fn one_way(root_id: u64, work: impl FnOnce() + Send + 'static) -> Self {
        Self { root_id, work: Box::new(work), completion_tx: None }
    }
    /// Synchronous root; caller blocks on completion_rx until this fires.
    pub fn synchronous(
        root_id: u64,
        work: impl FnOnce() + Send + 'static,
        completion_tx: std::sync::mpsc::SyncSender<()>,
    ) -> Self {
        Self { root_id, work: Box::new(work), completion_tx: Some(completion_tx) }
    }
}

// ─── Root / Branch scheduler class markers ───────────────────────────────
/// Root: independent handler execution; may hold Moss locks (E1, §7).
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct MossRootId(pub u64);

/// Branch: compiler-generated child of exactly one Root + join scope.
/// Phase 20 branches acquire no Moss locks (E1, §7.4).
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct MossBranchId(pub u64);

// ─── Branch join scope ────────────────────────────────────────────────────
/// Shared state for one branch join scope, owned by a single Root.
pub struct MossBranchScope {
    /// Owner Root — helpers may only run branches for THIS root (E3 / §7.2).
    pub owner_root_id: u64,
    /// Unstarted branches for this scope.
    pending: Mutex<VecDeque<(u64, Box<dyn FnOnce() + Send>)>>,
    /// Branches not yet complete (published + running).
    outstanding: AtomicUsize,
    done_mutex: Mutex<bool>,
    done_cond: Condvar,
}

impl MossBranchScope {
    pub fn new(owner_root_id: u64) -> Arc<Self> {
        Arc::new(Self {
            owner_root_id,
            pending: Mutex::new(VecDeque::new()),
            outstanding: AtomicUsize::new(0),
            done_mutex: Mutex::new(false),
            done_cond: Condvar::new(),
        })
    }
    fn mark_complete(&self, branch_id: u64) {
        #[cfg(any(test, moss_perf))]
        moss_rt_ev(MossRtEvent::BranchCompleted, self.owner_root_id, branch_id, usize::MAX);
        let prev = self.outstanding.fetch_sub(1, Ordering::AcqRel);
        if prev == 1 {
            let mut done = self.done_mutex.lock().unwrap();
            *done = true;
            self.done_cond.notify_all();
        }
    }
}

// ─── Executor inner state ─────────────────────────────────────────────────
struct MossExecInner {
    state: MossIngressState,
    root_queue: VecDeque<MossRootDescriptor>,
    queue_capacity: usize,
    target_threads: usize,
    max_threads: usize,
    /// Total physical workers (includes compensation).
    active_workers: usize,
    /// Workers currently inside a Solo kernel wait.
    solo_blocked: usize,
    /// Roots currently running on workers.
    running_roots: usize,
    shutdown: bool,
}

impl MossExecInner {
    fn runnable_workers(&self) -> usize {
        self.active_workers.saturating_sub(self.solo_blocked)
    }
    fn needs_compensation(&self) -> bool {
        self.runnable_workers() < self.target_threads
            && self.active_workers < self.max_threads
    }
}

// ─── Global executor ─────────────────────────────────────────────────────
struct MossGlobalExec {
    inner: Mutex<MossExecInner>,
    work_cv: Condvar,   // new root enqueued or shutdown
    drain_cv: Condvar,  // all admitted roots finished
    /// Inline root-ingress gate.  `true` = gate free (§5.2).
    gate: Mutex<bool>,
    gate_cv: Condvar,
}

impl MossGlobalExec {
    fn make(cap: usize, target: usize, max_t: usize) -> Arc<Self> {
        Arc::new(Self {
            inner: Mutex::new(MossExecInner {
                state: MossIngressState::Inline,
                root_queue: VecDeque::new(),
                queue_capacity: cap,
                target_threads: target,
                max_threads: max_t,
                active_workers: 0,
                solo_blocked: 0,
                running_roots: 0,
                shutdown: false,
            }),
            work_cv: Condvar::new(),
            drain_cv: Condvar::new(),
            gate: Mutex::new(true),
            gate_cv: Condvar::new(),
        })
    }
}

// ─── Process-global executor pointer ─────────────────────────────────────
// Uses a RwLock-protected Option so it can be replaced across start()/join() cycles.
static MOSS_EXEC: std::sync::OnceLock<Mutex<Option<Arc<MossGlobalExec>>>> =
    std::sync::OnceLock::new();

fn moss_exec_cell() -> &'static Mutex<Option<Arc<MossGlobalExec>>> {
    MOSS_EXEC.get_or_init(|| Mutex::new(None))
}

fn moss_exec_get() -> Option<Arc<MossGlobalExec>> {
    moss_exec_cell().lock().unwrap().clone()
}

fn moss_exec_set(gx: Arc<MossGlobalExec>) {
    *moss_exec_cell().lock().unwrap() = Some(gx);
}

fn moss_exec_clear() {
    *moss_exec_cell().lock().unwrap() = None;
}

// ─── Worker loop ──────────────────────────────────────────────────────────
/// Each physical worker runs this loop.
/// R4 / E2 / E11: worker runs ONE root at a time; never switches to another
/// root before the current root (and its nested messages) completes.
fn worker_loop(gx: Arc<MossGlobalExec>, worker_id: usize) {
    loop {
        let desc: MossRootDescriptor = {
            let mut inner = gx.inner.lock().unwrap();
            loop {
                if inner.shutdown && inner.root_queue.is_empty() {
                    inner.active_workers = inner.active_workers.saturating_sub(1);
                    #[cfg(any(test, moss_perf))]
                    moss_rt_ev(MossRtEvent::WorkerParked, 0, 0, worker_id);
                    return;
                }
                // Park excess compensation workers (E8 / §15).
                let is_excess = inner.runnable_workers() > inner.target_threads
                    && inner.solo_blocked == 0;
                if is_excess && inner.root_queue.is_empty() {
                    inner.active_workers = inner.active_workers.saturating_sub(1);
                    #[cfg(any(test, moss_perf))]
                    moss_rt_ev(MossRtEvent::WorkerParked, 0, 0, worker_id);
                    return;
                }
                if let Some(d) = inner.root_queue.pop_front() {
                    inner.running_roots += 1;
                    break d;
                }
                inner = gx.work_cv.wait(inner).unwrap();
            }
        };

        let root_id = desc.root_id;
        let completion_tx = desc.completion_tx;

        #[cfg(any(test, moss_perf))]
        moss_rt_ev(MossRtEvent::RootStarted, root_id, 0, worker_id);

        // Execute root body.  Nested messages run synchronously within this call.
        (desc.work)();

        #[cfg(any(test, moss_perf))]
        moss_rt_ev(MossRtEvent::RootCompleted, root_id, 0, worker_id);

        if let Some(tx) = completion_tx { let _ = tx.send(()); }

        {
            let mut inner = gx.inner.lock().unwrap();
            inner.running_roots = inner.running_roots.saturating_sub(1);
            if inner.running_roots == 0 && inner.root_queue.is_empty() {
                gx.drain_cv.notify_all();
            }
        }
    }
}

// ─── Gate helpers ─────────────────────────────────────────────────────────
fn gate_acquire(gx: &MossGlobalExec) {
    let mut gate = gx.gate.lock().unwrap();
    while !*gate { gate = gx.gate_cv.wait(gate).unwrap(); }
    *gate = false;
}
fn gate_release(gx: &MossGlobalExec) {
    let mut gate = gx.gate.lock().unwrap();
    *gate = true;
    gx.gate_cv.notify_all();
}

// ─── Spawn workers helper ─────────────────────────────────────────────────
fn spawn_workers(gx: &Arc<MossGlobalExec>, count: usize, start_id: usize) {
    for i in 0..count {
        let gx2 = Arc::clone(gx);
        let wid = start_id + i;
        thread::spawn(move || worker_loop(gx2, wid));
        #[cfg(any(test, moss_perf))]
        moss_rt_ev(MossRtEvent::WorkerSpawned, 0, 0, wid);
    }
}

// ─── MossExecutor (builder) ──────────────────────────────────────────────
/// Moss-facing executor builder.  Configuration is immutable after start().
pub struct MossExecutor {
    target_threads: usize,
    max_threads: usize,
    queue_capacity: usize,
}

impl MossExecutor {
    pub fn new() -> Self {
        let cpus = thread::available_parallelism().map(|n| n.get()).unwrap_or(4);
        Self { target_threads: cpus, max_threads: cpus * 4, queue_capacity: 1024 }
    }
    /// Set target compute parallelism (§3.1).
    pub fn threads(mut self, n: usize) -> Self { self.target_threads = n.max(1); self }
    /// Set finite cap on physical workers including compensation (§3.2 / E7).
    pub fn max_threads(mut self, n: usize) -> Self { self.max_threads = n; self }
    /// Set root queue capacity (§3.3).
    pub fn queue_capacity(mut self, n: usize) -> Self { self.queue_capacity = n.max(1); self }
    /// Best-effort affinity hint (§3.4) — warn and continue if OS refuses.
    pub fn affinity(self, _cores: Vec<usize>) -> Self { self }
    /// Best-effort priority hint (§3.4) — warn and continue if OS refuses.
    pub fn priority(self, _level: i32) -> Self { self }

    /// INLINE → ACTIVE transition (§5.2 / §5.3 / E6 / R15).
    /// Acquires the inline gate so no inline root overlaps ACTIVE.
    pub fn start(self) -> MossExecutorHandle {
        let target = self.target_threads;
        let max = self.max_threads.max(target);
        let cap = self.queue_capacity;

        // Assert at most one active executor (E6).
        if let Some(existing) = moss_exec_get() {
            let s = existing.inner.lock().unwrap().state;
            if s != MossIngressState::Inline {
                panic!("moss executor: at most one active executor allowed (E6/R15)");
            }
        }

        let gx = MossGlobalExec::make(cap, target, max);
        moss_exec_set(Arc::clone(&gx));

        // Acquire inline gate before activating workers (§5.2).
        // Waits for any currently running inline root to finish.
        gate_acquire(&gx);

        // Transition to ACTIVE.
        { gx.inner.lock().unwrap().state = MossIngressState::Active; }

        #[cfg(any(test, moss_perf))]
        moss_rt_ev(MossRtEvent::IngressStateChange, 0, 0, 0);

        // Spawn initial worker pool.
        { gx.inner.lock().unwrap().active_workers = target; }
        spawn_workers(&gx, target, 0);

        MossExecutorHandle { gx }
    }
}

impl Default for MossExecutor { fn default() -> Self { Self::new() } }

// ─── MossExecutorHandle (post-start) ─────────────────────────────────────
/// Returned by start(); valid until join().
pub struct MossExecutorHandle {
    gx: Arc<MossGlobalExec>,
}

impl MossExecutorHandle {
    /// Enqueue a one-way root (executor.invoke, §4 / E9 / E10).
    /// Waits for queue capacity; caller holds no Moss lock as part of ingress.
    pub fn enqueue_root(&self, desc: MossRootDescriptor) {
        let root_id = desc.root_id;
        loop {
            let mut inner = self.gx.inner.lock().unwrap();
            assert_eq!(inner.state, MossIngressState::Active,
                "moss executor: enqueue_root called outside ACTIVE state");
            if inner.root_queue.len() < inner.queue_capacity {
                inner.root_queue.push_back(desc);
                #[cfg(any(test, moss_perf))]
                moss_rt_ev(MossRtEvent::RootEnqueued, root_id, 0, usize::MAX);
                self.gx.work_cv.notify_one();
                return;
            }
            drop(inner);
            thread::sleep(Duration::from_micros(50));
        }
    }

    /// ACTIVE → DRAINING → INLINE (§5.4 / join()).
    pub fn join(self) {
        // ACTIVE → DRAINING: no new roots admitted.
        {
            let mut inner = self.gx.inner.lock().unwrap();
            assert_eq!(inner.state, MossIngressState::Active,
                "moss executor: join() called outside ACTIVE state");
            inner.state = MossIngressState::Draining;
        }
        #[cfg(any(test, moss_perf))]
        moss_rt_ev(MossRtEvent::IngressStateChange, 0, 0, 0);

        // Wait for all admitted roots to finish.
        {
            let mut inner = self.gx.inner.lock().unwrap();
            while inner.running_roots > 0 || !inner.root_queue.is_empty() {
                inner = self.gx.drain_cv.wait(inner).unwrap();
            }
            inner.shutdown = true;
        }
        self.gx.work_cv.notify_all();

        // Wait for all workers to exit (bounded poll).
        for _ in 0..2000 {
            if self.gx.inner.lock().unwrap().active_workers == 0 { break; }
            thread::sleep(Duration::from_millis(1));
        }

        // Restore INLINE state.
        {
            let mut inner = self.gx.inner.lock().unwrap();
            inner.state = MossIngressState::Inline;
            inner.shutdown = false;
            inner.active_workers = 0;
        }

        // Re-open inline gate so subsequent INLINE roots can proceed.
        gate_release(&self.gx);

        #[cfg(any(test, moss_perf))]
        moss_rt_ev(MossRtEvent::IngressStateChange, 0, 0, 0);
    }
}

// ─── runtime_invoke ───────────────────────────────────────────────────────
/// Synchronous host → Moss ingress (ROOT_RUNTIME_ABI §6).
///
/// - ACTIVE: enqueues through executor admission; caller blocks until done.
/// - INLINE: acquires inline gate, runs on caller thread, returns.
/// - DRAINING (unadmitted): waits outside Moss, holds no Moss lock, then runs
///   inline after the executor is consumed (§5.4).
///
/// The handler's reply value is returned from this function.
/// No future / task / result queue is involved.
pub fn runtime_invoke<F, R>(work: F) -> R
where
    F: FnOnce() -> R + Send + 'static,
    R: Send + 'static,
{
    // Build a reply channel that carries R.
    let (reply_tx, reply_rx) = std::sync::mpsc::sync_channel::<R>(0);
    let (done_tx, done_rx) = std::sync::mpsc::sync_channel::<()>(0);
    let done_tx2 = done_tx.clone();

    let root_id = next_root_id();

    // Wrap work to send the reply value then signal completion.
    let work_box: Box<dyn FnOnce() + Send + 'static> = Box::new(move || {
        let r = work();
        let _ = reply_tx.send(r);
        let _ = done_tx2.send(());
    });

    let gx_opt = moss_exec_get();

    match gx_opt {
        None => {
            // No executor ever started: run inline without a gate.
            #[cfg(any(test, moss_perf))]
            moss_rt_ev(MossRtEvent::RootStarted, root_id, 0, usize::MAX);
            (work_box)();
            #[cfg(any(test, moss_perf))]
            moss_rt_ev(MossRtEvent::RootCompleted, root_id, 0, usize::MAX);
            let _ = done_tx.send(());
            reply_rx.recv().unwrap()
        }
        Some(gx) => {
            let state = gx.inner.lock().unwrap().state;
            match state {
                MossIngressState::Active => {
                    // Enqueue through executor admission.
                    let desc = MossRootDescriptor::synchronous(root_id, work_box, done_tx);
                    loop {
                        let mut inner = gx.inner.lock().unwrap();
                        if inner.state == MossIngressState::Draining {
                            // State changed to DRAINING: fall to inline path.
                            drop(inner);
                            break;
                        }
                        if inner.root_queue.len() < inner.queue_capacity {
                            inner.root_queue.push_back(desc);
                            #[cfg(any(test, moss_perf))]
                            moss_rt_ev(MossRtEvent::RootEnqueued, root_id, 0, usize::MAX);
                            #[cfg(any(test, moss_perf))]
                            moss_rt_ev(MossRtEvent::AdmissionQueued, root_id, 0, usize::MAX);
                            gx.work_cv.notify_one();
                            // Block until root completes.
                            drop(inner);
                            let _ = done_rx.recv();
                            return reply_rx.recv().unwrap();
                        }
                        drop(inner);
                        thread::sleep(Duration::from_micros(50));
                    }
                    // Race: became DRAINING. Wait for inline gate then run inline.
                    gate_acquire(&gx);
                    #[cfg(any(test, moss_perf))]
                    moss_rt_ev(MossRtEvent::RootStarted, root_id, 0, usize::MAX);
                    (work_box)();
                    #[cfg(any(test, moss_perf))]
                    moss_rt_ev(MossRtEvent::RootCompleted, root_id, 0, usize::MAX);
                    let _ = done_rx.recv();
                    gate_release(&gx);
                    reply_rx.recv().unwrap()
                }
                MossIngressState::Inline | MossIngressState::Draining => {
                    // INLINE or DRAINING (unadmitted): wait for inline gate (§5.2/§5.4).
                    // Caller holds no Moss lock; waiting here is safe (A6).
                    gate_acquire(&gx);
                    #[cfg(any(test, moss_perf))]
                    moss_rt_ev(MossRtEvent::RootStarted, root_id, 0, usize::MAX);
                    (work_box)();
                    #[cfg(any(test, moss_perf))]
                    moss_rt_ev(MossRtEvent::RootCompleted, root_id, 0, usize::MAX);
                    let _ = done_rx.recv();
                    gate_release(&gx);
                    reply_rx.recv().unwrap()
                }
            }
        }
    }
}

// ─── branch_publish ───────────────────────────────────────────────────────
/// Publish a branch to the scope's pending queue (E4 / §7.3).
/// If the queue is saturated, execute inline in the owner root.
/// Publication never blocks (E4).
pub fn branch_publish<F>(scope: &Arc<MossBranchScope>, branch_fn: F)
where
    F: FnOnce() + Send + 'static,
{
    let branch_id = next_branch_id();
    // Increment outstanding before publication so join cannot fire too early.
    scope.outstanding.fetch_add(1, Ordering::SeqCst);

    const BRANCH_QUEUE_CAP: usize = 512;
    let mut pending = scope.pending.lock().unwrap();
    if pending.len() < BRANCH_QUEUE_CAP {
        pending.push_back((branch_id, Box::new(branch_fn)));
        drop(pending);
        #[cfg(any(test, moss_perf))]
        moss_rt_ev(MossRtEvent::BranchPublished, scope.owner_root_id, branch_id, usize::MAX);
    } else {
        // Saturated: owner executes inline (E4 / §7.3).
        drop(pending);
        #[cfg(any(test, moss_perf))]
        moss_rt_ev(MossRtEvent::BranchHelped, scope.owner_root_id, branch_id, usize::MAX);
        branch_fn();
        scope.mark_complete(branch_id);
    }
}

// ─── branch_join ──────────────────────────────────────────────────────────
/// Wait for all branches in the scope.  While waiting, help run unstarted
/// branches from THIS scope ONLY (E3 / §7.2 root-scoped helping).
/// Never executes unrelated queued Roots.
pub fn branch_join(scope: Arc<MossBranchScope>) {
    loop {
        if scope.outstanding.load(Ordering::SeqCst) == 0 { break; }

        // Root-scoped helping: pull from our own scope's pending queue (E3).
        let item = scope.pending.lock().unwrap().pop_front();
        if let Some((branch_id, f)) = item {
            #[cfg(any(test, moss_perf))]
            moss_rt_ev(MossRtEvent::BranchHelped, scope.owner_root_id, branch_id, usize::MAX);
            f();
            scope.mark_complete(branch_id);
        } else {
            // No unstarted branches left; wait briefly for running ones.
            let done = scope.done_mutex.lock().unwrap();
            if scope.outstanding.load(Ordering::SeqCst) == 0 { break; }
            let _ = scope.done_cond.wait_timeout(done, Duration::from_micros(100));
        }
    }
}

/// Create a new branch join scope for a Root.
pub fn branch_scope_new(owner_root_id: u64) -> Arc<MossBranchScope> {
    MossBranchScope::new(owner_root_id)
}

// ─── solo_enter / solo_leave ─────────────────────────────────────────────
/// Mark worker as entering a known Solo kernel wait (§15 / R1).
/// May activate a compensation worker to maintain target parallelism (E7).
/// Do NOT hold runtime-internal locks across the actual kernel wait (R1/R5).
pub fn solo_enter(worker_id: usize) {
    #[cfg(any(test, moss_perf))]
    moss_rt_ev(MossRtEvent::SoloEnter, 0, 0, worker_id);

    if let Some(gx) = moss_exec_get() {
        let (should_comp, wid) = {
            let mut inner = gx.inner.lock().unwrap();
            inner.solo_blocked += 1;
            let comp = inner.needs_compensation();
            let wid = inner.active_workers;
            if comp { inner.active_workers += 1; }
            (comp, wid)
        };
        if should_comp {
            // Spawn a compensation worker (E7: never exceed T_max).
            let gx2 = Arc::clone(&gx);
            thread::spawn(move || worker_loop(gx2, wid));
            #[cfg(any(test, moss_perf))]
            moss_rt_ev(MossRtEvent::WorkerSpawned, 0, 0, wid);
        }
    }
}

/// Mark worker as leaving a Solo kernel wait (§15 / R2 / E8).
/// The waking worker resumes immediately; never waits for a slot.
/// Excess compensation workers may park on their next idle cycle.
pub fn solo_leave(worker_id: usize) {
    #[cfg(any(test, moss_perf))]
    moss_rt_ev(MossRtEvent::SoloLeave, 0, 0, worker_id);

    if let Some(gx) = moss_exec_get() {
        let mut inner = gx.inner.lock().unwrap();
        inner.solo_blocked = inner.solo_blocked.saturating_sub(1);
        // Waking worker continues immediately (E8/R2). No slot wait.
    }
}

// ─── Test / perf accessors ────────────────────────────────────────────────
#[cfg(any(test, moss_perf))]
pub fn moss_executor_worker_count() -> usize {
    moss_exec_get().map(|gx| gx.inner.lock().unwrap().active_workers).unwrap_or(0)
}

#[cfg(any(test, moss_perf))]
pub fn moss_executor_state() -> &'static str {
    moss_exec_get()
        .map(|gx| match gx.inner.lock().unwrap().state {
            MossIngressState::Inline   => "INLINE",
            MossIngressState::Active   => "ACTIVE",
            MossIngressState::Draining => "DRAINING",
        })
        .unwrap_or("INLINE")
}

#[cfg(any(test, moss_perf))]
pub fn moss_executor_running_roots() -> usize {
    moss_exec_get().map(|gx| gx.inner.lock().unwrap().running_roots).unwrap_or(0)
}

#[cfg(any(test, moss_perf))]
pub fn moss_executor_queue_len() -> usize {
    moss_exec_get().map(|gx| gx.inner.lock().unwrap().root_queue.len()).unwrap_or(0)
}

#[cfg(any(test, moss_perf))]
pub fn moss_executor_max_threads() -> usize {
    moss_exec_get().map(|gx| gx.inner.lock().unwrap().max_threads).unwrap_or(0)
}

#[cfg(any(test, moss_perf))]
pub fn moss_executor_solo_blocked() -> usize {
    moss_exec_get().map(|gx| gx.inner.lock().unwrap().solo_blocked).unwrap_or(0)
}
)EXECUTOR_RUST";
}

} // namespace moss
