# Phase 21 unified engineering release review

> **Implementation complete; ready for independent Architect acceptance
> (2026-10-11).** The exact validated implementation is
> `c1ef3ebb13d7c74c3ed86a45d45e536d527161da` on
> `phase-21-integration`. ABI v9, standard errors and the external A–F repairs
> are integrated. The complete engineering gates passed on this source before
> the unchanged L7-supplied originals were imported; their focused acceptance
> runs pass separately. The new permanent original-source gate and final full
> rerun remain to be recorded. Independent Architect L6 review is
> pending. **NO-GO for merging into `main`.**

The last *independently reviewed* implementation remains `5941d98c824f31550f520891235c8b7ac96f3992`.
`docs/PHASE_21_ARCHITECT_REVIEW.md` records an independently attributable
**NO-GO** at that SHA. It identified ABI-v8 artifact forgery, missing
source-free warning provenance, and general proof gaps. L7 subsequently
approved ABI v9, compiler-defined standard error types, absolute Root typed
error boundaries, and corrected O2 wording. Later engineering work is
reported here without representing it as Architect acceptance.

## Baselines, scope and changed contract

| Item | Exact revision or contract |
| --- | --- |
| Original implementation-complete checkpoint | `8d8a9cc6f842e11701dbedc56e3d2d2d3da9852a` |
| External addendum failing baseline | `7775049` |
| Independent Architect L6 implementation | `5941d98c824f31550f520891235c8b7ac96f3992` |
| Validated implementation/source SHA | `c1ef3ebb13d7c74c3ed86a45d45e536d527161da`; later report commits change documentation only |
| Reconciled `origin/main` baseline | `d15e968778fa1cff3cd5085911c505592af54a30`, integrated at `7eb192b`; refetched at final validation and still an integration ancestor |
| Branch and merge gate | `phase-21-integration`; **do not merge into `main`** without explicit L7 approval |
| Independent Architect L6 and L7 acceptance | Pending exact-source Architect review and explicit L7 merge decision |

The language now provides variant-precise typed `raise`, lexical
`try`/`recover`, exhaustive Root-level `on_fail`, owned failure captures,
checked FileIO failures, bounded ordered Branch results, and checked numeric
parsing/conversion with canonical binary64 text. ABI v9 adds a provider-object
binding for canonical `.mossi` metadata, imported method effects and recovery
warning provenance. Compiler-recognized `std.FileError`, `std.ParseError`, and
`std.ConversionError` have stable identities and cross-crate READ/WRITE
transport. The specified numerical grammar and nearest, ties-to-even Float
formatting remain unchanged.

The ABI-v8 layout remains historically frozen; ABI v9 is an explicitly
versioned successor. A v8 source-free provider must be rebuilt. The
provider/object consistency guarantee assumes the selected provider build is
trusted; it is not publisher authentication. See
`docs/MOSS_SOURCE_FREE_ABI_V9.md` for exact digest, object marker, import,
cache and migration contracts.

Both external ZIP archives were absent from this workspace. L7 subsequently
supplied a single-text bundle described as a byte-for-byte transcription of
the original A–F README and six programs. We imported those seven files
unchanged and ran them against implementation `c1ef3eb`; every case met its
README expectation on supported execution paths. The ZIP itself was not
independently inspected. The earlier `p21_review_repros.zip` remains
unavailable, so its coverage remains reconstructed. Source hashes, exact
commands, exit codes and outputs are in `docs/PHASE_21_ORIGINAL_AF_RESULTS.md`;
the full issue-by-issue history, including EXT-01..20 and INT-01..13, is in
`docs/PHASE_21_PEER_REVIEW_DISPOSITION.md`.

## Layer 1: O1–O9 formal implementation obligations

A passing fixture establishes behavior on that path, not the general invariant.
The status below is an **engineering implementation argument** coupled to
exact-source behavioral gates. The independent Architect has not accepted it.
`docs/PHASE_21_FINAL_PROOF_AUDIT.md` contains the detailed O1/O4/O8 and
§42 early-release transfer-function argument and known counterexamples.

