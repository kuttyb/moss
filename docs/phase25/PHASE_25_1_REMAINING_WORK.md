# Phase 25.1 remaining-work ledger

**Accepted stabilization baseline:** `d289024c7c578de26ba670d4bd477ba90609c23e`
**Branch:** `phase25-1-work`
**Status:** L7 accepted R1 stabilization at `d289024c`; R1.1 restored the
source-backed borrowed-Range behavior at `ec0f64d5`. The accepted shadow-return
and generic-`For` behavior remains frozen. L7 accepted R2 at `2f1647b7` and
authorized R3; the R3A Range workload and R3B generic Map mutation slice have
scoped architect GO. Remaining R3 groups, R4, and R5 remain open.

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
| **R3 — remaining operations and control/effect forms** | The Map-parameter mutation slice is accepted at `9f9f168f`: source-backed, ABI-v9, and source-deleted ABI-v10 Debug/Release pass with output `7\n9\n7\n`; provider source and its two legacy bodies are removed, projection is traced, and four rehashed access/relation/effect/body mutations reject. M2b2 passes including its Map effect-erasure and v9 controls. At the same checkpoint, early-return and mutable-local `while` probes pass source-backed and ABI-v9 Debug/Release but remain unavailable at v10 consumer selection. Typed inspection shows no checked body for either. A Float call to early-return `magnitude` is valid and interprets to `1.0`, while retained branch TypeRefs are `Int` and open `T` against summary result `T`; no promotion/join fact connects them. The architect independently audited this and issued NO-GO for lowering from current facts. Typed enum `match`, functional pipeline, generic `assert`, and generic raise/recover remain unavailable. The broad R0 profile count has not yet been refreshed at the current R3 candidate. | Map gap closed for the accepted profile. Early-return has a checker occurrence/callable TypeRef mismatch around contextual numeric literal specialization; mutable-local `while` needs checked body and mutation/divergence validation. Typed match, pipeline, and generic assert/effect groups still require source-backed/v9 controls and earliest-invariant diagnosis. | R3B. Map-parameter indexed mutation has scoped architect GO. Early-return is stopped pending L7 language-design direction under the R3 hard-stop condition; other R3 groups are open. | L7 to decide whether numeric-literal contextual promotion must be retained as checker-owned evidence for generic early returns or this form remains unavailable to ABI-v10. After that decision, continue independent groups through checker facts, common IR, reader, and importer validation. |
| **R3A — actual borrowed Range / `bytes.from_range`** | Accepted implementation `84a9bb28` passes the unchanged `range_copy` workload source-backed, ABI-v9, and source-deleted ABI-v10 Debug/Release, all output `5`. A second source-free test copies actual `input.bin` bytes, closes FileIO, and checks weighted byte order (`991`) in Debug/Release. Rehashed Iterate-result, assert-effect, and missing-call-dependency mutations reject before projection. M2b2, checked-body IR, D2 loop codec, FileIO semantic/runtime and borrowed-view controls pass; architect independently executed positives and hostile tests and issued implementation GO with no P0/P1/P2. | No remaining R3A invariant known in the accepted test scope. | R3A accepted implementation; preserve behavior in R3B. | Keep the source-deleted Range workload in the cohesive R3 validation matrix. |
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

## R3 final arithmetic authority candidate — 2026-10-22

This section supersedes the earlier R3 availability counts above. Frozen
candidate `114a687313f98f0aa6f515f4c11d0cf3e41d3ae5` retains each admitted
operator requirement against its exact checked AST occurrence, including
operator identity, operand roles/TypeRefs, result TypeRef, and parent/child
links. Repeated operators remain distinct; the importer rejects duplicate,
missing, ambiguous, or unlinked facts before checked-body projection. Numeric
specialization uses the existing Moss checker/operator rules.

The complete compatibility artifact is
[`PHASE_25_1_R3_CANDIDATE_114A6873.json`](PHASE_25_1_R3_CANDIDATE_114A6873.json).
All 32 oracle cases passed expected outcomes across source-backed, ABI-v9, and
ABI-v10 in Debug and Release (64/64 per mode; no unexpected results). The
previously unavailable valid R3 forms now pass in all three modes, including
actual `bytes.from_range`, generic Map parameter mutation, relay, member
length, nonempty Vector literals, early returns, mutable-local While, typed
Match, Pipeline, Assert, and Raise/recover. The arithmetic profile independently
passes `20 + x * 2` as Int `30` and Float `25.0`, plus a second nested form,
with source deleted and legacy generic bodies poisoned for ABI-v10. Six
internally rehashed mutations of operator identity, operands, result TypeRefs,
parent relation, duplicate facts, and missing facts reject before import.

