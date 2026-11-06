# Phase 28 R7-010 Team B Clone Audit — Source SHA 3417bed

**Scope:** Evidence-only audit of the generated Rust clone and alternative-operation inventory for source SHA `3417bedcd523dbeff187014de18d4a699ab53b10`. This report does not change compiler source, the oracle, or the runtime registry, and it authorizes no runtime clone.

## Evidence set and hashes

Team C captured 84 generated Rust artifacts from the exact source SHA in:
`tmp/phase28-r7-010-team-c-combined-3417/tmp/team-c-combined-3417/artifacts/`.
Its `clone-inventory.json` reports 1,876 candidate operation occurrences in 36 site groups. SHA-256 values:

| Evidence | SHA-256 |
|---|---|
| `src/executor_runtime.cpp` | `3af71438b396ad0303eda7a1844902d2802c27bff8a3cdd18e8d6e82f5d22690` |
| `src/native_generator.cpp` | `0f8fb5937a52fd0fdebb5e34fda253b764e2cd31be5b062445b0dcd5a9d928e4` |
| `src/native_clone_provenance.hpp` | `19b2c3ff974a44e26d9603fa00687f253ab3f997d443cc4155e9a6962e7fc079` |
| Team C `clone-inventory.json` | `cbbd462d21eacfa628e83fa8f2d3be22934cf9704d5614d63c7dda0c1537add3` |
| Team C `ledger.md` | `cd54100485f2b1b5566961f9f55d3f9759e0f5a05ba4418636c8db754ff0c862` |
| Team C durable generation-site report | `0a436f61ceecda3cf4c192a6e0a9cfb337542982c2dbeb5c66593ff48196b2de` |
| Team C durable combined ledger | `7187fb0301a4fc8d91b6fb0d4820d2d0dd43d1a0ec20dbf28161ba0e7e918fa8` |

Team C's durable [generation-site report](team-c/P28_R7_010_GENERATION_SITE_AUDIT.md) is an earlier 71-artifact audit and is cross-linked for its method/limitations; its historical counts are not substituted for this 3417 inventory. The exact 3417 inventory and its Markdown rendering are under the temporary artifact root above. Team C owns the conformance ledger and oracle expectations.

## Direct `.clone()` sites

The inventory and recursive sidecar scan agree on 258 direct `.clone()` rows: 252 runtime-template rows, four capability-capture rows, and two explicit-copy rows. The runtime-template rows occur in all 84 artifacts and every corresponding sidecar marks them `unclassified`.

| Runtime site | Source location and Rust type/use | Generated Rust example span | Interpretation and authority |
|---|---|---|---|
| `r7-010-runtime-01` | `src/executor_runtime.cpp:497:57–98`; `Option<Arc<MossGlobalExec>>`, returned by `MossProcessRuntime::active_executor()` after locking runtime state | ORACLE-01 Debug full call `2014:57–98`; sidecar clone token `2014:90–98` | Shared process-executor handle escapes the lock guard. Runtime handle clone; no `TypedCopyFactV1` link. Sidecar origin is `unclassified`. |
| `r7-010-runtime-02` | `src/executor_runtime.cpp:857:86–107`; `Option<Vec<usize>>` affinity copied into owned `MossExecutorConfig` returned by `MossExecutor::config(&self)` | ORACLE-01 Debug full call `2374:86–107`; sidecar clone token `2374:99–107` | Copies executor runtime configuration metadata for the returned snapshot. It is a Rust `Vec` duplication, but the observed value is executor affinity metadata, not a Moss application collection. No checker copy authority; sidecar origin is `unclassified`. |
| `r7-010-runtime-03` | `src/executor_runtime.cpp:1407:108–117`; `MossBranchScope::clone()` calls `__moss_branch_scope_clone` from an opaque borrowed `*const ()`; `MossBranchScope` owns an `Arc<MossBranchScopeState>` (`:1195–1208`, provider wrapper `:1669–1700`) | ORACLE-01 Debug call `2924:108–117`; sidecar clone token `2924:109–117` | Increments the shared branch-scope state reference count and returns one owned opaque handle. Runtime capability handle, not Moss payload. No `TypedCopyFactV1` link; sidecar origin is `unclassified`. |

