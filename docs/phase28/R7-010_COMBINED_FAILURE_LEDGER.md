# Phase 28 R7-010 Combined Oracle Failure Ledger

# Final L7 candidate run (2026-11-05)

- Candidate branch `phase28-r7-010-l7-copy-authority` at
  `570ad802f0f887311f141dd4b9e5b7e83ae369b2`. Implementation reviewed GO at
  `94f6d1fe460fe0581ae75af414a0086dc7012484`; final Agent 2 registration
  review GO at `570ad802`. No merge to `phase28-work` or `main`.
- **Frozen v2 oracle:** 19/60 pass all applicable gates; 60/60 semantic PASS;
  0 cross-engine disagreements; 41/60 fail only clone provenance. Exact
  `results.json` and generated ledger are in
  `tmp/r7-010-570ad802-oracle/`. All 252 repeated direct runtime `.clone()`
  rows are reported UNAUTHORIZED against the unchanged v2 registry. This is an
  authorization result, not proof of 252 distinct semantic copies. The fresh
  scanner reports 84 generated Rust artifacts and 1,960 lexical duplication
  candidates across 37 groups. All 252 direct `.clone()` rows map to three
  repeated runtime template source sites (100% source-site-root-caused, 0/3
  sites removed or authorized); the exact-expression locator gate passes but
  authorizes none. Eight qualified `Arc::clone` sites and other duplication
  operations still need independent source/type/authority analysis.
- **Runtime source-site root cause (L7 source walk; authorization remains NONE):**

  | Site | Source | Rust value whose ownership count/value is duplicated | Purpose |
  |---|---|---|---|
  | 01 | `executor_runtime.cpp:497` | `Arc<MossGlobalExec>` | Return active executor ownership after releasing process-runtime mutex. |
  | 02 | `executor_runtime.cpp:847` | `Option<Vec<usize>>` | Return an owned `MossExecutorConfig` snapshot from `config(&self)`. |
  | 03 | `executor_runtime.cpp:1397` | `Arc<MossBranchScopeState>` through `MossBranchScope` | Produce a new owned FFI reference from a borrowed raw scope handle. |
  | 04 | `executor_runtime.cpp:58` | `Arc<MossFairNode>` | Retain the node for the lock queue's raw tail pointer. |
  | 05 | `executor_runtime.cpp:152` | `Arc<MossFairWaiter>` | Keep a queue-owned waiter while the waiting thread retains its handle. |
  | 06 | `executor_runtime.cpp:438` | `Arc<MossBranchScopeState>` | Keep a ready-queue entry alive across asynchronous Branch work. |
  | 07 | `executor_runtime.cpp:643` | `Arc<MossGlobalExec>` | Keep executor state alive in the worker context guard. |
  | 08 | `executor_runtime.cpp:723` | `Arc<MossGlobalExec>` | Transfer executor ownership into the worker thread closure. |
  | 09 | `executor_runtime.cpp:909` | `Arc<MossGlobalExec>` | Retain active executor ownership in process runtime state. |
  | 10 | `executor_runtime.cpp:1079` | `Arc<(Mutex<Option<R>>, Condvar)>` | Share root result storage between caller and queued worker closure. |
  | 11 | `executor_runtime.cpp:1106` | `Arc<MossGlobalExec>` | Keep executor alive after dropping ingress lock and before admission. |

  The source and type mapping accounts for the 3 direct rows and 8 qualified
  Arc sites. Independent Agent 2 audited these at checkout
  `7fb7122c860628e3ec89f38da38045f418c73214` (runtime source SHA-256
  `c84a1391e7fb4355d48f204578e1192e68dc69deb50bf829fe3b2497cc1ecda1`): all
  11 are Rust runtime scaffolding sharing handles/config across lifetimes; for
  site 10 only the `Arc` to the reply cell is shared, not payload `R`. The
  reviewer explicitly authorizes none under frozen v2. Two issues remain
  unresolved: no generated caller was located for the site 03 clone API, and
  site 07's TLS Arc duplicates the worker-slot Arc with necessity unproven.
  The source/type mapping does not by itself prove every surrounding safety
  invariant. A durable independent record is requested; no clone is authorized.
