// TEST ONLY -- NOT PHASE 20 RUNTIME.
//
// Minimal, deterministic shim for the three Rust-facing names Agent D's
// codegen emits for `Executor`/`executor.invoke`
// (src/executor_invoke_codegen.inc: emit_executor_start/emit_root_submit/
// emit_executor_join). It exists only so Agent D's own compiler tests can
// produce a runnable executable without Agent C's real root-admission
// runtime; it is never emitted by the compiler and must not be read as the
// normative Phase 20 Executor/Root behavior (see
// docs/ROOT_RUNTIME_ABI.md, which this file is deliberately absent from).
//
// Behavior: `moss_root_submit` snapshots the submitted thunk; `moss_root_join`
// drains and runs every submitted thunk, in submission order, on the
// joining thread. This is sequential by construction (same as Phase 20's
// own "no active executor" inline-mode fallback, sec. 5.2), which is enough
// to prove Agent D's RootSubmissionPlan/argument-snapshot/one-way codegen is
// correct; it does not exercise real concurrency, fairness, or
// compensation, which remain Agent C's to implement and test.
struct MossTestRootQueue {
    pending: std::cell::RefCell<std::collections::VecDeque<Box<dyn FnOnce()>>>,
}

fn moss_root_start() -> MossTestRootQueue {
    MossTestRootQueue { pending: std::cell::RefCell::new(std::collections::VecDeque::new()) }
}

fn moss_root_submit(executor: &MossTestRootQueue, work: impl FnOnce() + 'static) {
    executor.pending.borrow_mut().push_back(Box::new(work));
}

fn moss_root_join(executor: MossTestRootQueue) {
    loop {
        let next = executor.pending.borrow_mut().pop_front();
        match next {
            Some(work) => work(),
            None => break,
        }
    }
}