The Architect independently built and tested exact SHA
`114a687313f98f0aa6f515f4c11d0cf3e41d3ae5` in an isolated worktree and issued
scoped GO for the arithmetic correction, with no P0/P1/P2 findings. The broader
consolidated R3 review is in progress. The focused R3A–R3F suites, complete
M2b2 source-free/hostile corpus, feedback validator, and `git diff --check`
pass. The compiler was built with `make --jobs="$(nproc)" moss-dev`.

Remaining before L7 R3 acceptance: receive the consolidated exact-SHA Architect
GO, run final repository consistency checks, commit the ledger/report update,
and push the reviewed candidate and closeout. No R4 work has started. The
`packages/time` source build failure remains a separate inventory blocker; the
invalid Range-argument, open-receiver Queue mutation, and untyped-open-enum
Match probes remain intentional fail-closed language controls.

## R3 arithmetic authority — refreshed exact candidate — 2026-10-22

The initial consolidated review of `114a6873` identified valid generic
`x * 2 - 20` as still unavailable. Its checker already retained the exact
multiplication and subtraction occurrences; the ABI-v10 requirement converter
was missing the checker-proven generic relation `T - Int -> T`. The bounded
correction admits that relation alongside the already admitted opposite
orientation and continues to validate concrete Int/Float operations through
the existing Moss numeric specialization rules. No type is manufactured from a
TypeRef rewrite.

Frozen candidate `31b644e455004aa4c7fdbcd54044bf043007ca4c` passes the full
32-case oracle on exact SHA: 64/64 Debug/Release profiles for source-backed,
ABI-v9 source-free, and ABI-v10 source-free; zero unexpected results. Its raw
report is [`PHASE_25_1_R3_CANDIDATE_31B644E4.json`](PHASE_25_1_R3_CANDIDATE_31B644E4.json).
All earlier valid-but-unavailable R3 observations now pass in the three-mode
oracle. The focused arithmetic suite separately confirms the required Add/Mul
form, a repeated nested form, and mixed subtraction for Int and Float in all
three modes, with provider source deleted and legacy ABI-v10 bodies removed.
Twelve rehashed arithmetic evidence mutations reject, including missing facts
for both nested Add and Sub forms; nonnumeric operand substitutions reject.

Focused R3A–R3F suites, arithmetic records/hostile suites, complete M2b2
source-free/hostile corpus, feedback validation, and `git diff --check` pass.
The Architect issued consolidated scoped GO on exact candidate
`31b644e455004aa4c7fdbcd54044bf043007ca4c`, with no P0/P1/P2 findings. Their
isolated run independently built `moss-dev`, passed arithmetic and M2b2
focused suites, executed source-deleted ABI-v10 subtraction in Debug/Release
as Int `-10` and Float `-15.0` with checked projection, and rejected all six
resealed subtraction-targeted mutations. The earlier candidate's P2 is
resolved. Commit the durable closeout and push this reviewed checkpoint for
L7 acceptance. No R4 work has started; `packages/time` remains separately
classified as a source-package inventory failure.

## R4 recovery continuation — 2026-10-23

The direct-return nominal constructor gap found during R4B is narrowly
addressed at committed compiler checkpoint
`3b4689fe29f1393d512bbb07d1084e9f84898dcc` by admitting the checked
`return Box(value: parameter)` form alongside the established
local-constructor/return form. Rebuilt compiler hash:
`c62ac9534a1f817cf518ec458f987fbd5403352f2f747533bb805dd8f82e642c`.
The R2 WP3 source-free nominal gate and its seven hostile mutations pass.

The staged-v10 cross-crate matrix now passes 10/11 in both Debug and Release.
Scenarios 2–5 (source-deleted direct nominal returns, independent consumers,
and third-crate identity) pass; so do scenarios 1, 6, and 8–11 (artifact
emission, private helper, ownership conflict, ABI mismatch, metadata mutation,
and replaced-cache invalidation). Scenario 7 remains failed: generic
`invoke_action` calls an unconstrained member `custom_action` through a private
helper, leaving no checked body; source-free import reports
`SOURCE_FREE_CHECKED_BODY_UNAVAILABLE` instead of the fixture's required
consumer `METHOD_NOT_FOUND` diagnostic and provider/consumer spans. This is a
compiler ABI-v10 parity defect, not a harness invocation issue. Semantic
inspection shows both `invoke_action` and `private_action` open summaries are
incomplete with `unresolved_effects` because `custom_action` is an unbound
member requirement. Source-backed and ABI-v9 source-free controls report the
expected `METHOD_NOT_FOUND`; ABI-v10 stops at summary closure before consumer
specialization. The bounded R2 member-length acceptance does not cover this
general member shape, but R4 still treats the v9-valid diagnostic mismatch as
red.
Running this ABI-v10-only scenario harness in production-default mode fails
before consumer checking because the fixture requires ABI-v10 artifacts; that
is a harness mode mismatch, not ABI-v9 compiler evidence.

