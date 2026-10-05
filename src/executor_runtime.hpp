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
// Fully-qualified standard types prevent collisions with user symbols.
// ============================================================

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
static MOSS_NEXT_ROOT_ID: std::sync::atomic::AtomicU64 = std::sync::atomic::AtomicU64::new(1);
static MOSS_NEXT_BRANCH_ID: std::sync::atomic::AtomicU64 = std::sync::atomic::AtomicU64::new(1);
#[allow(dead_code)]
fn next_root_id() -> u64 { MOSS_NEXT_ROOT_ID.fetch_add(1, std::sync::atomic::Ordering::Relaxed) }
fn next_branch_id() -> u64 { MOSS_NEXT_BRANCH_ID.fetch_add(1, std::sync::atomic::Ordering::Relaxed) }

// ─── Type-erased work container ──────────────────────────────────────────
pub type MossWork = std::boxed::Box</*moss*/dyn/*moss*/FnOnce() + Send + 'static>;

// ─── RootDescriptor ──────────────────────────────────────────────────────
/// Internal submission record for one statically resolved handler invocation.
/// Not a first-class Moss value or dynamic dispatch target (ROOT_RUNTIME_ABI §3).
/// All three ingress paths share this representation.
pub struct MossRootDescriptor {
    /// Unique root identity (for instrumentation).
    pub root_id: u64,
    /// Type-erased root body; holds the domain ref and evaluated arg snapshot.
    pub work: MossWork,
}

impl MossRootDescriptor {
    /// Create a root descriptor with work closure.
    pub fn one_way(root_id: u64, work: impl FnOnce() + Send + 'static) -> Self {
        Self { root_id, work: std::boxed::Box::new(work) }
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
    pending: std::sync::Mutex<std::collections::VecDeque<(u64, MossWork)>>,
    /// Branches not yet complete (published + running).
    outstanding: std::sync::atomic::AtomicUsize,
    done_mutex: std::sync::Mutex<bool>,
    done_cond: std::sync::Condvar,
}

impl MossBranchScope {
    pub fn new(owner_root_id: u64) -> std::sync::Arc<Self> {
        std::sync::Arc::new(Self {
            owner_root_id,
            pending: std::sync::Mutex::new(std::collections::VecDeque::new()),
            outstanding: std::sync::atomic::AtomicUsize::new(0),
            done_mutex: std::sync::Mutex::new(false),
            done_cond: std::sync::Condvar::new(),
        })
    }

    fn mark_complete(&self, branch_id: u64) {
        #[cfg(any(test, moss_perf))]
        moss_rt_ev(MossRtEvent::BranchCompleted, self.owner_root_id, branch_id, usize::MAX);
        let prev = self.outstanding.fetch_sub(1, std::sync::atomic::Ordering::AcqRel);
        if prev == 1 {
            let mut done = self.done_mutex.lock().unwrap_or_else(|e| e.into_inner());
            *done = true;
            self.done_cond.notify_all();
        }
    }
}

// ─── Executor inner state ─────────────────────────────────────────────────
struct MossExecInner {
    root_queue: std::collections::VecDeque<MossRootDescriptor>,
    queue_capacity: usize,
    target_threads: usize,
    max_threads: usize,
    active_workers: usize,
    running_roots: usize,
    draining: bool,
    shutdown: bool,
    next_worker_id: usize,
}

// ─── Global executor instance ─────────────────────────────────────────────
struct MossGlobalExec {
    inner: std::sync::Mutex<MossExecInner>,
    work_cv: std::sync::Condvar,
    queue_not_full_cv: std::sync::Condvar,
    drain_cv: std::sync::Condvar,
    branch_scopes: std::sync::Mutex<std::collections::VecDeque<std::sync::Arc<MossBranchScope>>>,
    solo_blocked_workers: std::sync::Mutex<std::collections::HashSet<usize>>,
    worker_threads: std::sync::Mutex<std::vec::Vec<std::thread::JoinHandle<()>>>,
    affinity_cores: std::option::Option<std::vec::Vec<usize>>,
    priority_level: std::option::Option<i32>,
}

// ─── Process-global runtime ingress ──────────────────────────────────────
struct MossProcessRuntimeInner {
    state: MossIngressState,
    inline_running: bool,
    active_executor: std::option::Option<std::sync::Arc<MossGlobalExec>>,
}

struct MossProcessRuntime {
    inner: std::sync::Mutex<MossProcessRuntimeInner>,
    ingress_cv: std::sync::Condvar,
}

static PROCESS_RUNTIME: std::sync::OnceLock<MossProcessRuntime> = std::sync::OnceLock::new();

fn process_rt() -> &'static MossProcessRuntime {
    PROCESS_RUNTIME.get_or_init(|| MossProcessRuntime {
        inner: std::sync::Mutex::new(MossProcessRuntimeInner {
            state: MossIngressState::Inline,
            inline_running: false,
            active_executor: None,
        }),
        ingress_cv: std::sync::Condvar::new(),
    })
}

