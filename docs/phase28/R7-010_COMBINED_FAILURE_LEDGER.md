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
