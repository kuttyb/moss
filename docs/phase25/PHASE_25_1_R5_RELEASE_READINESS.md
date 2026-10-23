# Phase 25.1 R5 Release Readiness

**Branch:** `phase25-1-work`
**Original R5 compiler source candidate:** `60add1f31fabcb467445a3d7e72db93e4725b394`
**Evidence checkpoint at start:** `d1e3c662ca796b3328f0d1de86aefa9bca55d232`
**Owner:** L6 Coordinator
**Independent reviewer:** L6 Adversarial Architect
**Disposition:** **Architect GO on the frozen R5.1 candidate; submitted to L7 for acceptance**

## Candidate identity and scope

The compiler source remained at the accepted R4 commit throughout these runs.
The development compiler and `./moss` both have SHA-256
`f1ace0694ecd9440800daf57629e4b018749632cf30f9c609ea92ec35fce126e`.
The optimized compiler built by `make check` has SHA-256
`2ff630198d355147dcf409c104bc86989f2fffb8d0b4bd49f49c0034ee427e33`.
The acceptance runners used the same source commit; they report the current
evidence commit separately from the compiler source commit.

No compiler or ABI source was changed for R5. ABI-v9 remains the default;
ABI-v10 is selected with the existing `MOSS_STAGED_ABI=v10` opt-in. The staged
SDK matrix used that one ABI selector and no shape-specific enable flags. A
default-mode collections build after the staged matrix emitted an ABI-v9
interface with `MOSS_STAGED_ABI` unset.

## Regression and example results

`make --jobs="$(nproc)" check` ran with 16 jobs. The runner selected all 136
manifest jobs: **129 passed, 7 failed, 0 skipped**. The complete run took
990.26 seconds wall time and 5977.56 seconds cumulative worker time. It did
not automatically retry jobs, so no automatic flaky-test classification was
available.

| Failed job | Observed result | Triage |
|---|---|---|
| `check_phase25_typed_semantic_api` | The assertion expects requirement identity suffix `checked-arithmetic:0`; the compiler reports `checked-occurrence:0`. | Test expectation is out of sync with the common checked-occurrence record. |
| `check_phase25_checked_body_ir` | Its C++ test fixture does not compile: aggregate initializers pass string literals where the current `Expr` layout requires `uint32_t`. | Test fixture failure; no compiler execution occurred in this job. |
| `check_phase25_r2_wp4_oracle_inventory` | The self-test expects the old two-line callback output; the current corpus also includes `23` and `7`. | Inventory assertion is stale; the full compatibility oracle passes. |
| `check_phase25_conformance` | Gate 5 expects `SOURCE_FREE_CHECKED_BODY_UNAVAILABLE`; the candidate returns `METHOD_NOT_FOUND` for the missing member with provider provenance. | Expected diagnostic predates the R4 deferred-member correction and needs reconciliation. |
| `check_phase25_team_a_semantics` | `nominal_open_result` returns `TYPE_AMBIGUOUS_OPEN_RESULT` instead of passing. | Reproduced on the candidate; unresolved compiler semantic failure. |
| `check_phase21c_branch_errors` | The injected native read-fault case panicked at a generated assertion (`left: 1`, `right: 0`). | The complete branch-error script passed on one immediate rerun; this is an intermittent failure, not a stable pass. |
| `shard_phase25_int_payloads` | `phase47_iteration.moss` fails with `SOURCE_FREE_SUMMARY_INVALID`: checked ABI-v10 Iterate binder TypeRef cannot be matched to the checked iterable element type. | Reproduced with a fresh output directory; unresolved compiler failure. |

These failures keep the required full regression gate red. The two reproduced
compiler failures and the intermittent branch-error failure require triage
before R5 can be accepted; the four harness/assertion failures also need to be
reconciled and rerun. The compiler build emitted two existing
`TypeSolver::Undo::binding` missing-initializer warnings.

