// TEST ONLY -- NOT PHASE 20 RUNTIME.
//
// Minimal stand-in for Agent C's Branch join-scope ABI
// (branch_scope_new / branch_publish / branch_join). Signatures mirror Agent
// C's, including `F: FnOnce() + Send + 'static`, so a lowering that captures
// a parent borrow fails to compile here exactly as it would against Agent C.
// Never emitted by the compiler; not a proposal for Agent C's scheduler.
//
// Behavior: publication queues work without running it (models successful
// nonblocking publication to other workers) and prints "moss-branch publish"
// to stdout (interleaved with program output, so ordering is testable);
// join logs "moss-branch join" and then runs this scope's queued work, as
// root-scoped helping would. The log lets tests prove that K Branches exist
// before their join.
pub struct MossBranchScope {
    pub owner_root_id: u64,
    pending: std::sync::Mutex<std::collections::VecDeque<Box<dyn FnOnce() + Send + 'static>>>,
}

pub fn branch_scope_new(owner_root_id: u64) -> std::sync::Arc<MossBranchScope> {
    std::sync::Arc::new(MossBranchScope { owner_root_id, pending: std::sync::Mutex::new(std::collections::VecDeque::new()) })
}

pub fn branch_publish<F>(scope: &std::sync::Arc<MossBranchScope>, branch_fn: F)
where
    F: FnOnce() + Send + 'static,
{
    println!("moss-branch publish");
    scope.pending.lock().unwrap_or_else(|e| e.into_inner()).push_back(Box::new(branch_fn));
}

pub fn branch_join(scope: std::sync::Arc<MossBranchScope>) {
    println!("moss-branch join");
    loop {
        let next = scope.pending.lock().unwrap_or_else(|e| e.into_inner()).pop_front();
        match next {
            Some(work) => work(),
            None => break,
        }
    }
}
