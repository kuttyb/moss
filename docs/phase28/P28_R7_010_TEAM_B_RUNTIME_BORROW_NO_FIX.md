# Phase 28 R7-010 Team B — Runtime Borrow Elimination Review

## Scope and evidence

This is a source-only investigation of the three marked direct runtime clone
templates in `src/executor_runtime.cpp` at staging source SHA
`d4aa51111d62917c4a1caa180266291862c5b88d`:

| Marker | Current source location | Expression and type | Why a local borrowed/reference rewrite is not semantics-preserving |
| --- | --- | --- | --- |
| `r7-010-runtime-01` | `src/executor_runtime.cpp:496–498` | `self.lock_inner().active_executor.clone()`; `Option<Arc<MossGlobalExec>>` | `active_executor()` returns a strong handle after its fair-lock guard is released. `runtime_invoke` also clones at line 1116, drops the ingress guard at 1117, then calls `admit_root` at 1123; admission can wait for queue capacity. Keeping a borrow would require holding the process lifecycle lock through that potentially blocking operation, preventing lifecycle/join progress. The same helper is used by branch publication/join across subsequent scope/executor operations. A guard-scoped callback would need a larger lock-order/lifecycle redesign, not a local borrow lowering. |
| `r7-010-runtime-02` | `src/executor_runtime.cpp:854–858` | `self.affinity.clone()`; `Option<Vec<usize>>` | This backs public `MossExecutor::config(&self) -> MossExecutorConfig`, whose `affinity` field is an owned `Option<Vec<usize>>` snapshot. Borrowing changes the returned type/lifetime and prevents the snapshot from surviving builder mutation/drop. The Moss lowering path in `src/executor_invoke_codegen.inc:65–68` constructs the executor with setters and `.start()`; it does not call `.config()`. That limits this method’s generated-source use but does not remove the public snapshot contract. `start(self)` already uses the separate move path through `into_config(self)`. |
| `r7-010-runtime-03` | `src/executor_runtime.cpp:1404–1408` | `s.clone().into_raw_owned()`; `MossBranchScope` wrapping `Arc<MossBranchScopeState>` | The exported `__moss_branch_scope_clone` returns a newly owned count. `docs/ROOT_RUNTIME_ABI.md:264–277` specifies that `new_current`/`clone` return one owned count, `drop`/`join` consume one, and `publish` borrows. Provider wrappers implement `Clone` by calling this ABI (`src/executor_runtime.cpp:1696–1700`). Branch work is `FnOnce() + Send + 'static` (`src/executor_runtime.cpp:1274–1276`); its scheduled lifetime is not bounded by a borrow of the caller’s handle. Replacing this result with a borrowed pointer would break the cross-crate ownership/lifetime contract. |

Source digest: `src/executor_runtime.cpp` SHA-256
`3af71438b396ad0303eda7a1844902d2802c27bff8a3cdd18e8d6e82f5d22690`.
Supporting source digest: `src/file_chunk_codegen.inc` SHA-256
`30b29209b4f2974cc147647d0edfb5fd056145d7aa93305252761e2b40fed92a`.
ABI digest: `docs/ROOT_RUNTIME_ABI.md` SHA-256
`9f57390a05bcc524243f6cc2d81a430c03d2096dc4e2e8b1e77b2645ebd317a3`.

## Result

**NO-FIX for this scoped change.** Each site has an ownership or API boundary
that a borrowed/reference-only local rewrite would violate. A future broader
runtime/API redesign could be investigated separately, with its own behavior,
concurrency, ABI, and regression evidence.

This conclusion does not authorize or recategorize any occurrence. All three
runtime clone templates remain unapproved by the frozen provenance registry;
this report proposes no allowlist entry and grants no semantic-copy authority.
The Rust target types and the synchronous source path show no Moss application
payload at these exact expressions, but that observation is not provenance
approval and does not establish safety beyond the inspected code paths.

No compiler, checker, test, oracle, or registry source was changed. Per the L7
instruction that the full suite is in progress, no build or test was run. This
report does not independently validate generated artifacts, runtime behavior,
or Rust macro/MIR expansions. Independent review is pending for this exact
source-pinned NO-FIX analysis.
