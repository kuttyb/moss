# Phase 25.1 remaining-work ledger

**Accepted stabilization baseline:** `d289024c7c578de26ba670d4bd477ba90609c23e`
**Branch:** `phase25-1-work`
**Status:** L7 accepted R1 stabilization at `d289024c`; R1.1 restored the
source-backed borrowed-Range behavior at `ec0f64d5`. The accepted shadow-return
and generic-`For` behavior remains frozen. L7 accepted R2 at `2f1647b7` and
authorized R3; R3–R5 remain open.

## Reconciled evidence

At `d289024c`, a forced `make --jobs="$(nproc)" -B moss-dev` build passed
(two existing `TypeSolver::Undo::binding` initializer warnings), and
`python3 tests/tooling/check_phase25_m2b2_source_free_corpus.py
build/compiler/moss-dev` passed. It covers source-deleted shadow returns,
generic Vector iteration, checked-body projection, legacy-body poisoning,
ABI-v9 controls, and hostile summary/body mutations, including the valid
same-type outer-binding substitution.

The refreshed three-mode oracle evidence is `PHASE_25_1_R0_RECHECK_D289024C.json`.
That raw report contains the base corpus plus surface probes. The exact historical
19-case R0 cohort and its filtered profiles are recorded in
`PHASE_25_1_R0_COHORT_D289024C.json`; the historical Map ID was replaced with a
distinct surface-probe ID to avoid the combined-inventory collision. Results:

| Mode | Current R0 profile results |
|---|---|
| Source-backed | 36/38 pass; the two actual `bytes.from_range` profiles fail before execution with `SOURCE_FREE_SUMMARY_INVALID`. |
| ABI-v9 source-free | 34/36 measured profiles pass; Debug and Release for generic raise/recover still fail with `RAISE_REQUIRES_TYPED_VALUE`. `bytes.from_range` was not reached after its source-backed build failure. |
| ABI-v10 source-free | 20 pass and 16 valid profiles fail closed with `SOURCE_FREE_CHECKED_BODY_UNAVAILABLE`. The two `bytes.from_range` profiles were not reached. |

The previous post-R1 count of 24 unavailable ABI-v10 profiles is stale. Three
rows now pass in both profiles: generic `For` over Vector, zero-argument
`relay`, and nonempty homogeneous Vector return. The empty-vector `fresh`
profile was already passing at the previous recheck. Six profiles moved from
unavailable to pass; two `bytes.from_range` profiles now fail during
source-backed checking, leaving 16 confirmed ABI-v10 unavailable profiles
across these eight valid rows:

- generic String `.length()` member requirement (2 profiles);
- generic Map-parameter indexed mutation (2);
- generic early-return `if` (2);
- generic mutable-local `while` (2);
- typed enum `match` (2);
- generic functional pipeline (2);
- generic `assert` effect (2);
- generic `try`/`raise`/`recover` (2).

The three records in `PHASE_25_1_R0_INVALID_PROBES.json` remain invalid-by-
language and are not regressions: passing a Range value as the generic
iterator argument, calling Queue mutation through the open receiver, and
matching an enum through an untyped open parameter.

## R2 candidate refresh

The six-profile R2 oracle slice on `2f1647b7` passed all source-backed and
ABI-v9 Debug/Release runs. ABI-v10 passed the open empty-Vector result,
zero-argument relay, String `length()`, provider-owned nominal constructor,
and local Map mutation profiles in both modes. This moves the two String
member profiles from the previous unavailable set to pass. The Map-parameter
surface profile remains intentionally unavailable. Callback-result and
non-loop Count controls pass source-backed/ABI-v9; their v10 errors remain
fail-closed at the expected stages. Full M2b2 and focused R2 hostile gates
passed; architect issued implementation GO and L7 accepted R2.

Authorized first R3A work package: carry the actual
`bytes.from_range`/`range_copy` workload through authoritative Iterate binder,
effect and ownership evidence, common checked-body emission/reading, imported
dependency validation, and source-deleted Debug/Release execution. Include
legacy poisoning and hostile iterator, binder, dependency, and borrow cases.
R3A implementation is in progress.

## Remaining-work ledger

