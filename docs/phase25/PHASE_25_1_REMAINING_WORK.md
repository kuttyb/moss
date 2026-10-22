# Phase 25.1 remaining-work ledger

**Accepted stabilization baseline:** `d289024c7c578de26ba670d4bd477ba90609c23e`
**Branch:** `phase25-1-work`
**Status:** L7 accepted R1 stabilization. The shadow-return and generic-`For`
behavior at this baseline is frozen. This does not complete every R2–R5 gate.

## Reconciled evidence

At `d289024c`, a forced `make --jobs="$(nproc)" -B moss-dev` build passed
(two existing `TypeSolver::Undo::binding` initializer warnings), and
`python3 tests/tooling/check_phase25_m2b2_source_free_corpus.py
build/compiler/moss-dev` passed. It covers source-deleted shadow returns,
generic Vector iteration, checked-body projection, legacy-body poisoning,
ABI-v9 controls, and hostile summary/body mutations, including the valid
same-type outer-binding substitution.

The refreshed three-mode oracle used the 19 R0 case IDs at this commit. Raw
evidence is `PHASE_25_1_R0_RECHECK_D289024C.json`; the compatibility runner's
duplicate surface-probe ID was given a temporary unique ID so both distinct
Map profiles could run. Results:

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

## Remaining-work ledger

| Milestone / acceptance | Current status and evidence | First missing invariant or layer | Classification / dependency | Minimal next action |
|---|---|---|---|---|
| **R2 — open results and generic composition:** zero-argument `fresh`, `relay`, multi-hop/private closure | `fresh(): return []` and one-hop `relay(): return fresh()` pass Debug/Release source-free with distinct Int/Bool results. Direct one-argument helper calls and the tested H3 private constructor/identity chains pass with source removed and legacy records poisoned. See R0 oracle and M2b2/H3 harnesses. | Deeper open-result call chains and all intermediate callable/result/dependency links are not yet proven. The valid callback-result M2b2 control remains fail-closed because checker-owned callback result/effect evidence is incomplete. | R2 capability and coverage gap; no failure in the accepted one-hop path. Requires call-result authority and private dependency closure. | Add a two- or three-hop zero-argument private chain and hostile missing/forged intermediate summary, body, result TypeRef, and dependency hash cases. Keep the existing one-hop proof. |
| **R2 — selected member requirements:** `size(value): return value.length()` | Source-backed and ABI-v9 Debug/Release pass; both ABI-v10 profiles reject at source-free callable selection in the current oracle. | Checker-retained selected member identity, receiver/result relation, access/effects, then checked-body importer validation. | Valid-but-unavailable capability; depends on common call/member facts. | Add the String member positive plus wrong receiver TypeRef/member identity/access/effect hostile mutations. |
| **R2 — nominal provider identity and visibility across consumers** | Generic provider-owned constructor and field read pass Int/String in one source-deleted consumer; H3 direct-record and dependency cases pass. No current failing profile is known for this bounded shape. | Independent consumer crates sharing provider-owned identity, visibility/template closure, and wrong/stale provider identity are not all covered. | Acceptance gap, not a reproduced defect; required by R2/R4. | Run two independently built consumers against one provider artifact; mutate nominal identity and dependency/provider hashes. Do not introduce user generic declarations or dynamic dispatch. |
| **R3 — remaining operations and control/effect forms** | The 14 profiles for Map-parameter mutation, early return, `while`, typed `match`, pipeline, `assert`, and raise/recover remain fail-closed at ABI-v10. The already admitted local Map, Count/Index loop, arithmetic joins, and Vector `For` controls pass their hostile tests. | Checker fact completeness and common lowering/importer parity vary by node: Map access/effects; branch/loop reachability; match payload identity; pipeline callable/effects; assert failure effect; raise set and recovery control flow. The reader has some existing tags, but v10 admission remains bounded. | Valid-but-unavailable v10 work; belongs to R3, not separate per-profile patches. Depends on complete checker facts and common lowering. | Group by shared IR node/fact family; first add checker-owned operation/effect/control facts, then common lowering/reader/importer validation and positive/hostile tests. |
| **R3 — actual borrowed Range / `bytes.from_range`** | The direct generic Vector `For` profile passes source-free. At `d289024c`, both source-backed `source_free_range` builds fail at the call on line 8 with `SOURCE_FREE_SUMMARY_INVALID`: “checked ABI-v10 Iterate binder TypeRef cannot be matched to the checked iterable element type.” The same source-backed and v9 profiles passed in the prior R0 evidence at `aa542b71`. The error was introduced with the bounded Iterate substitution logic in `a36e9ec0`. | Checked Iterate result substitution does not preserve the actual borrowed Range element relation across the generic call; this error also prevents the runner reaching separate v9/v10 provider-consumer cases. | Newly discovered compiler regression in an R3-required workload, not an intentionally unsupported language form. R3 acceptance depends on restoring source-backed behavior first. | Reproduce/fix the exact Range relation while preserving Vector For and its hostile tests; then run the unchanged `bytes.from_range` and `range_copy` source-free, borrow-lifetime, ownership, output, and Debug/Release checks. |
| **R4 — SDK-wide/cross-crate/adversarial acceptance** | Only mapped profiles and focused source-free consumers have evidence. The current run is not the complete R4 matrix. | Full package/consumer inventory, independent consumers, complete legacy poisoning, stale/cache/ABI migration, provenance/effect/access mutations, and source-backed differential coverage. | Required acceptance gate; depends on R2/R3 capability closure. | Refresh the complete R0 matrix at one frozen candidate SHA, then run the exact R4 acceptance set. |
| **R4 — `packages/time` inventory** | A fresh isolated `../../margo build --json` still fails at `packages/time/src/main.moss:379`: `time__pad_digit` returns `char` while annotated `string`. `char_at` returns Char; the local likely needs an explicit conversion. | Package source type mismatch prevents interface and generic-export inventory. | P2 package/source defect, not a compiler checked-body failure. Blocks complete package inventory until fixed or explicitly dispositioned. | Resolve the package source mismatch in its own bounded package change or retain it as an explicit L7-approved inventory exclusion; then rebuild and inventory. |
| **R4/R5 — ABI-v9 raise/recover control** | Generic raise/recover source-backed profiles pass, while ABI-v9 source-free Debug/Release still fail at `Service.mossi:9` with `RAISE_REQUIRES_TYPED_VALUE`. This exact failure was present in the prior oracle. | ABI-v9 imported error identity is not retained as a statically typed raise value. | Existing source-free compatibility defect; separate from v10's intentional fail-closed rejection. R4 requires ABI-v9 controls to remain green. | Preserve the reproducer and classify/fix the v9 import path before R4 acceptance; do not mask it as invalid source. |
| **R4 harness reliability** | `--include-surface-probes` currently adds a second `generic-map-index-mutation` ID and the oracle aborts with `FileExistsError` when creating its case directory. The base corpus also has two fail-closed controls without `expected_output`; the wrapper labels their valid source/v9 runs `output-mismatch`. | Profile identity and negative-control expectation are conflated in `tests/phase25/compatibility/oracle.py`. | Test defect; it does not change compiler behavior or the M2b2 assertions. | Give the Map-parameter probe a distinct stable ID and represent expected source/v9 output separately from expected v10 rejection. Add an oracle self-test for combined inventories. |
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