`make --jobs="$(nproc)" examples` passed: **32 examples built**, and the
intentional negative `examples/use_after_transfer.moss` was skipped. Nested
`rustc` invocations emitted jobserver file-descriptor warnings inherited from
GNU Make; they did not fail the example target.

## Fresh compatibility, SDK, and cross-crate gates

- Full 32-case compatibility oracle, with surface probes and SDK bytes-range:
  **64/64 source-backed, 64/64 ABI-v9, and 64/64 ABI-v10 outcomes**, with zero
  unexpected results.
- Real SDK generic consumer matrix: **18/18** runs across source-backed,
  ABI-v9, and ABI-v10 profiles.
- Staged ABI-v10 cross-crate harness: **11/11 Debug and 11/11 Release**. This
  includes scenario 7's `METHOD_NOT_FOUND` diagnostic, consumer primary
  location, provider related location, and source-free provider artifacts.
- Primary SDK ABI-v10 matrix: **34/34 builds**, 17 packages in Debug and
  Release.
- Advanced-data compiled-only matrix: all four modes passed (ABI-v9 Debug and
  Release; ABI-v10 Debug and Release). Each mode used 14 provider artifact
  pairs, removed provider Moss source, then checked, formatted, built, and ran
  the consumer.
- Eight ABI-v9 source-free SDK controls passed: data acceptance, encoding,
  filesystem, text, path, Foundations collections, Foundations algorithms,
  and Foundations generic/Range acceptance. The Range/FileIO consumer returned
  `329` after provider source removal.
- Full Foundations acceptance passed: 44 seeded sort cases and 8 byte cases
  in native O0, native Release, and Fast Debug, plus negative contracts and
  source-free consumers.
- Full Text & Encoding acceptance passed: 12 text tests, 5 encoding tests,
  and 3 cross-package acceptance tests in native Debug and Release, plus the
  66-valid/15-invalid UTF-8 differential oracle in Debug, Release, and Fast
  Debug.

The advanced-data Fast Debug workload that had been interrupted before R5 was
freshly exercised inside `make check`: the package `margo debug` job passed in
966.09 seconds. The bounded seeded Fast Debug ndarray/table oracle also passed
in 428.24 seconds (seed `240024`). Both are part of the R5 regression scope;
neither is being counted from historical evidence.

## ABI-v10 artifact, migration, and hostile results

The following fresh gates passed on this compiler source:

- ABI-v9-to-v10 mismatch and damaged-v9 migration diagnostics require rebuild
  for an old ABI and retain the distinct malformed-current-v9 diagnostic.
- Warm provider binding and transitive external-provider cache revalidation
  passed. Cross-crate scenario 11 passed after replacing provider layout and
  reported `invalidation_proven: true`.
- Four cross-crate fault-injection controls passed, rejecting missing
  dependencies, unrelated compiler errors, forged/stale metadata, and
  incomplete consumer linking.
- Mixed-provider per-callable admission passed, including source-free
  supported specialization, unavailable/missing-body rejection, provenance
  mutations, and the ABI-v9 control.
- The focused deferred-member relay suite passed source-backed and ABI-v9
  controls in both profiles, the ABI-v10 concrete `custom_action()` result and
  assertion-effect positive control, and five member/result/effect/private
  dependency/provenance hostiles.
- Direct nominal source-free Int/String projection passed with seven hostile
  identity, field, visibility, and dependency mutations rejected.
- The M2b2 source-free corpus passed, including provider legacy-body removal,
  checked-body projection, and hostile identity/effect/dependency/provenance
  mutations.

## Independent Architect review

The Architect independently reviewed and executed representative cases against
the exact compiler source SHA and `./moss` fingerprint above. They independently
passed staged-v10 scenario 7 in Debug and Release, the deferred-member relay
suite, direct nominal controls, mixed-provider admission, and scenario 11
cache replacement. They found no P0 or P1 defects in that representative
slice.

