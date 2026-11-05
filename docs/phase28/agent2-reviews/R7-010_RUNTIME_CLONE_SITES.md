# R7-010 runtime clone site audit

Independent read-only audit of the eleven marked clone callsites in
`src/executor_runtime.cpp`.

## Frozen input and verdict

- Audited source commit: `7fb7122c860628e3ec89f38da38045f418c73214`
- SHA-256 of `src/executor_runtime.cpp` at that commit:
  `c84a1391e7fb4355d48f204578e1192e68dc69deb50bf829fe3b2497cc1ecda1`
- Frozen v2 expectations SHA-256:
  `7ca0d550e38f33c47105f57e22f5993d23449bf09c1cd5da043de924657f23a2`
- Frozen v2 runtime registry SHA-256:
  `1b4cc2f59ddaec51149f84468b098c53772815c3ca64e858febd976e53a6e19d`
- Frozen v2 certification SHA-256:
  `00e9d648286a90de7266622f18dad6f2b53693c5b3645cb37cb2b30bb4c4ccc9`
- Verdict: these sites are located in runtime Rust scaffolding, but **none is
  authorized under frozen v2**. A runtime marker identifies origin only and
  does not grant copy authority.

## Site inventory

| Site / line | Rust type and resource duplicated | Runtime ownership split | Audit note |
|---|---|---|---|
| `r7-010-runtime-01`, line 497 | `Option<Arc<MossGlobalExec>>`; executor strong count | `active_executor()` returns an owned executor reference after releasing the process-runtime lock; branch publish/join callers use it across that lock boundary. | Runtime scheduler reference; no v2 authorization. |
| `r7-010-runtime-02`, line 847 | `Option<Vec<usize>>`; affinity vector allocation and elements | `MossExecutor::config(&self)` returns an owned configuration snapshot from a borrowed builder. | Runtime configuration data, not a Moss semantic value; no v2 authorization. |
| `r7-010-runtime-03`, line 1397 | `MossBranchScope` wrapping `Arc<MossBranchScopeState>`; one strong count | The process-root FFI adapter borrows an opaque raw handle, clones its Arc, and transfers the new owned count back across the ABI. | Clone implementation purpose is clear; see residual question below. No v2 authorization. |
| `r7-010-runtime-04`, line 58 | `Arc<MossFairNode>`; per-acquisition fair-lock node count | A cloned count is transferred into the atomic tail while the local acquisition/guard retains its count. Tail handoff and guard drop reconstruct and release the appropriate owner. | FIFO lock lifetime bookkeeping; no v2 authorization. |
| `r7-010-runtime-05`, line 152 | `Arc<MossFairWaiter>`; waiter object count | The condvar queue retains the waiter while the sleeping thread keeps its local reference to observe notification. The notifier pops, signals, and unparks it. | Queue/waiter lifetime split; no v2 authorization. |
| `r7-010-runtime-06`, line 438 | `Arc<MossBranchScopeState>`; join-scope count | The executor ready queue retains a scope while asynchronous Branch work is queued; dequeue transfers a live reference to the worker/helping path. | Scheduler queue lifetime; no v2 authorization. |
| `r7-010-runtime-07`, line 643 | `Arc<MossGlobalExec>`; executor strong count | `MossWorkerContextGuard` installs an owned executor in worker TLS for Solo enter/leave callbacks, then clears TLS at thread exit. | TLS reference overlaps the worker-slot reference at site 08; necessity of the extra count is unproven. No v2 authorization. |
| `r7-010-runtime-08`, line 723 | `Arc<MossGlobalExec>`; executor strong count | The spawned thread closure owns an executor reference for its physical worker-slot lifetime through `MossWorkerThreadGuard`. | Thread lifetime; no v2 authorization. |
| `r7-010-runtime-09`, line 909 | `Arc<MossGlobalExec>`; executor strong count | The process runtime retains the active executor while the local value becomes the returned handle and worker threads hold their own references; the registry reference is cleared after drain. | Global registry/handle lifetime split; no v2 authorization. |
| `r7-010-runtime-10`, line 1079 | `Arc<(Mutex<Option<R>>, Condvar)>`; reply-cell control block count | The invoking thread retains the cell while the moved root-work closure owns a second count to publish its result and notify the waiter. The generic `R` is not cloned at this site. | Synchronous host-ingress completion; no v2 authorization. |
| `r7-010-runtime-11`, line 1106 | `Arc<MossGlobalExec>`; executor strong count | Active ingress clones the registry executor under lock, releases the lock, then attempts admission; the local reference survives a concurrent transition to draining. | Admission/drain race lifetime; no v2 authorization. |

## Residual questions

1. Site 03 implements a cross-crate owned-handle clone, but no generated Rust
   caller was found. The file-chunk lowering calls `branch_publish(&scope, …)`
   and consumes the scope with `branch_join(scope)` without cloning it. The
   actual caller frequency and whether a generated program needs this API
   remain unverified.
2. Site 07 installs a TLS Arc while site 08 already gives the worker slot an
   Arc for the same `MossGlobalExec`. TLS is used by current-worker Solo
   callbacks, but the need for a second independently counted owner rather
   than a borrow from the worker slot remains unproven.

This report classifies source purpose only. It does not approve runtime
exceptions or alter the v2 specification, oracle, or certification.
