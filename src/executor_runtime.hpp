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
  // The crate is always its program's executable: an implicit-module program.
  ProcessRoot,
  // The crate is only ever a library of an executable whose `main` lives in
  // another crate.
  Provider,
  // An explicit module can be built both as a provider .rlib and as the
  // program's executable (including a unit with no `main`). The role is
  // chosen by `--cfg moss_process_root`, which only the executable passes.
  SelectedByExecutableBuild,
};

// Rust runtime payload providers are compiled in executor_runtime.cpp.
const char* fair_leaf_runtime_items();
const char* executor_runtime_root_items();
const char* executor_runtime_provider_items();
std::string executor_runtime_rust(ExecutorRuntimeRole role);

} // namespace moss
