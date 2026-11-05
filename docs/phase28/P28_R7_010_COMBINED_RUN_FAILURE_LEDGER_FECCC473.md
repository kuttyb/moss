# P28 R7-010 Combined Oracle Ledger — `feccc473`

**Implementation SHA:** `feccc47323b4b081edb64bb4e9a618fb7ad4e9b9`
**Branch:** `phase28-r7-010-l7-copy-authority`
**Oracle:** certified v2, 60 fixtures; frozen expectation and mapping digests validated by the harness
**Fresh outputs:** `tmp/phase28-r7-010-combined-feccc473.json` and `.md`

## Result

| Gate | Result |
|---|---:|
| All applicable gates pass | 19 / 60 |
| Any gate fails | 41 / 60 |
| Checker, Fast Debug, Native Debug and Native Optimized semantic/execution | all applicable checks pass |
| Clone provenance | 41 fixtures fail |
| Clone occurrences observed | 508 unverified |
| Explicitly unauthorized rows from artifacts with provenance | 484 |
| Missing-provenance rows (ORACLE-54 consumer/provider, Debug/Optimized) | 24 |
| Harness/infrastructure failures | 0 |

The categories are disjoint for this run: all 41 failures are provenance failures. The 484 unauthorized rows are repeated generated Rust occurrences, not distinct emitter causes or confirmed Moss-level illegal copies. ORACLE-54 has no valid sidecar for consumer/provider outputs in either native mode. No runtime clone is authorized by this result.

The certified v2 harness completed all 60 rows and validated its frozen expectation and runtime-callsite digests. No expectation was edited. This replaces the earlier result at `8b9ad2c5` for this checkpoint; that older result had four semantic/execution failures, all now absent in this run. It does not resolve the independent clone audit: Team C's pinned 67-artifact report groups 1,933 candidates into 84 lexical output groups and explicitly states those are not proven emitter identities. Team B's runtime/emitter inventory also remains subject to independent source and authority review.

## Required follow-up

1. Repair the Team A generic ReturnValue checker fact regression exposed by `check_phase26_1_1_x1.py ./moss` at this checkpoint, then rerun X1 and the EST `.mossi` gates.
2. Implement exact, fail-closed clone provenance joined to checked `TypedCopyFact` occurrences. Reconcile generated operations by source emitter and actual value semantics; do not infer authorization from repetition, helper names, comments, or runtime labels.
3. Add and pass missing, forged, duplicated and corrupted provenance tests, including imported provider and consumer artifacts.
4. Rerun all targeted gates and the 60-fixture oracle against the eventual frozen implementation SHA. Full default and EST-shadow suites and final independent review remain outstanding.
