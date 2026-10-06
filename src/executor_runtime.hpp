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
//   - MossExecutorConfig / MossExecutor: immutable post-start config (§3)
//   - MossExecutorHandle: linear capability, enqueues roots, join() drains (§4, §5)
//   - runtime_invoke: synchronous host ingress with reply return (§6)
//   - Current-Root and current-worker identity are runtime-owned TLS
//   - branch_scope_new_current / branch_publish / branch_join: one-shot,
//     Root-owned, nonblocking branch API (§7.2 / §7.3)
//   - Solo compensation callbacks for the FileIO hook table (§15).  The
//     process-wide moss_solo_enter / moss_solo_leave symbols are owned by
//     the FileIO root runtime, which forwards to
//     executor_solo_enter_from_fileio / executor_solo_leave_from_fileio.
//   - A5 / R5 leaf-lock invariant: runtime locks are never nested
//   - R4 fairness: ticket-based FIFO ingress and root queue admission
//   - Agent D seam: moss_root_start_with_config / moss_root_submit /
//     moss_root_join
//   - Test instrumentation under #[cfg(any(test, moss_perf))]
//
// Crate roles: exactly one generated crate per executable (the crate built
// as the executable) owns the process runtime and exports the opaque
// cross-crate Branch ABI.  Provider crates emit only an opaque
// MossBranchScope wrapper over that ABI and never own a scheduler.  See
// ExecutorRuntimeRole.
//
// src/moss.cpp includes this and emits it alongside handler_runtime_rust().
// See docs/ROOT_RUNTIME_ABI.md and docs/MOSS_PHASE_20_FILE_IO_AND_EXECUTORS.md.

#include <string>

namespace moss {

enum class ExecutorRuntimeRole {
  // The crate is always its program's executable: an implicit-module
  // program, or the explicit module that declares `main`.
  ProcessRoot,
  // The crate is only ever a library of an executable whose `main` lives in
  // another crate.
  Provider,
  // A unit with no `main`: the same crate source is built as a provider
  // .rlib and as the unit's (stub or test) executable, so the role is chosen
  // by `--cfg moss_process_root`, which only the executable build passes.
  SelectedByExecutableBuild,
};

// Shared by every generated crate, including source-free providers. The
// runtime block delimiter keeps this implementation out of user lowering.
inline const char* fair_leaf_runtime_items() {
  return R"EXECUTOR_RUST(
// FIFO leaf lock. Each entrant atomically swaps its node into the tail and
// waits only on its predecessor. No entrant can bypass an enqueued waiter.
// park/unpark handles spurious wakeups and does not spin. The current holder
// never parks, joins, or takes another runtime lock while holding this lock.
struct MossFairNode {
    released: std::sync::atomic::AtomicBool,
    waiter: std::sync::atomic::AtomicPtr<std::thread::Thread>,
}

#[cfg(any(test, moss_perf))]
static MOSS_FAIR_ENQUEUE_HOOK: std::sync::OnceLock<fn(usize)> = std::sync::OnceLock::new();
#[cfg(any(test, moss_perf))]
pub fn moss_fair_set_enqueue_hook(hook: fn(usize)) {
    let _ = MOSS_FAIR_ENQUEUE_HOOK.set(hook);
}

pub struct MossFairMutex<T> {
    tail: std::sync::atomic::AtomicPtr<MossFairNode>,
    value: std::cell::UnsafeCell<T>,
    #[cfg(any(test, moss_perf))]
    parked: std::sync::atomic::AtomicUsize,
}

// SAFETY: the FIFO turn grants one mutable access at a time. T must be Send
// because a turn can be acquired on a different thread.
unsafe impl<T: Send> Send for MossFairMutex<T> {}
unsafe impl<T: Send> Sync for MossFairMutex<T> {}

pub struct MossFairGuard<'a, T> {
    lock: &'a MossFairMutex<T>,
    node: std::sync::Arc<MossFairNode>,
}

impl<T> MossFairMutex<T> {
    pub const fn new(value: T) -> Self {
        Self {
            tail: std::sync::atomic::AtomicPtr::new(std::ptr::null_mut()),
            value: std::cell::UnsafeCell::new(value),
            #[cfg(any(test, moss_perf))]
            parked: std::sync::atomic::AtomicUsize::new(0),
        }
    }

    pub fn lock(&self) -> MossFairGuard<'_, T> {
        use std::sync::atomic::Ordering;
        let node = std::sync::Arc::new(MossFairNode {
            released: std::sync::atomic::AtomicBool::new(false),
            waiter: std::sync::atomic::AtomicPtr::new(std::ptr::null_mut()),
        });
        let raw = std::sync::Arc::into_raw(std::sync::Arc::clone(&node)) as *mut MossFairNode;
        let previous = self.tail.swap(raw, Ordering::AcqRel);
        if !previous.is_null() {
            // SAFETY: swapping the tail transferred its owned Arc to us.
            let predecessor = unsafe { std::sync::Arc::from_raw(previous) };
            let waiter = std::sync::Arc::new(std::thread::current());
            let waiter_raw = std::sync::Arc::into_raw(waiter) as *mut std::thread::Thread;
            assert!(predecessor.waiter.compare_exchange(
                std::ptr::null_mut(), waiter_raw, Ordering::AcqRel, Ordering::Acquire).is_ok());
            #[cfg(any(test, moss_perf))]
            self.parked.fetch_add(1, Ordering::SeqCst);
            #[cfg(any(test, moss_perf))]
            if let Some(hook) = MOSS_FAIR_ENQUEUE_HOOK.get() { hook(self as *const Self as usize); }
            while !predecessor.released.load(Ordering::Acquire) {
                std::thread::park();
            }
            let remaining = predecessor.waiter.swap(std::ptr::null_mut(), Ordering::AcqRel);
            if !remaining.is_null() {
                // SAFETY: the successful swap transferred the waiter Arc.
                drop(unsafe { std::sync::Arc::from_raw(remaining) });
            }
            #[cfg(any(test, moss_perf))]
            self.parked.fetch_sub(1, Ordering::SeqCst);
        }
        MossFairGuard { lock: self, node }
    }

    #[cfg(any(test, moss_perf))]
    pub fn parked_waiters(&self) -> usize {
        self.parked.load(std::sync::atomic::Ordering::SeqCst)
    }
}

impl<T> std::ops::Deref for MossFairGuard<'_, T> {
    type Target = T;
    fn deref(&self) -> &T { unsafe { &*self.lock.value.get() } }
}
impl<T> std::ops::DerefMut for MossFairGuard<'_, T> {
    fn deref_mut(&mut self) -> &mut T { unsafe { &mut *self.lock.value.get() } }
}
impl<T> Drop for MossFairGuard<'_, T> {
    fn drop(&mut self) {
        use std::sync::atomic::Ordering;
        self.node.released.store(true, Ordering::Release);
        let waiter = self.node.waiter.swap(std::ptr::null_mut(), Ordering::AcqRel);
        if !waiter.is_null() {
            // SAFETY: the successful swap transferred the waiter Arc.
            unsafe { std::sync::Arc::from_raw(waiter) }.unpark();
        }
        let raw = std::sync::Arc::as_ptr(&self.node) as *mut MossFairNode;
        if self.lock.tail.compare_exchange(raw, std::ptr::null_mut(),
                Ordering::AcqRel, Ordering::Acquire).is_ok() {
            // SAFETY: the successful CAS removed the tail-owned Arc.
            drop(unsafe { std::sync::Arc::from_raw(raw) });
        }
    }
}

struct MossFairWaiter {
    thread: std::thread::Thread,
    notified: std::sync::atomic::AtomicBool,
}

pub struct MossFairCondvar {
    owner: std::sync::atomic::AtomicPtr<()>,
    waiters: std::cell::UnsafeCell<std::collections::VecDeque<std::sync::Arc<MossFairWaiter>>>,
}
// SAFETY: every access to waiters requires a guard of the one bound fair lock.
unsafe impl Send for MossFairCondvar {}
unsafe impl Sync for MossFairCondvar {}

