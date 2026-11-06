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

### Nine `.to_string()` sites

The following exact spans come from representative ORACLE-01 Debug output, SHA-256 `ee6ebfbbe15d928ff312be7218ab1935fe91e51d1b657977d7d39fc433ba8be1`; every template repeats in all 84 artifacts. Source/artifact columns are 1-based byte columns, end-exclusive.

| Runtime group and source span | Generated Rust span | Source and semantics |
|---|---|---|
| `display_runtime.cpp:8:24–36` | `1422:24–36` | `out.push_str(&value.to_string())`; formats an `i64` to a temporary Rust String for display. Source SHA `e5df9f89684ec03181a7296d12f0bbdb7fb5a1114b24a0bc300cd79752522158`. |
| `char_string_ordering_runtime.cpp:115:74–86` | `1393:74–86` | ASCII branch of `__moss_string_slice`; creates an owned substring result. |
| `char_string_ordering_runtime.cpp:128:46–58` | `1406:46–58` | UTF-8-boundary branch of the same slice result construction. |
| `float_format_runtime.cpp:1187:37–49` | `1242:37–49` | Formats NaN text for canonical float output. |
| `float_format_runtime.cpp:1188:45–57` | `1243:45–57` | Formats positive infinity text. |
| `float_format_runtime.cpp:1189:50–62` | `1244:50–62` | Formats negative infinity text. |
| `float_format_runtime.cpp:1191:69–81` | `1246:69–81` | Formats signed zero text. |
| `float_format_runtime.cpp:1198:34–46` | `1253:34–46` | Formats the decimal mantissa before assembling canonical float output. |
| `float_format_runtime.cpp:1225:34–46` | `1280:34–46` | Formats the decimal exponent before appending canonical float output. |