// ─── Apply affinity and priority hints ────────────────────────────────────
fn apply_affinity_and_priority(_cores: std::option::Option<&std::vec::Vec<usize>>, _priority: std::option::Option<i32>) {}

// ─── Worker loop ──────────────────────────────────────────────────────────
/// Each physical worker runs this loop.
/// R4 / E2 / E11: worker runs ONE root at a time; never switches to another
/// root before the current root (and its nested messages) completes.
fn worker_loop(gx: std::sync::Arc<MossGlobalExec>, worker_id: usize) {
    let _abort_guard = MossAbortOnUnwind;
    loop {
        enum Task {
            Root(MossRootDescriptor),
            Branch(std::sync::Arc<MossBranchScope>, u64, MossWork),
        }

        let task = {
            let mut inner = gx.inner.lock().unwrap_or_else(|e| e.into_inner());
            loop {
                if inner.shutdown && inner.root_queue.is_empty() {
                    inner.active_workers = inner.active_workers.saturating_sub(1);
                    #[cfg(any(test, moss_perf))]
                    moss_rt_ev(MossRtEvent::WorkerParked, 0, 0, worker_id);
                    return;
                }

                // Check excess compensation parking (E8 / §15)
                let solo_count = gx.solo_blocked_workers.lock().unwrap_or_else(|e| e.into_inner()).len();
                let runnable = inner.active_workers.saturating_sub(solo_count);
                let is_excess = runnable > inner.target_threads && solo_count == 0;

                // Priority 1: Pick Root work if available
                if let Some(desc) = inner.root_queue.pop_front() {
                    inner.running_roots += 1;
                    gx.queue_not_full_cv.notify_one();
                    break Task::Root(desc);
                }

                // Priority 2: Pick Branch work from any active branch scope
                let branch_item = {
                    let mut scopes = gx.branch_scopes.lock().unwrap_or_else(|e| e.into_inner());
                    let mut found = None;
                    let len = scopes.len();
                    'find_branch: for _ in 0..len {
                        if let Some(scope) = scopes.pop_front() {
                            let (has_more, item) = {
                                let mut pending = scope.pending.lock().unwrap_or_else(|e| e.into_inner());
                                let item = pending.pop_front();
                                let has_more = !pending.is_empty();
                                (has_more, item)
                            };
                            if has_more {
                                scopes.push_back(std::sync::Arc::clone(&scope));
                            }
                            if let Some((bid, bfn)) = item {
                                found = Some((scope, bid, bfn));
                                break 'find_branch;
                            }
                        }
                    }
                    found
                };

                if let Some((scope, bid, bfn)) = branch_item {
                    break Task::Branch(scope, bid, bfn);
                }

                if is_excess && inner.root_queue.is_empty() {
                    inner.active_workers = inner.active_workers.saturating_sub(1);
                    #[cfg(any(test, moss_perf))]
                    moss_rt_ev(MossRtEvent::WorkerParked, 0, 0, worker_id);
                    return;
                }

                inner = gx.work_cv.wait(inner).unwrap_or_else(|e| e.into_inner());
            }
        };

        match task {
            Task::Root(desc) => {
                let root_id = desc.root_id;
                #[cfg(any(test, moss_perf))]
                moss_rt_ev(MossRtEvent::RootStarted, root_id, 0, worker_id);

                (desc.work)();

                #[cfg(any(test, moss_perf))]
                moss_rt_ev(MossRtEvent::RootCompleted, root_id, 0, worker_id);

                {
                    let mut inner = gx.inner.lock().unwrap_or_else(|e| e.into_inner());
                    inner.running_roots = inner.running_roots.saturating_sub(1);
                    if inner.running_roots == 0 && inner.root_queue.is_empty() {
                        gx.drain_cv.notify_all();
                    }
                }
            }
            Task::Branch(scope, branch_id, branch_fn) => {
                #[cfg(any(test, moss_perf))]
                moss_rt_ev(MossRtEvent::BranchHelped, scope.owner_root_id, branch_id, worker_id);

                branch_fn();

                scope.mark_complete(branch_id);
            }
        }
    }
}