impl MossFairCondvar {
    pub const fn new() -> Self {
        Self { owner: std::sync::atomic::AtomicPtr::new(std::ptr::null_mut()),
               waiters: std::cell::UnsafeCell::new(std::collections::VecDeque::new()) }
    }
    fn check<T>(&self, guard: &MossFairGuard<'_, T>) {
        use std::sync::atomic::Ordering;
        let lock = guard.lock as *const MossFairMutex<T> as *mut ();
        let owner = self.owner.load(Ordering::Acquire);
        if owner.is_null() {
            let _ = self.owner.compare_exchange(std::ptr::null_mut(), lock,
                                                Ordering::AcqRel, Ordering::Acquire);
        }
        assert_eq!(self.owner.load(Ordering::Acquire), lock,
                   "fair condition used with a different state lock");
    }
    pub fn wait<'a, T>(&self, guard: MossFairGuard<'a, T>) -> MossFairGuard<'a, T> {
        self.check(&guard);
        let waiter = std::sync::Arc::new(MossFairWaiter {
            thread: std::thread::current(),
            notified: std::sync::atomic::AtomicBool::new(false),
        });
        // SAFETY: the associated fair guard exclusively protects this queue.
        unsafe { &mut *self.waiters.get() }.push_back(std::sync::Arc::clone(&waiter));
        let lock = guard.lock;
        drop(guard);
        while !waiter.notified.load(std::sync::atomic::Ordering::Acquire) {
            std::thread::park();
        }
        lock.lock()
    }
    pub fn notify_one<T>(&self, guard: &MossFairGuard<'_, T>) {
        self.check(guard);
        // SAFETY: the associated fair guard exclusively protects this queue.
        if let Some(waiter) = unsafe { &mut *self.waiters.get() }.pop_front() {
            waiter.notified.store(true, std::sync::atomic::Ordering::Release);
            waiter.thread.unpark();
        }
    }
    pub fn notify_all<T>(&self, guard: &MossFairGuard<'_, T>) {
        self.check(guard);
        // SAFETY: the associated fair guard exclusively protects this queue.
        for waiter in unsafe { &mut *self.waiters.get() }.drain(..) {
            waiter.notified.store(true, std::sync::atomic::Ordering::Release);
            waiter.thread.unpark();
        }
    }
}
)EXECUTOR_RUST";
}

// Process-root runtime items (without the block delimiters).
inline const char* executor_runtime_root_items() {
  return R"EXECUTOR_RUST(
// ─── Lifecycle ───────────────────────────────────────────────────────────
/// Ingress lifecycle. §5.4 / ROOT_RUNTIME_ABI §"Exact ingress lifecycle".
#[derive(Clone, Copy, PartialEq, Eq, Debug)]
enum MossIngressState {
    /// No active executor; roots run one-at-a-time on the caller thread.
    Inline,
    /// Executor active; every new root enters through executor admission.
    Active,
    /// join() called; no new admission; admitted roots run to completion.
    Draining,
}

// ─── Test / perf instrumentation ────────────────────────────────────────
/// Event kinds for the test/perf hook.
#[cfg(any(test, moss_perf))]
#[derive(Clone, Copy, PartialEq, Eq, Debug)]
pub enum MossRtEvent {
    RootEnqueued,
    RootStarted,
    RootCompleted,
    BranchPublished,
    /// A free worker started a published Branch.
    BranchStarted,
    /// The owning Root ran one of its own unstarted Branches while joining.
    BranchHelped,
    /// The Branch ran inline at publication (no executor, or overflow).
    BranchInline,
    BranchCompleted,
    /// Branch publication bumped the work generation (before the wake).
    BranchWakeSignal,
    SoloEnter,
    SoloLeave,
    WorkerSpawned,
    WorkerExited,
    IngressStateChange,
    AdmissionQueued,
    StartPendingSet,
    InlineTicketIssued,
    AdmissionTicketIssued,
}

/// Install the test/perf event hook.
/// Signature: fn(event, root_id_or_ticket, branch_id, worker_id)
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

/// Test hook run by an idle worker after its Condvar predicate check and
/// before its Condvar wait, while it still holds the executor lock.  It
/// lets a test publish work in exactly that window.
#[cfg(any(test, moss_perf))]
static MOSS_RT_PREWAIT_HOOK: std::sync::OnceLock<fn(usize)> = std::sync::OnceLock::new();

#[cfg(any(test, moss_perf))]
pub fn moss_rt_set_prewait_hook(f: fn(usize)) {
    let _ = MOSS_RT_PREWAIT_HOOK.set(f);
}

// ─── Type-erased work container ──────────────────────────────────────────
pub type MossWork = std::boxed::Box<dyn FnOnce() + Send + 'static>;

// ─── RootDescriptor ──────────────────────────────────────────────────────
/// Internal submission record for one statically resolved handler invocation.
/// Not a first-class Moss value or dynamic dispatch target (ROOT_RUNTIME_ABI §3).
/// The compiler lowers the concrete domain, handler identity, and evaluated
/// argument snapshot into the typed `work` closure; the runtime only runs it.
pub struct MossRootDescriptor {
    /// Unique root identity; becomes the current-Root identity while it runs.
    pub root_id: u64,
    /// Optional compiler-owned semantic target identity, for instrumentation.
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

// ─── Executor configuration ──────────────────────────────────────────────
/// Fully resolved Executor configuration (§3).  The compiler lowers each
/// source-specified field and resolves omitted ones with `resolved`, so the
/// Agent D seam never needs scheduler internals.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct MossExecutorConfig {
    /// Target compute parallelism (§3.1).
    pub threads: usize,
    /// Finite physical worker cap T_max, including compensation (§3.2 / E7).
    pub max_threads: usize,
    /// Bound on roots waiting to start (§3.3).
    pub queue_capacity: usize,
    /// Best-effort CPU affinity hint (§3.4).
    pub affinity: std::option::Option<std::vec::Vec<usize>>,
    /// Best-effort scheduling priority hint (§3.4).
    pub priority: std::option::Option<i32>,
}

impl MossExecutorConfig {
    pub const DEFAULT_QUEUE_CAPACITY: usize = 1024;

    fn host_parallelism() -> usize {
        std::thread::available_parallelism().map(|n| n.get()).unwrap_or(4)
    }

    /// Resolve omitted source fields to runtime defaults.  The default
    /// T_max is finite and never below the target thread count.
    pub fn resolved(
        threads: std::option::Option<usize>,
        max_threads: std::option::Option<usize>,
        queue_capacity: std::option::Option<usize>,
        affinity: std::option::Option<std::vec::Vec<usize>>,
        priority: std::option::Option<i32>,
    ) -> Self {
        let threads = threads.unwrap_or_else(Self::host_parallelism);
        let max_threads = max_threads.unwrap_or_else(|| {
            std::cmp::max(threads, Self::host_parallelism().saturating_mul(4))
        });
        Self {
            threads,
            max_threads,
            queue_capacity: queue_capacity.unwrap_or(Self::DEFAULT_QUEUE_CAPACITY),
            affinity,
            priority,
        }
    }

    /// Fail closed on configurations that break E7 or admission (§3).
    fn validate(&self) {
        if self.threads == 0 { panic!("moss executor: threads count must be > 0"); }
        if self.max_threads == 0 { panic!("moss executor: max_threads must be > 0"); }
        if self.queue_capacity == 0 { panic!("moss executor: queue_capacity must be > 0"); }
        if self.max_threads < self.threads {
            panic!("moss executor: max_threads ({}) cannot be less than threads ({})",
                   self.max_threads, self.threads);
        }
    }
}

impl Default for MossExecutorConfig {
    fn default() -> Self { Self::resolved(None, None, None, None, None) }
}

// ─── Executor inner state ─────────────────────────────────────────────────
struct MossExecInner {
    root_queue: std::collections::VecDeque<MossRootDescriptor>,
    /// Workers in the scheduling loop (runnable or Solo-blocked).
    active_workers: usize,
    /// Physical worker threads not yet finished: a worker that has decided to
    /// exit still counts until its thread body returns, so a replacement
    /// compensation worker cannot overlap it beyond T_max (E7).
    live_threads: usize,
    running_roots: usize,
    draining: bool,
    shutdown: bool,
    next_worker_id: usize,
    /// Solo nesting depth per blocked worker.
    solo_blocked_workers: std::collections::HashMap<usize, usize>,
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
    config: MossExecutorConfig,
    inner: MossFairMutex<MossExecInner>,
    work_cv: MossFairCondvar,
    queue_not_full_cv: MossFairCondvar,
    drain_cv: MossFairCondvar,
    /// Ready Branch scopes; each scope appears at most once (its `queued` flag).
    branch_scopes: MossFairMutex<std::collections::VecDeque<std::sync::Arc<MossBranchScopeState>>>,
    /// Branch work generation, read by idle workers before they sleep.
    branch_seq: std::sync::atomic::AtomicU64,
    worker_threads: MossFairMutex<std::vec::Vec<std::thread::JoinHandle<()>>>,
}

impl MossGlobalExec {
    fn lock_inner(&self) -> MossFairGuard<'_, MossExecInner> {
        self.inner.lock()
    }

    /// Wake one idle worker for newly published Branch work.  Acquiring and
    /// releasing `inner` (not nested with any other runtime lock) orders the
    /// notification after any worker that already passed its predicate check
    /// has entered its Condvar wait, so the wakeup cannot be lost.
    fn signal_branch_work(&self) {
        self.branch_seq.fetch_add(1, std::sync::atomic::Ordering::SeqCst);
        #[cfg(any(test, moss_perf))]
        moss_rt_ev(MossRtEvent::BranchWakeSignal, 0, 0, usize::MAX);
        let inner = self.lock_inner();
        self.work_cv.notify_one(&inner);
    }

    /// Pop one unstarted Branch from the ready-scope queue.  Lock order is
    /// strictly sequential: branch_scopes, then the scope's pending lock,
    /// then branch_scopes again; no two are ever held together.
    fn take_ready_branch(&self) -> std::option::Option<(std::sync::Arc<MossBranchScopeState>, u64, MossWork)> {
        loop {
            let scope = self.branch_scopes.lock().pop_front()?;
            let (item, requeue) = {
                let mut pending = scope.lock_pending();
                let item = pending.items.pop_front();
                let requeue = !pending.items.is_empty();
                pending.queued = requeue;
                (item, requeue)
            };
            if requeue {
                self.push_ready_scope(&scope);
            }
            if let Some((branch_id, work)) = item {
                return Some((scope, branch_id, work));
            }
            // The owner already helped every pending Branch of this scope.
        }
    }

    /// Push a scope's single ready-queue entry, whose `queued` flag the caller
    /// set under the scope's pending lock.  The owner may have joined while
    /// the entry was in transit; its join then cleared `queued` but could not
    /// find the entry, so the pusher removes it.
    fn push_ready_scope(&self, scope: &std::sync::Arc<MossBranchScopeState>) {
        self.branch_scopes.lock().push_back(std::sync::Arc::clone(scope));
        let stale = {
            let pending = scope.lock_pending();
            pending.joined && !pending.queued
        };
        if stale { self.remove_ready_scope(scope); }
    }

    /// Remove one ready-queue entry of a joined scope.  After join no
    /// publication can enqueue the scope again, so any entry is stale.
    fn remove_ready_scope(&self, scope: &std::sync::Arc<MossBranchScopeState>) {
        let removed = {
            let mut ready = self.branch_scopes.lock();
            ready.iter().position(|s| std::sync::Arc::ptr_eq(s, scope)).and_then(|index| ready.remove(index))
        };
        // Release the entry's reference after the queue lock.
        drop(removed);
    }
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
    inner: MossFairMutex<MossProcessRuntimeInner>,
    ingress_cv: MossFairCondvar,
    next_root_id: std::sync::atomic::AtomicU64,
    next_branch_id: std::sync::atomic::AtomicU64,
}