The Architect issued **overall R5 NO-GO** pending triage of the seven
`make check` failures. Two P2 hardening notes remain unresolved:

1. The ABI-v10 validator does not reject extra private records outside the
   exported dependency closure, although the producer emits reachable closure
   and the loader does not add these records to public declarations.
2. Provider bytes contribute to the native cache fingerprint through the
   64-bit FNV `stable_hash`. Ordinary replacement invalidation passed; a
   crafted same-FNV collision was not tested. ABI-v10 design §2 reserves FNV
   for transient lookup, not security/integrity fields.

The Architect's verdict, run evidence, and these P2 notes are recorded here;
they were not closed by the representative test runs.

## Invocation notes

Some exploratory invocations were not acceptance runs: three scripts without a
`--help` option were mistakenly given `--help` as their compiler positional
argument, and the first Foundations source-free invocation omitted required
arguments. The first full Foundations command also used a relative compiler
path that Margo resolved under the package directory. The migration/cache
scripts were rerun with `./moss` and passed; both Foundations gates were rerun
with absolute repository-local compiler paths and required workdir arguments,
then passed. These setup errors are not included in the 136-job `make check`
count or the gate pass totals above.

## Recommendation to L7

Do **not** accept R5 release readiness yet. Resolve or explicitly disposition
the seven full-suite failures, close the two P2 findings to the Architect's
satisfaction, and rerun the required gates on one resulting committed
compiler candidate. ABI-v9 remains the default, ABI-v10 remains opt-in, and no
R5 acceptance or Phase 25.1 completion is declared by this report.

Raw command logs and JSON reports from this run are in disposable
`tmp/r5-final/`; the durable counts and findings above are the release record.

## R5.1 stabilization checkpoint — requalification in progress

The R5.1 compiler/test correction checkpoint `2b0b5f81` (repository HEAD
`b7d3d28b` after the task-scoped push authorization update) fixes the seven
original R5 failures and closes the two previously reported P2 findings. Its
focused compiler, ABI-v10 unit, semantic, integer-payload, conformance,
source-free hostile, native cache collision-injection, and feedback checks
passed. The existing Architect reviewed the final public-enum identity binding
delta and confirmed that public exports remain admitted while missing private
identities and non-imported external identities remain rejected.

A fresh full `make check` on `b7d3d28b`, using 16 jobs, ran all 136 jobs and
reported **134 passed, 2 failed, 0 skipped** in 1015.96 seconds wall time
(6073.10 cumulative worker seconds). All seven original R5 failures passed.
The two failures were test synchronization assumptions discovered by the full
parallel run: `check_phase20_executor_fileio` assumed a fixed order between
asynchronous root output and the submitter's print; `check_phase21c_branch_errors`
asserted quiescence before submitted roots had joined. The executor test now
allows either root/submitter print order but requires both once and the joined
result last. Branch scope and read-borrow checks now occur after `join`, with
the injected read fault retained.

Both corrected gates pass individually. The corrected branch fault gate passed
20/20 serial repetitions in isolated output directories (137.762 seconds
total, 0 failures). At that checkpoint these test-only changes were not yet
committed; the first full-suite count is historical evidence for that
checkpoint, not final R5.1 qualification. A new full `make check`, `make examples`, compatibility/SDK
and advanced-data matrices, and exact-SHA Architect verdict remain required.
ABI-v9 remains the default, ABI-v10 remains opt-in, and R5.1 remains open.

## R5.1 final qualification — frozen candidate

The final tested compiler candidate is `caea7335f86f58f53d6f17662444e7c613ad39d0`.
Compiler source changes are from `2b0b5f81`; `caea7335` adds only the two
parallel-test synchronization corrections. `moss-dev` and `./moss` SHA-256:
`119416cc5aee866e7b025ad034a41430cb5ede90a4de8ee65f0800c01225567e`.
`moss-opt` SHA-256: `ab2d9348b3bc8a59b176f8486d5bd029f391d60bcd0fabe916817954ed272543`.
The candidate preserves ABI-v9 as the default and enables ABI-v10 through the
existing opt-in selector only.