- **Regression suites:** candidate Default and EST-shadow each report 87/167
  PASS, 79 FAIL, 1 SKIP. Logs are
  `tmp/r7-010-f388ee2a-default.log` and
  `tmp/r7-010-570ad802-shadow.log`; the only intervening source-neutral change
  sets explicit default/shadow environment for the Makefile gate. At baseline
  `phase28-work` SHA `150ab36b41e45504aaa400aa717da241be7542fb`, Default was
  166/167 PASS with one `check_phase21_borrowed_view_write` failure; Shadow
  was 165/167 PASS with that same failure plus the known `shard_swarm_cases`
  `put(take())` structural divergence. Baseline logs are under
  `../phase28-r7-010-phase28-work-baseline/tmp/`. Candidate failures show
  widespread earlier-phase regressions versus both baselines. The Phase 25.4
  performance baseline also fails two pinned fixture SHA checks. The new
  ReturnValue gate passes separately in both modes on the test compiler,
  including all 6 Reply corruption hooks and Native Debug/Optimized/Fast Debug
  parity. It is registered in Makefile focused test-build gates, not the
  production-style parallel shards whose isolated compiler disables hooks.
- `make check` stops at the known `take_map_field` failure (17/18 take cases);
  the same Fast Debug nonescaping CollectionView invariant reproduces on the
  `phase28-work` baseline compiler. This is not a full-suite result.
- Frozen v2 digests are unchanged: expectations
  `7ca0d550e38f33c47105f57e22f5993d23449bf09c1cd5da043de924657f23a2`, runtime
  registry `1b4cc2f59ddaec51149f84468b098c53772815c3ca64e858febd976e53a6e19d`,
  certification `00e9d648286a90de7266622f18dad6f2b53693c5b3645cb37cb2b30bb4c4ccc9`.
- **NO-GO:** semantic oracle and engine parity are clean, but runtime clone
  authority is unresolved and the full suites have 79 failures each. Baseline
  default/shadow comparisons and the broad independent final architecture GO
  remain required. No clone marker or runtime site is authorized by this run.

# Fresh marker-build oracle run (2026-11-05)

- **Implementation base:** `a8abbe32b98971e6d0470a167369123d39b77598`
  plus uncommitted runtime-site locator comments (source SHA-256
  `c84a1391e7fb4355d48f204578e1192e68dc69deb50bf829fe3b2497cc1ecda1`).
- **Run artifacts:** `tmp/r7-010-oracle-marker-final/results.json` and
  `tmp/r7-010-oracle-marker-final/failure-ledger.md`.
- Fresh 60-fixture result: **19/60 all applicable gates PASS**; **60/60
  semantic PASS**; no Fast Debug/Native Debug/Native Optimized disagreements;
  no infrastructure failures; **41 fixtures fail only clone provenance**.
  There are **252 direct runtime `.clone()` occurrences**, all reported
  unauthorized under the unchanged frozen v2 registry. The 41 failures are
  provenance blocks, not 41 confirmed Moss semantic copy defects.
- The ORACLE-10A current output has one checker-authorized explicit copy and
  three unauthorized/unclassified direct runtime `.clone()` rows. The
  independent locator gate identifies 11 runtime source sites total (3 direct
  `.clone()`, 8 `Arc::clone()`), but it does not authorize any. Qualified Arc
  calls remain outside the per-artifact v3 `.clone()` sidecar bijection and
  require separate successor audit coverage.
- Independent scanner inventory over the 84 generated debug/optimized Rust
  artifacts: **1,960 duplication candidates across 37 site groups** — 258
  `.clone()`, 672 qualified `Arc::clone()`, 756 `.to_string()`, 252
  `mem::replace`, 14 `mem::take`, 6 `.copied()`, and 2 `String`/`Vec::from`.
  This is a lexical inventory, not 1,960 confirmed Moss-value copies; all
  groups remain subject to source/type/authority mapping. The six direct clones
  beyond the 252 repeated runtime clones are explicit-copy materializers.
- The independent reviewer returned **GO for the marker gate as a
  non-authoritative locator only** and **NO authorization for every runtime
  site**. The marker-stripped ORACLE-10A artifact digest matches its pre-marker
  baseline. Durable review is
  `docs/phase28/agent2-reviews/R7-010-executor-runtime-3711.md` (review commit
  `e2a7da7ca8597a61ab382e272d3fc299756805f0`). Frozen v2 expectation, registry
  and certification digests remain unchanged.
