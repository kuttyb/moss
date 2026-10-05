#pragma once

namespace moss {
// Phase 20 Branch abstraction (docs/ROOT_RUNTIME_ABI.md "Branch publish/join
// API"; docs/MOSS_PHASE_20_FILE_IO_AND_EXECUTORS.md sec. 7.3, E4).
//
// Unlike `executor.invoke` (which has no valid inline production fallback --
// see executor_invoke_codegen.inc), a compiler-generated Branch is explicitly
// allowed to execute inline when it cannot be published to another worker:
// "If a branch cannot be published to another worker, the owning root may
// execute it inline. A branch never waits for a queue slot..." (E4). This
// pair of functions *is* that normative inline-fallback behavior, not a
// substitute Agent-C scheduler: `branch_publish` always runs its work
// immediately on the calling (owning Root's) thread and `branch_join` is
// therefore the identity function. Agent C may replace this file with a real
// worker-thread implementation (publishing to another thread, returning a
// handle `branch_join` actually waits on) without any Agent-D-generated
// chunk-pipeline code changing, because that code only calls these two names
// and never inspects their internals (see file_chunk_codegen.inc).
inline const char* branch_runtime_rust() {
  return R"RUST(
#[inline]
fn branch_publish<T>(work: impl FnOnce() -> T) -> T {
    work()
}
#[inline]
fn branch_join<T>(outcome: T) -> T {
    outcome
}
)RUST";
}
}  // namespace moss