| Obligation and guarantee | Compiler/runtime locations and argument | Behavioral and adversarial evidence | Assumption, limit and current status |
| --- | --- | --- | --- |
| **O1:** Every accepted expression and call preserves variant-precise typed raises, separate panic, and observable effects through recovery and imports. | `src/moss.cpp`: `observable_expression_effects`, `analyze_effect_block`, specialized handler effects, imported callable records; `src/file_chunk_lowering.inc` uses their summaries. | `check_phase21_peer_effects.py`, unary/projection gates, A/C addendum exact `check --json`/`effects --json`, imported-effects and source-free method gates. `6aa69be` repairs trailing method expression and concrete generic specialization; `9af6d79` excludes the raised effect of a well-formed recovery arm no variant can enter. | Accepted syntax has a recursive transfer; unresolved concrete calls and typed-raising custom iterators fail closed. ABI-v9 imported effects are artifact-bound. The 13-gate independent engineering audit and full `make check` passed on `c1ef3eb`. **ENGINEERING PASS under documented assumptions; Architect pending.** |
| **O2:** `h` and `h$fail` have independent lock plans. The trailer adds no per-leaf WRITE requirement to `h`, but its signature column can repartition classes and change normal exclusive lock count. | `src/moss.cpp`: `specialized_handler_failure_effects`, `build_synchronization_plan`; `src/handler_lowering.inc`: failure wrapper. | `check_phase21_handler_failure_native.py`, `check_phase21_peer_warnings.py`, A/C grouped Root plan check; exact-source full suite passes. | Uses the final class partition and ranked acquisition; no assumption of invariant class count. L7 approved wording correction; normative text updated. **ENGINEERING PASS, conditional on O1.** |
| **O3:** Required recovery locks are planned and acquired in rank order before the relevant `try`/first observable action; no dynamic upgrade. | `src/moss.cpp`: `derive_path_placement`; `src/handler_lowering.inc`: path wrapper. | Recovery-plan, lock inflation warning, `check_phase21_early_placement_proof.py`, concurrent Root gate; exact-source full suite passes. | Closed static routes and complete effect/remaining-path sets are required. **ENGINEERING PASS, conditional on O1/O4.** |
| **O4:** Exceptional edges, definite initialization, moves and D7 recovery ownership agree at the expression evaluation point, including loops, nested recovery and re-raise. | `src/moss.cpp`: `check_ownership_block` and exceptional effect/CFG walkers; `src/handler_lowering.inc`: tagged carrier and `Option<T>` capture slots. | `check_phase21_expression_ownership_edges.py`, `check_phase21_recover_reinit.py`, `check_phase21_early_condition_raise.py`, `check_phase21_on_fail_capture.py`; A/C grouped reachable-variant gate. `f4519fd` still checks unreachable arm bodies but excludes their impossible ownership successors; the gate includes unreachable consume positive and reachable consume negative. | The proof audit traces evaluation-point snapshots, selected exceptional edges, joins and native lowering. The 13-gate audit and full suite passed on `c1ef3eb`. **ENGINEERING PASS under documented assumptions; Architect pending.** |
| **O5:** A raise materializes an owned payload and releases exactly acquired old guards after their last protected access. | `src/handler_lowering.inc`: typed wrapper result and guard drops. | Native Root failure, capture and lock-hook gates, including source-free provider frames; exact-source full suite passes. | Assumes Rust RAII and no hidden guard in owned carrier; depends on O8. **ENGINEERING PASS conditional.** |
| **O6:** Root failure sequence is join/quiesce, abandon Root-local FileIO, release old guards, acquire fresh failure locks, dispatch `on_fail`, fulfill reply, release. | `src/handler_lowering.inc`: Root wrapper; `src/fileio_runtime.hpp`: abandon; `src/file_chunk_codegen.inc` and `src/executor_runtime.hpp`: Branch join. | `check_phase21_fileio_root_cleanup.py`, `check_phase21_lock_fileio_adversarial.py`, `check_phase21_chunk_fileio_raise.py`, concurrent Roots and injected cleanup failures; exact-source full suite passes. | Finite OS work and complete O1/O4/O9 summaries required. **ENGINEERING PASS conditional.** |
| **O7:** Nested handlers propagate tagged raises without Root trailer dispatch; static routes preserve Root totality. | `src/handler_lowering.inc`: `_shared` versus Root dispatch; `src/moss.cpp`: post-inference totality and route closure. | `check_phase21_root_totality.py`, `check_phase21_source_free_abi.py`, ordinary/nested/source-free handlers; exact-source full suite passes. | Absolute Root boundary holds under checker totality and the tested ingress paths, including trailer body. **ENGINEERING PASS conditional on bound provider effects.** |
| **O8:** Failure frame owns its values and carries no guard, protected borrow, FileIO, Range, RangeBatch or domain capability across cleanup. | `src/moss.cpp`: per-edge capture/type checks and recursive scoped-resource predicate; `src/handler_lowering.inc`: `Option<T>` slots; `src/interpreter_domains.inc`: fresh frame. | `check_phase21_on_fail_capture.py`, `check_phase21_source_free_proof_edges.py`, protected-state negative, removed-source provider capture/method call; exact-source full suite passes. | Depends on O1/O4 for complete exits and ABI-v9 binding for imported contracts. The recursive capture/type and selected-edge audit closes the implemented invariant. **ENGINEERING PASS under documented assumptions; Architect pending.** |
| **O9:** Each bounded-K window joins fully before observing indexed outcomes or parent fold; terminal exits have zero live read tokens and no further publication. | `src/file_chunk_lowering.inc`: mapper eligibility; `src/file_chunk_codegen.inc`: publish/join/ordered commit; `src/executor_runtime.hpp`: scope join; `src/fileio_runtime.hpp`: read tokens. | `check_phase21c_branch_errors.py` actual four-Root concurrent cases, indexed error/EOF/short/fold precedence, D/E mapper and seed cases; exact-source full suite passes. | Assumes complete O1 panic/raise facts, finite mapper and OS work, scheduler progress. **ENGINEERING PASS conditional.** |