- Targeted validation: isolated `moss-dev` build PASS; runtime evidence gate
  PASS including missing/duplicate/string-decoy, ID-swap and relocation
  mutations; oracle and clone-inventory unit tests **46/46 PASS**; `git diff
  --check` PASS.
- **Disposition remains NO-GO.** Remaining acceptance work includes a reviewed
  separately versioned provenance successor, complete duplicate-operator
  audit, full default and EST-shadow regression suites, and final independent
  architect GO. Do not change frozen oracle expectations or merge to
  `phase28-work`/`main`.

- **Implementation SHA:** `5120cfbf85d7eb7b274d6c25532dd84e72c3877e`
- **Branch:** `phase28-r7-010-l7-copy-authority`
- **Specification:** certified frozen R7-005 v2 expectations; no expectation or digest changes.
- **Run artifacts:** `tmp/r7-010-oracle-5120cfbf/results.json` and `tmp/r7-010-oracle-5120cfbf/failure-ledger.md`.

## Category summary

| Category | Result |
| --- | ---: |
| Fixtures | 60 |
| All applicable gates passed | 19 |
| Fixtures with at least one failed gate | 41 |
| Semantic/checker failures | 0 |
| Native/Fast Debug or Native Debug/Optimized disagreements | 0 |
| Clone provenance failures with sidecars | 40 fixtures |
| Fixtures blocked by missing/invalid sidecars | 1 |
| Infrastructure failures | 0 |

All semantic statuses passed. Every fixture that completed execution had the expected Fast Debug, Native Debug, and Native Optimized result, including expected panics. ORACLE-54's checker, imported-effect, and three execution outcomes passed; only its four missing clone sidecars blocked provenance.

## Clone provenance detail

The run observed **504 unverified clone occurrences**: 480 unclassified occurrences covered by emitted provenance sidecars (six in each Native Debug and Native Optimized artifact across 40 fixtures), plus 24 occurrences in ORACLE-54's four missing sidecars (six per consumer/provider artifact per mode). The 480 count is not 480 proven Moss-value copies; they remain unclassified runtime-template clone occurrences and block acceptance. The 24 missing-sidecar count is reported separately.

Four Root-ingress routing-handle clones are now explicitly accounted by exact checker capture facts: one each in ORACLE-43 and ORACLE-45, in Debug and Optimized output. The authority remains `permits_moss_value_copy=false`. The capture rows do not authorize Moss-value copies. Six unclassified clone occurrences recur in each accepted artifact/mode. Their grouping into distinct current emitter sites remains to be refreshed and independently reconciled; the prior inventory grouped the pattern into six runtime-template sites. No mechanical/runtime exception has been added.