impl MossProcessRuntime {
    fn new() -> Self {
        Self {
            inner: MossFairMutex::new(MossProcessRuntimeInner {
                state: MossIngressState::Inline,
                start_pending: false,
                inline_running: false,
                active_executor: None,
                next_inline_ticket: 0,
                serving_inline_ticket: 0,
            }),
            ingress_cv: MossFairCondvar::new(),
            next_root_id: std::sync::atomic::AtomicU64::new(1),
            next_branch_id: std::sync::atomic::AtomicU64::new(1),
        }
    }

    fn lock_inner(&self) -> MossFairGuard<'_, MossProcessRuntimeInner> {
        self.inner.lock()
    }

    fn active_executor(&self) -> std::option::Option<std::sync::Arc<MossGlobalExec>> {
        self.lock_inner().active_executor.clone()
    }
}

static RUNTIME: std::sync::OnceLock<MossProcessRuntime> = std::sync::OnceLock::new();

fn process_rt() -> &'static MossProcessRuntime {
    RUNTIME.get_or_init(MossProcessRuntime::new)
}

// ─── ID counters ─────────────────────────────────────────────────────────
/// Allocate a Root identity.  Only root ingress (in the process-root crate)
/// allocates Root identities; providers never do.
pub fn next_root_id() -> u64 {
    process_rt().next_root_id.fetch_add(1, std::sync::atomic::Ordering::Relaxed)
}

fn next_branch_id() -> u64 {
    process_rt().next_branch_id.fetch_add(1, std::sync::atomic::Ordering::Relaxed)
}

// ─── Runtime-owned execution context (TLS) ───────────────────────────────
thread_local! {
    static MOSS_CURRENT_WORKER_ID: std::cell::Cell<std::option::Option<usize>> = const { std::cell::Cell::new(None) };
    static MOSS_CURRENT_WORKER_EXEC: std::cell::RefCell<std::option::Option<std::sync::Arc<MossGlobalExec>>> = const { std::cell::RefCell::new(None) };
    static MOSS_CURRENT_ROOT_ID: std::cell::Cell<std::option::Option<u64>> = const { std::cell::Cell::new(None) };
}

/// Executor worker identity of this thread, or None for a non-worker thread.
/// FileIO never manufactures a worker identity; it asks the runtime.
pub fn current_worker_id() -> std::option::Option<usize> {
    MOSS_CURRENT_WORKER_ID.with(|c| c.get())
}

/// Identity of the Root this thread is currently executing (including a
/// Branch, which executes on behalf of its scope's owning Root).
pub fn current_root_id() -> std::option::Option<u64> {
    MOSS_CURRENT_ROOT_ID.with(|c| c.get())
}

/// RAII: install a worker identity for the lifetime of a worker thread.
struct MossWorkerContextGuard;

impl MossWorkerContextGuard {
    fn enter(worker_id: usize, gx: std::sync::Arc<MossGlobalExec>) -> Self {
        MOSS_CURRENT_WORKER_ID.with(|c| c.set(Some(worker_id)));
        MOSS_CURRENT_WORKER_EXEC.with(|c| *c.borrow_mut() = Some(gx));
        MossWorkerContextGuard
    }
}

impl Drop for MossWorkerContextGuard {
    fn drop(&mut self) {
        MOSS_CURRENT_WORKER_ID.with(|c| c.set(None));
        MOSS_CURRENT_WORKER_EXEC.with(|c| *c.borrow_mut() = None);
    }
}

/// RAII: set the current-Root identity and restore the previous one.
struct MossRootContextGuard {
    previous: std::option::Option<u64>,
}

impl MossRootContextGuard {
    fn enter(root_id: u64) -> Self {
        let previous = MOSS_CURRENT_ROOT_ID.with(|c| c.replace(Some(root_id)));
        MossRootContextGuard { previous }
    }
}

impl Drop for MossRootContextGuard {
    fn drop(&mut self) {
        let previous = self.previous;
        MOSS_CURRENT_ROOT_ID.with(|c| c.set(previous));
    }
}

/// The single Root execution discipline shared by every ingress path
/// (worker Root, INLINE runtime_invoke, DRAINING fallback): reject a Root
/// inside a Root (§7.1 / E2), establish the fail-closed unwind guard, set
/// the current-Root identity, run, and restore the context.
fn run_root<R>(root_id: u64, work: impl FnOnce() -> R) -> R {
    if let Some(running) = current_root_id() {
        panic!("moss runtime: Root {} cannot start inside running Root {} (§7.1 / E2)", root_id, running);
    }
    let _abort = MossAbortOnUnwind;
    let _root = MossRootContextGuard::enter(root_id);
    work()
}

