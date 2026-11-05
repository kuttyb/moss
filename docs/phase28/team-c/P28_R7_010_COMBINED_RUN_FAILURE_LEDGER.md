# Phase 28 R7-010 Combined Oracle Run — Conformance Ledger

## Run identity and evidence

- **Implementation SHA:** `422d08a4bf3e5d2a34b3bbf34ef3487b9af0131b`.
- **Run:** certified v2 60-fixture oracle, combined debug/optimized implementation result.
- **Results JSON:** `tmp/phase28-r7-010-l7-query/tmp/phase28-r7-010-combined-results.json`, SHA-256 `077618b0b60f3037b72e48d53f4296dca0b56df516c5a2cd322d8d9da213a4e8`.
- **Harness ledger:** `tmp/phase28-r7-010-l7-query/tmp/phase28-r7-010-combined-ledger.md`, SHA-256 `e93f1b2877fd3c8f5ae463096a0d6984e6ce1940f2d003ef35e6399cf3faea57`.
- **Frozen authority inputs:** `tests/phase28/oracle_r7_005_1/expectations.json` (SHA-256 `7ca0d550e38f33c47105f57e22f5993d23449bf09c1cd5da043de924657f23a2`) and `tests/phase28/oracle_r7_005_1/diagnostic_mapping.json` (SHA-256 `38eb36e06d4df980181174299680e3db360da0c8c54eeb95ec19273ccc9de1cd`). The runner is `tests/phase28/oracle_r7_005_1/oracle_harness.py`.

The summary below was recomputed from all 60 JSON rows and cross-checked against the generated ledger. It does not reuse the older R7-008 result. Categories overlap; in particular ORACLE-07 fails both an execution/semantic gate and clone provenance.

## Combined verdict

| Measure | Fixture count / occurrence count | Interpretation |
|---|---:|---|
| All applicable gates pass | 13 / 60 fixtures | Every all-gate PASS is a rejected fixture whose checker diagnostic matches the frozen contract. No accepted fixture passes all gates because clone provenance remains unresolved. |
| At least one gate fails or blocks | 47 / 60 fixtures | Union of semantic-status failures, submitted unauthorized clone evidence, and missing provenance; categories overlap. |
| `semantic_status == PASS` | 53 / 60 fixtures | This field excludes clone provenance. |
| `semantic_status == FAIL` | 7 / 60 fixtures | Three implementation/runtime failures and four diagnostic-contract mismatches detailed below. |
| Submitted clone rows marked unauthorized | 512 rows | Present sidecars claim unclassified/runtime occurrences that the harness rejects as unauthorized. These are failed authority claims, not a claim that all 512 are distinct confirmed illegal Moss copies. |
| Unverified clone observations | 536 occurrences | 512 rows with submitted but unauthorized provenance, plus 24 observations in four ORACLE-54 consumer/provider debug/optimized artifacts missing sidecars. The 536 count is distinct from prior 524/500 artifact scopes. |
| Missing/invalid provenance block | 1 fixture | ORACLE-54; four missing sidecars contribute six unclassified clone observations each. |
| Infrastructure failures | 0 fixtures | No timeout, artifact-generation failure, Rust compilation failure, or other infrastructure failure is recorded in the JSON run. |

## Checker and semantic findings

The checker outcome counters are 54 `PASS`, four `WRONG_DIAGNOSTIC`, and two `UNEXPECTED_ACCEPT`. Overall semantic status is 53 PASS / 7 FAIL because ORACLE-07's checked program passes the checker but later fails execution requirements.

### Implementation/runtime failures (3 fixtures)

| Fixture | Evidence | Classification |
|---|---|---|
| `ORACLE-07-CATCHABLE-COLL-ERRORS` | Fast Debug reports `MOSS_INTERNAL_INVARIANT missing checked CollectionError variant fact: enum:std__CollectionError:MissingKey`. Native Debug and Native Optimized panic at the missing-key lowering (`Moss missing map key`). All three modes fail the fixture's catchable-error behavior. | Checker-authority semantic defect: the required CollectionError checked fact is unavailable to consumers. Fast Debug and native lowering expose different failure modes. |
| `ORACLE-15-PLAIN-FIELD-CONSUME-REJECT` | Checker `UNEXPECTED_ACCEPT` for the frozen rejection case. | Checker semantic defect: invalid nonprimitive consume accepted. |
| `ORACLE-46-HANDLER-NONPRIM-REPLY-REJECT` | Checker `UNEXPECTED_ACCEPT` for the frozen rejection case. | Checker semantic defect: nonprimitive message payload reply accepted. |

### Diagnostic-contract mismatches (4 fixtures)

These are checker rejections, but the oracle harness rejects their diagnostics against the current frozen mapping. No mapping or expectation was changed for this report.

