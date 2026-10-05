// ============================================================
// Phase 20 Executor & Root Runtime
// Module ownership: src/executor_runtime.hpp.
//
// Shared root-admission, branch-helping scheduler, and Solo-block
// compensation runtime for Moss programs.
//
// Key contracts:
//   - INLINE -> ACTIVE -> DRAINING -> INLINE lifecycle (§5.4)
//   - RootDescriptor: shared representation for all ingress paths (§3)
//   - MossExecutor: builder pattern with immutable post-start config (§3)
//   - MossExecutorHandle: linear capability, enqueues roots, join() drains (§4, §5)
//   - runtime_invoke: synchronous host ingress with reply return (§6)
//   - branch_publish / branch_join: bounded non-blocking branch API (§7.3)
//   - solo_enter / solo_leave: compensation hooks for Solo kernel waits (§15)
//   - A5 / R5 leaf-lock invariant: runtime locks are never nested
//   - R5 fairness: ticket-based FIFO ingress and root queue admission
//   - Agent D seam: moss_root_start / moss_root_submit / moss_root_join
//   - Agent B seam: solo_enter_current / solo_leave_current installed via
//     moss_root_runtime::moss_set_solo_hooks (B owns moss_solo_enter/leave)
//   - Test instrumentation under #[cfg(any(test, moss_perf))]
//
// Module ownership: src/executor_runtime.hpp.
// src/moss.cpp includes this and emits it alongside handler_runtime_rust().
// See docs/ROOT_RUNTIME_ABI.md and docs/MOSS_PHASE_20_FILE_IO_AND_EXECUTORS.md.

#include <string>