| Milestone / acceptance | Current status and evidence | First missing invariant or layer | Classification / dependency | Minimal next action |
|---|---|---|---|---|
| **R2 — open results and generic composition:** zero-argument `fresh`, `relay`, multi-hop/private closure | WP1 passes a zero-argument three-function chain after source deletion and poisoning `fresh`, `middle`, and `relay` legacy bodies. Separate Debug/Release calls infer Vector[Int] and Vector[Bool]; exact projection identities and ABI-v9 controls pass. Hostile missing/forged intermediate summary, body, result type, identity, dependency, and hash cases reject. | No remaining WP1 blocker for the tested call-result closure. The higher-order callback result/effect control remains intentionally unavailable at v10. | Bounded R2 candidate complete; final L7 closeout pending. | Preserve focused coverage; do not generalize beyond accepted call shapes. |
| **R2 — selected member requirements:** `size(value): return value.length()` | WP2 passes source-backed, ABI-v9, and source-free ABI-v10 Debug/Release. Source-deleted consumers poison the selected legacy body, require projection traces, and print `3`; eight hostile member/receiver/result/access/effect/duplicate/order mutations reject before projection. | No remaining gap in the tested String `length()` profile. General member shapes remain bounded by existing requirements and are outside R2. | Bounded R2 candidate complete; final L7 closeout pending. | Preserve focused tests; do not generalize to unsupported member shapes. |
| **R2 — nominal provider identity and visibility across consumers** | WP3 passes two independent source-deleted ABI-v10 consumers against one provider, specializing Box[Int]/Box[String] with stable declaration/module identity and distinct instance keys. Seven hostile identity, field, visibility, provenance and dependency mutations reject before projection. | No remaining gap in the bounded one-field inferred Box producer. Broader nominal declarations are outside R2. | Bounded R2 candidate complete; final L7 closeout pending. | Preserve focused tests; do not add user generic declarations or dynamic dispatch. |
| **R3 — remaining operations and control/effect forms** | The 14 previously unavailable profiles for Map-parameter mutation, early return, `while`, typed `match`, pipeline, `assert`, and raise/recover remain fail-closed at ABI-v10. The String member pair now passes under R2. The already admitted local Map, Count/Index loop, arithmetic joins, and Vector `For` controls pass their hostile tests. | Checker fact completeness and common lowering/importer parity vary by node: Map access/effects; branch/loop reachability; match payload identity; pipeline callable/effects; assert failure effect; raise set and recovery control flow. The reader has some existing tags, but v10 admission remains bounded. | Valid-but-unavailable v10 work; belongs to R3, not separate per-profile patches. Depends on complete checker facts and common lowering. | Group by shared IR node/fact family; first add checker-owned operation/effect/control facts, then common lowering/reader/importer validation and positive/hostile tests. |
| **R3 — actual borrowed Range / `bytes.from_range`** | At `d289024c`, both source-backed `source_free_range` builds failed at line 8 with `SOURCE_FREE_SUMMARY_INVALID`; the same source-backed program passed Debug/Release at `aa542b71`. R1.1 commit `ec0f64d5` restores source-backed Debug/Release output `329` in `check_phase25_team_a_semantics.py`; source-free ABI-v9 Range and Phase 20/21 ownership controls pass. The unchanged source-free ABI-v10 `bytes.from_range` and actual `range_copy` workload have not been accepted or run as R3. | R1.1 established that an absent Iterate result in an incomplete source/ABI-v9 summary is not a source-level mismatch. Source-free ABI-v10 still requires complete checked Iterate binder evidence plus structured effects, mutation, ownership and dependency closure. | R1.1 source-backed regression resolved; valid ABI-v10 workload remains an R3 capability/acceptance gap. | Under R3, run `bytes.from_range` and `range_copy` source-free with source deletion, legacy poisoning, borrow lifetime, output and Debug/Release checks; preserve invalid-borrow and mutation hostility. |
| **R4 — SDK-wide/cross-crate/adversarial acceptance** | Only mapped profiles and focused source-free consumers have evidence. The current run is not the complete R4 matrix. | Full package/consumer inventory, independent consumers, complete legacy poisoning, stale/cache/ABI migration, provenance/effect/access mutations, and source-backed differential coverage. | Required acceptance gate; depends on R2/R3 capability closure. | Refresh the complete R0 matrix at one frozen candidate SHA, then run the exact R4 acceptance set. |
| **R4 — `packages/time` inventory** | A fresh isolated `../../margo build --json` still fails at `packages/time/src/main.moss:379`: `time__pad_digit` returns `char` while annotated `string`. `char_at` returns Char; the local likely needs an explicit conversion. | Package source type mismatch prevents interface and generic-export inventory. | P2 package/source defect, not a compiler checked-body failure. Blocks complete package inventory until fixed or explicitly dispositioned. | Resolve the package source mismatch in its own bounded package change or retain it as an explicit L7-approved inventory exclusion; then rebuild and inventory. |
| **R4/R5 — ABI-v9 raise/recover control** | Generic raise/recover source-backed profiles pass, while ABI-v9 source-free Debug/Release still fail at `Service.mossi:9` with `RAISE_REQUIRES_TYPED_VALUE`. This exact failure was present in the prior oracle. | ABI-v9 imported error identity is not retained as a statically typed raise value. | Existing source-free compatibility defect; separate from v10's intentional fail-closed rejection. R4 requires ABI-v9 controls to remain green. | Preserve the reproducer and classify/fix the v9 import path before R4 acceptance; do not mask it as invalid source. |
| **R4 harness reliability** | WP4 assigns the surface Map probe a distinct `surface-map-index-mutation` ID, validates combined inventory uniqueness, supports mode-specific expected outputs, and makes unexpected build/output/projection statuses fail the oracle command. Its self-test covers 31 unique cases, duplicate IDs, expected fail-closed outcomes, required provider-stage success, and mismatch rejection; callback and non-loop Count controls pass source-backed and ABI-v9 with expected outputs. | No remaining defect in the repaired oracle inventory, expectation, exit-status, or required-provider-stage handling. Broader R4 acceptance remains unrun. | Scoped test-harness defect resolved by WP4; not evidence that R4 is complete. | Keep the self-test in CI and preserve the full R4 acceptance gate separately. |
| **R5 — opt-in release readiness** | Not run and not implied by R1 acceptance. ABI-v9 remains default; no 132-job suite rerun was made. | Complete R0/R4 green evidence, `make check`, examples/SDK/cross-crate gates, migration/cache/provenance review, and independent final architectural review. | Final acceptance gate. | Run only after R2–R4 close; report fresh executable gates, not historical 62-gate bookkeeping. |

