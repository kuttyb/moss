# Phase 21 unified engineering release review

> **Engineering and independent Architect L6: GO for Phase 21 at exact
> implementation candidate `19cf731ae08291500e7488c8a03df123aac4f9aa`
> (2026-10-11).** Architect L6 independently rejected earlier `96eef9e`,
> then retested the repaired Root expressions and adjacent ABI, effect,
> ownership and locking gates. Its attributable verdict is in
> `PHASE_21_ARCHITECT_V9_RETEST_19CF731.md`. **HOLD merging into `main`
> until explicit L7 authorization.**

The latest *independently reviewed* implementation is
`19cf731ae08291500e7488c8a03df123aac4f9aa`, with an attributable
**GO** in `docs/PHASE_21_ARCHITECT_V9_RETEST_19CF731.md`. Its source fix is
`3a964dc9aa22bfc7d85cccaa57515c532ee839b2`. The prior Architect
**NO-GO** reports remain unchanged and attributable to `96eef9e` and
`5941d98`, respectively.

## Baselines, scope and changed contract

| Item | Exact revision or contract |
| --- | --- |
| Original implementation-complete checkpoint | `8d8a9cc6f842e11701dbedc56e3d2d2d3da9852a` |
| External addendum failing baseline | `7775049` |
| Latest independently reviewed implementation | `19cf731ae08291500e7488c8a03df123aac4f9aa` (**Architect GO for Phase 21 scope**) |
| Current engineering repair/source SHA | `3a964dc9aa22bfc7d85cccaa57515c532ee839b2`; preserves ABI v9 and adds nested Root ingress propagation plus a permanent regression gate |
| Reconciled `origin/main` baseline | `d15e968778fa1cff3cd5085911c505592af54a30`, integrated at `7eb192b`; refetched at final validation and still an integration ancestor |
| Branch and merge gate | `phase-21-integration`; **do not merge into `main`** without explicit L7 approval |
| Independent Architect L6 and L7 acceptance | Architect GO at `19cf731`; explicit L7 merge decision remains pending |

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
exact-source behavioral gates. The independent Architect challenged these
claims at `19cf731` and voted GO within the approved Phase 21 scope; its
independent evidence and limits are in the exact-SHA retest addendum.
`docs/PHASE_21_FINAL_PROOF_AUDIT.md` contains the detailed O1/O4/O8 and
§42 early-release transfer-function argument and known counterexamples.

