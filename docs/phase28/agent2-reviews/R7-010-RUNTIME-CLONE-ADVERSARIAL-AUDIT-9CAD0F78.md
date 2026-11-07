# Agent 2 adversarial review: runtime clone sites at 9cad0f78

Date: 2026-11-06
Reviewed tree: `tmp/phase28-r7-010-vertical-integration`, branch `phase28-r7-010-vertical-integration`
Exact HEAD: `9cad0f78bea157ffee1a3838fdd41373f95dfa5d`
Verdict scope: source and existing test evidence for Team C's ten `executor_runtime.cpp` sites. This is not an exception approval, full Phase 28 verdict, or authorization to add registry entries.

## Method and pinned inputs

Read-only review. The source hashes reported by Team C match this tree:

| File | SHA-256 |
|---|---|
| `src/executor_runtime.cpp` | `3af71438b396ad0303eda7a1844902d2802c27bff8a3cdd18e8d6e82f5d22690` |
| `src/fileio_runtime.cpp` | `d46fc2b25db17fd62ebd7951a826ef201e94efc2804e210c1570f07c2ef918a6` |
| `src/file_chunk_codegen.inc` | `30b29209b4f2974cc147647d0edfb5fd056145d7aa93305252761e2b40fed92a` |

Commands used to pin and inspect evidence included:

```sh
git -C tmp/phase28-r7-010-vertical-integration rev-parse HEAD
git -C tmp/phase28-r7-010-vertical-integration status --short
rg -n 'r7-010-runtime-(01|02|03|04|05|06|08|09|10|11)|provider_refcount_exact|provider_refcount_stress|affinity\(' tmp/phase28-r7-010-vertical-integration/src/executor_runtime.cpp tmp/phase28-r7-010-vertical-integration/tests/tooling/check_phase20c_executor.py tmp/phase28-r7-010-vertical-integration/src
sed -n '1160,1238p' tmp/phase28-r7-010-vertical-integration/tests/tooling/check_phase20c_executor.py
sed -n '1688,1770p' tmp/phase28-r7-010-vertical-integration/tests/tooling/check_phase20c_executor.py
```

No build or test was run. Existing worktree dirt was observed and left untouched. The frozen clone registry is empty per the supplied Team C report, so no row below grants an exception. A marker is a locator only.

## Per-site disposition

“Site evidence” below means evidence sufficient to classify this exact operation's mechanism and relevant ownership path. It does **not** mean the site has been authorized. An `INSUFFICIENT` verdict means a material lifetime/count edge is asserted in source but lacks direct test evidence. No finding here establishes a Moss-language copy at an Arc site.