The direct-return boundary received scoped Architect GO on exact compiler SHA
`3b4689fe29f1393d512bbb07d1084e9f84898dcc`, with no P0/P1 and one P2 for
missing durable direct-form legacy poisoning/projection/hostile coverage. That
P2 is addressed by `tests/tooling/check_phase25_r4_direct_nominal.py`: after
deleting provider source and poisoning all legacy generic statements, two
independent Int/String consumers require checked projection and execute; seven
nominal identity, field, visibility, and dependency mutations reject before
the fresh body projects. Exact-SHA Architect follow-up remains pending.

The cohesive 32-case compatibility oracle passed on this frozen compiler SHA:
64/64 source-backed, 64/64 ABI-v9, and 64/64 ABI-v10 Debug/Release profiles,
with no unexpected outcomes. Report:
`tmp/r4-cross-crate-resume/r3-oracle.json`. The real-SDK generic matrix passed
18/18, and the mixed-provider per-callable admission gate passed all six checks.
Eight ABI-v9-only SDK source-free harnesses, the ABI-v9 migration/rebuild
diagnostic, warm provider/library cache binding, and the focused M2b2 source-
free hostile corpus also pass on this compiler. The corpus covers legacy-body
poisoning, forged identity/provenance/digest, weakened effects, dependency
deletion/hash, branch/control-flow/binding lineage, and ABI-v9 controls.

An exact-candidate rerun of the advanced-data ABI/profile matrix was
interrupted during ABI-v9 Debug after about six minutes of CPU-bound provider
builds. It exited 130 and provides no pass/fail result. Previous R4B evidence
on `a854c901` records passes for all four modes, but these are not exact-current-
candidate evidence. ABI-v9-only source-free SDK scripts previously passed
8/8 on `c0ff87fd`, and the four real generic exports passed 18/18 on
`a854c901`; exact-candidate reruns now pass 8/8 and 18/18 respectively. Only
the exact-candidate advanced-data matrix remained unverified at the time of
this checkpoint; see the exact-candidate continuation below for its completed
four-mode rerun.

**R4 remains open.** Cross-crate scenario 7 is red in both profiles and an
overall exact-SHA independent Architect verdict remains required. The
inventory maps all four open generic exports to the 18-case SDK matrix; the
separate 14-provider advanced-data graph is concrete-API coverage.
Do not advance to R5 or change the ABI-v9 default.

## R4 exact-candidate matrix continuation — 2026-10-23

This entry supersedes the interrupted-matrix status above. On compiler source
SHA `3b4689fe29f1393d512bbb07d1084e9f84898dcc` (executable SHA-256
`c62ac9534a1f817cf518ec458f987fbd5403352f2f747533bb805dd8f82e642c`),
the compiled-only advanced-data 14-provider source-free gate passed in all
four modes: ABI-v9 Debug/Release and ABI-v10 Debug/Release. Each run removed
provider source and checked, formatted, built, and executed the consumer. Raw
logs are in `tmp/r4-cross-crate-resume/advanced-*-final.log`.

All other current candidate evidence remains green: full 32-case R3 oracle
(64/64 source-backed, ABI-v9, and ABI-v10 outcomes per Debug/Release matrix),
real-SDK generic matrix 18/18, ABI-v9-only SDK source-free harnesses 8/8,
mixed-provider admission 6/6, focused M2b2 source-free/hostile corpus, ABI-v9
migration/cache controls, and direct nominal source-free hostiles. The exact-
binary cross-crate staged-v10 harness is 10/11 in both Debug and Release.

Scenario 7 was rerun against the clean exact candidate and remains red with
`SOURCE_FREE_CHECKED_BODY_UNAVAILABLE`; the source-backed and ABI-v9 source-
free controls produce the expected `METHOD_NOT_FOUND` with provider and
consumer provenance. Inspection shows the private direct-member leaf has a
checked body and member requirement, while the exported forwarding callable's
open type scheme is incomplete. The first missing invariant is checker-owned
result/requirement closure through that private member call. No result
relation may be copied or guessed to make the ABI record fit. Treat this as a
compiler ABI-v10 diagnostic-parity defect, not a test-harness failure.

The scoped direct-nominal Architect P2 is closed on exact SHA. The overall
exact-SHA R4 review at evidence checkpoint `40db78652ae767cd73a936b4e3a60801ca6e237d`
returned **NO-GO**, with one P1, no P0, and no additional P2. The Architect
independently reran the direct-nominal hostile gate and full M2b2 source-free/
hostile corpus on the exact compiler; both passed. **R4 remains open and is
not accepted** until scenario 7 is resolved and a fresh exact-SHA GO is issued.
R5 remains unstarted; its recorded gates require green R0/R4 evidence,
`make check`, examples/SDK/cross-crate and migration/cache/provenance gates,
and an independent final architectural review. ABI-v9 remains the default.