| Obligation and guarantee | Compiler/runtime locations and argument | Behavioral and adversarial evidence | Assumption, limit and current status |
| --- | --- | --- | --- |
| **O1:** Every accepted expression and call preserves variant-precise typed raises, separate panic, and observable effects through recovery and imports. | `src/moss.cpp`: `observable_expression_effects`, `analyze_effect_block`, specialized handler effects, imported callable records; `src/file_chunk_lowering.inc` uses their summaries. | Prior peer-effects, unary/projection, A/C and source-free gates pass. `check_phase21_root_expression_ingress.py` adds Architect-discovered binary/vector/call/condition Root context and source-removed provider controls. | The `96eef9e` Architect NO-GO exposed a main-expression Root projection gap. `3a964dc` propagates Root ingress through every recursive transfer and conditions; a no-trailer nested Root remains rejected. **ENGINEERING PASS; Architect independently retested the repair at `19cf731` and voted GO.** |
| **O2:** `h` and `h$fail` have independent lock plans. The trailer adds no per-leaf WRITE requirement to `h`, but its signature column can repartition classes and change normal exclusive lock count. | `src/moss.cpp`: `specialized_handler_failure_effects`, `build_synchronization_plan`; `src/handler_lowering.inc`: failure wrapper. | `check_phase21_handler_failure_native.py`, `check_phase21_peer_warnings.py`, A/C grouped Root plan check; exact-source full suite passes. | Uses the final class partition and ranked acquisition; no assumption of invariant class count. L7 approved wording correction; normative text updated. **ENGINEERING PASS, conditional on O1.** |
| **O3:** Required recovery locks are planned and acquired in rank order before the relevant `try`/first observable action; no dynamic upgrade. | `src/moss.cpp`: `derive_path_placement`; `src/handler_lowering.inc`: path wrapper. | Recovery-plan, lock inflation warning, `check_phase21_early_placement_proof.py`, concurrent Root gate; exact-source full suite passes. | Closed static routes and complete effect/remaining-path sets are required. **ENGINEERING PASS, conditional on O1/O4.** |
| **O4:** Exceptional edges, definite initialization, moves and D7 recovery ownership agree at the expression evaluation point, including loops, nested recovery and re-raise. | `src/moss.cpp`: `check_ownership_block` and exceptional effect/CFG walkers; `src/handler_lowering.inc`: tagged carrier and `Option<T>` capture slots. | `check_phase21_expression_ownership_edges.py`, `check_phase21_recover_reinit.py`, `check_phase21_early_condition_raise.py`, `check_phase21_on_fail_capture.py`; A/C grouped reachable-variant gate. `f4519fd` still checks unreachable arm bodies but excludes their impossible ownership successors; the gate includes unreachable consume positive and reachable consume negative. | The proof audit traces evaluation-point snapshots, selected exceptional edges, joins and native lowering. The 13-gate audit and full suite passed on `c1ef3eb`; Architect independently reran focused O4/O8 gates at `19cf731`. **ENGINEERING PASS under documented assumptions; Architect GO within Phase 21 scope.** |
| **O5:** A raise materializes an owned payload and releases exactly acquired old guards after their last protected access. | `src/handler_lowering.inc`: typed wrapper result and guard drops. | Native Root failure, capture and lock-hook gates, including source-free provider frames; exact-source full suite passes. | Assumes Rust RAII and no hidden guard in owned carrier; depends on O8. **ENGINEERING PASS conditional.** |
| **O6:** Root failure sequence is join/quiesce, abandon Root-local FileIO, release old guards, acquire fresh failure locks, dispatch `on_fail`, fulfill reply, release. | `src/handler_lowering.inc`: Root wrapper; `src/fileio_runtime.hpp`: abandon; `src/file_chunk_codegen.inc` and `src/executor_runtime.hpp`: Branch join. | `check_phase21_fileio_root_cleanup.py`, `check_phase21_lock_fileio_adversarial.py`, `check_phase21_chunk_fileio_raise.py`, concurrent Roots and injected cleanup failures; exact-source full suite passes. | Finite OS work and complete O1/O4/O9 summaries required. **ENGINEERING PASS conditional.** |
| **O7:** Nested handlers propagate tagged raises without Root trailer dispatch; static routes preserve Root totality. | `src/handler_lowering.inc`: `_shared` versus Root dispatch; `src/moss.cpp`: post-inference totality and route closure. | `check_phase21_root_totality.py`, `check_phase21_source_free_abi.py`, ordinary/nested/source-free handlers; exact-source full suite passes. | Absolute Root boundary holds under checker totality and the tested ingress paths, including trailer body. **ENGINEERING PASS conditional on bound provider effects.** |
| **O8:** Failure frame owns its values and carries no guard, protected borrow, FileIO, Range, RangeBatch or domain capability across cleanup. | `src/moss.cpp`: per-edge capture/type checks and recursive scoped-resource predicate; `src/handler_lowering.inc`: `Option<T>` slots; `src/interpreter_domains.inc`: fresh frame. | `check_phase21_on_fail_capture.py`, `check_phase21_source_free_proof_edges.py`, protected-state negative, removed-source provider capture/method call; exact-source full suite passes. | Depends on O1/O4 for complete exits and ABI-v9 binding for imported contracts. The recursive capture/type and selected-edge audit closes the implemented invariant. **ENGINEERING PASS under documented assumptions; Architect GO within Phase 21 scope.** |
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
Architect independently reran the focused gate and voted GO within Phase 21 scope.**