Source hashes: `char_string_ordering_runtime.cpp` `8b68c96fd117294d4146ceb3a2fd020edf34b7a21d7e2a5daa45c4c3467e0c0a` and `float_format_runtime.cpp` `a488de64270c396542b4bcb2a250df0d4e38463922b610b993616a59ac2eaa11`. Seven rows are rendering/formatting. The two slice rows are different: the native emitter maps a checked string `.slice` call to `__moss_string_slice` at `src/native_generator.cpp:6662`; that helper returns `MossStr` (plain runtime ABI: `String`) from the selected UTF-8 substring. [Phase 24 text architecture](../PHASE_24_TEXT_ENCODING_ARCHITECTURE.md#confirmed-string-and-encoding-surface) specifies that `String.slice(start, length)` makes one result copy. This is owned substring construction, not a comparison/rendering buffer and not general authorization for `.to_string()` or a Moss value clone. The helper body is emitted unconditionally in these artifacts, but has no callsite in this corpus; therefore there is no per-occurrence checker fact to join here and no slice operation is exercised by these 84 fixtures. The documented String.slice result contract is the semantic basis for the helper when used; it does not authorize general materialization.

### Six `.copied()` rows and primitive type evidence

Each of the three source loops appears in Debug and Optimized Rust. Type-query outputs below come from the 3417 worktree compiler. `Int` is a scalar primitive with implicit Copy behavior under the frozen ownership contract. These facts establish only these primitive loop items; no nonprimitive copy authority is inferred.

| Fixture/source span | Generated `.copied()` span; Debug artifact SHA-256 | Checked type evidence |
|---|---|---|
| ORACLE-29 `oracle_29_loop_carried_ownership_rebind.moss:4:3–20`, `for x in frontier:` | Debug `ORACLE-29-LOOP-CARRIED-OWNERSHIP.rs:3437:31–40` (SHA `e6fa0b91d18ad2794f6e8d2750c1cd4c2eced49fc7c53a4e9dd4242d0f7c4284`); Optimized has same Rust SHA. Sidecars: Debug `6004b1381d80841f7ba32e2edbb70a66107d545850f6e1ecb33d7872a035670a`, Optimized `53bd92d0bd6789850af5eb95dde6e85875c2b0be998b466444e607b0111fda2f`. | `./moss inspect expand --source <fixture> --json` reports checked parameter `frontier: vector[int]`, effect READ; iteration item `x: Int`. Fixture SHA `f2afa5348afec5917b781c3dcf68a38155cf560a34a6898e87ecd3649c611b9f`. |
| ORACLE-56 `oracle_56_primitive_vector_element_read.moss:10:3–22`, `for value in values:` | Debug `ORACLE-56-PRIMITIVE-VECTOR-ELEMENT-READ.rs:3458:33–42` (SHA `771dc71e6554bd789a5dab1ecc5f9a52ae56e23b78fcb1f6e50b357353ce9460`); Optimized has same Rust SHA. Sidecars: Debug `cf0af98b204bef3bd0bb407fff52833295d7fdd10cd76c11523f772a4ac387aa`, Optimized `a361621551f3e1f1e19a904e40bfa3cc3d33bcf779b49bfc80ec5a9843e4102c`. | `./moss type values --source <fixture> --json` resolves `vector[int]` (inferred); iteration item `value: Int`. Fixture SHA `fa78608fa7ca2c638541fce0b8be3fddbd17626f7790d1368834efade9844e05`. |
| ORACLE-59 `oracle_59_shadowed_binding_identity.moss:11:3–26`, `for double_val in [101]:` | Debug `ORACLE-59-SHADOWED-BINDING-IDENTITY.rs:3464:45–54` (SHA `7af49587435bc635a2a13b6cca190e16c8568fcbcad643384531ff4eecd756b4`); Optimized has same Rust SHA. Sidecars: Debug `46ae3552a9c5f7adf202b193df0837076e6bb2d08cf3b616c2ae3376ff0d28a9`, Optimized `978ae96e41f93ca1d067be40f87f56cc4be2c41cba880704f83f55614a7f71b0`. | `./moss type double_val --source <fixture> --enclosing main --json` resolves the line-11 binding as `int` (inferred); collection element is the literal `101`. Fixture SHA `382d35151bc9ea60a49fdf03716739d63189b34f0e7501e036639f776be5f0fa`. |

### Other scanned alternatives

A recursive scan of all 84 generated `*.rs` files found zero `.cloned()`, `.to_vec()`, `.to_owned()`, and `Clone::clone` occurrences. This is limited to these emitted artifacts.

The two `String::from` occurrences are Debug/Optimized outputs for ORACLE-20. Source `oracle_20_take_nested_map_vector.moss:9:13–23` uses the literal key in `it = take(m["first"].items)`; generated `ORACLE-20-TAKE-NESTED-MAP-VECTOR.rs:3493:52–73` materializes `String::from("first")` for `.get_mut(...)`. `./moss type m --source <fixture> --json` resolves `map[string,Bucket]`. Debug artifact SHA-256: `0b66dfa556f6456974ca27f3a7a73998b58f05ad4be99ef5f26fb1eb0e122c3a`; Optimized has the same Rust SHA. Sidecars: Debug `54ccafce628d0f71ab8870c33fe5cadc832fc325821740b0d6162de5a46a0956`, Optimized `47f66693b350c7f5616783bf06665e0c19e2ea0a13d5711b2fd14534f3de40ab`. Fixture SHA `5d54e3f0afc85d57c0d0f66813ca77f38a9a476f67f68bfa086f8915926652ac`. This constructs a temporary lookup key from a literal, not a stored Moss key/value clone; no `TypedCopyFactV1` row applies.

The `mem::replace` and `mem::take` totals remain as listed above: runtime `replace` sites reset scalar state or transfer a pointer; source Moss `take(...)` sites lower only after `resolve_take_fact` finds a checked `TypedTakeFactV1` (see `src/native_generator.cpp:8852–8904, 9279+`), so those are transfers.

## Result and limits

- The two semantic-copy rows have exact checked `TypedCopyFactV1` provenance.
- The four domain-capture rows have exact Root-ingress facts and explicitly set `permits_moss_value_copy=false`.
- All 252 direct runtime `.clone()` rows and all 588 `Arc::clone` rows are still unclassified/unapproved by the provenance contract. This report does not add a runtime exception or registry entry.
- No compiler source fix is proposed: no Moss payload duplication was observed at these exact three marked runtime templates in the inspected artifacts. This is not proof about macro expansion, Rust MIR/Copy loads, unobserved call paths, or other builds, and it does not establish blanket runtime safety.
- The audit is based on generated source and sidecars, not Rust MIR; macros and unobserved call paths are outside its proof. Candidate operation classifications are limited to the exact 84 artifacts and source SHA above.

No oracle expectation, compiler source, shared runner, or clone registry was modified for this audit.

Team-efficiency metrics are not duplicated for this continuation; see the existing [Team B closeout record](agent-patterns/team-b-w08-w11.md). This audit report adds source/artifact evidence only and does not infer new team headcount, review, or usage metrics.