// ─── Spawn workers helper ─────────────────────────────────────────────────
fn spawn_one_worker(gx: &std::sync::Arc<MossGlobalExec>, wid: usize) {
    let gx2 = std::sync::Arc::clone(gx);
    let affinity = gx.affinity_cores.clone();
    let priority = gx.priority_level;
    let handle = std::thread::Builder::new()
        .name(format!("moss-worker-{}", wid))
        .spawn(move || {
            apply_affinity_and_priority(affinity.as_ref(), priority);
            worker_loop(gx2, wid);
        })
        .expect("failed to spawn worker thread");
    gx.worker_threads.lock().unwrap_or_else(|e| e.into_inner()).push(handle);
    #[cfg(any(test, moss_perf))]
    moss_rt_ev(MossRtEvent::WorkerSpawned, 0, 0, wid);
}

// ─── MossExecutor (builder) ──────────────────────────────────────────────
/// Moss-facing executor builder. Configuration is immutable after start().
pub struct MossExecutor {
    target_threads: usize,
    max_threads: usize,
    queue_capacity: usize,
    affinity_cores: std::option::Option<std::vec::Vec<usize>>,
    priority_level: std::option::Option<i32>,
}

impl MossExecutor {
    pub fn new() -> Self {
        let cpus = std::thread::available_parallelism().map(|n| n.get()).unwrap_or(4);
        Self {
            target_threads: cpus,
            max_threads: cpus * 4,
            queue_capacity: 1024,
            affinity_cores: None,
            priority_level: None,
        }
    }
    /// Set target compute parallelism (§3.1).
    pub fn threads(mut self, n: usize) -> Self { self.target_threads = n.max(1); self }
    /// Set finite cap on physical workers including compensation (§3.2 / E7).
    pub fn max_threads(mut self, n: usize) -> Self { self.max_threads = n; self }
    /// Set root queue capacity (§3.3).
    pub fn queue_capacity(mut self, n: usize) -> Self { self.queue_capacity = n.max(1); self }
    /// Best-effort affinity hint (§3.4) — warn and continue if OS refuses.
    pub fn affinity(mut self, cores: std::vec::Vec<usize>) -> Self { self.affinity_cores = Some(cores); self }
    /// Best-effort priority hint (§3.4) — warn and continue if OS refuses.
    pub fn priority(mut self, level: i32) -> Self { self.priority_level = Some(level); self }