## Layer 2: public language semantics

| Surface | Exact-source engineering evidence | Acceptance boundary |
| --- | --- | --- |
| Local helpers, methods, generics, collections and ordinary expressions | Exact variants and panic separation in peer-effects, unary/projection and reconstructed A/C gates; the unchanged supplied A rejects unhandled `E.A`, while C checks with only `E.A` escaping and prints `outer A`. Other A/C gates cover unreachable explicit raise and reachable/unreachable consume. | The O1/O4 proof closes accepted and fail-closed syntax under stated assumptions; Architect's exact-SHA addendum records its focused retest and Phase 21 GO. |
| Root ingress and recovery | Unhandled synchronous/Executor Roots receive `ROOT_UNHANDLED_RAISE`; source-free and positive lexical recovery/`on_fail` pass native and Fast Debug gates. | No typed raise may cross a Root; native `runtime_invoke` and bound imported effects rely on the checked Root contract. |
| Nested Root expressions | `check_phase21_root_expression_ingress.py` proves the valid direct/binary/vector-value/call/condition forms check and execute in O0/O3 and Fast Debug; it removes provider source and rebuilds a nested consumer. A nested no-trailer Root remains `ROOT_UNHANDLED_RAISE`. | Architect L6 found the false rejection on `96eef9e`; it independently retested the `3a964dc` repair at `19cf731` and voted GO. |
| Pipelines and Branches | The unchanged supplied D/E programs use `p4.bin=ABABC` and `p6.bin=ABCD`, require `-1` through `on_fail`; exact effects and O0/O3 output pass. Separate gates cover ordered speculative precedence and concurrent quiescence. | File-chunk Fast Debug has a documented interpreter limit; native ordering and resource cleanup are the required physical witnesses. |
| Source-free packages | ABI v9 binds canonical `.mossi` to selected `.rlib`; exact-source gates reject v8, missing, stale, forged, warm-cache and valid-but-mismatched artifact pairs. Removed-source methods, warning provenance and cross-crate standard-error WRITE recovery execute. | Contract consistency assumes a trusted provider build; this is not publisher authentication. Architect reran focused forgery/cache/source-free gates at `19cf731` and voted GO within that trust boundary. |
| FileIO, locks and errors | Typed FileIO, standard error identity, injected cleanup, fresh failure locks and concurrent Roots pass exact-source gates. | Failed `create` may leave a name; later retry `sync()` does not prove that name durable. Descriptor cleanup remains required. |
| Numeric conversion and Float | Exact-source parse/conversion gates and B/F expression corpus pass; unchanged supplied B/F also match their README output in O0/O3/Fast Debug. GCC/Clang × Rust 1.97/1.98 matches the pinned corpus bytes. | Grammar and nearest, ties-to-even formatting remain frozen; retain pinned corpora in CI. |
| Phase 20 Range | Concrete cross-crate `Range` emits `FILEIO_CROSS_CRATE_ABI` at Moss checking; legal same-module and consumer-specialized generic forms pass strict Phase 20 review. | Full concrete interoperability is **DEFERRED-APPROVED** as Phase 20 work. |

## Validation ledger and exact environment

The repair-specific results below used source
`3a964dc9aa22bfc7d85cccaa57515c532ee839b2` in an isolated worktree and
the content-identical integration tree.
Earlier ABI-v9 and original A–F evidence used source `c1ef3eb`; those gates
also pass in the repaired full suite. Independent Architect NO-GO reports
remain attributable to `5941d98` and `96eef9e`; the later Architect GO is
attributable to exact implementation candidate `19cf731`.