## R4 final P1 correction — exact candidate `60add1f3` — 2026-10-23

Compiler candidate `60add1f31fabcb467445a3d7e72db93e4725b394` has binary
SHA-256 `f1ace0694ecd9440800daf57629e4b018749632cf30f9c609ea92ec35fce126e`.
Scenario 7 passes source-backed, ABI-v9 source-free, and staged-v10 Debug and
Release with `METHOD_NOT_FOUND` for `custom_action`, consumer call primary at
`main.moss:9`, provider related location at `provider.moss:6`. A concrete
source-free specialization returns 42 with an assertion effect. Five resealed
relay hostiles reject; forged member identity rejects before any body
projection. No result type is synthesized and no ABI wire schema changed.

The full staged-v10 cross-crate suite passes 11/11 Debug and 11/11 Release.
The accepted 32-case compatibility oracle passes 64/64 each for source-backed,
ABI-v9, and ABI-v10 profiles, with zero unexpected results. The real-SDK
generic matrix passes 18/18; eight ABI-v9 source-free SDK harnesses pass; and
the advanced-data compiled-only matrix passes all four ABI/profile modes with
14 exact provider pairs per mode and provider source removed. Direct nominal
hostiles, mixed-provider admission (6/6), the M2b2 hostile corpus, ABI-v9
migration/rebuild, warm cache/provider binding, transitive external-cache
revalidation, legacy inventory-provenance refresh, and feedback validation
pass on this compiler. A rehashed legacy inventory lacking original artifact
compiler identity keeps those fields unknown and records the refresh
commit/executable/hash separately.

The Architect independently built this exact SHA and issued scoped GO on the
relay correction with no P0/P1/P2. Their independent scenario 7 Debug/Release
and five-hostile runs passed, including the reject-before-projection trace.
After reviewing the consolidated evidence at commit
`f84a4909aded15526e53b6d8f48e339364329344`, the Architect issued overall R4
GO with no P0/P1/P2 findings. They verified the matrix reports, inventory
provenance refresh, and the scenario 7 hostiles. Durable evidence is in
[`PHASE_25_1_R4_CANDIDATE_60ADD1F3.json`](PHASE_25_1_R4_CANDIDATE_60ADD1F3.json);
raw reports are in disposable `tmp/r4-final/`.

**R4 candidate acceptance: GO.** Return the exact compiler SHA and evidence
to L7. R5 has not started; preserve ABI-v9 as default. The broader advanced-
data `run_all.py` Fast Debug workload remains outside this compiled-only
matrix and unverified on this candidate.

## R5 final release qualification — exact R4 compiler candidate — 2026-10-23

R5 executed against compiler source SHA
`60add1f31fabcb467445a3d7e72db93e4725b394`, with development executable
SHA-256 `f1ace0694ecd9440800daf57629e4b018749632cf30f9c609ea92ec35fce126e`.
The durable evidence and complete failure ledger are in
[`PHASE_25_1_R5_RELEASE_READINESS.md`](PHASE_25_1_R5_RELEASE_READINESS.md).

Fresh results include `make check` at 16 jobs (136 selected, 129 pass, 7 fail,
0 skipped), `make examples` (32 examples built, intentional negative skipped),
the 32-case compatibility oracle (64/64 each for source-backed, ABI-v9, and
ABI-v10), SDK generic (18/18), staged-v10 cross-crate (11/11 in both profiles),
the 17-package ABI-v10 SDK build matrix (34/34), and the advanced-data
source-free 14-provider matrix in all four ABI/profile modes. Eight ABI-v9
SDK source-free controls, focused migration/cache/hostile suites, full
Foundations and Text & Encoding acceptance, and the advanced-data Fast Debug
package run also passed.

The independent Architect reviewed and ran exact-SHA representative positive
and hostile cases. Their overall R5 verdict is **NO-GO**. The full regression
failures include two reproduced compiler failures (`nominal_open_result` and
the ABI-v10 Iterate binder) and one intermittent native read-fault failure;
four failures are test/fixture expectation mismatches. The Architect also
retains two P2 hardening findings: reject unreachable private records, and use
a collision-resistant provider cache fingerprint. R5 release acceptance is
blocked until all seven regression failures are triaged/resolved, both P2s are
closed to the Architect's satisfaction, and the required fresh gates pass on
one resulting committed compiler candidate. ABI-v9 remains default and
ABI-v10 remains opt-in; no R5 acceptance or Phase 25.1 completion is declared.