    /// INLINE → ACTIVE transition (§5.2 / §5.3 / E6 / R15).
    /// Acquires the inline gate so no inline root overlaps ACTIVE.
    pub fn start(self) -> MossExecutorHandle {
        let rt = process_rt();
        let target = self.target_threads;
        let max = self.max_threads.max(target);
        let cap = self.queue_capacity;

        let gx = std::sync::Arc::new(MossGlobalExec {
            inner: std::sync::Mutex::new(MossExecInner {
                root_queue: std::collections::VecDeque::new(),
                queue_capacity: cap,
                target_threads: target,
                max_threads: max,
                active_workers: target,
                running_roots: 0,
                draining: false,
                shutdown: false,
                next_worker_id: target,
            }),
            work_cv: std::sync::Condvar::new(),
            queue_not_full_cv: std::sync::Condvar::new(),
            drain_cv: std::sync::Condvar::new(),
            branch_scopes: std::sync::Mutex::new(std::collections::VecDeque::new()),
            solo_blocked_workers: std::sync::Mutex::new(std::collections::HashSet::new()),
            worker_threads: std::sync::Mutex::new(std::vec::Vec::default()),
            affinity_cores: self.affinity_cores,
            priority_level: self.priority_level,
        });

        {
            let mut inner = rt.inner.lock().unwrap_or_else(|e| e.into_inner());
            if inner.state != MossIngressState::Inline {
                drop(inner);
                panic!("moss executor: at most one active executor allowed (E6/R15)");
            }
            // Wait for any running INLINE root to complete
            while inner.inline_running {
                inner = rt.ingress_cv.wait(inner).unwrap_or_else(|e| e.into_inner());
            }

            inner.state = MossIngressState::Active;
            inner.active_executor = Some(std::sync::Arc::clone(&gx));
        }

        #[cfg(any(test, moss_perf))]
        moss_rt_ev(MossRtEvent::IngressStateChange, 0, 0, 0);

        // Spawn target workers
        for wid in 0..target {
            spawn_one_worker(&gx, wid);
        }

        MossExecutorHandle { gx }
    }
}

impl Default for MossExecutor { fn default() -> Self { Self::new() } }

// ─── MossExecutorHandle (post-start) ─────────────────────────────────────
/// Returned by start(); valid until join().
pub struct MossExecutorHandle {
    gx: std::sync::Arc<MossGlobalExec>,
}

impl MossExecutorHandle {
    /// Enqueue a one-way root (executor.invoke, §4 / E9 / E10).
    /// Waits for queue capacity; caller holds no Moss lock as part of ingress.
    pub fn enqueue_root(&self, desc: MossRootDescriptor) {
        let root_id = desc.root_id;
        let mut inner = self.gx.inner.lock().unwrap_or_else(|e| e.into_inner());
        loop {
            if inner.draining {
                drop(inner);
                panic!("moss executor: enqueue_root called outside ACTIVE state");
            }
            if inner.root_queue.len() < inner.queue_capacity {
                inner.root_queue.push_back(desc);
                #[cfg(any(test, moss_perf))]
                moss_rt_ev(MossRtEvent::RootEnqueued, root_id, 0, usize::MAX);
                self.gx.work_cv.notify_one();
                return;
            }
            inner = self.gx.queue_not_full_cv.wait(inner).unwrap_or_else(|e| e.into_inner());
        }
    }

    /// ACTIVE → DRAINING → INLINE (§5.4 / join()).
    pub fn join(self) {
        let rt = process_rt();

        // Step 1: Transition ACTIVE → DRAINING
        {
            let mut inner = rt.inner.lock().unwrap_or_else(|e| e.into_inner());
            if inner.state != MossIngressState::Active {
                drop(inner);
                panic!("moss executor: join() called outside ACTIVE state");
            }
            inner.state = MossIngressState::Draining;
        }

        {
            let mut exec_inner = self.gx.inner.lock().unwrap_or_else(|e| e.into_inner());
            exec_inner.draining = true;
            self.gx.queue_not_full_cv.notify_all();
        }

        #[cfg(any(test, moss_perf))]
        moss_rt_ev(MossRtEvent::IngressStateChange, 0, 0, 0);

        // Step 2: Wait for all admitted roots to finish
        {
            let mut exec_inner = self.gx.inner.lock().unwrap_or_else(|e| e.into_inner());
            while exec_inner.running_roots > 0 || !exec_inner.root_queue.is_empty() {
                exec_inner = self.gx.drain_cv.wait(exec_inner).unwrap_or_else(|e| e.into_inner());
            }
            exec_inner.shutdown = true;
        }
        self.gx.work_cv.notify_all();

        // Step 3: Wait for every physical worker to terminate
        let handles: std::vec::Vec<_> = {
            let mut guard = self.gx.worker_threads.lock().unwrap_or_else(|e| e.into_inner());
            guard.drain(..).collect()
        };
        for h in handles {
            let _ = h.join();
        }

        // Step 4: Transition DRAINING → INLINE
        {
            let mut inner = rt.inner.lock().unwrap_or_else(|e| e.into_inner());
            inner.state = MossIngressState::Inline;
            inner.active_executor = None;
            rt.ingress_cv.notify_all();
        }

        #[cfg(any(test, moss_perf))]
        moss_rt_ev(MossRtEvent::IngressStateChange, 0, 0, 0);
    }
}