## Fixtures blocked by unclassified runtime-template clones

  - ORACLE-01-VEC-WRITE-THROUGH
  - ORACLE-02-INDEXED-FIELD-READ-NO-CLONE
  - ORACLE-04-MAP-WRITE-THROUGH
  - ORACLE-05-MAP-GET-BORROW
  - ORACLE-06-MATCH-CONSUME-OPT-NATIVE
  - ORACLE-07-CATCHABLE-COLL-ERRORS
  - ORACLE-09-USER-ENUM-SOME-NONE
  - ORACLE-10A-EXPLICIT-COPY-ONLY
  - ORACLE-11-ASSIGN-READ-BORROW
  - ORACLE-12-ASSIGN-WRITE-BORROW
  - ORACLE-13-ASSIGN-CONSUME-RETURN
  - ORACLE-16-TAKE-FRONTIER-ARCHIVE
  - ORACLE-17-TAKE-OPTION-FIELD
  - ORACLE-18-TAKE-QUEUE-FIELD
  - ORACLE-19-TAKE-NESTED-VEC-OPTION
  - ORACLE-20-TAKE-NESTED-MAP-VECTOR
  - ORACLE-25-TAKE-BEFORE-RAISE-MUTATION
  - ORACLE-26-VEC-REMOVE-API
  - ORACLE-27-REBIND-AFTER-BORROWS
  - ORACLE-29-LOOP-CARRIED-OWNERSHIP
  - ORACLE-31-JOIN-PROMOTED-CONSUME
  - ORACLE-32-MATCH-PAYLOAD-WRITE
  - ORACLE-33-MATCH-LAST-USE-OVERWRITE
  - ORACLE-36-MATCH-CONSUME-QUEUE-POP
  - ORACLE-38-MAP-VALUES-BORROWED-VIEW
  - ORACLE-39-VEC-FOR-READ
  - ORACLE-40-VEC-FOR-WRITE
  - ORACLE-42-CUSTOM-WRAPPER-REMOVE
  - ORACLE-43-HANDLER-PRIMITIVE-REPLY
  - ORACLE-45-HANDLER-PRIMITIVE-REBIND-LOCAL
  - ORACLE-47-VECTOR-INDEX-PANICS
  - ORACLE-49-OWNERSHIP-AT-RECOVER
  - ORACLE-50-EAGER-PIPELINE-CAPTURES
  - ORACLE-51-JOIN-BORROWED-PROVENANCE-SETS
  - ORACLE-53-MAP-REMOVE-MISSING
  - ORACLE-55-COLLECTION-ERROR-INFERRED-RAISE
  - ORACLE-56-PRIMITIVE-VECTOR-ELEMENT-READ
  - ORACLE-57-MAP-GET-WRITE-THROUGH
  - ORACLE-58-SPECIALIZATION-IDENTITY-COLLISION
  - ORACLE-59-SHADOWED-BINDING-IDENTITY

Each listed fixture has zero semantic failures and passes its checker and execution expectations; provenance fails on six unclassified runtime-template clones in both Native Debug and Native Optimized output.

## Missing provenance

- **ORACLE-54-IMPORTED-MOSSI-EFFECTS:** consumer/provider sidecars are missing for Native Debug and Native Optimized output. Each missing sidecar contains six unverified clone occurrences, totaling 24. Do not classify these as infrastructure failures or silently authorize them.

## Disposition

**NO-GO.** The combined compiler is semantically conformant on this oracle run, but the 480 unclassified clone rows and 24 missing-sidecar occurrences remain blocking provenance gaps. The 19/60 overall pass count reflects provenance acceptance, not 41 semantic defects. Next work: refresh the independent grouping by actual emitter site, establish exact authorization or eliminate Moss-value duplication, and restore complete ORACLE-54 consumer/provider provenance. Do not alter frozen expectations.

## Focused follow-up after implementation `a07a03e8` (not a full oracle rerun)

- Single-source explicit-module artifacts now receive their own v3 provenance sidecars, bound to the module source and full checked Program facts. Multi-source modules still omit the sidecar and remain fail-closed because v3 currently has a single-source digest contract.
- The targeted ORACLE-54 project rerun now reports sidecar-backed `UNAUTHORIZED` rows for each runtime clone instead of `PROVENANCE_MISSING`: 5 unclassified/unauthorized clones in each of four provider/consumer Debug/Optimized artifacts. Checker, imported `.mossi` raise effects, Fast Debug, and Native Debug/Optimized outputs all pass. This is a focused result, not a refreshed 60-fixture total.
- Removed the worker-spawn affinity `Vec<usize>` clone by borrowing the affinity list through the executor `Arc` already owned by the worker closure. `check_phase20c_executor.py` passes all 54 runtime tests. Independent Agent 2 verified the affinity clone is absent from generated Rust.
- Independent Agent 2 re-audited the changed runtime template at source SHA-256 `e8a06b21315910792200c820d6777f005b91986379517db03d1fec28855e9ebc`: five sites remain. Four clone executor/branch `Arc` handles; one clones an owned config snapshot including its affinity vector. Verdict remains NO-GO to authorize/allowlist any of the five: no checker `copy(x)` authority or approved runtime exception exists, and the config snapshot duplicates data.
- Oracle unit and inventory gates pass 46/46. A fresh complete 60-fixture run is still required after implementation changes; the prior `5120cfbf` counts are stale for this tree.

### Refreshed executor runtime callsite audit