/// Run one Branch on behalf of its owning Root.  Branch failure fails the
/// owning Root, which is fail-closed (abort) in Phase 20.
fn run_branch(owner_root_id: u64, work: MossWork) {
    let _abort = MossAbortOnUnwind;
    let _root = MossRootContextGuard::enter(owner_root_id);
    work();
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
            // SAFETY: PRIO_PROCESS with who=0 targets the calling thread on Linux.
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
    let _worker = MossWorkerContextGuard::enter(worker_id, std::sync::Arc::clone(&gx));

    loop {
        // Phase 1: exit decision or Root work, under gx.inner only.
        let root = {
            let mut inner = gx.lock_inner();
            let excess = inner.runnable_workers() > gx.config.threads;
            if inner.root_queue.is_empty() && (inner.shutdown || excess) {
                inner.active_workers = inner.active_workers.saturating_sub(1);
                #[cfg(any(test, moss_perf))]
                moss_rt_ev(MossRtEvent::WorkerExited, 0, 0, worker_id);
                // Pass on any wakeup this exiting worker may have consumed.
                gx.work_cv.notify_one(&inner);
                return;
            }
            let root = inner.root_queue.pop_front();
            if root.is_some() { inner.running_roots += 1; }
            root
        };

        if let Some(desc) = root {
            let inner = gx.lock_inner();
            gx.queue_not_full_cv.notify_all(&inner);
            drop(inner);
            let root_id = desc.root_id;
            #[cfg(any(test, moss_perf))]
            moss_rt_ev(MossRtEvent::RootStarted, root_id, 0, worker_id);
            run_root(root_id, desc.work);
            #[cfg(any(test, moss_perf))]
            moss_rt_ev(MossRtEvent::RootCompleted, root_id, 0, worker_id);

            let mut inner = gx.lock_inner();
            inner.running_roots = inner.running_roots.saturating_sub(1);
            if inner.running_roots == 0 && inner.root_queue.is_empty() {
                gx.drain_cv.notify_all(&inner);
            }
            continue;
        }

        // Phase 2: Branch work, without gx.inner.  Read the generation first
        // so a publication racing this inspection is seen in phase 3.
        let branch_seq_seen = gx.branch_seq.load(std::sync::atomic::Ordering::SeqCst);
        if let Some((scope, branch_id, work)) = gx.take_ready_branch() {
            #[cfg(any(test, moss_perf))]
            moss_rt_ev(MossRtEvent::BranchStarted, scope.owner_root_id, branch_id, worker_id);
            run_branch(scope.owner_root_id, work);
            scope.mark_complete(branch_id);
            continue;
        }

        // Phase 3: Condvar predicate under gx.inner.  Branch publishers bump
        // branch_seq and then pass through gx.inner before notifying.
        let inner = gx.lock_inner();
        let idle = inner.root_queue.is_empty()
            && !inner.shutdown
            && inner.runnable_workers() <= gx.config.threads
            && gx.branch_seq.load(std::sync::atomic::Ordering::SeqCst) == branch_seq_seen;
        if idle {
            #[cfg(any(test, moss_perf))]
            if let Some(hook) = MOSS_RT_PREWAIT_HOOK.get() { hook(worker_id); }
            drop(gx.work_cv.wait(inner));
        }
    }
}

// ─── Spawn workers helper ─────────────────────────────────────────────────
/// RAII: release one physical-thread slot when a worker thread body ends,
/// after every other worker-owned value (including its TLS context) is gone.
struct MossWorkerThreadGuard(std::sync::Arc<MossGlobalExec>);

impl Drop for MossWorkerThreadGuard {
    fn drop(&mut self) {
        let mut inner = self.0.lock_inner();
        inner.live_threads = inner.live_threads.saturating_sub(1);
    }
}

/// Spawn a worker whose physical slot the caller already reserved in
/// `live_threads` (and `active_workers`).
fn spawn_one_worker(gx: &std::sync::Arc<MossGlobalExec>, wid: usize) {
    let gx2 = std::sync::Arc::clone(gx);
    let affinity = gx.config.affinity.clone();
    let priority = gx.config.priority;
    let handle = std::thread::Builder::new()
        .name(format!("moss-worker-{}", wid))
        .spawn(move || {
            let _slot = MossWorkerThreadGuard(std::sync::Arc::clone(&gx2));
            #[cfg(any(test, moss_perf))]
            moss_rt_ev(MossRtEvent::WorkerSpawned, 0, 0, wid);
            apply_affinity_and_priority(affinity.as_ref(), priority);
            worker_loop(gx2, wid);
        })
        .expect("failed to spawn worker thread");
    // Reap exited (excess compensation) workers so handles stay bounded.
    let finished: std::vec::Vec<std::thread::JoinHandle<()>> = {
        let mut threads = gx.worker_threads.lock();
        let mut finished = std::vec::Vec::new();
        let mut index = 0;
        while index < threads.len() {
            if threads[index].is_finished() {
                finished.push(threads.swap_remove(index));
            } else {
                index += 1;
            }
        }
        threads.push(handle);
        finished
    };
    for h in finished { let _ = h.join(); }
}

// ─── Shared Root Admission Primitive (R4 FIFO Fairness) ─────────────────
enum MossAdmissionResult {
    Admitted,
    DrainingBeforeAdmission(MossRootDescriptor),
}

/// Centralized ingress guard (E12 / R4): running Roots and Branches may never
/// enter root admission.
#[inline(always)]
fn assert_external_root_ingress(operation: &str) {
    if current_root_id().is_some() || current_worker_id().is_some() {
        panic!("moss runtime: {} is legal only outside Root execution (E12 / R4)", operation);
    }
}

fn admit_root(gx: &std::sync::Arc<MossGlobalExec>, desc: MossRootDescriptor) -> MossAdmissionResult {
    assert_external_root_ingress("admit_root");
    let mut inner = gx.lock_inner();
    let my_ticket = inner.next_admission_ticket;
    inner.next_admission_ticket += 1;
    #[cfg(any(test, moss_perf))]
    moss_rt_ev(MossRtEvent::AdmissionTicketIssued, my_ticket, 0, usize::MAX);

    loop {
        if inner.draining {
            if my_ticket == inner.serving_admission_ticket {
                inner.serving_admission_ticket += 1;
                gx.queue_not_full_cv.notify_all(&inner);
            }
            return MossAdmissionResult::DrainingBeforeAdmission(desc);
        }
        if my_ticket == inner.serving_admission_ticket && inner.root_queue.len() < gx.config.queue_capacity {
            #[cfg(any(test, moss_perf))]
            moss_rt_ev(MossRtEvent::RootEnqueued, desc.root_id, 0, usize::MAX);
            #[cfg(any(test, moss_perf))]
            moss_rt_ev(MossRtEvent::AdmissionQueued, desc.root_id, 0, usize::MAX);
            inner.root_queue.push_back(desc);
            inner.serving_admission_ticket += 1;
            gx.work_cv.notify_one(&inner);
            gx.queue_not_full_cv.notify_all(&inner);
            return MossAdmissionResult::Admitted;
        }
        inner = gx.queue_not_full_cv.wait(inner);
    }
}

// ─── MossExecutor (builder) ──────────────────────────────────────────────
/// Moss-facing executor builder. Configuration is immutable after start().
pub struct MossExecutor {
    threads: std::option::Option<usize>,
    max_threads: std::option::Option<usize>,
    queue_capacity: std::option::Option<usize>,
    affinity: std::option::Option<std::vec::Vec<usize>>,
    priority: std::option::Option<i32>,
}

impl MossExecutor {
    pub fn new() -> Self {
        Self { threads: None, max_threads: None, queue_capacity: None, affinity: None, priority: None }
    }
    /// Set target compute parallelism (§3.1).
    pub fn threads(mut self, n: usize) -> Self {
        if n == 0 { panic!("moss executor: threads count must be > 0"); }
        self.threads = Some(n);
        self
    }
    /// Set finite cap on physical workers including compensation (§3.2 / E7).
    pub fn max_threads(mut self, n: usize) -> Self {
        if n == 0 { panic!("moss executor: max_threads must be > 0"); }
        self.max_threads = Some(n);
        self
    }
    /// Set root queue capacity (§3.3).
    pub fn queue_capacity(mut self, n: usize) -> Self {
        if n == 0 { panic!("moss executor: queue_capacity must be > 0"); }
        self.queue_capacity = Some(n);
        self
    }
    /// Best-effort affinity hint (§3.4) — warn and continue if OS refuses.
    pub fn affinity(mut self, cores: std::vec::Vec<usize>) -> Self { self.affinity = Some(cores); self }
    /// Best-effort priority hint (§3.4) — warn and continue if OS refuses.
    pub fn priority(mut self, level: i32) -> Self { self.priority = Some(level); self }

    /// The resolved configuration this builder would start with.
    pub fn config(&self) -> MossExecutorConfig {
        MossExecutorConfig::resolved(self.threads, self.max_threads, self.queue_capacity,
                                     self.affinity.clone(), self.priority)
    }

    /// INLINE → ACTIVE transition (§5.2 / §5.3 / E6 / R15).
    pub fn start(self) -> MossExecutorHandle {
        moss_root_start_with_config(self.config())
    }
}

impl Default for MossExecutor { fn default() -> Self { Self::new() } }