Generated artifact SHA-256: ORACLE-01 Debug `.rs` `ee6ebfbbe15d928ff312be7218ab1935fe91e51d1b657977d7d39fc433ba8be1`; sidecar `e019843db9c2352f11ca08788bb709d5a20f6d61d607b7a92695c387b9e31c2b`. The optimized artifact has the same generated Rust SHA; its sidecar SHA is `2709f73ad82bcadd7c5cd83bc4520c874410c36126e4603cebbe9dc6c5a5454d`.

## Seven `Arc::clone` sites

Each site below occurs once per generated artifact (84 rows/site, 588 rows total). In the representative ORACLE-01 Debug artifact, all corresponding operations are in runtime definitions, and sidecar `.clone()` rows remain unclassified. `Arc::clone` preserves a shared allocation; it does not clone the inner value. This type/data-flow observation is not a runtime authorization.

| Site | Source location/type and use | ORACLE-01 Debug Rust span |
|---|---|---|
| `r7-010-runtime-04` | `executor_runtime.cpp:58:92–120`, `Arc<MossFairNode>`; retain the node while transferring another owned count to the atomic raw tail pointer | `3205:92–120` |
| `r7-010-runtime-05` | `executor_runtime.cpp:152:103–133`, `Arc<MossFairWaiter>`; queue retains a waiter while the local thread waits | `3299:103–133` |
| `r7-010-runtime-06` | `executor_runtime.cpp:438:93–121`, `Arc<MossBranchScopeState>`; ready-queue entry retains a scope | `1955:93–121` |
| `r7-010-runtime-08` | `executor_runtime.cpp:733:63–88`, `Arc<MossGlobalExec>`; move a worker-owned executor handle into the new worker closure | `2250:63–88` |
| `r7-010-runtime-09` | `executor_runtime.cpp:919:86–112`, `Arc<MossGlobalExec>`; store an active-executor handle in process runtime state | `2436:86–112` |
| `r7-010-runtime-10` | `executor_runtime.cpp:1089:65–99`, `Arc<(Mutex<Option<R>>, Condvar)>`; share the reply-slot handle between caller and root closure; `R` remains one value in the mutex slot | `2606:65–99` |
| `r7-010-runtime-11` | `executor_runtime.cpp:1116:74–177`, `Arc<MossGlobalExec>`; acquire a handle under the runtime lock, then admit work after releasing the lock | `2633:74–177` |

All seven have exact runtime markers in the hashed source file. None has a checker `TypedCopyFactV1` row, and the certified runtime registry remains empty. They therefore remain unapproved for provenance purposes even though the inspected use paths operate on runtime handles.

## Checked capability captures and explicit copy

Four additional direct `.clone()` rows are two Root-ingress source sites in Debug and Optimized output. They are not Moss payload copies and their facts explicitly deny that authority.

| Source | Generated Rust span and fact | Sidecar evidence |
|---|---|---|
| ORACLE-45 `echo (message w.Increment(10))` | Debug `3510:188–196`; capture ID `root-ingress-domain-capture-fact-v1:56b26210213ca7a86ea7cc7ab1176e8e09ae4a0c3037bc6a22c2a3be8fd09bf0` | `root_ingress_domain_capture`; `compiler_generated_capture_only;permits_moss_value_copy=false` |
| ORACLE-43 `res = message w.Process(42)` | Debug `3505:187–195`; capture ID `root-ingress-domain-capture-fact-v1:742fa3d62ed31f496a4ad8b9d1ff33435a4d887b4ef548b96c88416e50c631bb` | `root_ingress_domain_capture`; `compiler_generated_capture_only;permits_moss_value_copy=false` |

Each site has a Debug and Optimized row. Debug/Optimized artifact and sidecar SHA-256 pairs:

