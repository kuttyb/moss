# Agent 2 review: ORACLE-45 Root-ingress primitive TypeRef

Date: 2026-11-06
Exact candidate: `dce8e519238e89bce228f4db90e18c782d1f4def` (`phase28-r7-010-root-ingress-primitive-type`)
Base: `9cad0f78bea157ffee1a3838fdd41373f95dfa5d`
Scope: checker-issued Root-ingress argument TypeRef facts and Fast Debug type classification for the nested primitive-message failure in ORACLE-45.

## Verdict

**SCOPED GO for the ORACLE-45 checker/ Fast Debug TypeRef repair.** The new fact has no copy, move, ownership, or disposition authority; the Fast Debug consumer looks it up on the exact Root statement/arena/owner/message/argument/handler/context tuple and fails closed when required evidence is missing, duplicated, or perturbed. The focused gate and an independent full oracle rerun pass for semantic behavior on the exact candidate.

This is **not** an end-to-end Phase 28 GO. The full suites remain red, the oracle still has clone-provenance failures, and the runtime clone registry/exception status is outside this review.

## Exact-SHA reproduction

The candidate worktree was clean at `HEAD=dce8e519238e89bce228f4db90e18c782d1f4def`. I forced a fresh test-build compiler in that worktree:

```sh
make -B --jobs="$(nproc)" moss-dev
env -u MOSS_EST python3 tests/tooling/check_phase28_root_ingress_capture_authority.py ./build/compiler/moss-dev
MOSS_EST=shadow python3 tests/tooling/check_phase28_root_ingress_capture_authority.py ./build/compiler/moss-dev
python3 tests/phase28/oracle_r7_005_1/oracle_harness.py \
  --compiler build/compiler/moss-dev \
  --tmp tmp/agent2-review-dce8e519/oracle \
  --json-out tmp/agent2-review-dce8e519/oracle.json \
  --ledger-out tmp/agent2-review-dce8e519/oracle.md
```

Results and artifact hashes:

| Evidence | Result | SHA-256 / artifact |
|---|---|---|
| Fresh compiler `build/compiler/moss-dev` | built from candidate worktree | `9c1bb95b827d3b5ea98160e6f3d80287ee612c32f5940d372c6a661a435d1511` |
| Root-ingress gate Default | 474 scenarios PASS | `tmp/agent2-review-dce8e519/root-ingress-default-fresh.log` |
| Root-ingress gate EST-shadow | 474 scenarios PASS | `tmp/agent2-review-dce8e519/root-ingress-shadow-fresh.log` |
| Fresh v2 semantic oracle | 60/60 semantic pass; zero semantic engine disagreements; 19/60 all applicable gates | `oracle.json` SHA-256 `5ff4d6ac9e0b49ee30ad85982f1b318a1fe272a0d1c6f4b2fff880585a3f3dc2`; ledger SHA-256 `1bacad2f3a1e1ed4d5fc9b92a9889a4a657af7d6bbce8f21d19b5f3cbca7be8c` |

ORACLE-45 is semantically PASS in checker, Fast Debug, Native Debug, and Native Optimized, with expected output `11`. Its overall oracle row remains FAIL **only** because Native Debug and Native Optimized clone provenance each report three unauthorized/unclassified rows. This is a whole-oracle provenance failure, not a failure of this TypeRef repair.

The full-suite evidence is kept distinct from these fresh exact-SHA runs. The candidate's retained precommit `make-check.log` is a content-pinned run for the tree committed as `dce8e519` (candidate tree was clean after commit), but is not a SHA-stamped invocation: `tmp/phase28-r7-010-root-ingress-primitive-type-results/make-check.log`, SHA-256 `0a4c2adfdfe6fec3ab9a3fd2a91b4b13b86e6fc8aa7e29b3d1d8a56f687a4e58`, reports 115/167 PASS, 52 FAIL, 0 SKIP. The committed status records the same result for both Default and Shadow; I did not rerun either full matrix. The fresh oracle above was rerun after the forced build at the exact candidate source.

Against the exact `9cad0f78` baseline, the retained suite summary is 114/167 PASS, 53 FAIL, 0 SKIP, versus candidate 115/167 and 52 FAIL. This is a net one-job improvement, not a full failed-job-set comparison: the baseline worktree's preserved `make-check.log` ends with `KeyboardInterrupt` before a complete summary/list (SHA-256 `e6056efbc2770b918a67204c3aaae4b7d44d3d006f9838bb3a60d4ea70a047c5`). The directly verified per-case delta is ORACLE-45 Fast Debug: I force-built the baseline compiler and reproduced exit 1 with `MOSS_INTERNAL_INVARIANT Root-ingress argument lacks an exact closed TypeRef`; candidate Default/Shadow focused runs and the fresh oracle pass it. No claim is made that the incomplete baseline log identifies all 53 failing job IDs.

## Authority and adversarial code review

The checker stores a separate `TypedRootIngressArgumentTypeFactV1` and requirement for each closed-typed argument of an exact Root-ingress message. The fact binds the statement occurrence, EST arena, owner expression, nested Message ExprId, argument index and ExprId, source fingerprint, checker-selected handler identity, specialization context, and EST fact context. Its authority predicate requires these identities and a closed TypeRef. The checker obtains the TypeRef from the already selected handler parameter after message argument checking, rejects a contradictory existing EST TypeRef, and rejects duplicate occurrences. No fact is emitted for an unclosed or unparseable TypeRef; if a consumer needs the missing type, it fails closed.

The TypeRef fact contains no action, effect, ownership, move, copy permission, or `permits_moss_value_copy` field. Fast Debug uses it only as the `checked_type` input for the existing primitive-vs-nonprimitive ingress classification. For nonprimitive arguments, explicit `copy` still requires the separate exact copy fact; local transfer still requires its separate exact Consume disposition fact; a fresh constructor/literal still requires its separate fresh-rvalue transfer fact. Otherwise the interpreter throws `MOSS_INTERNAL_INVARIANT`. It checks that an existing EST argument type agrees structurally with this fact when one is present. Thus this change does not make a Moss copy or ownership decision from a TypeRef.

The consumer selects requirements and facts only when statement occurrence, arena, owner-root ExprId, nested Message ExprId, argument index and ExprId, source fingerprint, selected-handler identity, and specialization context match the active checked occurrence. It requires exactly one requirement and one fact; the API lookup validates full requirement equality including TypeRef and EST context, uniqueness, and authority. It also rechecks selected handler/context, closedness, and any existing EST TypeRef consistency. No source-text lookup or inferred fallback supplies this decision.

The registered gate exercised checker drop/duplicate and perturbations of statement, arena, owner, message, argument, source fingerprint, handler identity, TypeRef, specialization context, and EST context. Its Fast Debug cases exercised requirement/fact drop and duplicate plus the same identity/type/context perturbations. All cases failed closed in Default and EST-shadow. The new `type` hook clears the TypeRef; it tests missing-type rejection. The checker source also compares against any previously retained argument TypeRef and emits a contradiction diagnostic.

## Failure-list / scope disposition

The focused ORACLE-45 blocker present on base `9cad0f78` is closed. Whole suite totals improve by one passing job according to the retained summaries (114/167 → 115/167), but a complete baseline job-list artifact is unavailable, so no broader failure ownership is inferred. The candidate's 41 oracle all-gate failures remain provenance-only; semantic failures and engine disagreements are zero. Whole Phase 28 remains NO-GO.

No compiler or test source was edited. Review artifacts are under `tmp/agent2-review-dce8e519/`; full-suite and oracle production artifacts under the candidate's existing results directory were left intact.