The **general early-release guarantee** is separate from O3. The implemented
path optimization handles a first top-level `if`: it hoists rank-forced
classes, cancels only classes untouched by the chosen complete path, and
never releases a touched class before the path completes. Condition effects
that can externally observe work force full entry. The source-backed witness
in `check_phase21_early_placement_proof.py` is positive behavioral evidence;
`a10d8bb` clarifies that unreachable recovery state writes may conservatively
widen a normal handler's lock and warning footprint. Safety requires a sound
superset with rank ordering and no touched-guard cancellation, not a minimal
lock count. This does not reintroduce unreachable typed raises or ownership
successors into O1/O4. The proof audit's structural O1/O4 argument and
13 exact-source gates close the implemented bounded-placement claim in
engineering review. **ENGINEERING PASS under documented assumptions;
independent Architect acceptance pending.**

## Layer 2: public language semantics

| Surface | Exact-source engineering evidence | Acceptance boundary |
| --- | --- | --- |
| Local helpers, methods, generics, collections and ordinary expressions | Exact variants and panic separation in peer-effects, unary/projection and reconstructed A/C gates; the unchanged supplied A rejects unhandled `E.A`, while C checks with only `E.A` escaping and prints `outer A`. Other A/C gates cover unreachable explicit raise and reachable/unreachable consume. | The O1/O4 proof closes accepted and fail-closed syntax under stated assumptions; independent Architect assessment remains pending. |
| Root ingress and recovery | Unhandled synchronous/Executor Roots receive `ROOT_UNHANDLED_RAISE`; source-free and positive lexical recovery/`on_fail` pass native and Fast Debug gates. | No typed raise may cross a Root; native `runtime_invoke` and bound imported effects rely on the checked Root contract. |
| Pipelines and Branches | The unchanged supplied D/E programs use `p4.bin=ABABC` and `p6.bin=ABCD`, require `-1` through `on_fail`; exact effects and O0/O3 output pass. Separate gates cover ordered speculative precedence and concurrent quiescence. | File-chunk Fast Debug has a documented interpreter limit; native ordering and resource cleanup are the required physical witnesses. |
| Source-free packages | ABI v9 binds canonical `.mossi` to selected `.rlib`; exact-source gates reject v8, missing, stale, forged, warm-cache and valid-but-mismatched artifact pairs. Removed-source methods, warning provenance and cross-crate standard-error WRITE recovery execute. | Contract consistency assumes a trusted provider build; this is not publisher authentication. Architect review pending. |
| FileIO, locks and errors | Typed FileIO, standard error identity, injected cleanup, fresh failure locks and concurrent Roots pass exact-source gates. | Failed `create` may leave a name; later retry `sync()` does not prove that name durable. Descriptor cleanup remains required. |
| Numeric conversion and Float | Exact-source parse/conversion gates and B/F expression corpus pass; unchanged supplied B/F also match their README output in O0/O3/Fast Debug. GCC/Clang × Rust 1.97/1.98 matches the pinned corpus bytes. | Grammar and nearest, ties-to-even formatting remain frozen; retain pinned corpora in CI. |
| Phase 20 Range | Concrete cross-crate `Range` emits `FILEIO_CROSS_CRATE_ABI` at Moss checking; legal same-module and consumer-specialized generic forms pass strict Phase 20 review. | Full concrete interoperability is **DEFERRED-APPROVED** as Phase 20 work. |

## Validation ledger and exact environment

All results below used exact implementation/source
`c1ef3ebb13d7c74c3ed86a45d45e536d527161da`. Later release-report commits
change documentation only. The historical `5941d98` full-suite result and
independent Architect NO-GO remain separately attributable in
`docs/PHASE_21_ARCHITECT_REVIEW.md`.