// ─── runtime_invoke ───────────────────────────────────────────────────────
/// Synchronous host → Moss ingress (ROOT_RUNTIME_ABI §6).
///
/// - ACTIVE: enqueues through executor admission; caller blocks until done.
/// - INLINE: acquires inline admission, runs on caller thread, returns.
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
    let rt = process_rt();
    let root_id = next_root_id();
    let mut work_opt = Some(work);

    loop {
        let mut inner = rt.inner.lock().unwrap_or_else(|e| e.into_inner());
        match inner.state {
            MossIngressState::Inline => {
                while inner.state == MossIngressState::Inline && inner.inline_running {
                    inner = rt.ingress_cv.wait(inner).unwrap_or_else(|e| e.into_inner());
                }
                if inner.state != MossIngressState::Inline {
                    continue;
                }
                inner.inline_running = true;
                drop(inner);

                #[cfg(any(test, moss_perf))]
                moss_rt_ev(MossRtEvent::RootStarted, root_id, 0, usize::MAX);

                let work = work_opt.take().expect("work closure already consumed");
                let result = {
                    let _abort = MossAbortOnUnwind;
                    work()
                };

                #[cfg(any(test, moss_perf))]
                moss_rt_ev(MossRtEvent::RootCompleted, root_id, 0, usize::MAX);

                let mut inner = rt.inner.lock().unwrap_or_else(|e| e.into_inner());
                inner.inline_running = false;
                rt.ingress_cv.notify_all();
                return result;
            }
            MossIngressState::Active => {
                let gx = std::sync::Arc::clone(inner.active_executor.as_ref().expect("active executor missing in ACTIVE state"));
                drop(inner);

                let reply_slot: std::sync::Arc<(std::sync::Mutex<std::option::Option<R>>, std::sync::Condvar)> =
                    std::sync::Arc::new((std::sync::Mutex::new(None), std::sync::Condvar::new()));
                let reply_slot_clone = std::sync::Arc::clone(&reply_slot);

                let mut exec_inner = gx.inner.lock().unwrap_or_else(|e| e.into_inner());
                'admit_loop: loop {
                    if exec_inner.draining {
                        // Became DRAINING before admission; fall back to waiting for INLINE
                        break 'admit_loop;
                    }
                    if exec_inner.root_queue.len() < exec_inner.queue_capacity {
                        let work = work_opt.take().expect("work closure already consumed");
                        let desc = MossRootDescriptor::one_way(root_id, move || {
                            let res = {
                                let _abort = MossAbortOnUnwind;
                                work()
                            };
                            let (lock, cvar) = &*reply_slot_clone;
                            let mut guard = lock.lock().unwrap_or_else(|e| e.into_inner());
                            *guard = Some(res);
                            cvar.notify_one();
                        });

                        exec_inner.root_queue.push_back(desc);
                        #[cfg(any(test, moss_perf))]
                        moss_rt_ev(MossRtEvent::RootEnqueued, root_id, 0, usize::MAX);
                        #[cfg(any(test, moss_perf))]
                        moss_rt_ev(MossRtEvent::AdmissionQueued, root_id, 0, usize::MAX);
                        gx.work_cv.notify_one();
                        drop(exec_inner);

                        let (lock, cvar) = &*reply_slot;
                        let mut guard = lock.lock().unwrap_or_else(|e| e.into_inner());
                        while guard.is_none() {
                            guard = cvar.wait(guard).unwrap_or_else(|e| e.into_inner());
                        }
                        return guard.take().unwrap();
                    }
                    exec_inner = gx.queue_not_full_cv.wait(exec_inner).unwrap_or_else(|e| e.into_inner());
                }
            }
            MossIngressState::Draining => {
                while inner.state == MossIngressState::Draining {
                    inner = rt.ingress_cv.wait(inner).unwrap_or_else(|e| e.into_inner());
                }
            }
        }
    }
}