/// Start the single active Executor from a resolved configuration.
/// Closes the inline gate before waiting so no inline root overlaps ACTIVE;
/// the exclusive activation reservation makes a concurrent start() fail.
pub fn moss_root_start_with_config(config: MossExecutorConfig) -> MossExecutorHandle {
    config.validate();
    assert_external_root_ingress("start");
    let rt = process_rt();
    let target = config.threads;

    let gx = std::sync::Arc::new(MossGlobalExec {
        config,
        inner: MossFairMutex::new(MossExecInner {
            root_queue: std::collections::VecDeque::new(),
            active_workers: target,
            live_threads: target,
            running_roots: 0,
            draining: false,
            shutdown: false,
            next_worker_id: target,
            solo_blocked_workers: std::collections::HashMap::new(),
            next_admission_ticket: 0,
            serving_admission_ticket: 0,
        }),
        work_cv: MossFairCondvar::new(),
        queue_not_full_cv: MossFairCondvar::new(),
        drain_cv: MossFairCondvar::new(),
        branch_scopes: MossFairMutex::new(std::collections::VecDeque::new()),
        branch_seq: std::sync::atomic::AtomicU64::new(0),
        worker_threads: MossFairMutex::new(std::vec::Vec::new()),
    });

    {
        let mut inner = rt.lock_inner();
        if inner.state != MossIngressState::Inline || inner.start_pending {
            drop(inner);
            panic!("moss executor: at most one active executor allowed (E6/R15)");
        }
        // Exclusive activation reservation: close the gate to future INLINE
        // admissions and concurrent start() callers.
        inner.start_pending = true;
        #[cfg(any(test, moss_perf))]
        moss_rt_ev(MossRtEvent::StartPendingSet, 0, 0, usize::MAX);
        while inner.inline_running {
            inner = rt.ingress_cv.wait(inner);
            if inner.state != MossIngressState::Inline || !inner.start_pending {
                panic!("moss executor: invalid state during start()");
            }
        }

        inner.state = MossIngressState::Active;
        inner.start_pending = false;
        inner.active_executor = Some(std::sync::Arc::clone(&gx));
        // Waiting callers re-evaluate and use ACTIVE executor admission.
        rt.ingress_cv.notify_all(&inner);
    }

    #[cfg(any(test, moss_perf))]
    moss_rt_ev(MossRtEvent::IngressStateChange, 0, 0, 0);

    for wid in 0..target {
        spawn_one_worker(&gx, wid);
    }

    MossExecutorHandle { gx }
}

// ─── MossExecutorHandle (post-start) ─────────────────────────────────────
/// Returned by start(); valid until join(). Linear capability (never Clone).
pub struct MossExecutorHandle {
    gx: std::sync::Arc<MossGlobalExec>,
}

impl MossExecutorHandle {
    /// Enqueue a one-way root (executor.invoke, §4 / E9 / E10).
    /// Waits for queue capacity; caller holds no Moss lock as part of ingress.
    pub fn enqueue_root(&self, desc: MossRootDescriptor) {
        assert_external_root_ingress("enqueue_root");
        match admit_root(&self.gx, desc) {
            MossAdmissionResult::Admitted => {},
            MossAdmissionResult::DrainingBeforeAdmission(_) => {
                panic!("moss executor: enqueue_root called outside ACTIVE state");
            }
        }
    }

    /// The immutable configuration this executor started with.
    pub fn config(&self) -> &MossExecutorConfig { &self.gx.config }

    /// ACTIVE → DRAINING → INLINE (§5.4 / join()).
    pub fn join(self) {
        assert_external_root_ingress("join");
        let rt = process_rt();

        // Step 1: ACTIVE → DRAINING and close executor admission.
        {
            let mut inner = rt.lock_inner();
            if inner.state != MossIngressState::Active {
                drop(inner);
                panic!("moss executor: join() called outside ACTIVE state");
            }
            inner.state = MossIngressState::Draining;
        }
        {
            let mut exec_inner = self.gx.lock_inner();
            exec_inner.draining = true;
            self.gx.queue_not_full_cv.notify_all(&exec_inner);
        }

        #[cfg(any(test, moss_perf))]
        moss_rt_ev(MossRtEvent::IngressStateChange, 0, 0, 0);

        // Step 2: wait for every admitted root, then signal shutdown.
        {
            let mut exec_inner = self.gx.lock_inner();
            while exec_inner.running_roots > 0 || !exec_inner.root_queue.is_empty() {
                exec_inner = self.gx.drain_cv.wait(exec_inner);
            }
            exec_inner.shutdown = true;
        }
        let exec_inner = self.gx.lock_inner();
        self.gx.work_cv.notify_all(&exec_inner);
        drop(exec_inner);

        // Step 3: join every physical worker (no runtime lock held).
        let handles: std::vec::Vec<_> = {
            let mut guard = self.gx.worker_threads.lock();
            guard.drain(..).collect()
        };
        for h in handles {
            let _ = h.join();
        }

        // Step 4: DRAINING → INLINE; the executor is consumed.
        {
            let mut inner = rt.lock_inner();
            inner.state = MossIngressState::Inline;
            inner.active_executor = None;
            rt.ingress_cv.notify_all(&inner);
        }

        #[cfg(any(test, moss_perf))]
        moss_rt_ev(MossRtEvent::IngressStateChange, 0, 0, 0);
    }
}

// ─── Agent D Seam Bridge (ROOT_RUNTIME_ABI §2 / §3) ──────────────────────
/// Start with every field resolved to its runtime default.
#[inline]
pub fn moss_root_start() -> MossExecutorHandle {
    moss_root_start_with_config(MossExecutorConfig::default())
}

#[inline]
pub fn moss_root_submit(executor: &MossExecutorHandle, work: impl FnOnce() + Send + 'static) {
    executor.enqueue_root(MossRootDescriptor::one_way(next_root_id(), work));
}

#[inline]
pub fn moss_root_submit_with_target(executor: &MossExecutorHandle, target_identity: &'static str,
                                    work: impl FnOnce() + Send + 'static) {
    executor.enqueue_root(MossRootDescriptor::with_target(next_root_id(), target_identity, work));
}

#[inline]
pub fn moss_root_join(executor: MossExecutorHandle) {
    executor.join();
}

// ─── runtime_invoke ───────────────────────────────────────────────────────
fn take_inline_ticket(inner: &mut MossProcessRuntimeInner) -> u64 {
    let ticket = inner.next_inline_ticket;
    inner.next_inline_ticket += 1;
    #[cfg(any(test, moss_perf))]
    moss_rt_ev(MossRtEvent::InlineTicketIssued, ticket, 0, usize::MAX);
    ticket
}

fn inline_gate_open(inner: &MossProcessRuntimeInner, ticket: u64) -> bool {
    inner.state == MossIngressState::Inline
        && !inner.start_pending
        && !inner.inline_running
        && ticket == inner.serving_inline_ticket
}

/// Run a Root on the caller's thread while holding the inline gate, then
/// release the gate to the next ticket.
fn run_inline_root(rt: &MossProcessRuntime, root_id: u64, work: MossWork) {
    #[cfg(any(test, moss_perf))]
    moss_rt_ev(MossRtEvent::RootStarted, root_id, 0, usize::MAX);
    run_root(root_id, work);
    #[cfg(any(test, moss_perf))]
    moss_rt_ev(MossRtEvent::RootCompleted, root_id, 0, usize::MAX);

    let mut inner = rt.lock_inner();
    inner.inline_running = false;
    inner.serving_inline_ticket += 1;
    rt.ingress_cv.notify_all(&inner);
}

/// Synchronous host → Moss ingress (ROOT_RUNTIME_ABI §6).
///
/// Every caller takes one FIFO ingress ticket and keeps it as the head of
/// the line until its Root is admitted, so order is preserved across
/// INLINE → ACTIVE, across a DRAINING fallback, and across a later start():
/// - INLINE: runs on the caller thread under the inline gate.
/// - ACTIVE: admits through the shared executor admission; blocks until done.
/// - DRAINING (unadmitted): waits outside Moss, holds no Moss lock, then
///   re-evaluates once the executor is consumed (§5.4).
///
/// The handler's reply value is returned. No future / task / result queue.
pub fn runtime_invoke<F, R>(work: F) -> R
where
    F: FnOnce() -> R + Send + 'static,
    R: Send + 'static,
{
    assert_external_root_ingress("runtime_invoke");
    let rt = process_rt();
    let root_id = next_root_id();

    let reply_slot: std::sync::Arc<(std::sync::Mutex<std::option::Option<R>>, std::sync::Condvar)> =
        std::sync::Arc::new((std::sync::Mutex::new(None), std::sync::Condvar::new()));
    let reply = std::sync::Arc::clone(&reply_slot);
    let mut root_work: std::option::Option<MossWork> = Some(std::boxed::Box::new(move || {
        let res = work();
        let (lock, cvar) = &*reply;
        *lock.lock().unwrap_or_else(|e| e.into_inner()) = Some(res);
        cvar.notify_one();
    }));

    let mut inner = rt.lock_inner();
    let ticket = take_inline_ticket(&mut inner);

    loop {
        match inner.state {
            MossIngressState::Inline => {
                if inline_gate_open(&inner, ticket) {
                    inner.inline_running = true;
                    drop(inner);
                    run_inline_root(rt, root_id, root_work.take().expect("root work already consumed"));
                    break;
                }
                inner = rt.ingress_cv.wait(inner);
            }
            MossIngressState::Active => {
                if ticket != inner.serving_inline_ticket {
                    inner = rt.ingress_cv.wait(inner);
                    continue;
                }
                let gx = std::sync::Arc::clone(inner.active_executor.as_ref().expect("active executor missing in ACTIVE state"));
                drop(inner);
                let desc = MossRootDescriptor {
                    root_id,
                    target_identity: None,
                    work: root_work.take().expect("root work already consumed"),
                };
                match admit_root(&gx, desc) {
                    MossAdmissionResult::Admitted => {
                        let mut inner = rt.lock_inner();
                        inner.serving_inline_ticket += 1;
                        rt.ingress_cv.notify_all(&inner);
                        break;
                    }
                    MossAdmissionResult::DrainingBeforeAdmission(unadmitted) => {
                        // Still the head ticket: wait outside Moss and retry.
                        root_work = Some(unadmitted.work);
                        inner = rt.lock_inner();
                    }
                }
            }
            MossIngressState::Draining => {
                inner = rt.ingress_cv.wait(inner);
            }
        }
    }

    let (lock, cvar) = &*reply_slot;
    let mut guard = lock.lock().unwrap_or_else(|e| e.into_inner());
    while guard.is_none() {
        guard = cvar.wait(guard).unwrap_or_else(|e| e.into_inner());
    }
    guard.take().expect("reply missing after Root completed")
}