### Common-lowerer and H3 boundary

Admitted profiles use the common occurrence lowerer and unified checked-body
reader, with importer admission remaining bounded by validated requirements.
The M2b2 positives prove their selected bodies after provider source deletion
and legacy `generic_stmt` removal; H3 direct/private chain tests prove the
specific helper bodies and dependency hashes they exercise. Unsupported body
families still fail closed, and source-side legacy payload retention remains a
migration fallback when common occurrence lowering cannot prove completeness.
Neither fallback nor a green bounded corpus establishes exhaustive Moss-v0.1
occurrence totality. Keep that parity requirement attached to R2/R3 and the R4
full matrix.

The refreshed run also found two inventory/test accounting issues outside the
19 R0 profiles: callback-result and non-loop Count negative controls have no
`expected_output`, so the compatibility wrapper reports source/v9 output
mismatches even though M2b2 checks their intended v10 fail-closed behavior.
They must not be counted as compiler regressions without correcting the
expectation metadata.

There is no established language-design decision blocking the proposed R2
work. If later implementation requires changing ABI-v10 meaning or replacing
checked-body-v1 rather than adding a bounded append-only node, stop and ask L7
before changing that contract.

R3–R5 remain downstream milestones as specified in
`PHASE_25_1_V10_READINESS.md`: R3 closes valid structured iteration/effect/
mutation workloads including the actual borrowed-Range SDK use; R4 runs the
full SDK, cross-crate, cache, ABI-migration, and adversarial matrix; R5 is the
opt-in release-readiness gate. These are not R2 implementation packages.