// ─── branch_publish ───────────────────────────────────────────────────────
/// Publish a branch to the scope's pending queue (E4 / §7.3).
/// If the queue is saturated, execute inline in the owner root.
/// Publication never blocks (E4).
pub fn branch_publish<F>(scope: &std::sync::Arc<MossBranchScope>, branch_fn: F)
where
    F: FnOnce() + Send + 'static,
{
    let branch_id = next_branch_id();
    scope.outstanding.fetch_add(1, std::sync::atomic::Ordering::SeqCst);

    const BRANCH_QUEUE_CAP: usize = 512;
    let mut pending = scope.pending.lock().unwrap_or_else(|e| e.into_inner());
    if pending.len() < BRANCH_QUEUE_CAP {
        pending.push_back((branch_id, std::boxed::Box::new(branch_fn)));
        drop(pending);

        #[cfg(any(test, moss_perf))]
        moss_rt_ev(MossRtEvent::BranchPublished, scope.owner_root_id, branch_id, usize::MAX);

        let gx_opt = {
            let rt = process_rt();
            let inner = rt.inner.lock().unwrap_or_else(|e| e.into_inner());
            inner.active_executor.clone()
        };
        if let Some(gx) = gx_opt {
            gx.branch_scopes.lock().unwrap_or_else(|e| e.into_inner()).push_back(std::sync::Arc::clone(scope));
            gx.work_cv.notify_one();
        }
    } else {
        // Saturated: owner executes inline (E4 / §7.3)
        drop(pending);
        #[cfg(any(test, moss_perf))]
        moss_rt_ev(MossRtEvent::BranchHelped, scope.owner_root_id, branch_id, usize::MAX);
        branch_fn();
        scope.mark_complete(branch_id);
    }
}