Independent Agent 2 pinned the updated source `src/executor_runtime.cpp` to
SHA-256 `e8a06b21315910792200c820d6777f005b91986379517db03d1fec28855e9ebc`
and generated a minimal artifact with SHA-256
`e20f94bab26b4482300431ee5c0f34420e188b71c90388eb4d7ec02c8534a059`. The
removed affinity clone is absent from both. Remaining generated Rust lines map
to these exact C++ source lines:

| Rust line | C++ line | Type / behavior | Review disposition |
|---:|---:|---|---|
| 2014 | 497 | `Option<Arc<MossGlobalExec>>`, process active-executor handle | Not authorized |
| 2356 | 839 | `Option<Vec<usize>>`, owned executor config snapshot | Data duplication; not authorized |
| 2906 | 1389 | `MossBranchScope`, raw-ABI Arc reference count | Not authorized |
| 2948 | 1431 | `Option<Arc<MossGlobalExec>>`, worker TLS handle | Not authorized |
| 3111 | 1594 | test/perf config snapshot including affinity vector | Not authorized |

Agent 2's refreshed verdict is NO-GO to allowlist any of the five. The four
handle sites have no approved runtime exception; the config snapshot also
copies owned vector data. All five lack checker `copy(x)` facts. This table is
source/artifact evidence, not authorization.

## Fresh combined 60-fixture oracle — implementation `cd2310ef`

- Run used the isolated `moss-dev` rebuilt from exact implementation commit
  `cd2310ef` on `phase28-r7-010-l7-copy-authority`. Oracle expectations and
  certification files/digests remain unchanged. Raw results and fixture ledger
  are in disposable `tmp/r7-010-oracle-cd2310ef/`.
- **19/60 fixtures pass all applicable gates; 41/60 fail only provenance.** All
  checker decisions pass; all applicable Fast Debug and Native Debug/Optimized
  semantic outputs match the frozen expectations. Semantic failures: 0.
  Native/Fast Debug or Debug/Optimized disagreements: 0. Infrastructure
  failures: 0. Missing or invalid sidecars: 0.
- Across audited artifacts, 426 `.clone()` occurrences are observed: 2 linked
  to exact checked Moss `copy(...)` facts and 4 linked to checker-issued
  Root-ingress capture-only facts (`permits_moss_value_copy=false`). The
  remaining **420 occurrences are unclassified/unauthorized**, five repeated
  runtime sites per artifact. This comprises 400 rows in direct fixture
  artifacts and 20 rows in ORACLE-54's four provider/consumer artifacts.
- The affinity vector site removed in this implementation is absent. The
  remaining five sites are independently reviewed but not authorized, as
  recorded above. Disposition remains **NO-GO**; no oracle expectations were
  modified to obtain this result.

## Local executor test-config follow-up (after `cd2310ef`, not a full run)

- Independent Agent 2 reviewed the test-only `moss_executor_active_config_matches`
  borrowed comparison as GO. It removes the cfg(test/moss_perf) config snapshot
  clone while preserving the documented `MossExecutor::config(&self)` API.
  The Phase 20 executor gate passes 54/54, including a new check that config
  inspection can be followed by builder reuse and `start()`.
- Targeted ORACLE-10A and ORACLE-54 audits now show four unauthorized direct
  `.clone()` rows in each generated artifact. The remaining source sites are
  lines 497, 839, 1389 and 1431 of `executor_runtime.cpp`; line 839 remains to
  preserve the documented owned snapshot API. A minimal generated artifact
  also contains nine qualified `Arc::clone(...)` calls, which are outside the
  current v2 `.clone()` occurrence sidecar schema and remain unaudited here.
- The full 60-fixture result recorded above is bound to `cd2310ef` before this
  local change; no complete oracle rerun or new corpus total is claimed for the
  current working tree.

## Fresh combined 60-fixture oracle — implementation `fc305b645e96f46edadbd75b3023f4ef051ba590`

- Run used the rebuilt compiler from exact staging implementation SHA
  `fc305b645e96f46edadbd75b3023f4ef051ba590`; certified v2 expectations and
  certification digests were unchanged. Results and raw artifacts are in
  `tmp/r7-010-oracle-fc305b64/`.
- **19/60 fixtures pass all applicable gates; 41/60 fail only clone
  provenance.** All semantic statuses and applicable execution results pass;
  there are zero engine disagreements, infrastructure errors, or
  missing/invalid sidecars.