| Fixture | Actual structured diagnostic | Frozen mapping mismatch |
|---|---|---|
| `ORACLE-28-REBIND-WHILE-BORROWED-REJECT` | `OWNERSHIP_SOURCE_BORROWED`; message says overwrite/consume is blocked while a READ alias is live; points at fixture line 11. | Code is an allowed alternate, but message fails the mapping regex `borrow|rebind|ownership`. |
| `ORACLE-30-JOIN-BORROW-FRESH-REJECT` | `OWNERSHIP_INCOMPATIBLE_JOIN`; message identifies merging owned and borrowed values at a control-flow join; points at line 13 (`echo b.val`). | Code is an allowed alternate and message is relevant, but the mapping requires a source line containing `=`, so the diagnostic is rejected as mislocated. |
| `ORACLE-35-MATCH-ORDINARY-CONSUME-REJECT` | `BORROWED_ENUM_PAYLOAD_ACCESS`; message says the borrowed payload cannot be written or transferred; points at line 16 (`let transferred = take_buffer(buf)`). | Code is an allowed alternate and message describes the rejected transfer, but the mapping's source-line regex requires `match|archive`. |
| `ORACLE-48-ROOT-MISSING-COLLECTION-ERROR` | `MOSS_COMPILE_ERROR`; message says `main may not let typed errors escape; handle every raised variant with try/recover`; points at line 8. | Compiler rejects the root totality violation, but frozen mapping accepts only `ROOT_UNHANDLED_RAISE`, so the diagnostic code mismatch fails the gate. |

Stable authority and mapping records are in `tests/phase28/oracle_r7_005_1/diagnostic_mapping.json`; fixture source is under `tests/phase28/oracle_r7_005_1/fixtures/`.

## Native / Fast Debug comparison

There is **no separate fixture where one engine passes while another fails**. ORACLE-07 is the one shared execution-failure fixture across Fast Debug, Native Debug, and Native Optimized, and exposes different failure mechanisms: Fast Debug fails closed on a missing checked CollectionError variant fact, while both native modes panic at the missing Map key. Fast Debug's partial transcript includes `missing_key_read`; native aborts before that output. Treat this as one cross-engine execution defect, not a separate pass/fail parity-only case.

## Clone provenance findings

| Provenance status | Fixtures | Result |
|---|---:|---|
| Submitted sidecar rejected as unauthorized | 40 | All generated debug/optimized sidecar clone rows in these fixtures are unclassified and fail the current provenance contract. ORACLE-07 is also one of the three implementation/runtime semantic failures. |
| Sidecar missing/invalid | 1 | ORACLE-54; missing consumer and provider sidecars in both modes. |
| Not applicable | 19 | Rejected programs do not generate the accepted-program execution artifacts required for this provenance gate. |

The 40 unauthorized-provenance fixtures are:

`ORACLE-01-VEC-WRITE-THROUGH`, `ORACLE-02-INDEXED-FIELD-READ-NO-CLONE`, `ORACLE-04-MAP-WRITE-THROUGH`, `ORACLE-05-MAP-GET-BORROW`, `ORACLE-06-MATCH-CONSUME-OPT-NATIVE`, `ORACLE-07-CATCHABLE-COLL-ERRORS`, `ORACLE-09-USER-ENUM-SOME-NONE`, `ORACLE-10A-EXPLICIT-COPY-ONLY`, `ORACLE-11-ASSIGN-READ-BORROW`, `ORACLE-12-ASSIGN-WRITE-BORROW`, `ORACLE-13-ASSIGN-CONSUME-RETURN`, `ORACLE-16-TAKE-FRONTIER-ARCHIVE`, `ORACLE-17-TAKE-OPTION-FIELD`, `ORACLE-18-TAKE-QUEUE-FIELD`, `ORACLE-19-TAKE-NESTED-VEC-OPTION`, `ORACLE-20-TAKE-NESTED-MAP-VECTOR`, `ORACLE-25-TAKE-BEFORE-RAISE-MUTATION`, `ORACLE-26-VEC-REMOVE-API`, `ORACLE-27-REBIND-AFTER-BORROWS`, `ORACLE-29-LOOP-CARRIED-OWNERSHIP`, `ORACLE-31-JOIN-PROMOTED-CONSUME`, `ORACLE-32-MATCH-PAYLOAD-WRITE`, `ORACLE-33-MATCH-LAST-USE-OVERWRITE`, `ORACLE-36-MATCH-CONSUME-QUEUE-POP`, `ORACLE-38-MAP-VALUES-BORROWED-VIEW`, `ORACLE-39-VEC-FOR-READ`, `ORACLE-40-VEC-FOR-WRITE`, `ORACLE-42-CUSTOM-WRAPPER-REMOVE`, `ORACLE-43-HANDLER-PRIMITIVE-REPLY`, `ORACLE-45-HANDLER-PRIMITIVE-REBIND-LOCAL`, `ORACLE-47-VECTOR-INDEX-PANICS`, `ORACLE-49-OWNERSHIP-AT-RECOVER`, `ORACLE-50-EAGER-PIPELINE-CAPTURES`, `ORACLE-51-JOIN-BORROWED-PROVENANCE-SETS`, `ORACLE-53-MAP-REMOVE-MISSING`, `ORACLE-55-COLLECTION-ERROR-INFERRED-RAISE`, `ORACLE-56-PRIMITIVE-VECTOR-ELEMENT-READ`, `ORACLE-57-MAP-GET-WRITE-THROUGH`, `ORACLE-58-SPECIALIZATION-IDENTITY-COLLISION`, and `ORACLE-59-SHADOWED-BINDING-IDENTITY`.

The 536 is an aggregate observation count across all audited consumer/provider and debug/optimized artifacts. It must not be read as 536 confirmed illegal semantic copies. The missing-proof category remains independently blocking even though its 24 observations are included in 536.

## Infrastructure and scope boundary

No infrastructure issue is recorded for this run. All native artifacts that reached compilation compiled; the observed runtime crashes are fixture execution failures, not build failures. This ledger reports the exact frozen run only. It does not modify the certified oracle, diagnostics mapping, compiler, or test runner and does not reclassify clone observations as authorized.