// ─── Branch join scope ────────────────────────────────────────────────────
struct MossBranchPending {
    items: std::collections::VecDeque<(u64, MossWork)>,
    /// True while this scope has an entry in the executor's ready queue
    /// (or a worker holds that entry in transit).
    queued: bool,
    /// One-shot contract: set by the single branch_join.  Publication checks
    /// it under this lock, so no Branch can be added once a join began.
    joined: bool,
}

/// Shared state for one one-shot Branch join scope, owned by exactly one Root.
pub struct MossBranchScopeState {
    /// Owner Root — helpers may only run branches for THIS root (E3 / §7.2).
    owner_root_id: u64,
    pending: std::sync::Mutex<MossBranchPending>,
    /// Branches published to workers and not yet complete.
    outstanding: std::sync::atomic::AtomicUsize,
    done_mutex: std::sync::Mutex<()>,
    done_cond: std::sync::Condvar,
}

impl MossBranchScopeState {
    fn lock_pending(&self) -> std::sync::MutexGuard<'_, MossBranchPending> {
        self.pending.lock().unwrap_or_else(|e| e.into_inner())
    }

    fn mark_complete(&self, branch_id: u64) {
        #[cfg(any(test, moss_perf))]
        moss_rt_ev(MossRtEvent::BranchCompleted, self.owner_root_id, branch_id, current_worker_id().unwrap_or(usize::MAX));
        #[cfg(not(any(test, moss_perf)))]
        let _ = branch_id;
        if self.outstanding.fetch_sub(1, std::sync::atomic::Ordering::AcqRel) == 1 {
            // Pass through done_mutex so a joiner that observed outstanding != 0
            // under the mutex is already waiting when this notifies.
            drop(self.done_mutex.lock().unwrap_or_else(|e| e.into_inner()));
            self.done_cond.notify_all();
        }
    }
}

/// One-shot Branch join scope belonging to the current Root:
/// create, publish zero or more Branches, join exactly once.
#[derive(Clone)]
pub struct MossBranchScope {
    state: std::sync::Arc<MossBranchScopeState>,
}

impl MossBranchScope {
    pub fn owner_root_id(&self) -> u64 { self.state.owner_root_id }

    /// Transfer one owned reference count to a raw cross-crate pointer.
    fn into_raw_owned(self) -> *const () {
        std::sync::Arc::into_raw(self.state) as *const ()
    }

    /// Reclaim one owned reference count from a raw cross-crate pointer.
    /// SAFETY: `raw` came from `into_raw_owned` and its count is not reused.
    unsafe fn from_raw_owned(raw: *const ()) -> Self {
        MossBranchScope { state: unsafe { std::sync::Arc::from_raw(raw as *const MossBranchScopeState) } }
    }

    /// Borrow a raw cross-crate pointer without touching its reference count.
    /// SAFETY: `raw` holds a live owned count for the duration of the call.
    unsafe fn with_raw_borrowed<T>(raw: *const (), f: impl FnOnce(&MossBranchScope) -> T) -> T {
        let borrowed = std::mem::ManuallyDrop::new(unsafe { Self::from_raw_owned(raw) });
        f(&borrowed)
    }
}

#[cfg(any(test, moss_perf))]
static MOSS_LIVE_BRANCH_SCOPES: std::sync::atomic::AtomicI64 = std::sync::atomic::AtomicI64::new(0);

/// Live Branch scope states; a double drop drives this below the true count.
#[cfg(any(test, moss_perf))]
pub fn moss_live_branch_scopes() -> i64 {
    MOSS_LIVE_BRANCH_SCOPES.load(std::sync::atomic::Ordering::SeqCst)
}

#[cfg(any(test, moss_perf))]
impl Drop for MossBranchScopeState {
    fn drop(&mut self) {
        MOSS_LIVE_BRANCH_SCOPES.fetch_sub(1, std::sync::atomic::Ordering::SeqCst);
    }
}

/// Create a test/runtime-internal scope for an explicit owner Root.
/// Compiler-generated code uses branch_scope_new_current().
fn branch_scope_new(owner_root_id: u64) -> MossBranchScope {
    #[cfg(any(test, moss_perf))]
    MOSS_LIVE_BRANCH_SCOPES.fetch_add(1, std::sync::atomic::Ordering::SeqCst);
    MossBranchScope {
        state: std::sync::Arc::new(MossBranchScopeState {
            owner_root_id,
            pending: std::sync::Mutex::new(MossBranchPending {
                items: std::collections::VecDeque::new(),
                queued: false,
                joined: false,
            }),
            outstanding: std::sync::atomic::AtomicUsize::new(0),
            done_mutex: std::sync::Mutex::new(()),
            done_cond: std::sync::Condvar::new(),
        }),
    }
}

/// Create a Branch join scope owned by the Root currently executing on this
/// thread.  Fails closed outside a Root: every scope belongs to one Root.
pub fn branch_scope_new_current() -> MossBranchScope {
    match current_root_id() {
        Some(owner) => branch_scope_new(owner),
        None => panic!("moss runtime: branch_scope_new_current() called outside a running Root"),
    }
}

/// Bounded per-scope publication window; beyond it the owner runs inline.
/// Compiler lowering bounds in-flight Branches by K well below this.
const MOSS_BRANCH_QUEUE_CAP: usize = 512;

// ─── branch_publish ───────────────────────────────────────────────────────
/// Publish a Branch for the scope's owning Root (E4 / §7.3).  Never blocks:
/// with no executor, or when the scope's window is full, the owner runs it
/// inline immediately.
pub fn branch_publish<F>(scope: &MossBranchScope, branch_fn: F)
where
    F: FnOnce() + Send + 'static,
{
    let st = &scope.state;
    if current_root_id() != Some(st.owner_root_id) {
        panic!("moss runtime: branch_publish outside the scope's owning Root {}", st.owner_root_id);
    }
    let gx = process_rt().active_executor();
    let mut pending = st.lock_pending();
    if pending.joined {
        drop(pending);
        panic!("moss runtime: branch_publish after branch_join (one-shot scope)");
    }
    let branch_id = next_branch_id();

    let gx = match gx {
        Some(gx) => gx,
        None => {
            // INLINE mode: compiler branches execute inline (§5.2).
            drop(pending);
            #[cfg(any(test, moss_perf))]
            moss_rt_ev(MossRtEvent::BranchInline, st.owner_root_id, branch_id, usize::MAX);
            branch_fn();
            return;
        }
    };

    if pending.items.len() >= MOSS_BRANCH_QUEUE_CAP {
        // Window full: the owner runs this Branch inline now (E4).
        drop(pending);
        #[cfg(any(test, moss_perf))]
        moss_rt_ev(MossRtEvent::BranchInline, st.owner_root_id, branch_id, current_worker_id().unwrap_or(usize::MAX));
        branch_fn();
        return;
    }
    st.outstanding.fetch_add(1, std::sync::atomic::Ordering::AcqRel);
    pending.items.push_back((branch_id, std::boxed::Box::new(branch_fn)));
    // Enter the ready queue only on the not-queued -> queued transition.
    let enqueue = !pending.queued;
    pending.queued = true;
    drop(pending);

    #[cfg(any(test, moss_perf))]
    moss_rt_ev(MossRtEvent::BranchPublished, st.owner_root_id, branch_id, usize::MAX);
    if enqueue {
        gx.push_ready_scope(st);
    }
    gx.signal_branch_work();
}