### Original seven R5 failures

| Original failing job | Correction commit | Final result on frozen candidate | Review cycle |
|---|---|---|---|
| `check_phase25_team_a_semantics` — generic nominal open result | `2b0b5f81` | PASS in full `make check`; focused positive result-inference and ambiguity controls pass | Final Architect GO; no open P0/P1/P2 |
| `shard_phase25_int_payloads` — Iterate binder/element TypeRef mismatch | `2b0b5f81` | PASS in full `make check`; fresh `phase47_iteration.moss` shard passes | Final Architect GO; no open P0/P1/P2 |
| `check_phase25_typed_semantic_api` — requirement identity | `2b0b5f81` | PASS; checked-occurrence identity is verified by semantic control | Final Architect GO; no open P0/P1/P2 |
| `check_phase25_checked_body_ir` — stale C++ aggregate fixture | `2b0b5f81` | PASS; fixture now initializes current IR shape and checked-body gate passes | Final Architect GO; no open P0/P1/P2 |
| `check_phase25_r2_wp4_oracle_inventory` — callback outputs | `2b0b5f81` | PASS; expected `23` and `7` verified by oracle control | Final Architect GO; no open P0/P1/P2 |
| `check_phase25_conformance` — obsolete unavailable-body contract | `2b0b5f81` | PASS; source-free missing-member result is `METHOD_NOT_FOUND` with locations | Final Architect independently ran scenario 7 Debug/Release; GO |
| `check_phase21c_branch_errors` — injected read-fault synchronization | `caea7335` | PASS; 20/20 isolated repetitions, 0 failures, 137.762 seconds total; fault injection retained | Final Architect GO; no open P0/P1/P2 |

The full parallel run on `b7d3d28b` additionally exposed two test-ordering
assumptions. `caea7335` corrected asynchronous executor output ordering and
moved branch quiescence assertions after `join`; both are harness-only changes.
The initial `b7d3d28b` result (134/136) is superseded by the final full run.

### Final regression and package gates

- `TMPDIR="$PWD/tmp" make --jobs="$(nproc)" check`: **136/136 pass, 0 fail,
  0 skipped**, using 16 jobs; 1019.14 seconds wall time and 6133.92 cumulative
  worker seconds. No retries were performed by the runner.
- `TMPDIR="$PWD/tmp" make --jobs="$(nproc)" examples`: **32 examples built**;
  the intentional negative `use_after_transfer.moss` example was skipped.
  Nested rustc jobserver file-descriptor warnings were non-fatal.
- 32-case compatibility oracle: **64/64** outcomes each for source-backed,
  ABI-v9, and ABI-v10; no unexpected results.
- Real SDK generic consumer matrix: **18/18**.
- Staged ABI-v10 cross-crate harness: **11/11 Debug and 11/11 Release**,
  including scenario 7's `METHOD_NOT_FOUND`, member name, consumer location,
  and related provider location.
- Staged ABI-v10 primary SDK package matrix: **34/34** Debug/Release builds
  across 17 packages.
- ABI-v9 SDK source-free controls: **8/8** (data acceptance, encoding, fs,
  text, path, Foundations generic/Range acceptance, algorithms, collections).
  Foundations Range source-free execution returned `329`.
- Advanced-data compiled-only source-free matrix: **4/4 modes** (ABI-v9/v10 ×
  Debug/Release), 14 provider pairs per mode, provider Moss source removed.
- R4 deferred-member positive and hostile suite, direct nominal suite, mixed
  provider suite, ABI migration, native cache binding/collision injection,
  and full prioritized M2b2 source-free/hostile corpus all pass.
