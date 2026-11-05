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