- The refreshed scanner reports 336 unverified generated `.clone()` rows,
  all tied to provenance sidecars and currently unclassified/unauthorized: four
  per each direct artifact and four per each of ORACLE-54's four
  provider/consumer Debug/Optimized artifacts. This is a measured reduction
  from the earlier 420 rows after the test-only config snapshot clone removal.
  Exact checked Moss copies and Root-ingress capture-only rows remain
  separately accounted; neither permits a runtime clone.
- Commit `fc305b64` also removes the worker-thread guard's redundant
  `Arc::clone`: the thread guard takes ownership of the captured executor Arc,
  and setup/worker-loop borrow it while the guard remains live.
  `check_phase20c_executor.py` passes 54/54. A current ORACLE-10A generated
  probe contains eight `Arc::clone` calls, one fewer than the previous
  nine-site probe; this qualified-call inventory is outside the certified v2
  `.clone()` sidecar count. Independent review of this exact change is pending.
- The candidate-specific provenance builder and corruption test passes with
  one checked copy and four unclassified direct clone rows. The historical
  Team C runtime-evidence script rejects this changed source hash before
  reusing its pinned artifact; its historical evidence is unchanged.
- Disposition remains **NO-GO**. The 336 provenance rows and the wider
  qualified duplication audit remain unresolved. Full default and EST-shadow
  suites have not run at this SHA.

### Refreshed generated-artifact duplication inventory at `fc305b64`

- Re-scanned the exact oracle artifact directory with
  `check_phase28_r7_010_clone_inventory.py`: 84 artifacts, 2,044 candidate
  operations, and 38 lexical site groups. The JSON and Markdown outputs are
  disposable under `tmp/r7-010-oracle-fc305b64/`.
- Operator counts: `dot_clone` 342 (336 runtime rows plus six Moss-marked
  rows, of which two are checked `copy` and four are Root-ingress
  capture-only); qualified `Arc::clone` 672 (eight runtime sites repeated in
  84 artifacts); `dot_to_string` 756; `mem_replace` 252; `mem_take` 14;
  `dot_copied` 6; and String/Vec constructors 2. The scanner also sees 408
  generated Moss-type `Clone` derives; these are capabilities, not invocation
  evidence.
- The counts are operation/site observations, not counts of confirmed illegal
  Moss copies. Existing Team C analysis classifies the listed Arc occurrences
  as runtime handles; the current sidecar scanner omits them, so they remain a
  provenance-completeness blocker until the independent audit and successor
  evidence path are complete. Other APIs and `Copy` behavior remain subject to
  the scanner's documented limits.

## Fresh combined 60-fixture oracle — implementation `3711f2ab8ac45357b6dd6c993dc6e45be9d21884`

- Run used the rebuilt compiler from exact staging SHA
  `3711f2ab8ac45357b6dd6c993dc6e45be9d21884`; the certified v2 expectation
  table and all digest pins are unchanged. Results and artifacts are in
  `tmp/r7-010-oracle-3711f2ab/`.
- **19/60 fixtures pass all applicable gates; 41/60 fail only clone
  provenance.** All 60 semantic statuses and applicable execution expectations
  pass. Semantic failures, engine disagreements, infrastructure errors, and
  missing/invalid sidecars: zero.
- The v2 scanner reports **252 unverified/unauthorized direct `.clone()` rows**
  (three per artifact). The current static inventory covers 84 Rust artifacts
  and reports 1,960 candidate operations in 37 site groups: 258 direct
  `.clone()` calls, 672 qualified `Arc::clone` calls across eight runtime
  sites, 756 `.to_string()`, 252 `mem::replace`, 14 `mem::take`, six
  `.copied()`, and two String/Vec constructors. The 408 Clone derives are
  capabilities, not invocation evidence. Exact scan output is under the
  disposable oracle directory.
- Independent Agent 2 returned **GO** for the source repairs at exact SHA
  `3711f2ab8ac45357b6dd6c993dc6e45be9d21884` (executor source SHA-256
  `1682c206d93efa3b26b697a71283b634e16c04210cafaceb27a03792a006a984`). The
  TLS borrow does not escape its synchronous callback; `Executor.start` moves
  its config through `into_config`, preserving public `config(&self)`. The
  55/55 executor gate and repeated targeted probes pass.
