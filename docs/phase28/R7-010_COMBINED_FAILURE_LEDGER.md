# Phase 28 R7-010 Combined Oracle Failure Ledger

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