| Gate | Exact-source result |
| --- | --- |
| `TMPDIR="$PWD/tmp" make check` | **PASS** on isolated and integrated repair source `3a964dc`, exit 0, ending `all Moss v0.1 tests passed`; includes the new nested Root/source-free gate and Phase 20 Executor runtime 54/54. |
| `TMPDIR="$PWD/tmp" make examples` | **PASS** on isolated and integrated repair source; 32 positive example builds. |
| Strict GCC and Clang C++17 | **PASS** on isolated repair source under GCC 13.3.0 and repository-local Clang 18.1.3, both `-std=c++17 -O2 -Wall -Wextra -Werror -pedantic`. |
| Strict Phase 20 review in `make check` | **PASS**: 36 bug, 30 guard and two pre-existing probes; eight design proposals remain INFO. Phase 20 Executor runtime 54/54 passes. |
| Unchanged L7-supplied A/C | **PASS**: A is rejected at Moss checking, both native generator modes and Fast Debug for the unhandled `E.A`; a recovery-only scratch companion reports exact `via_local` raise set. C checks with only `E.A` escaping and prints `outer A` in native O0/O3 and Fast Debug. Other permanent A/C controls pass. |
| Unchanged L7-supplied D/E | **PASS**: exact `FormatError.Bad`/`SetupError.Bad` plus six standard FileError effects and native O0/O3 `-1` through Root `on_fail` with isolated `ABABC`/`ABCD` input files. Fast Debug reports its intentional FileIO unsupported diagnostic. |
| Unchanged L7-supplied B/F and numerical parity | **PASS**: B/F exact README output in native O0/O3 and Fast Debug; separate numeric parsing/conversion, IEEE edge and 270-pattern corpus gates pass. |
| ABI-v9 source-free adversarial | **PASS**: removed-source execution; recomputed-hash pattern forgery, warm-cache edited/deleted provider, valid-but-mismatched provider swap, v8 rebuild and malformed v9 contract rejected at checking/build boundary. |
| O1/O4/O8 and early-release proof retest | **PASS**: independent engineering reviewer ran 13 focused exact-source gates and closed the implementation argument under accepted/fail-closed syntax and stated assumptions. Architect separately inspected the repair and reran adjacent focused gates at `19cf731`; it voted GO within the stated scope. |
| Concurrent and fault-injection suites | **PASS** in `make check`: actual concurrent Executor/Root/Branch, FileIO cleanup and failure injection, zero terminal Branch token witnesses. |
| Float portability | **PASS** on isolated repair source: GCC 13.3.0 and Clang 18.1.3 × Rust 1.97.0 and 1.98.1. All four pairs matched native O0/O3 and Fast Debug byte-for-byte against both pinned corpora. |
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

The Architect also reproduced a **separate pre-existing Moss v0.1 defect**:
`fn main(): echo [1]` passes `moss check` but generated Rust fails E0277;
Fast Debug prints `[1]`. The same result occurs at both `96eef9e` and
`19cf731`. Whole-vector `echo` is outside the approved Phase 21 Root/ABI
contract; the Architect explicitly excluded it from its Phase 21 GO. Track it
for general checker/native output hardening. The accepted Phase 21 vector
element case (`values = [message worker.Fetch()]; echo values[0]`) passes.

| Owner | Recommendation | Basis |
| --- | --- | --- |
| Engineering L6 | **GO at `19cf731`** | The new nested Root counterexample and source-free variant pass focused native/Fast Debug gates; isolated and integrated full suites and examples, strict C++ builds and Float matrix pass. |
| Independent Architect L6 | **GO at exact `19cf731` for approved Phase 21 scope** | The independent addendum retests original and source-free counterexamples, no-trailer safety and adjacent ABI/O1/O4/O8/locking gates. Its earlier `96eef9e` NO-GO remains intact. |
| L7 | **HOLD merging into `main` pending explicit approval** | L7 owns the final merge decision. |

Engineering repaired the Architect's concrete nested Root finding and the
Architect independently accepted the exact repaired candidate. Present this
release packet to L7 and stop before merging into `main`.
