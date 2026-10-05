// TEST ONLY -- NOT PHASE 20 RUNTIME.
//
// Minimal deterministic stand-in for the subset of Agent C's executor ABI
// that Agent D's codegen calls (docs/ROOT_RUNTIME_ABI.md, "Physical
// signatures & Integration Seam (Agent C Runtime)"): the MossExecutor
// builder, MossExecutorHandle::{enqueue_root, join}, MossRootDescriptor and
// next_root_id. Signatures mirror Agent C's so integration mismatches
// surface here (notably: root work must be `FnOnce() + Send + 'static`).
// Never emitted by the compiler; not a proposal for Agent C's runtime.
//
// runtime_invoke (synchronous Root ingress for top-level `message` from main)
// runs the Root on the caller's thread and returns its result -- a valid
// sequential stand-in for inline or executor admission that logs
// "moss-executor runtime_invoke".
//
// Behavior: start() prints the received configuration to stderr; each
// enqueue_root records the submitted root and returns immediately (the
// caller continues after submission); join() drains every admitted root in
// submission order on the joining thread. A valid sequential schedule of
// independent roots -- no workers, fairness, or compensation.
pub struct MossExecutor {
    threads: Option<usize>,
    max_threads: Option<usize>,
    queue_capacity: Option<usize>,
    affinity: Option<Vec<usize>>,
    priority: Option<i32>,
}

impl MossExecutor {
    pub fn new() -> Self {
        MossExecutor { threads: None, max_threads: None, queue_capacity: None, affinity: None, priority: None }
    }
    pub fn threads(mut self, n: usize) -> Self { self.threads = Some(n); self }
    pub fn max_threads(mut self, n: usize) -> Self { self.max_threads = Some(n); self }
    pub fn queue_capacity(mut self, n: usize) -> Self { self.queue_capacity = Some(n); self }
    pub fn affinity(mut self, cores: Vec<usize>) -> Self { self.affinity = Some(cores); self }
    pub fn priority(mut self, level: i32) -> Self { self.priority = Some(level); self }
    pub fn start(self) -> MossExecutorHandle {
        eprintln!(
            "moss-executor start threads={:?} max_threads={:?} queue_capacity={:?} affinity={:?} priority={:?}",
            self.threads, self.max_threads, self.queue_capacity, self.affinity, self.priority
        );
        MossExecutorHandle { pending: std::sync::Arc::new(std::sync::Mutex::new(std::collections::VecDeque::new())) }
    }
}

pub type MossWork = Box<dyn FnOnce() + Send + 'static>;

pub struct MossRootDescriptor {
    pub root_id: u64,
    pub target_identity: Option<&'static str>,
    pub work: MossWork,
}

impl MossRootDescriptor {
    pub fn one_way(root_id: u64, work: impl FnOnce() + Send + 'static) -> Self {
        MossRootDescriptor { root_id, target_identity: None, work: Box::new(work) }
    }
    pub fn with_target(root_id: u64, target_identity: &'static str, work: impl FnOnce() + Send + 'static) -> Self {
        MossRootDescriptor { root_id, target_identity: Some(target_identity), work: Box::new(work) }
    }
}

static MOSS_TEST_NEXT_ROOT: std::sync::atomic::AtomicU64 = std::sync::atomic::AtomicU64::new(1);

pub fn next_root_id() -> u64 {
    MOSS_TEST_NEXT_ROOT.fetch_add(1, std::sync::atomic::Ordering::Relaxed)
}

// Linear, like Agent C's handle: no Clone.
pub struct MossExecutorHandle {
    pending: std::sync::Arc<std::sync::Mutex<std::collections::VecDeque<MossRootDescriptor>>>,
}

impl MossExecutorHandle {
    pub fn enqueue_root(&self, desc: MossRootDescriptor) {
        eprintln!("moss-executor enqueue {}", desc.target_identity.unwrap_or("<anonymous>"));
        self.pending.lock().unwrap_or_else(|e| e.into_inner()).push_back(desc);
    }
    pub fn join(self) {
        loop {
            let next = self.pending.lock().unwrap_or_else(|e| e.into_inner()).pop_front();
            match next {
                Some(root) => (root.work)(),
                None => break,
            }
        }
        eprintln!("moss-executor join");
    }
}

pub fn runtime_invoke<R: Send + 'static, F: FnOnce() -> R + Send + 'static>(work: F) -> R {
    eprintln!("moss-executor runtime_invoke");
    work()
}