| Gate | Exact-source result |
| --- | --- |
| `TMPDIR="$PWD/tmp" make check` | **PASS**, exit 0, ending `all Moss v0.1 tests passed` before import of the unchanged supplied A–F. Includes reconstructed A–F, ABI-v9 forgery/cache/migration, imported effects and warning, standard error, Root/Branch and FileIO fault gates. Rerun after the new original-source gate is integrated. |
| `TMPDIR="$PWD/tmp" make examples` | **PASS**, exit 0; 32 positive example builds. |
| Strict GCC and Clang C++17 | **PASS** under GCC 13.3.0 and Clang 18.1.3, both `-std=c++17 -O2 -Wall -Wextra -Werror -pedantic`. |
| Strict Phase 20 review in `make check` | **PASS**: 36 bug, 30 guard and two pre-existing probes; eight design proposals remain INFO. Phase 20 Executor runtime 54/54 passes. |
| Unchanged L7-supplied A/C | **PASS**: A is rejected at Moss checking, both native generator modes and Fast Debug for the unhandled `E.A`; a recovery-only scratch companion reports exact `via_local` raise set. C checks with only `E.A` escaping and prints `outer A` in native O0/O3 and Fast Debug. Other permanent A/C controls pass. |
| Unchanged L7-supplied D/E | **PASS**: exact `FormatError.Bad`/`SetupError.Bad` plus six standard FileError effects and native O0/O3 `-1` through Root `on_fail` with isolated `ABABC`/`ABCD` input files. Fast Debug reports its intentional FileIO unsupported diagnostic. |
| Unchanged L7-supplied B/F and numerical parity | **PASS**: B/F exact README output in native O0/O3 and Fast Debug; separate numeric parsing/conversion, IEEE edge and 270-pattern corpus gates pass. |
| ABI-v9 source-free adversarial | **PASS**: removed-source execution; recomputed-hash pattern forgery, warm-cache edited/deleted provider, valid-but-mismatched provider swap, v8 rebuild and malformed v9 contract rejected at checking/build boundary. |
| O1/O4/O8 and early-release proof retest | **PASS**: independent engineering reviewer ran 13 focused exact-source gates and closed the implementation argument under accepted/fail-closed syntax and stated assumptions. Architect review remains separate. |
| Concurrent and fault-injection suites | **PASS** in `make check`: actual concurrent Executor/Root/Branch, FileIO cleanup and failure injection, zero terminal Branch token witnesses. |
| Float portability | **PASS**: GCC 13.3.0 and Clang 18.1.3 × Rust 1.97.0 and 1.98.1. All four pairs matched native O0/O3 and Fast Debug byte-for-byte against both pinned corpora. |
| `origin/main` reconciliation | **PASS**: refetched `d15e968778fa1cff3cd5085911c505592af54a30`; remains ancestor of integration. |
| `git diff --check` | **PASS** for this documentation revision; integration final clean-tree check belongs to the release commit. |

Host: Linux `6.18.33.2-microsoft-standard-WSL2` x86_64, Python 3.12.3,
GCC 13.3.0, Clang 18.1.3, Rust 1.97.0 and 1.98.1. The pinned 270-pattern
canonical Float text SHA-256 is
`91553f7482c35cfef6c108c429c7bdb54fba4cc8d4959c4d0a7bd3faf1ca28ef`;
the reconstructed expression corpus SHA-256 is
`4a14b7bee3f4f391105be90ddda1e4c01eac629ac0f6184fc0e703c8c5e23c00`.
A reproducible CI job should pin both toolchain pairs and digests and archive
byte-for-byte outputs.

## Outstanding decisions, risk and release recommendation

L7 approved ABI v9, compiler-recognized standard errors, the absolute Root
typed-error boundary, O2 wording correction and two narrow Phase 20 deferrals.
No mandatory acceptance criterion is waived. The final ABI-v9 contract remains
subject to L7 acceptance before merging.

| Owner | Recommendation | Basis |
| --- | --- | --- |
| Engineering L6 | **GO conditional on the final original-source gate and full rerun** | The unchanged supplied A–F pass focused acceptance and the prior full validation passed on identical compiler source. The new permanent gate and final full-suite run remain to be recorded. O1/O4/O8 and bounded early-release implementation arguments are closed under documented assumptions. |
| Independent Architect L6 | **Historic NO-GO at `5941d98`; no vote on `c1ef3eb`** | The attributable review predates approved ABI v9 and A–F repairs. L7 coordinates exact-source independent assessment. |
| L7 | **NO-GO for merging into `main` pending review and explicit approval** | Architectural acceptance and merge authorization remain with L7. |

The remaining difference is temporal: engineering repaired the Architect's
observed ABI-v8 blockers, while that independent reviewer has not assessed the
new implementation. The exact source SHA and evidence are available for that
review. Stop before merging into `main`.