namespace moss {

inline std::string executor_runtime_rust(bool is_library = false) {
  std::string s;
  s += R"EXECUTOR_RUST(
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

// ─── Type-erased work container ──────────────────────────────────────────
pub type MossWork = std::boxed::Box<dyn FnOnce() + Send + 'static>;

// ─── RootDescriptor ──────────────────────────────────────────────────────
/// Internal submission record for one statically resolved handler invocation.
/// Not a first-class Moss value or dynamic dispatch target (ROOT_RUNTIME_ABI §3).
/// All three ingress paths share this representation.
pub struct MossRootDescriptor {
    /// Unique root identity (for instrumentation).
    pub root_id: u64,
    /// Optional compiler-owned semantic target identity metadata.
    pub target_identity: std::option::Option<&'static str>,
    /// Type-erased root body; holds the domain ref and evaluated arg snapshot.
    pub work: MossWork,
}

impl MossRootDescriptor {
    /// Create a root descriptor with work closure.
    pub fn one_way(root_id: u64, work: impl FnOnce() + Send + 'static) -> Self {
        Self { root_id, target_identity: None, work: std::boxed::Box::new(work) }
    }
    /// Create a root descriptor with target identity metadata for diagnostics.
    pub fn with_target(root_id: u64, target_identity: &'static str, work: impl FnOnce() + Send + 'static) -> Self {
        Self { root_id, target_identity: Some(target_identity), work: std::boxed::Box::new(work) }
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
)EXECUTOR_RUST";

  if (!is_library) {
    s += R"EXECUTOR_RUST(
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
        } else {
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
    solo_blocked_workers: std::collections::HashSet<usize>,
    next_admission_ticket: u64,
    serving_admission_ticket: u64,
}

impl MossExecInner {
    fn runnable_workers(&self) -> usize {
        self.active_workers.saturating_sub(self.solo_blocked_workers.len())
    }
}

// ─── Global executor instance ─────────────────────────────────────────────
struct MossGlobalExec {
    inner: std::sync::Mutex<MossExecInner>,
    work_cv: std::sync::Condvar,
    queue_not_full_cv: std::sync::Condvar,
    drain_cv: std::sync::Condvar,
    branch_scopes: std::sync::Mutex<std::collections::VecDeque<std::sync::Arc<MossBranchScope>>>,
    branch_seq: std::sync::atomic::AtomicU64,
    worker_threads: std::sync::Mutex<std::vec::Vec<std::thread::JoinHandle<()>>>,
    affinity_cores: std::option::Option<std::vec::Vec<usize>>,
    priority_level: std::option::Option<i32>,
}

// ─── Process-global runtime ingress ──────────────────────────────────────
struct MossProcessRuntimeInner {
    state: MossIngressState,
    start_pending: bool,
    inline_running: bool,
    active_executor: std::option::Option<std::sync::Arc<MossGlobalExec>>,
    next_inline_ticket: u64,
    serving_inline_ticket: u64,
}

struct MossProcessRuntime {
    inner: std::sync::Mutex<MossProcessRuntimeInner>,
    ingress_cv: std::sync::Condvar,
    next_root_id: std::sync::atomic::AtomicU64,
    next_branch_id: std::sync::atomic::AtomicU64,
}

impl MossProcessRuntime {
    fn new() -> Self {
        Self {
            inner: std::sync::Mutex::new(MossProcessRuntimeInner {
                state: MossIngressState::Inline,
                start_pending: false,
                inline_running: false,
                active_executor: None,
                next_inline_ticket: 0,
                serving_inline_ticket: 0,
            }),
            ingress_cv: std::sync::Condvar::new(),
            next_root_id: std::sync::atomic::AtomicU64::new(1),
            next_branch_id: std::sync::atomic::AtomicU64::new(1),
        }
    }
}

static RUNTIME: std::sync::OnceLock<MossProcessRuntime> = std::sync::OnceLock::new();

fn process_rt() -> &'static MossProcessRuntime {
    RUNTIME.get_or_init(|| {
        // Agent B owns the process-wide moss_solo_enter/moss_solo_leave
        // symbols (moss_root_runtime). Install the executor compensation
        // callbacks once, before any worker can exist; they are never cleared
        // or replaced, and are no-ops off-worker or with no active executor.
        moss_root_runtime::moss_set_solo_hooks(solo_enter_current, solo_leave_current);
        MossProcessRuntime::new()
    })
}

// ─── Cross-crate C ABI exports ───────────────────────────────────────────
#[no_mangle]
pub extern "C" fn __moss_next_root_id() -> u64 {
    process_rt().next_root_id.fetch_add(1, std::sync::atomic::Ordering::Relaxed)
}

#[no_mangle]
pub extern "C" fn __moss_next_branch_id() -> u64 {
    process_rt().next_branch_id.fetch_add(1, std::sync::atomic::Ordering::Relaxed)
}

#[no_mangle]
pub extern "C" fn __moss_branch_scope_new(owner_root_id: u64) -> *mut () {
    let scope = MossBranchScope::new(owner_root_id);
    std::sync::Arc::into_raw(scope) as *mut ()
}

#[no_mangle]
pub unsafe extern "C" fn __moss_branch_scope_clone(scope_ptr: *mut ()) -> *mut () {
    // SAFETY: caller passes a valid Arc pointer from __moss_branch_scope_new or clone
    let arc = unsafe { std::sync::Arc::from_raw(scope_ptr as *const MossBranchScope) };
    let cloned = std::sync::Arc::clone(&arc);
    let _ = std::sync::Arc::into_raw(arc);
    std::sync::Arc::into_raw(cloned) as *mut ()
}

#[no_mangle]
pub unsafe extern "C" fn __moss_branch_scope_drop(scope_ptr: *mut ()) {
    // SAFETY: caller relinquishes ownership of one Arc count
    let _ = unsafe { std::sync::Arc::from_raw(scope_ptr as *const MossBranchScope) };
}

#[no_mangle]
pub unsafe extern "C" fn __moss_branch_publish_trampoline(
    scope_ptr: *mut (),
    data: *mut (),
    invoker: unsafe extern "C" fn(*mut ()),
) {
    // SAFETY: caller borrows a valid Arc pointer
    let arc = unsafe { std::sync::Arc::from_raw(scope_ptr as *const MossBranchScope) };
    let scope_clone = std::sync::Arc::clone(&arc);
    let _ = std::sync::Arc::into_raw(arc);

    let data_addr = data as usize;
    branch_publish(&scope_clone, move || {
        // SAFETY: invoking foreign closure thunk exactly once
        unsafe { invoker(data_addr as *mut ()) };
    });
}

#[no_mangle]
pub unsafe extern "C" fn __moss_branch_join(scope_ptr: *mut ()) {
    // SAFETY: caller passes a valid Arc pointer to join
    let arc = unsafe { std::sync::Arc::from_raw(scope_ptr as *const MossBranchScope) };
    branch_join(arc);
}

// ─── ID counters ─────────────────────────────────────────────────────────
#[allow(dead_code)]
pub fn next_root_id() -> u64 {
    __moss_next_root_id()
}

pub fn next_branch_id() -> u64 {
    __moss_next_branch_id()
}

// ─── Thread-local worker identity ─────────────────────────────────────────
thread_local! {
    static MOSS_CURRENT_WORKER_ID: std::cell::Cell<std::option::Option<usize>> = const { std::cell::Cell::new(None) };
}

pub fn current_worker_id() -> std::option::Option<usize> {
    MOSS_CURRENT_WORKER_ID.with(|c| c.get())
}

// ─── Apply affinity and priority hints ────────────────────────────────────
fn apply_affinity_and_priority(cores: std::option::Option<&std::vec::Vec<usize>>, priority: std::option::Option<i32>) {
    #[cfg(target_os = "linux")]
    {
        if let Some(cores) = cores {
            let mut set: [u8; 128] = [0; 128]; // 1024 bits cpu_set_t
            for &c in cores {
                if c < 1024 {
                    set[c / 8] |= 1 << (c % 8);
                }
            }
            extern "C" {
                fn sched_setaffinity(pid: i32, cpusetsize: usize, mask: *const u8) -> i32;
            }
            // SAFETY: Calling libc sched_setaffinity with valid pointer and size.
            let ret = unsafe { sched_setaffinity(0, std::mem::size_of_val(&set), set.as_ptr()) };
            if ret != 0 {
                eprintln!("moss executor warning: sched_setaffinity refused");
            }
        }
        if let Some(pri) = priority {
            extern "C" {
                fn setpriority(which: i32, who: i32, prio: i32) -> i32;
            }
            // SAFETY: Calling libc setpriority with current process target.
            let ret = unsafe { setpriority(0, 0, pri) };
            if ret != 0 {
                eprintln!("moss executor warning: setpriority refused");
            }
        }
    }
    #[cfg(not(target_os = "linux"))]
    {
        if cores.is_some() || priority.is_some() {
            eprintln!("moss executor warning: affinity/priority not supported on this platform");
        }
    }
}

// ─── Worker loop (A5 / R5 leaf-lock compliant) ───────────────────────────
/// Each physical worker runs this loop.
/// R4 / E2 / E11: worker runs ONE root at a time; never switches to another
/// root before the current root (and its nested messages) completes.
///
/// A5 / R5 leaf-lock invariant: no runtime lock is EVER held while acquiring
/// another lock. Lock acquisition is strictly non-nested.
fn worker_loop(gx: std::sync::Arc<MossGlobalExec>, worker_id: usize) {
    let _abort_guard = MossAbortOnUnwind;
    MOSS_CURRENT_WORKER_ID.with(|c| c.set(Some(worker_id)));

    struct WorkerTlsGuard;
    impl Drop for WorkerTlsGuard {
        fn drop(&mut self) {
            MOSS_CURRENT_WORKER_ID.with(|c| c.set(None));
        }
    }
    let _tls_guard = WorkerTlsGuard;

    loop {
        enum Task {
            Root(MossRootDescriptor),
            Branch(std::sync::Arc<MossBranchScope>, u64, MossWork),
        }

        let mut task = None;

        // Phase 1: Check root queue under gx.inner leaf lock
        {
            let mut inner = gx.inner.lock().unwrap_or_else(|e| e.into_inner());
            if inner.shutdown && inner.root_queue.is_empty() {
                inner.active_workers = inner.active_workers.saturating_sub(1);
                #[cfg(any(test, moss_perf))]
                moss_rt_ev(MossRtEvent::WorkerParked, 0, 0, worker_id);
                return;
            }

            let is_excess = inner.runnable_workers() > inner.target_threads && inner.solo_blocked_workers.is_empty();
            if is_excess && inner.root_queue.is_empty() {
                inner.active_workers = inner.active_workers.saturating_sub(1);
                #[cfg(any(test, moss_perf))]
                moss_rt_ev(MossRtEvent::WorkerParked, 0, 0, worker_id);
                return;
            }

            if let Some(desc) = inner.root_queue.pop_front() {
                inner.running_roots += 1;
                gx.queue_not_full_cv.notify_all();
                task = Some(Task::Root(desc));
            }
        }

        // Phase 2: If no root found, check branch work without holding gx.inner
        if task.is_none() {
            let branch_seq_before = gx.branch_seq.load(std::sync::atomic::Ordering::SeqCst);
            let scope_opt = {
                let mut scopes = gx.branch_scopes.lock().unwrap_or_else(|e| e.into_inner());
                scopes.pop_front()
            };

            if let Some(scope) = scope_opt {
                let (has_more, item) = {
                    let mut pending = scope.pending.lock().unwrap_or_else(|e| e.into_inner());
                    let item = pending.pop_front();
                    let has_more = !pending.is_empty();
                    (has_more, item)
                };

                if has_more {
                    let mut scopes = gx.branch_scopes.lock().unwrap_or_else(|e| e.into_inner());
                    scopes.push_back(std::sync::Arc::clone(&scope));
                }

                if let Some((bid, bfn)) = item {
                    task = Some(Task::Branch(scope, bid, bfn));
                }
            }

            // If still no task found, wait under gx.inner unless new work arrived
            if task.is_none() {
                let mut inner = gx.inner.lock().unwrap_or_else(|e| e.into_inner());
                if inner.shutdown && inner.root_queue.is_empty() {
                    inner.active_workers = inner.active_workers.saturating_sub(1);
                    #[cfg(any(test, moss_perf))]
                    moss_rt_ev(MossRtEvent::WorkerParked, 0, 0, worker_id);
                    return;
                }

                let is_excess = inner.runnable_workers() > inner.target_threads && inner.solo_blocked_workers.is_empty();
                if is_excess && inner.root_queue.is_empty() {
                    inner.active_workers = inner.active_workers.saturating_sub(1);
                    #[cfg(any(test, moss_perf))]
                    moss_rt_ev(MossRtEvent::WorkerParked, 0, 0, worker_id);
                    return;
                }

                // If no root arrived and branch_seq didn't change, sleep on condvar
                if inner.root_queue.is_empty() && gx.branch_seq.load(std::sync::atomic::Ordering::SeqCst) == branch_seq_before {
                    drop(gx.work_cv.wait(inner).unwrap_or_else(|e| e.into_inner()));
                }
                continue;
            }
        }

        match task.unwrap() {
            Task::Root(desc) => {
                let root_id = desc.root_id;
                #[cfg(any(test, moss_perf))]
                moss_rt_ev(MossRtEvent::RootStarted, root_id, 0, worker_id);

                (desc.work)();

                #[cfg(any(test, moss_perf))]
                moss_rt_ev(MossRtEvent::RootCompleted, root_id, 0, worker_id);

                let mut inner = gx.inner.lock().unwrap_or_else(|e| e.into_inner());
                inner.running_roots = inner.running_roots.saturating_sub(1);
                if inner.running_roots == 0 && inner.root_queue.is_empty() {
                    gx.drain_cv.notify_all();
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

// ─── Shared Root Admission Primitive (R5 FIFO Fairness) ─────────────────
enum MossAdmissionResult {
    Admitted,
    DrainingBeforeAdmission(MossRootDescriptor),
}

fn admit_root(gx: &std::sync::Arc<MossGlobalExec>, desc: MossRootDescriptor) -> MossAdmissionResult {
    let mut inner = gx.inner.lock().unwrap_or_else(|e| e.into_inner());
    let my_ticket = inner.next_admission_ticket;
    inner.next_admission_ticket += 1;

    loop {
        if inner.draining {
            if my_ticket == inner.serving_admission_ticket {
                inner.serving_admission_ticket += 1;
                gx.queue_not_full_cv.notify_all();
            }
            return MossAdmissionResult::DrainingBeforeAdmission(desc);
        }
        if my_ticket == inner.serving_admission_ticket && inner.root_queue.len() < inner.queue_capacity {
            #[cfg(any(test, moss_perf))]
            moss_rt_ev(MossRtEvent::RootEnqueued, desc.root_id, 0, usize::MAX);
            #[cfg(any(test, moss_perf))]
            moss_rt_ev(MossRtEvent::AdmissionQueued, desc.root_id, 0, usize::MAX);
            inner.root_queue.push_back(desc);
            inner.serving_admission_ticket += 1;
            gx.work_cv.notify_one();
            gx.queue_not_full_cv.notify_all();
            return MossAdmissionResult::Admitted;
        }
        inner = gx.queue_not_full_cv.wait(inner).unwrap_or_else(|e| e.into_inner());
    }
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
    pub fn threads(mut self, n: usize) -> Self {
        if n == 0 { panic!("moss executor: threads count must be > 0"); }
        self.target_threads = n;
        self
    }
    /// Set finite cap on physical workers including compensation (§3.2 / E7).
    pub fn max_threads(mut self, n: usize) -> Self {
        if n == 0 { panic!("moss executor: max_threads must be > 0"); }
        self.max_threads = n;
        self
    }
    /// Set root queue capacity (§3.3).
    pub fn queue_capacity(mut self, n: usize) -> Self {
        if n == 0 { panic!("moss executor: queue_capacity must be > 0"); }
        self.queue_capacity = n;
        self
    }
    /// Best-effort affinity hint (§3.4) — warn and continue if OS refuses.
    pub fn affinity(mut self, cores: std::vec::Vec<usize>) -> Self { self.affinity_cores = Some(cores); self }
    /// Best-effort priority hint (§3.4) — warn and continue if OS refuses.
    pub fn priority(mut self, level: i32) -> Self { self.priority_level = Some(level); self }

    /// INLINE → ACTIVE transition (§5.2 / §5.3 / E6 / R15).
    /// Closes the inline gate before waiting so no inline root overlaps ACTIVE.
    /// Exclusive activation reservation guarantees concurrent start() fails deterministically.
    pub fn start(self) -> MossExecutorHandle {
        let rt = process_rt();
        let target = self.target_threads;
        let max = self.max_threads;
        if max < target {
            panic!("moss executor: max_threads ({}) cannot be less than threads ({})", max, target);
        }
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
                solo_blocked_workers: std::collections::HashSet::new(),
                next_admission_ticket: 0,
                serving_admission_ticket: 0,
            }),
            work_cv: std::sync::Condvar::new(),
            queue_not_full_cv: std::sync::Condvar::new(),
            drain_cv: std::sync::Condvar::new(),
            branch_scopes: std::sync::Mutex::new(std::collections::VecDeque::new()),
            branch_seq: std::sync::atomic::AtomicU64::new(0),
            worker_threads: std::sync::Mutex::new(std::vec::Vec::default()),
            affinity_cores: self.affinity_cores,
            priority_level: self.priority_level,
        });

        {
            let mut inner = rt.inner.lock().unwrap_or_else(|e| e.into_inner());
            if inner.state != MossIngressState::Inline || inner.start_pending {
                drop(inner);
                panic!("moss executor: at most one active executor allowed (E6/R15)");
            }
            // Exclusive activation reservation: close gate to future INLINE and start callers
            inner.start_pending = true;
            while inner.inline_running {
                inner = rt.ingress_cv.wait(inner).unwrap_or_else(|e| e.into_inner());
                // Revalidate state after condvar wake
                if inner.state != MossIngressState::Inline || !inner.start_pending {
                    panic!("moss executor: invalid state during start()");
                }
            }

            inner.state = MossIngressState::Active;
            inner.start_pending = false;
            inner.active_executor = Some(std::sync::Arc::clone(&gx));
            // Wake waiting callers so they re-evaluate and use ACTIVE executor admission
            rt.ingress_cv.notify_all();
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
/// Returned by start(); valid until join(). Linear capability (never Clone).
pub struct MossExecutorHandle {
    gx: std::sync::Arc<MossGlobalExec>,
}

impl MossExecutorHandle {
    /// Enqueue a one-way root (executor.invoke, §4 / E9 / E10).
    /// Waits for queue capacity; caller holds no Moss lock as part of ingress.
    /// Workers executing another root cannot enqueue new roots (E11).
    pub fn enqueue_root(&self, desc: MossRootDescriptor) {
        if current_worker_id().is_some() {
            panic!("moss executor: worker threads cannot enqueue new roots (E11)");
        }
        match admit_root(&self.gx, desc) {
            MossAdmissionResult::Admitted => {},
            MossAdmissionResult::DrainingBeforeAdmission(_) => {
                panic!("moss executor: enqueue_root called outside ACTIVE state");
            }
        }
    }

    /// ACTIVE → DRAINING → INLINE (§5.4 / join()).
    pub fn join(self) {
        let rt = process_rt();

        // Step 1: Transition ACTIVE → DRAINING under rt.inner leaf lock
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

        // Step 3: Wait for every physical worker to terminate (no locks held)
        let handles: std::vec::Vec<_> = {
            let mut guard = self.gx.worker_threads.lock().unwrap_or_else(|e| e.into_inner());
            guard.drain(..).collect()
        };
        for h in handles {
            let _ = h.join();
        }

        // Step 4: Transition DRAINING → INLINE under rt.inner leaf lock
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

// ─── Agent D Seam Bridge (ROOT_RUNTIME_ABI §2 / §3) ──────────────────────
#[inline]
pub fn moss_root_start() -> MossExecutorHandle {
    MossExecutor::new().start()
}

#[inline]
pub fn moss_root_submit(executor: &MossExecutorHandle, work: impl FnOnce() + Send + 'static) {
    let desc = MossRootDescriptor::one_way(next_root_id(), work);
    executor.enqueue_root(desc);
}

#[inline]
pub fn moss_root_join(executor: MossExecutorHandle) {
    executor.join();
}

// ─── runtime_invoke ───────────────────────────────────────────────────────
/// Synchronous host → Moss ingress (ROOT_RUNTIME_ABI §6).
///
/// - ACTIVE: enqueues through executor admission; caller blocks until done.
/// - INLINE: acquires fair ticket admission, runs on caller thread, returns.
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

    let mut inner = rt.inner.lock().unwrap_or_else(|e| e.into_inner());
    let my_ticket = inner.next_inline_ticket;
    inner.next_inline_ticket += 1;

    loop {
        match inner.state {
            MossIngressState::Inline => {
                if !inner.start_pending && my_ticket == inner.serving_inline_ticket && !inner.inline_running {
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
                    inner.serving_inline_ticket += 1;
                    rt.ingress_cv.notify_all();
                    return result;
                }
                inner = rt.ingress_cv.wait(inner).unwrap_or_else(|e| e.into_inner());
            }
            MossIngressState::Active => {
                // Must retire inline tickets in contiguous FIFO order
                if my_ticket != inner.serving_inline_ticket {
                    inner = rt.ingress_cv.wait(inner).unwrap_or_else(|e| e.into_inner());
                    continue;
                }
                inner.serving_inline_ticket += 1;
                rt.ingress_cv.notify_all();

                let gx = std::sync::Arc::clone(inner.active_executor.as_ref().expect("active executor missing in ACTIVE state"));
                drop(inner);

                let reply_slot: std::sync::Arc<(std::sync::Mutex<std::option::Option<R>>, std::sync::Condvar)> =
                    std::sync::Arc::new((std::sync::Mutex::new(None), std::sync::Condvar::new()));
                let reply_slot_clone = std::sync::Arc::clone(&reply_slot);

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

                match admit_root(&gx, desc) {
                    MossAdmissionResult::Admitted => {
                        let (lock, cvar) = &*reply_slot;
                        let mut guard = lock.lock().unwrap_or_else(|e| e.into_inner());
                        while guard.is_none() {
                            guard = cvar.wait(guard).unwrap_or_else(|e| e.into_inner());
                        }
                        return guard.take().unwrap();
                    }
                    MossAdmissionResult::DrainingBeforeAdmission(unadmitted_desc) => {
                        // Fallback: wait for INLINE restoration, then run unadmitted descriptor with fair fallback ticket
                        let mut inner = rt.inner.lock().unwrap_or_else(|e| e.into_inner());
                        let fallback_ticket = inner.next_inline_ticket;
                        inner.next_inline_ticket += 1;
                        loop {
                            if inner.state == MossIngressState::Inline && !inner.start_pending && fallback_ticket == inner.serving_inline_ticket && !inner.inline_running {
                                inner.inline_running = true;
                                drop(inner);

                                #[cfg(any(test, moss_perf))]
                                moss_rt_ev(MossRtEvent::RootStarted, root_id, 0, usize::MAX);

                                (unadmitted_desc.work)();

                                #[cfg(any(test, moss_perf))]
                                moss_rt_ev(MossRtEvent::RootCompleted, root_id, 0, usize::MAX);

                                let mut inner = rt.inner.lock().unwrap_or_else(|e| e.into_inner());
                                inner.inline_running = false;
                                inner.serving_inline_ticket += 1;
                                rt.ingress_cv.notify_all();

                                let (lock, _cvar) = &*reply_slot;
                                let mut guard = lock.lock().unwrap_or_else(|e| e.into_inner());
                                return guard.take().expect("reply missing after synchronous fallback execution");
                            }
                            inner = rt.ingress_cv.wait(inner).unwrap_or_else(|e| e.into_inner());
                        }
                    }
                }
            }
            MossIngressState::Draining => {
                inner = rt.ingress_cv.wait(inner).unwrap_or_else(|e| e.into_inner());
            }
        }
    }
}

// ─── branch_publish ───────────────────────────────────────────────────────
/// Publish a branch to the scope's pending queue (E4 / §7.3).
/// If the queue is saturated, execute inline in the owner root.
/// Publication never blocks (E4).
/// A5 / R5 leaf-lock compliant: no locks are nested.
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
            let mut scopes = gx.branch_scopes.lock().unwrap_or_else(|e| e.into_inner());
            scopes.push_back(std::sync::Arc::clone(scope));
            drop(scopes);
            gx.branch_seq.fetch_add(1, std::sync::atomic::Ordering::SeqCst);
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
            // No unstarted branches in this scope; event-driven wait on condvar
            let mut done = scope.done_mutex.lock().unwrap_or_else(|e| e.into_inner());
            while scope.outstanding.load(std::sync::atomic::Ordering::SeqCst) != 0 && !*done {
                done = scope.done_cond.wait(done).unwrap_or_else(|e| e.into_inner());
            }
            if scope.outstanding.load(std::sync::atomic::Ordering::SeqCst) == 0 { break 'join_loop; }
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
/// Synchronized entirely by gx.inner (no nested mutex locks).
fn solo_enter_worker(worker_id: usize, _reason: &str) {
    #[cfg(any(test, moss_perf))]
    moss_rt_ev(MossRtEvent::SoloEnter, 0, 0, worker_id);

    let rt = process_rt();
    let gx_opt = {
        let inner = rt.inner.lock().unwrap_or_else(|e| e.into_inner());
        inner.active_executor.clone()
    };
    if let Some(gx) = gx_opt {
        let mut inner = gx.inner.lock().unwrap_or_else(|e| e.into_inner());
        if inner.solo_blocked_workers.insert(worker_id) {
            let runnable = inner.runnable_workers();
            let should_comp = runnable < inner.target_threads
                && inner.active_workers < inner.max_threads;
            let new_wid = if should_comp {
                inner.active_workers += 1;
                let wid = inner.next_worker_id;
                inner.next_worker_id += 1;
                Some(wid)
            } else {
                None
            };
            drop(inner);
            if let Some(wid) = new_wid {
                spawn_one_worker(&gx, wid);
            }
        }
    }
}

/// Mark worker as leaving a Solo kernel wait (§15 / R2 / E8).
/// The waking worker resumes immediately; never waits for a slot.
/// Excess compensation workers park on their next idle cycle.
fn solo_leave_worker(worker_id: usize, _reason: &str) {
    #[cfg(any(test, moss_perf))]
    moss_rt_ev(MossRtEvent::SoloLeave, 0, 0, worker_id);

    let rt = process_rt();
    let gx_opt = {
        let inner = rt.inner.lock().unwrap_or_else(|e| e.into_inner());
        inner.active_executor.clone()
    };
    if let Some(gx) = gx_opt {
        let mut inner = gx.inner.lock().unwrap_or_else(|e| e.into_inner());
        inner.solo_blocked_workers.remove(&worker_id);
        drop(inner);
        gx.work_cv.notify_one();
    }
}

pub fn solo_enter(worker_id: usize) {
    solo_enter_worker(worker_id, "explicit");
}

pub fn solo_leave(worker_id: usize) {
    solo_leave_worker(worker_id, "explicit");
}

pub fn solo_enter_current(reason: &str) {
    if let Some(wid) = current_worker_id() {
        solo_enter_worker(wid, reason);
    }
}

pub fn solo_leave_current(reason: &str) {
    if let Some(wid) = current_worker_id() {
        solo_leave_worker(wid, reason);
    }
}

pub fn executor_solo_enter_from_fileio(reason: &str) {
    solo_enter_current(reason);
}

pub fn executor_solo_leave_from_fileio(reason: &str) {
    solo_leave_current(reason);
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
    inner.active_executor.as_ref().map(|gx| gx.inner.lock().unwrap_or_else(|e| e.into_inner()).solo_blocked_workers.len()).unwrap_or(0)
}
)EXECUTOR_RUST";
  } else {
    // Provider crate (.rlib) implementation: opaque C / Rust ABI linkage with zero type punning
    s += R"EXECUTOR_RUST(
extern "C" {
    fn __moss_next_root_id() -> u64;
    fn __moss_next_branch_id() -> u64;
    fn __moss_branch_scope_new(owner_root_id: u64) -> *mut ();
    fn __moss_branch_scope_clone(scope_ptr: *mut ()) -> *mut ();
    fn __moss_branch_scope_drop(scope_ptr: *mut ());
    fn __moss_branch_publish_trampoline(
        scope_ptr: *mut (),
        data: *mut (),
        invoker: unsafe extern "C" fn(*mut ()),
    );
    fn __moss_branch_join(scope_ptr: *mut ());
}

extern "Rust" {
    pub fn moss_solo_enter(reason: &str);
    pub fn moss_solo_leave(reason: &str);
}

pub fn next_root_id() -> u64 {
    // SAFETY: Process runtime root crate exports __moss_next_root_id.
    unsafe { __moss_next_root_id() }
}

pub fn next_branch_id() -> u64 {
    // SAFETY: Process runtime root crate exports __moss_next_branch_id.
    unsafe { __moss_next_branch_id() }
}

pub fn solo_enter_current(reason: &str) {
    // SAFETY: Process runtime root crate exports moss_solo_enter.
    unsafe { moss_solo_enter(reason); }
}

pub fn solo_leave_current(reason: &str) {
    // SAFETY: Process runtime root crate exports moss_solo_leave.
    unsafe { moss_solo_leave(reason); }
}

pub fn executor_solo_enter_from_fileio(reason: &str) {
    // SAFETY: Process runtime root crate exports moss_solo_enter.
    unsafe { moss_solo_enter(reason); }
}

pub fn executor_solo_leave_from_fileio(reason: &str) {
    // SAFETY: Process runtime root crate exports moss_solo_leave.
    unsafe { moss_solo_leave(reason); }
}

pub struct MossBranchScope {
    raw: *mut (),
}
// SAFETY: Branch scope pointer is managed across threads by the process runtime.
unsafe impl Send for MossBranchScope {}
unsafe impl Sync for MossBranchScope {}

impl Clone for MossBranchScope {
    fn clone(&self) -> Self {
        // SAFETY: Cloning the reference count in the owning root runtime.
        let raw = unsafe { __moss_branch_scope_clone(self.raw) };
        Self { raw }
    }
}

impl Drop for MossBranchScope {
    fn drop(&mut self) {
        // SAFETY: Dropping scope handle in the owning root runtime.
        unsafe { __moss_branch_scope_drop(self.raw) };
    }
}

pub fn branch_scope_new(owner_root_id: u64) -> MossBranchScope {
    // SAFETY: Creating new branch scope in the owning root runtime.
    let raw = unsafe { __moss_branch_scope_new(owner_root_id) };
    MossBranchScope { raw }
}

unsafe extern "C" fn __moss_invoke_foreign_branch(ptr: *mut ()) {
    let boxed: std::boxed::Box<std::boxed::Box<dyn FnOnce() + Send + 'static>> = unsafe {
        std::boxed::Box::from_raw(ptr as *mut _)
    };
    (*boxed)();
}

pub fn branch_publish<F>(scope: &MossBranchScope, branch_fn: F)
where
    F: FnOnce() + Send + 'static,
{
    let boxed: std::boxed::Box<std::boxed::Box<dyn FnOnce() + Send + 'static>> =
        std::boxed::Box::new(std::boxed::Box::new(branch_fn));
    let raw_data = std::boxed::Box::into_raw(boxed) as *mut ();
    // SAFETY: Passing heap-allocated thunk to process runtime trampoline.
    unsafe {
        __moss_branch_publish_trampoline(scope.raw, raw_data, __moss_invoke_foreign_branch);
    }
}

pub fn branch_join(scope: MossBranchScope) {
    // SAFETY: Joining branch scope in the owning root runtime.
    unsafe { __moss_branch_join(scope.raw) };
}
)EXECUTOR_RUST";
  }

  return s;
}

} // namespace moss