// ─── branch_join ──────────────────────────────────────────────────────────
/// Join a one-shot scope exactly once.  While waiting, help run unstarted
/// branches from THIS scope ONLY (E3 / §7.2 root-scoped helping); never run
/// unrelated queued Roots or other Roots' Branches.
pub fn branch_join(scope: MossBranchScope) {
    let st = scope.state;
    // Check ownership first so a rejected foreign join leaves the scope intact.
    if current_root_id() != Some(st.owner_root_id) {
        panic!("moss runtime: branch_join outside the scope's owning Root {}", st.owner_root_id);
    }
    if std::mem::replace(&mut st.lock_pending().joined, true) {
        panic!("moss runtime: Branch scope joined twice (one-shot scope)");
    }
    let was_queued = loop {
        let mut pending = st.lock_pending();
        match pending.items.pop_front() {
            Some((branch_id, work)) => {
                drop(pending);
                #[cfg(any(test, moss_perf))]
                moss_rt_ev(MossRtEvent::BranchHelped, st.owner_root_id, branch_id, current_worker_id().unwrap_or(usize::MAX));
                run_branch(st.owner_root_id, work);
                st.mark_complete(branch_id);
            }
            // Joined and empty: nothing can be published again, so the
            // ready-queue entry (if any) is dead.  Release it now rather than
            // whenever a free worker next pops it: under saturation no worker
            // may, and dead entries would grow with every joined scope.
            None => break std::mem::replace(&mut pending.queued, false),
        }
    };
    if was_queued {
        if let Some(gx) = process_rt().active_executor() {
            gx.remove_ready_scope(&st);
        }
    }
    // Remaining Branches were started by free workers; wait for them.
    let mut done = st.done_mutex.lock().unwrap_or_else(|e| e.into_inner());
    while st.outstanding.load(std::sync::atomic::Ordering::Acquire) != 0 {
        done = st.done_cond.wait(done).unwrap_or_else(|e| e.into_inner());
    }
}

// ─── Opaque cross-crate Branch ABI (exported by the process root) ─────────
// Providers hold only an opaque pointer that owns one reference count.
// Ownership per call: new_current / clone RETURN one owned count; drop and
// join CONSUME one owned count; publish BORROWS the scope and takes ownership
// of `data`, which is released by exactly one of `invoke` or `discard`.

struct MossForeignBranch {
    data: *mut (),
    invoke: unsafe extern "C" fn(*mut ()),
    discard: unsafe extern "C" fn(*mut ()),
}

// SAFETY: the provider wrapper only accepts `FnOnce() + Send + 'static` data.
unsafe impl Send for MossForeignBranch {}

impl MossForeignBranch {
    fn run(mut self) {
        let data = std::mem::replace(&mut self.data, std::ptr::null_mut());
        // SAFETY: `data` is invoked exactly once; Drop then sees null.
        unsafe { (self.invoke)(data) };
    }
}

impl Drop for MossForeignBranch {
    fn drop(&mut self) {
        if !self.data.is_null() {
            // SAFETY: the Branch never ran; release its foreign closure once.
            unsafe { (self.discard)(self.data) };
        }
    }
}

#[no_mangle]
pub extern "C" fn __moss_branch_scope_new_current() -> *const () {
    branch_scope_new_current().into_raw_owned()
}

#[no_mangle]
pub unsafe extern "C" fn __moss_branch_scope_clone(scope: *const ()) -> *const () {
    // SAFETY: the caller owns a live count for `scope` across this call.
    unsafe { MossBranchScope::with_raw_borrowed(scope, |s| s.clone().into_raw_owned()) }
}

#[no_mangle]
pub unsafe extern "C" fn __moss_branch_scope_drop(scope: *const ()) {
    // SAFETY: the caller transfers exactly one owned count.
    drop(unsafe { MossBranchScope::from_raw_owned(scope) });
}

#[no_mangle]
pub unsafe extern "C" fn __moss_branch_publish_trampoline(
    scope: *const (),
    data: *mut (),
    invoke: unsafe extern "C" fn(*mut ()),
    discard: unsafe extern "C" fn(*mut ()),
) {
    let branch = MossForeignBranch { data, invoke, discard };
    // SAFETY: the caller owns a live count for `scope` across this call.
    unsafe { MossBranchScope::with_raw_borrowed(scope, |s| branch_publish(s, move || branch.run())) }
}

#[no_mangle]
pub unsafe extern "C" fn __moss_branch_join(scope: *const ()) {
    // SAFETY: the caller transfers exactly one owned count.
    branch_join(unsafe { MossBranchScope::from_raw_owned(scope) });
}

// ─── Solo compensation (§15 / R1 / R2 / E7 / E8) ─────────────────────────
fn current_worker_exec() -> std::option::Option<(usize, std::sync::Arc<MossGlobalExec>)> {
    let worker_id = current_worker_id()?;
    let gx = MOSS_CURRENT_WORKER_EXEC.with(|c| c.borrow().clone())?;
    Some((worker_id, gx))
}

/// A worker enters a known Solo kernel wait.  May activate one compensation
/// worker to keep target parallelism, never beyond T_max (E7).  No runtime
/// lock is held across the caller's subsequent kernel wait (R1).
fn solo_enter_worker(gx: &std::sync::Arc<MossGlobalExec>, worker_id: usize) {
    #[cfg(any(test, moss_perf))]
    moss_rt_ev(MossRtEvent::SoloEnter, current_root_id().unwrap_or(0), 0, worker_id);
    let spawn = {
        let mut inner = gx.lock_inner();
        let depth = inner.solo_blocked_workers.entry(worker_id).or_insert(0);
        *depth += 1;
        if *depth == 1
            && inner.runnable_workers() < gx.config.threads
            && inner.live_threads < gx.config.max_threads
            && !inner.shutdown
        {
            inner.active_workers += 1;
            inner.live_threads += 1;
            let wid = inner.next_worker_id;
            inner.next_worker_id += 1;
            Some(wid)
        } else {
            None
        }
    };
    if let Some(wid) = spawn {
        spawn_one_worker(gx, wid);
    }
}

/// A worker leaves a Solo wait.  It resumes immediately and never waits for
/// a worker slot (R2 / E8); excess workers exit at their next idle point.
fn solo_leave_worker(gx: &std::sync::Arc<MossGlobalExec>, worker_id: usize) {
    #[cfg(any(test, moss_perf))]
    moss_rt_ev(MossRtEvent::SoloLeave, current_root_id().unwrap_or(0), 0, worker_id);
    {
        let mut inner = gx.lock_inner();
        let unblocked = match inner.solo_blocked_workers.get_mut(&worker_id) {
            Some(depth) => { *depth -= 1; *depth == 0 }
            None => false,
        };
        if unblocked { inner.solo_blocked_workers.remove(&worker_id); }
    }
    // Let one idle worker re-evaluate whether it is now excess.
    let inner = gx.lock_inner();
    gx.work_cv.notify_one(&inner);
}

/// Enter a Solo wait on behalf of the current thread.  Returns false (and does
/// nothing) on a thread that is not an Executor worker.
pub fn solo_enter_current(_reason: &str) -> bool {
    match current_worker_exec() {
        Some((wid, gx)) => { solo_enter_worker(&gx, wid); true }
        None => false,
    }
}

/// Leave a Solo wait on behalf of the current thread.
pub fn solo_leave_current(_reason: &str) -> bool {
    match current_worker_exec() {
        Some((wid, gx)) => { solo_leave_worker(&gx, wid); true }
        None => false,
    }
}

/// FileIO hook-table callback (registered through the FileIO root runtime's
/// moss_set_solo_hooks).  FileIO never supplies a worker identity.
pub fn executor_solo_enter_from_fileio(reason: &str) {
    let _ = solo_enter_current(reason);
}

/// FileIO hook-table callback paired with executor_solo_enter_from_fileio.
pub fn executor_solo_leave_from_fileio(reason: &str) {
    let _ = solo_leave_current(reason);
}

/// Callback for final integration: moss_set_solo_hooks(enter, leave).
pub fn executor_fileio_solo_enter_callback() -> fn(&str) {
    executor_solo_enter_from_fileio
}

/// Callback for final integration: moss_set_solo_hooks(enter, leave).
pub fn executor_fileio_solo_leave_callback() -> fn(&str) {
    executor_solo_leave_from_fileio
}

// ─── Test / perf accessors ────────────────────────────────────────────────
// Each accessor releases the process lock before taking an executor lock.
#[cfg(any(test, moss_perf))]
fn moss_test_exec<T>(f: impl FnOnce(&MossGlobalExec) -> T) -> std::option::Option<T> {
    process_rt().active_executor().map(|gx| f(&gx))
}