| Site | Exact operation and adversarial result | Exact-site evidence verdict | Missing test / Moss-copy risk |
|---|---|---|---|
| runtime-01 `executor_runtime.cpp:497` | `Option<Arc<MossGlobalExec>>::clone()` returns an owned executor handle after `lock_inner()`'s guard is dropped. Arc shares queues/closures; it does not clone a Moss payload. | **INSUFFICIENT** for exact lifetime provenance: source purpose is clear, but no strong-count/liveness test pins this slot snapshot. | Add an interleaved publish/join test that observes executor lifetime through slot replacement and final owner release. Queued Moss captures can be retained longer, not copied. |
| runtime-02 `:857` | `self.affinity.clone()` deep-clones `Option<Vec<usize>>` to make `MossExecutor::config(&self)`'s owned snapshot. The vector is populated by `.affinity(Vector[Int])`; lowering converts Moss integers into runtime `usize`s. The checked Moss config-method allowlist (`src/moss.cpp:1153`; validation/codegen in `src/executor_invoke_lowering.inc` and `src/executor_invoke_codegen.inc`) excludes `config`, while the Moss chain emits `.start()`. | **NO-GO** for Team C's broad “Moss-value clone disproven” claim at this boundary. This is a real deep clone of source-derived config data, although current checker/codegen evidence does not expose `.config()` to Moss or show a general Moss collection/record copy. | Keep this explicitly classified as a Rust runtime config snapshot; add a test for snapshot independence after changing/consuming a builder and retain a negative Moss-callability check. Risk is a mistaken broad `Vec::clone()` exception, not demonstrated implicit Moss copying in the checked path. |
| runtime-03 `:1407` | FFI clone borrows a live raw scope without changing its count, calls `MossBranchScope::clone()` (Arc increment), then transfers that owned count via `into_raw_owned()`. The `Clone` impl at `:1696-1700` calls this FFI function. | **GO** for mechanism/lifetime classification. Test 39 directly calls the raw ABI clone/drop, checks identical pointer and strong counts (1→2→2+queued→1→0), repeats under INLINE, saturated ACTIVE, free-worker stress, and checks all scopes released. | Preserve exact ABI test and bind the reviewed source site/type in any future provenance record. The clone shares `MossBranchScopeState`; it does not copy a Moss payload. No exception is authorized here. |
| runtime-04 `:58` | `Arc::clone(&node)` creates the tail-owned raw Arc while the guard keeps its own Arc. A successor takes the predecessor's raw count; guard drop removes the tail-owned count only when its CAS wins. `MossFairNode` contains only atomics/pointer state. | **INSUFFICIENT**: the two-owner transfer and both source branches are visible, but there is no direct node count/drop assertion for CAS-win versus successor takeover. | Add deterministic tests for both tail-CAS outcomes and exact node destruction/FIFO wake order. No Moss payload is in the node. |
| runtime-05 `:152` | Condvar wait creates a local Arc and queues a second Arc under the fair guard; notify pops/drains the queue owner while the waiter-local owner survives through park/relock. `MossFairWaiter` contains a thread handle and atomic flag. | **INSUFFICIENT**: notify/park source explains ownership, but no per-waiter Arc/drop count hook was found. | Add deterministic `notify_one`/`notify_all`, spurious/unrelated unpark, and drop-count coverage. The cloned value is waiter bookkeeping, not Moss payload. |
| runtime-06 `:438` | Ready queue stores `Arc::clone(scope)`; dequeue transfers/drops it, while join removes stale entries and handles the in-transit repush race. Arc may prolong scope/closure lifetime. | **GO** for mechanism and queue-reference behavior. Test 39 asserts the additional queued count and count after join/removal; tests 42/46/47 cover queue bound, saturation cleanup, and repush race. | These tests support this site but do not create provenance authorization. Arc shares the scope and its closures; this does not duplicate their captured Moss values. |
| runtime-08 `:733` | Worker thread closure owns a cloned `Arc<MossGlobalExec>` inside `MossWorkerThreadGuard` until worker exit; a rejected spawn closure would also drop its capture. | **INSUFFICIENT**: worker/join behavior is exercised, but no exact worker-Arc drop/count evidence for normal exit, unwind, or spawn failure was found. | Add a weak/count lifetime test over those exits (spawn-failure injection if practical). The captured object is executor state, not a Moss value. |
| runtime-09 `:919` | Start stores a cloned Arc in the process active slot while retaining local ownership for worker startup and returned handle. | **INSUFFICIENT**: state/race tests exist, but no exact count assertion covers local + active-slot + workers through teardown. | Add an exact startup/join owner-count check, including rejected concurrent start. The Arc clone shares executor state. |
| runtime-10 `:1089` | `Arc<Mutex<Option<R>>>` is cloned into the `MossWork` closure; closure writes `Some(res)` once and caller takes it once. `R: Send + 'static` has no `Clone` bound. | **GO** for ruling out an `R`/Moss-reply clone at this operation: type bounds and write/take code establish that only the Arc is duplicated and the reply moves. | A non-Clone reply with a Drop counter would improve exactly-once lifecycle evidence under panic/delay/concurrency; existing tests do not make this a Moss-value copy. |
| runtime-11 `:1116` | Active executor Arc is cloned under process lock, then guard is dropped before potentially blocking admission. On DRAINING, work is restored and the ingress loop retries. | **INSUFFICIENT**: lock boundary and source path are clear; no exact Arc lifetime test covers transition between snapshot and admission. | Add a deterministic draining-before-admission interleaving with progress and final-drop assertions. Shared executor state only; no Moss payload clone. |

## Reconciliation of the highlighted claims

* **runtime-02:** The precise claim is narrower than “no Moss-origin data”: it deep-clones a Rust-owned `Vec<usize>` whose elements can derive from Moss affinity input. The checker exposes only the five config operations `threads`, `max_threads`, `queue_capacity`, `affinity`, `priority`; native lowering converts affinity elements and emits `.start()`, not `.config()`. Thus no checked Moss call path to this clone was found, but an origin-independent / generic non-Moss classification would overstate the evidence. Do not use this site to authorize other `Vec::clone()` sites.
* **runtime-03:** The ABI clone claim is supported by test 39, not merely inferred from marker comments: test code calls `__moss_branch_scope_clone` and exact count assertions bracket clone, publish, join, and drop. The wrapper `Clone` implementation routes through that ABI function.
* **runtime-06:** Test 39's `queued` argument explicitly models zero ready-queue owners in INLINE and one in the one-worker ACTIVE case. It checks the queue count before and after join; test 39 also has free-worker stress and leak assertions. Tests 42/46/47 are separate queue safety/race coverage. These are substantive behavior checks for the Arc owner, but they are not a registry binding or exception.
* The other seven Arc operations' types strongly indicate handle/state sharing, but Team C's own report identifies missing per-site lifetime counters for the exact paths. That is a real evidence gap for lifetime claims, not evidence of hidden Moss copies.
* The report's ten-site scope excludes `fileio_runtime.cpp:1293` and `file_chunk_codegen.inc:238`. This review does not classify or approve those additional Arc sites.

## Overall verdict

**No runtime site is approved or registry-ready by this review.** Runtime-03 and runtime-06 have good direct ownership-count evidence; runtime-10 has strong type/flow evidence that the reply itself is moved; runtime-02 requires explicit boundary treatment because it copies source-derived runtime configuration. The remaining sites have source-supported mechanical-Arc explanations but need the listed exact lifetime tests before treating their full provenance/lifetime claims as closed. I found no demonstrated implicit nonprimitive Moss copy among the nine Arc sites. This verdict does not clear the broader runtime clone inventory, authorize exceptions, or imply full Phase 28 acceptance.