// ─── branch_join ──────────────────────────────────────────────────────────
/// Wait for all branches in the scope. While waiting, help run unstarted
/// branches from THIS scope ONLY (E3 / §7.2 root-scoped helping).
/// Never executes unrelated queued Roots.
pub fn branch_join(scope: std::sync::Arc<MossBranchScope>) {
    'join_loop: loop {
        if scope.outstanding.load(std::sync::atomic::Ordering::SeqCst) == 0 { break 'join_loop; }

        // Root-scoped helping: pull from our own scope's pending queue (E3)
        let item = scope.pending.lock().unwrap_or_else(|e| e.into_inner()).pop_front();
        if let Some((branch_id, f)) = item {
            #[cfg(any(test, moss_perf))]
            moss_rt_ev(MossRtEvent::BranchHelped, scope.owner_root_id, branch_id, usize::MAX);
            f();
            scope.mark_complete(branch_id);
        } else {
            // No unstarted branches in this scope; wait for running ones
            let done = scope.done_mutex.lock().unwrap_or_else(|e| e.into_inner());
            if scope.outstanding.load(std::sync::atomic::Ordering::SeqCst) == 0 { break 'join_loop; }
            let _ = scope.done_cond.wait_timeout(done, std::time::Duration::from_micros(100));
        }
    }
}

/// Create a new branch join scope for a Root.
pub fn branch_scope_new(owner_root_id: u64) -> std::sync::Arc<MossBranchScope> {
    MossBranchScope::new(owner_root_id)
}

// ─── solo_enter / solo_leave ─────────────────────────────────────────────
/// Mark worker as entering a known Solo kernel wait (§15 / R1).
/// May activate a compensation worker to maintain target parallelism (E7).
/// Do NOT hold runtime-internal locks across the actual kernel wait (R1/R5).
pub fn solo_enter(worker_id: usize) {
    #[cfg(any(test, moss_perf))]
    moss_rt_ev(MossRtEvent::SoloEnter, 0, 0, worker_id);

    let rt = process_rt();
    let gx_opt = {
        let inner = rt.inner.lock().unwrap_or_else(|e| e.into_inner());
        inner.active_executor.clone()
    };
    if let Some(gx) = gx_opt {
        let mut blocked = gx.solo_blocked_workers.lock().unwrap_or_else(|e| e.into_inner());
        if blocked.insert(worker_id) {
            let mut exec_inner = gx.inner.lock().unwrap_or_else(|e| e.into_inner());
            let solo_count = blocked.len();
            let runnable = exec_inner.active_workers.saturating_sub(solo_count);
            let should_comp = runnable < exec_inner.target_threads
                && exec_inner.active_workers < exec_inner.max_threads;
            let new_wid = if should_comp {
                exec_inner.active_workers += 1;
                let wid = exec_inner.next_worker_id;
                exec_inner.next_worker_id += 1;
                Some(wid)
            } else {
                None
            };
            drop(exec_inner);
            drop(blocked);
            if let Some(wid) = new_wid {
                spawn_one_worker(&gx, wid);
            }
        }
    }
}

/// Mark worker as leaving a Solo kernel wait (§15 / R2 / E8).
/// The waking worker resumes immediately; never waits for a slot.
/// Excess compensation workers may park on their next idle cycle.
pub fn solo_leave(worker_id: usize) {
    #[cfg(any(test, moss_perf))]
    moss_rt_ev(MossRtEvent::SoloLeave, 0, 0, worker_id);

    let rt = process_rt();
    let gx_opt = {
        let inner = rt.inner.lock().unwrap_or_else(|e| e.into_inner());
        inner.active_executor.clone()
    };
    if let Some(gx) = gx_opt {
        let mut blocked = gx.solo_blocked_workers.lock().unwrap_or_else(|e| e.into_inner());
        blocked.remove(&worker_id);
        gx.work_cv.notify_all();
    }
}

// ─── Test / perf accessors ────────────────────────────────────────────────
#[cfg(any(test, moss_perf))]
pub fn moss_executor_worker_count() -> usize {
    let rt = process_rt();
    let inner = rt.inner.lock().unwrap_or_else(|e| e.into_inner());
    inner.active_executor.as_ref().map(|gx| gx.inner.lock().unwrap_or_else(|e| e.into_inner()).active_workers).unwrap_or(0)
}

#[cfg(any(test, moss_perf))]
pub fn moss_executor_state() -> &'static str {
    let rt = process_rt();
    let inner = rt.inner.lock().unwrap_or_else(|e| e.into_inner());
    match inner.state {
        MossIngressState::Inline   => "INLINE",
        MossIngressState::Active   => "ACTIVE",
        MossIngressState::Draining => "DRAINING",
    }
}

#[cfg(any(test, moss_perf))]
pub fn moss_executor_running_roots() -> usize {
    let rt = process_rt();
    let inner = rt.inner.lock().unwrap_or_else(|e| e.into_inner());
    inner.active_executor.as_ref().map(|gx| gx.inner.lock().unwrap_or_else(|e| e.into_inner()).running_roots).unwrap_or(0)
}

#[cfg(any(test, moss_perf))]
pub fn moss_executor_queue_len() -> usize {
    let rt = process_rt();
    let inner = rt.inner.lock().unwrap_or_else(|e| e.into_inner());
    inner.active_executor.as_ref().map(|gx| gx.inner.lock().unwrap_or_else(|e| e.into_inner()).root_queue.len()).unwrap_or(0)
}

#[cfg(any(test, moss_perf))]
pub fn moss_executor_max_threads() -> usize {
    let rt = process_rt();
    let inner = rt.inner.lock().unwrap_or_else(|e| e.into_inner());
    inner.active_executor.as_ref().map(|gx| gx.inner.lock().unwrap_or_else(|e| e.into_inner()).max_threads).unwrap_or(0)
}

#[cfg(any(test, moss_perf))]
pub fn moss_executor_solo_blocked() -> usize {
    let rt = process_rt();
    let inner = rt.inner.lock().unwrap_or_else(|e| e.into_inner());
    inner.active_executor.as_ref().map(|gx| gx.solo_blocked_workers.lock().unwrap_or_else(|e| e.into_inner()).len()).unwrap_or(0)
}
)EXECUTOR_RUST";
}

} // namespace moss