- ABI-v10 mode remains opt-in without shape-specific flags. ABI-v9 mismatch
  rebuild diagnostics, stale/replaced artifacts, cache invalidation, forged
  provenance, unreachable/substituted private records, and legacy generic-body
  removal remain covered and pass.
- The broader advanced-data `run_all.py` Fast Debug workload passed inside the
  final `make check` (`sdk_adv_data_margo_debug`, 993.25 seconds). The bounded
  seeded Fast Debug ndarray/table oracle was not rerun separately on this
  candidate; the historical 428.24-second result is not counted as fresh.

The branch-fault event was not disabled: the corrected test still injects the
native read fault and checks the joined executor state. Twenty controlled
isolated repetitions passed after the synchronization fix. The earlier R5
single failure is retained as historical evidence rather than erased by the
rerun.

One exploratory invocation of the Foundations source-free validator omitted
its required `--foundations-root`, `--workdir`, and `--moss` arguments and exited
with usage status 2. It was rerun with all required repository-local paths and
passed; this setup error is not counted as a test failure or as a pass.

### Architect review and disposition

The existing independent Adversarial Architect reviewed exact source SHA
`caea7335f86f58f53d6f17662444e7c613ad39d0` and issued **GO**, with zero open
P0/P1/P2. The Architect verified `./moss` SHA-256
`119416cc5aee866e7b025ad034a41430cb5ede90a4de8ee65f0800c01225567e` and
`moss-opt` SHA-256
`ab2d9348b3bc8a59b176f8486d5bd029f391d60bcd0fabe916817954ed272543`.

The independent review cycle executed staged-v10 scenario 7 in Debug and
Release with `./moss`, and Release with `moss-opt`; all returned
`METHOD_NOT_FOUND` for `custom_action` with consumer primary and provider
related locations. The Architect also independently passed the member-relay
positive/effect/result and hostile suite; direct nominal source-free controls
and seven hostiles; mixed-provider admission; the full prioritized M2b2
source-free corpus; ABI-v10 closure/referential-integrity/public-enum identity
adversarial executable; deterministic fast-hash collision executable; stale
provider replacement scenario 11; and the isolated expression ownership-edge
suite. The two original P2 findings are closed: private records are constrained
to the validated dependency closure, and native cache artifact trust includes
SHA-256 of exact bytes in addition to the fast lookup hash. The public enum
identity review also closed the follow-up compatibility concern.

This was an independent representative adversarial review; the Architect did
not rerun the entire 136-job manifest or all 17 SDK packages. Those full
qualification gates were executed by the coordinator as listed above. The
Architect returned GO on the compiler candidate before the documentation-only
closeout commit; no compiler or test source changes followed the reviewed SHA.

Known scope limitation: these gates establish the listed compiler, SDK,
source-free, migration, cache, and hostile paths only. They do not authorize a
new language feature, ABI-v10 default promotion, merge to another branch, R5,
or Phase 25.1 completion. ABI-v9 remains default and ABI-v10 remains opt-in.

## L7 administrative closeout — ACCEPTED; COMPLETE — 2026-10-23

L7 formally accepts Phase 25.1 R5.1 and declares **Phase 25.1 COMPLETE** at
the reviewed compiler candidate `caea7335f86f58f53d6f17662444e7c613ad39d0`.
The accepted compiler fingerprints remain `./moss`/`moss-dev` SHA-256
`119416cc5aee866e7b025ad034a41430cb5ede90a4de8ee65f0800c01225567e` and
`moss-opt` SHA-256
`ab2d9348b3bc8a59b176f8486d5bd029f391d60bcd0fabe916817954ed272543`. The
validation results and exact-SHA independent Architect GO above are preserved
without rerun or modification; this closeout changes documentation only.

ABI-v9 remains the default and ABI-v10 remains opt-in. No merge to `main` or
`phase25-integration` was performed, and ABI-v10 was not promoted. No further
stabilization work is authorized without a new L7 assignment.