#[cfg(any(test, moss_perf))]
pub fn moss_executor_worker_count() -> usize {
    moss_test_exec(|gx| gx.lock_inner().active_workers).unwrap_or(0)
}

/// Physical worker threads whose bodies have not returned (E7 bound).
#[cfg(any(test, moss_perf))]
pub fn moss_executor_live_threads() -> usize {
    moss_test_exec(|gx| gx.lock_inner().live_threads).unwrap_or(0)
}

#[cfg(any(test, moss_perf))]
pub fn moss_executor_physical_threads() -> usize {
    moss_test_exec(|gx| {
        let threads = gx.worker_threads.lock();
        let mut live = 0;
        for h in threads.iter() {
            if !h.is_finished() { live += 1; }
        }
        live
    }).unwrap_or(0)
}

#[cfg(any(test, moss_perf))]
pub fn moss_executor_state() -> &'static str {
    match process_rt().lock_inner().state {
        MossIngressState::Inline   => "INLINE",
        MossIngressState::Active   => "ACTIVE",
        MossIngressState::Draining => "DRAINING",
    }
}

#[cfg(any(test, moss_perf))]
pub fn moss_runtime_start_pending() -> bool {
    process_rt().lock_inner().start_pending
}

/// (issued, serving) inline ingress tickets.
#[cfg(any(test, moss_perf))]
pub fn moss_runtime_inline_tickets() -> (u64, u64) {
    let inner = process_rt().lock_inner();
    (inner.next_inline_ticket, inner.serving_inline_ticket)
}

/// (issued, serving) executor admission tickets.
#[cfg(any(test, moss_perf))]
pub fn moss_executor_admission_tickets() -> (u64, u64) {
    moss_test_exec(|gx| { let i = gx.lock_inner(); (i.next_admission_ticket, i.serving_admission_ticket) }).unwrap_or((0, 0))
}

#[cfg(any(test, moss_perf))]
pub fn moss_executor_running_roots() -> usize {
    moss_test_exec(|gx| gx.lock_inner().running_roots).unwrap_or(0)
}

#[cfg(any(test, moss_perf))]
pub fn moss_executor_queue_len() -> usize {
    moss_test_exec(|gx| gx.lock_inner().root_queue.len()).unwrap_or(0)
}

#[cfg(any(test, moss_perf))]
pub fn moss_executor_max_threads() -> usize {
    moss_test_exec(|gx| gx.config.max_threads).unwrap_or(0)
}

#[cfg(any(test, moss_perf))]
pub fn moss_executor_active_config() -> std::option::Option<MossExecutorConfig> {
    moss_test_exec(|gx| gx.config.clone())
}

#[cfg(any(test, moss_perf))]
pub fn moss_executor_solo_blocked() -> usize {
    moss_test_exec(|gx| gx.lock_inner().solo_blocked_workers.len()).unwrap_or(0)
}

/// Entries in the executor's ready-scope queue (bounded by live scopes).
#[cfg(any(test, moss_perf))]
pub fn moss_executor_ready_scopes() -> usize {
    moss_test_exec(|gx| gx.branch_scopes.lock().len()).unwrap_or(0)
}
)EXECUTOR_RUST";
}

// Provider items: an opaque wrapper over the process root's Branch ABI.
// Providers never own a scheduler, never allocate Root identities, and never
// define the FileIO Solo symbols.
inline const char* executor_runtime_provider_items() {
  return R"EXECUTOR_RUST(extern "C" {
    fn __moss_branch_scope_new_current() -> *const ();
    fn __moss_branch_scope_clone(scope: *const ()) -> *const ();
    fn __moss_branch_scope_drop(scope: *const ());
    fn __moss_branch_publish_trampoline(
        scope: *const (),
        data: *mut (),
        invoke: unsafe extern "C" fn(*mut ()),
        discard: unsafe extern "C" fn(*mut ()),
    );
    fn __moss_branch_join(scope: *const ());
}

/// Opaque handle owning exactly one reference count of a process-root
/// Branch scope.  Same surface as the process root's MossBranchScope.
pub struct MossBranchScope {
    raw: *const (),
}

// SAFETY: the process root's scope is an Arc of thread-safe state.
unsafe impl Send for MossBranchScope {}
unsafe impl Sync for MossBranchScope {}

impl MossBranchScope {
    /// SAFETY: `raw` carries one owned count that this handle now owns.
    unsafe fn from_raw_owned(raw: *const ()) -> Self {
        MossBranchScope { raw }
    }

    /// Borrow without transferring ownership.
    fn as_raw_borrowed(&self) -> *const () {
        self.raw
    }

    /// Transfer this handle's owned count to the caller; Drop does not run.
    fn into_raw_owned(self) -> *const () {
        let this = std::mem::ManuallyDrop::new(self);
        this.raw
    }
}

impl Clone for MossBranchScope {
    fn clone(&self) -> Self {
        // SAFETY: clone borrows our count and returns a new owned count.
        unsafe { Self::from_raw_owned(__moss_branch_scope_clone(self.as_raw_borrowed())) }
    }
}

impl Drop for MossBranchScope {
    fn drop(&mut self) {
        // SAFETY: drop consumes this handle's single owned count.
        unsafe { __moss_branch_scope_drop(self.raw) };
    }
}

/// Create a one-shot scope owned by the current Root (fails closed outside a Root).
pub fn branch_scope_new_current() -> MossBranchScope {
    // SAFETY: new_current returns one owned count.
    unsafe { MossBranchScope::from_raw_owned(__moss_branch_scope_new_current()) }
}

unsafe extern "C" fn moss_foreign_branch_invoke<F: FnOnce() + Send + 'static>(data: *mut ()) {
    // SAFETY: `data` is the Box<F> leaked by branch_publish; reclaimed once.
    let work = unsafe { std::boxed::Box::from_raw(data as *mut F) };
    (*work)();
}

unsafe extern "C" fn moss_foreign_branch_discard<F: FnOnce() + Send + 'static>(data: *mut ()) {
    // SAFETY: `data` is the Box<F> leaked by branch_publish; reclaimed once.
    drop(unsafe { std::boxed::Box::from_raw(data as *mut F) });
}

pub fn branch_publish<F>(scope: &MossBranchScope, branch_fn: F)
where
    F: FnOnce() + Send + 'static,
{
    let data = std::boxed::Box::into_raw(std::boxed::Box::new(branch_fn)) as *mut ();
    // SAFETY: the scope is borrowed; the root takes ownership of `data`.
    unsafe {
        __moss_branch_publish_trampoline(scope.as_raw_borrowed(), data,
                                         moss_foreign_branch_invoke::<F>,
                                         moss_foreign_branch_discard::<F>);
    }
}

pub fn branch_join(scope: MossBranchScope) {
    // SAFETY: join consumes exactly the one count this handle owned.
    unsafe { __moss_branch_join(scope.into_raw_owned()) };
}
)EXECUTOR_RUST";
}

// One delimited runtime block per crate, whatever its role.
inline std::string executor_runtime_rust(ExecutorRuntimeRole role) {
  std::string s = "\n// ============================================================\n";
  switch (role) {
    case ExecutorRuntimeRole::ProcessRoot:
      s += "// Phase 20 Executor Runtime  (moss executor_runtime_rust: process root)\n"
           "// INLINE ─start()→ ACTIVE ─join()→ DRAINING ─drained→ INLINE\n"
           "// Fully-qualified standard types prevent collisions with user symbols.\n"
           "// ============================================================\n";
      s += executor_runtime_root_items();
      break;
    case ExecutorRuntimeRole::Provider:
      s += "// Phase 20 Executor Runtime  (moss executor_runtime_rust: provider wrapper)\n"
           "// Opaque Branch ABI exported by the process-root crate.\n"
           "// ============================================================\n";
      s += executor_runtime_provider_items();
      break;
    case ExecutorRuntimeRole::SelectedByExecutableBuild:
      s += "// Phase 20 Executor Runtime  (moss executor_runtime_rust: process root when built as the executable)\n"
           "// Built as the executable (--cfg moss_process_root) this crate owns the\n"
           "// process runtime; built as a provider .rlib it is the opaque wrapper.\n"
           "// ============================================================\n"
           "#[cfg(moss_process_root)]\nmod __moss_executor_runtime {\nuse super::*;\n";
      s += executor_runtime_root_items();
      s += "}\n#[cfg(not(moss_process_root))]\nmod __moss_executor_runtime {\nuse super::*;\n";
      s += executor_runtime_provider_items();
      s += "}\npub use __moss_executor_runtime::*;\n";
      break;
  }
  s += fair_leaf_runtime_items();
  s += "// ─── End Phase 20 Executor Runtime ───────────────────────────────────────\n";
  return s;
}

} // namespace moss