| Output | Rust SHA-256 | Sidecar SHA-256 |
|---|---|---|
| ORACLE-45 Debug | `0cf74cd2e09ab970946aaaff491476b922d1f007ad8915f910b3a1f10b713737` | `77a07bac0faa3f8b7f46f2e5d8e0e75d9d7e7dcf8ae580cae461740dea9925e1` |
| ORACLE-45 Optimized | `0cf74cd2e09ab970946aaaff491476b922d1f007ad8915f910b3a1f10b713737` | `f0a234f0e814737a7b375746d3b0f3518419697c118b55fddcf11e78f8ac7e2c` |
| ORACLE-43 Debug | `b6b8bc21bc5e7599c00d6b04b534fd006d142fcd52b93ec28fae25b4d1aa4c57` | `ccb83aea188a87371da2dfd80a51215817cf240ffaad2cd21e54bea6cd655446` |
| ORACLE-43 Optimized | `b6b8bc21bc5e7599c00d6b04b534fd006d142fcd52b93ec28fae25b4d1aa4c57` | `96630624917170aa5fcf544ee1c48203833d28f611b12b9717a83b882c4706a5` |

The two explicit-copy rows implement `var copied = copy(v[0])` in ORACLE-10A. Each is inside a fact-specific `__moss_copy_value_d28651f281792b4a` helper; source locator is `copy@7:16`; checked identity is `typed-copy-fact-v1:d28651f281792b4a13db2d162e5d1819b290399fab0ba98d1280f6bd0d7d8015`; sidecar `semantic_type` is the checked result type and `ownership_fact` is `READ`. This is the only source-level explicit-copy group in the 3417 direct-clone inventory.

| Output | Rust SHA-256 | Sidecar SHA-256 | Clone span |
|---|---|---|---|
| ORACLE-10A Debug | `e55fb43817e553ea8f3c55be8d2c4b41d9ae2d4040616a77a4a0a39c8db27cf2` | `807876265e3baf49b14ecad76f42b357465c0c45a117005bd00213e9c66d09c3` | `3478:98–106` |
| ORACLE-10A Optimized | `e55fb43817e553ea8f3c55be8d2c4b41d9ae2d4040616a77a4a0a39c8db27cf2` | `801e74b01f69630b4bc3dc28860d58ce2668229a7835bdc63d86f62e0265b57e` | `3478:98–106` |

## Alternative operation inventory

These are candidate operations from the same 84-artifact scan, grouped by source operation; counts are observations, not Moss-copy verdicts.

| Operator/group | Count and sites | Evidence-based classification |
|---|---:|---|
| `Arc::clone` | 588; seven sites above ×84 | Shared runtime handles; unapproved, no checked Moss copy authority. |
| `.to_string()` | 756; nine runtime groups ×84 | Integer/float display and owned string-slice materialization (`__moss_display_int`, `canonical_float_text`, `__moss_string_slice`); formatting/materialization, not grounds to authorize a duplicate Moss value. |
| `mem::replace` | 252; three runtime groups ×84 | `joined`/`queued` flag replacement and moving the foreign closure pointer out before invoke. These are scalar state reset or ownership transfer, not value cloning. |
| `mem::take` | 14; seven Moss `take(...)` source groups ×2 modes | Native generator resolves an exact `TypedTakeFactV1` before `emit_take_expression` and emits `std::mem::take`; checked transfer. |
| `.copied()` | 6; three Moss loop groups ×2 modes | Generated values in the examples are primitive integers; this scan alone does not query checker facts, so the operator is not itself authority. |
| `String::from` / `Vec::from` | 2; one Map-key expression ×2 modes | Materializes the source string-literal key; no evidence that it duplicates a stored Moss Map value. |

The scanner separately reports 408 generated `#[derive(Clone)]` declarations as capabilities, not invocations. It associates two helper-body clone calls with the explicit-copy helper above. Static scanning does not expand macros or inspect Rust MIR/Copy loads.

## Result and limits

- The two semantic-copy rows have exact checked `TypedCopyFactV1` provenance.
- The four domain-capture rows have exact Root-ingress facts and explicitly set `permits_moss_value_copy=false`.
- All 252 direct runtime `.clone()` rows and all 588 `Arc::clone` rows are still unclassified/unapproved by the provenance contract. This report does not add a runtime exception or registry entry.
- No compiler source fix is proposed: no Moss payload duplication was observed at these exact three marked runtime templates in the inspected artifacts. This is not proof about macro expansion, Rust MIR/Copy loads, unobserved call paths, or other builds, and it does not establish blanket runtime safety.
- The audit is based on generated source and sidecars, not Rust MIR; macros and unobserved call paths are outside its proof. Candidate operation classifications are limited to the exact 84 artifacts and source SHA above.

No oracle expectation, compiler source, shared runner, or clone registry was modified for this audit.