- The reviewer finds the three direct runtime sites (active executor Arc,
  public host config snapshot, and BranchScope handle) and eight qualified Arc
  sites are potentially classifiable only through a reviewed successor
  registry. The config snapshot is runtime-support eligible in principle now
  that Moss startup does not call it; the frozen v2 registry remains empty, so
  **none is currently authorized**. Qualified Arc calls are still omitted
  from the v2 occurrence sidecar scan. A durable reviewer report is pending.
- Disposition remains **NO-GO**. The 252 v2 sidecar rows and eight qualified
  clone sites are not covered by the frozen provenance contract. Full default
  and EST-shadow suites have not run at this SHA.

## Worker-context TLS Arc clone removal — pending freeze

- Candidate based on staging commit `d92e6dcf42069daee3e08e064099e79a7f0ad4f2` replaces the TLS-owned `Arc<MossGlobalExec>` with a synchronous borrowed pointer to the worker-slot guard's Arc. `MossWorkerContextGuard<'a>` carries the Arc borrow lifetime and is made `!Send`/`!Sync` with `PhantomData<Rc<()>>`; Drop clears TLS before the worker-slot guard can end. `with_current_worker_exec` null-checks and invokes its private callback synchronously; it does not return the borrowed reference.
- Independent adversarial review: **GO, no required fixups**, exact source SHA-256 `3af71438b396ad0303eda7a1844902d2802c27bff8a3cdd18e8d6e82f5d22690`. Reviewer confirmed the slot guard outlives the worker loop/TLS guard, thread-local cleanup stays on its owning thread, and the callback cannot retain the reference. Review reused the existing `team_b_adversarial` agent, which made no edits.
- Isolated `moss-dev` and `moss-opt` builds pass. The runtime marker/corruption gate passes against both. The current ORACLE-10A artifact has four direct `.clone()` calls (three runtime template sites and one checked Moss `copy`) and seven qualified `Arc::clone` calls, down from eight. Marker-stripped generated artifact SHA-256 is `113847626e46a1ec7e92107fa9c45d207f35664b27eccafcb6c5315f844e45e6`. Candidate Rust compiles and executes, printing `copy` and `original`. Phase 20 executor gate passes 55/55, including nested scoped TLS lookup and Solo enter/leave compensation; `git diff --check` passes.
- This removes one qualified runtime Arc increment. The v2 sidecar does not count qualified Arc operations, and no remaining runtime clone site is authorized. The three direct runtime clones remain unclassified; no oracle expectation, registry, or certification file changed. Full 60-fixture oracle and default/shadow suites have not been rerun on this candidate. Overall disposition remains **NO-GO** pending those broader conformance and provenance requirements.

## Fresh combined 60-fixture oracle — implementation `e86f02de5989808257802a85ac3f1b38073e1961`

- Run used `tmp/r7-010-worker-tls/build/compiler/moss-dev` from the exact source implementation at `e86f02de`; oracle artifacts and JSON are under `tmp/r7-010-oracle-e86f02de/`. Frozen v2 expectations and digests were unchanged.
- **19/60 pass all applicable gates; 41/60 fail provenance only.** All 60 semantic statuses and applicable execution results pass. There are zero Native/Fast Debug or Native Debug/Optimized disagreements, zero infrastructure errors, and zero missing/invalid sidecars.
- The frozen v2 sidecar scan reports **252 unauthorized direct `.clone()` rows**. Each artifact still emits three repeated direct runtime clones whose sites remain unclassified/unauthorized; the single checked Moss `copy` site is separately authorized. The runtime TLS change removes a qualified `Arc::clone` operation, which this v2 scanner does not count. The candidate ORACLE-10A probe has seven qualified Arc clone operations, down from eight.
- Overall disposition remains **NO-GO**. No oracle expectation, registry, or certification digest changed. Full default and EST-shadow suites remain outstanding for final acceptance; they are not being represented by this targeted oracle run.

## Final regression suites for focused TLS candidate `e86f02de`

- Both suites used the dev/optimized compiler pair built from implementation `e86f02de5989808257802a85ac3f1b38073e1961`; each full 167-job runner was executed once, sequentially, with isolated runner output directories. Logs are disposable at `tmp/r7-010-e86f02de-default.log` and `tmp/r7-010-e86f02de-shadow.log`.
- **Default:** 87/167 PASS, 79 FAIL, 1 SKIP. **EST-shadow:** 87/167 PASS, 79 FAIL, 1 SKIP. These are materially below the baseline (`phase28-work` `150ab36b`: default 166/167 PASS and 1 known failure; shadow 165/167 PASS and 2 known failures). The TLS edit is not shown to cause these broad regressions, but neither full suite is acceptable for release.
- Failures include ownership/semantic, Phase 21–27 and package/runtime gates. The known borrowed-view write failure remains. `check_phase26_0_1_c3_parse_cache` fails on `OWNERSHIP_COLLECTION_ELEMENT_CONSUME` in regex `compile`; the performance baseline gate also rejects two pinned corpus hashes (`phase106d1_borrowed_reads` and `phase106d1_read_projection`). `shard_phase5_tooling` is skipped because its `shard_phase4_diff` dependency fails. Full per-gate output is in the two logs.
- Combined with the fresh oracle result above (19/60 all-gate PASS, 41 provenance-only failures, 252 unauthorized direct clone rows), the staging branch remains **NO-GO**. Independent focused GO for the TLS change is not final architect approval for the combined implementation.

## In-flight checker repair found by regex package follow-up (not yet integrated)

- At L7 HEAD `563275155d961ae9d44ef1bf4a247cc8bf0b4e56`, native generation of `packages/regex/src/main.moss` is blocked after explicit-copy compatibility edits by `MOSS_INTERNAL_INVARIANT checked match arm fact is missing or contradictory` at `regex_is_match` line 893. Team B traced the exact statement occurrence 1: `res: MatchResult` has an authoritative fact containing `NoMatch` only, omitting the source `Matched(start,end)` arm and its two Int payloads. The consumer finds the exact scrutinee requirement and correctly fails closed; no native consumer weakening is warranted.
- Team A independently reproduced the result on an isolated branch and found the producer loop scans consecutive `Case` statements only at `match_indent+1`, then stops at the first nested arm-body statement. The fix must walk the entire match-owned subtree while collecting only immediate Case arms. Team A is adding payload/binding/access assertions plus drop/perturb coverage. No Team A checker commit has been integrated yet.
- Provisional L7 package source edits make the semantic check and `tests/tooling/check_phase26_0_1_c3_parse_cache.py` pass; the EST functional-tree default/shadow gate also passes. However, package `margo test` remains blocked on the missing `Matched` fact. The source changes are uncommitted and not accepted until native package generation and package tests pass after the checker fix. No oracle expectations were changed. The overall NO-GO remains.

## L7 integration — nested match-arm fact completeness (in progress)

- Cherry-picked Team A implementation `dee17faf7ee8acc0eca2579c15f461cbbb4715c7` onto the isolated L7 branch as `583fc919`. Team A's exact implementation has independent Agent 2 GO; its status-only follow-up is `c6b5429c71f3899fe4dfbfd0c9d6caaf3a7d231b`. The checker now walks the complete flattened Match-owned subtree while collecting only immediate Case arms. The added fact gate checks nested arm bodies, both MatchResult arms, exact payload identities/types/access/transfer, and drop/duplicate/field corruption.
- L7 registered `check_phase28_match_arm_authority.py` in Makefile `check-parallel` and `tests/run.sh`, each in default and EST-shadow modes. Rebuilt dev and optimized compilers pass. The registered gate passes **9/9 scenarios in each mode**; W12 team validation passed dev/opt on Team A's exact delivery. `check_phase26_0_1_c3_parse_cache.py` passes after the provisional regex package's explicit indexed-String copies. Feedback validation and diff checks pass.
- Provisional package source edits are still uncommitted. `moss check packages/regex/src/main.moss --json` passes, but package `margo build --json` fails closed with `MOSS_INTERNAL_INVARIANT explicit copy lacks checked EST context`. Team B is tracing that consumer/context failure on an isolated worktree. No consumer requirement was weakened and no oracle data changed. Native package tests, full suites and fresh oracle remain pending after this repair settles.
