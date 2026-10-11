# Phase 21 unified engineering release review

> **Engineering code-freeze checkpoint pending validation, not release approval
> (2026-10-11).** The latest implementation considered here is
> `c1ef3ebb13d7c74c3ed86a45d45e536d527161da` on
> `phase-21-integration`. ABI v9, standard errors and the reconstructed
> external A–F repairs are integrated. Final combined validation and independent
> Architect L6 review of this implementation have not finished. **NO-GO for
> merging into `main`.**

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
| Engineering code-freeze checkpoint | `c1ef3ebb13d7c74c3ed86a45d45e536d527161da`; validation in progress |
| Reconciled `origin/main` baseline | `d15e968778fa1cff3cd5085911c505592af54a30`, integrated at `7eb192b`; recheck before final freeze |
| Branch and merge gate | `phase-21-integration`; **do not merge into `main`** without explicit L7 approval |
| Final validated implementation SHA | **Pending** complete engineering gates; if validation requires another code edit, record and retest the new exact SHA |

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

The external `p21_review_repros.zip` and `p21_open_at_7775049.zip` were absent
from this workspace. L7 approved reconstruction of A–F. These permanent
fixtures preserve the reported expression, inputs and outputs, but do not
claim byte identity with the missing archive. The full issue-by-issue history,
including all EXT-01..20 and INT-01..13, remains in
`docs/PHASE_21_PEER_REVIEW_DISPOSITION.md`.

## Layer 1: O1–O9 formal implementation obligations

A passing fixture establishes behavior on that path, not the general invariant.
The status below is an **engineering assessment at the current checkpoint**;
it is subject to final-SHA gate reruns and independent Architect review.
`docs/PHASE_21_FINAL_PROOF_AUDIT.md` contains the detailed O1/O4/O8 and
§42 early-release transfer-function argument and known counterexamples.

| Obligation and guarantee | Compiler/runtime locations and argument | Behavioral and adversarial evidence | Assumption, limit and current status |
| --- | --- | --- | --- |
| **O1:** Every accepted expression and call preserves variant-precise typed raises, separate panic, and observable effects through recovery and imports. | `src/moss.cpp`: `observable_expression_effects`, `analyze_effect_block`, specialized handler effects, imported callable records; `src/file_chunk_lowering.inc` uses their summaries. | `check_phase21_peer_effects.py`, unary/projection gates, A/C addendum exact `check --json`/`effects --json`, imported-effects and source-free method gates. `6aa69be` repairs trailing method expression and concrete generic specialization; `9af6d79` excludes the raised effect of a well-formed recovery arm no variant can enter. | Imported ABI-v9 effects must be bound to the selected artifact; unknown effects fail closed. The proof audit must confirm every accepted expression form and downstream Root/Branch/lock consumer. **INCOMPLETE pending final proof and retest.** |
| **O2:** `h` and `h$fail` have independent lock plans. The trailer adds no per-leaf WRITE requirement to `h`, but its signature column can repartition classes and change normal exclusive lock count. | `src/moss.cpp`: `specialized_handler_failure_effects`, `build_synchronization_plan`; `src/handler_lowering.inc`: failure wrapper. | `check_phase21_handler_failure_native.py`, `check_phase21_peer_warnings.py`, A/C grouped Root plan check. | Uses the final class partition and ranked acquisition; no assumption of invariant class count. L7 approved wording correction; normative text updated. **PASS conditional on O1 and final plan rerun.** |
| **O3:** Required recovery locks are planned and acquired in rank order before the relevant `try`/first observable action; no dynamic upgrade. | `src/moss.cpp`: `derive_path_placement`; `src/handler_lowering.inc`: path wrapper. | Recovery-plan, lock inflation warning, `check_phase21_early_placement_proof.py`, concurrent Root gate. | Closed static routes and complete effect/remaining-path sets are required. **PASS for implemented placement, conditional on O1/O4 and final tests.** |
| **O4:** Exceptional edges, definite initialization, moves and D7 recovery ownership agree at the expression evaluation point, including loops, nested recovery and re-raise. | `src/moss.cpp`: `check_ownership_block` and exceptional effect/CFG walkers; `src/handler_lowering.inc`: tagged carrier and `Option<T>` capture slots. | `check_phase21_expression_ownership_edges.py`, `check_phase21_recover_reinit.py`, `check_phase21_early_condition_raise.py`, `check_phase21_on_fail_capture.py`; A/C grouped reachable-variant gate. `f4519fd` still checks unreachable arm bodies but excludes their impossible ownership successors; the gate includes unreachable consume positive and reachable consume negative. | Source order and variant selection must agree in checker, lowering and Fast Debug. Additional proof and final-SHA adversarial retest needed. **INCOMPLETE.** |
| **O5:** A raise materializes an owned payload and releases exactly acquired old guards after their last protected access. | `src/handler_lowering.inc`: typed wrapper result and guard drops. | Native Root failure, capture and lock-hook gates, including source-free provider frames. | Assumes Rust RAII and no hidden guard in owned carrier; depends on O8. **PASS conditional.** |
| **O6:** Root failure sequence is join/quiesce, abandon Root-local FileIO, release old guards, acquire fresh failure locks, dispatch `on_fail`, fulfill reply, release. | `src/handler_lowering.inc`: Root wrapper; `src/fileio_runtime.hpp`: abandon; `src/file_chunk_codegen.inc` and `src/executor_runtime.hpp`: Branch join. | `check_phase21_fileio_root_cleanup.py`, `check_phase21_lock_fileio_adversarial.py`, `check_phase21_chunk_fileio_raise.py`, concurrent Roots and injected cleanup failures. | Finite OS work and complete O1/O4/O9 summaries required. **PASS conditional on final native stress/fault rerun.** |
| **O7:** Nested handlers propagate tagged raises without Root trailer dispatch; static routes preserve Root totality. | `src/handler_lowering.inc`: `_shared` versus Root dispatch; `src/moss.cpp`: post-inference totality and route closure. | `check_phase21_root_totality.py`, `check_phase21_source_free_abi.py`, ordinary/nested/source-free handlers. | Absolute Root boundary must hold uniformly at synchronous, Executor and `runtime_invoke` ingress, including trailer body. **PASS on focused cases, final cross-ingress retest pending.** |
| **O8:** Failure frame owns its values and carries no guard, protected borrow, FileIO, Range, RangeBatch or domain capability across cleanup. | `src/moss.cpp`: per-edge capture/type checks and recursive scoped-resource predicate; `src/handler_lowering.inc`: `Option<T>` slots; `src/interpreter_domains.inc`: fresh frame. | `check_phase21_on_fail_capture.py`, `check_phase21_source_free_proof_edges.py`, protected-state negative, removed-source provider capture/method call. | Depends on O1/O4 for complete exits and ABI-v9 binding for imported contracts. General recursive type/edge audit and final-SHA retest pending. **INCOMPLETE.** |
| **O9:** Each bounded-K window joins fully before observing indexed outcomes or parent fold; terminal exits have zero live read tokens and no further publication. | `src/file_chunk_lowering.inc`: mapper eligibility; `src/file_chunk_codegen.inc`: publish/join/ordered commit; `src/executor_runtime.hpp`: scope join; `src/fileio_runtime.hpp`: read tokens. | `check_phase21c_branch_errors.py` actual four-Root concurrent cases, indexed error/EOF/short/fold precedence, D/E mapper and seed cases. | Assumes complete O1 panic/raise facts, finite mapper and OS work, scheduler progress. Reassess after A/D/E changes; final concurrent rerun pending. **PASS conditional.** |

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
successors into O1/O4. Full acceptance depends on the O1/O4 transfer-function audit and final proof
review. **Engineering proof status: INCOMPLETE at this checkpoint.**

## Layer 2: public language semantics

| Surface | Engineering evidence at or before `c1ef3eb` | Acceptance boundary |
| --- | --- | --- |
| Local helpers, methods, generics, collections and ordinary expressions | Exact variants and panic separation checked by peer-effects, unary/projection and reconstructed A/C gates; A/C now cover implicit final expressions, grouped reachable re-raise, unreachable explicit raise and unreachable/reachable consume distinctions. | Final proof must establish O1/O4 across all accepted syntax and prevent `moss check` acceptance followed by rustc failure. |
| Root ingress and recovery | Unhandled synchronous/Executor Roots receive `ROOT_UNHANDLED_RAISE`; positive lexical recovery and `on_fail` run native/Fast Debug. | No typed raise may cross any Root, including source-free and Rust `runtime_invoke`; final cross-ingress retest pending. |
| Pipelines and Branches | D/E fixtures use exact `p4.bin=ABABC` and `p6.bin=ABCD`, require `-1` through `on_fail`; source order and typed callback Result bridges have focused O0/O3 gates. | Fast Debug parity where file chunks are supported; ordered speculative precedence and terminal cleanup must be rerun after effect changes. |
| Source-free packages | ABI v9 binds canonical `.mossi` to selected `.rlib` object, carries method effects and warning witnesses, and rejects v8/missing/forged/stale pairs. `2b6e94f` adds a valid but mismatched provider-artifact swap to the warm-cache gate. Cross-crate standard-error WRITE mutation and recovery gates exist. | Final source-removed provider execution, warm-cache falsification and independent Architect review are required. This is contract consistency, not publisher authentication. |
| FileIO, locks and errors | Typed FileIO, standard error identity, cleanup injection, fresh failure locks and concurrent Roots have focused gates. | Failed `create` can leave a name; later retry `sync()` does not prove that name durable. Descriptor cleanup remains required. |
| Numeric conversion and Float | 270-pattern corpus and B/F expression fixtures pin O0/O3/Fast Debug output; grammar and tie rule frozen. | Repeat GCC/Clang × Rust 1.97/1.98 matrix after the final compiler edit. |
| Phase 20 Range | Concrete cross-crate `Range` emits `FILEIO_CROSS_CRATE_ABI` at Moss checking; legal same-module and consumer-specialized generic forms remain valid. | Full concrete interoperability is **DEFERRED-APPROVED** as Phase 20 work; no claim of ABI support. |

## Validation ledger and exact environment

The historic `5941d98` result is **not** a validation result for the current
implementation: `make check`, `make examples`, strict GCC/Clang C++17, strict
Phase 20 review, source-free ABI-v8 execution, and GCC/Clang × Rust 1.97/1.98
Float matrix passed there, while the Architect reproduced metadata forgery and
missing warning provenance. Its full record remains in
`docs/PHASE_21_ARCHITECT_REVIEW.md` and the previous revision of this report.

| Gate on current implementation | Recorded engineering result | Final freeze requirement |
| --- | --- | --- |
| Reconstructed A/C `check_phase21_addendum_a_c.py` | Focused check/effects, O0/O3 native and Fast Debug passed after `6aa69be`; follow-ups `9af6d79` and `f4519fd` add unreachable explicit raise, unreachable consume and reachable consume negative. Their commit-time focused gates passed; this is not the final combined suite result. | Rerun at exact final SHA; archive baseline `7775049` versus fixed output. |
| Reconstructed D/E `check_phase21_peer_open_pipelines.py` and lexical `check_phase21_pipeline_try_placeholder.py` | Focused exact raise sets and O0/O3 native pass after `95e7d85`. | Rerun at final SHA; state Fast Debug file-chunk support explicitly. |
| Reconstructed B/F via `check_phase21_float_ieee.py` | Focused O0/O3/Fast Debug pass after `6a08830`. | Four-toolchain matrix and byte comparison at final SHA. |
| ABI-v9 source-free, imported methods/warnings, standard errors, cache and forgery | Focused removed-source and falsification gates passed at earlier combined v9 checkpoints; `4e917d6` migrated physical ABI gates, `2b6e94f` added a valid-provider swap negative, and `c1ef3eb` added a v8 rebuild-versus-damaged-v9 diagnostic gate that passed at its commit. | Rerun *all* ABI-v9 and source-removed gates at final SHA; inspect selected artifact and cache path. |
| O1/O4/O8, Root, concurrent Branch/FileIO | Focused gates passed at varying combined checkpoints; A/C/D/E changed effect and pipeline paths later. | Rerun targeted plus full native/optimized/Fast Debug/concurrency/fault-injection suites at final SHA. |
| `TMPDIR="$PWD/tmp" make check` | **PENDING** on current implementation. | Complete pass, including every external/addendum permanent gate. |
| `TMPDIR="$PWD/tmp" make examples` | **PENDING** on current implementation. | Complete pass. |
| Strict GCC and Clang C++17 | **PENDING** after A–F changes. | Both `-std=c++17 -O2 -Wall -Wextra -Werror -pedantic`. |
| Strict Phase 20 review | **PENDING** after A–F changes. | 36 bug, 30 guard, two pre-existing controls; eight design INFO. |
| Float portability matrix | **PENDING** after A–F changes. | GCC 13.3.0 and Clang 18.1.3 × Rust 1.97.0 and 1.98.1, O0/O3/Fast Debug, with both corpus digests. |
| `git diff --check` | **PENDING** final integration. | Clean exact candidate. |

Known host at the earlier full matrix: Linux
`6.18.33.2-microsoft-standard-WSL2` x86_64, GCC 13.3.0, Clang 18.1.3,
Rust 1.97.0 and 1.98.1, Python 3.12.3. Confirm versions and tool paths in
the final freeze ledger. The pinned 270-pattern canonical Float text SHA-256 is
`91553f7482c35cfef6c108c429c7bdb54fba4cc8d4959c4d0a7bd3faf1ca28ef`;
reconstructed expression corpus SHA-256 is
`4a14b7bee3f4f391105be90ddda1e4c01eac629ac0f6184fc0e703c8c5e23c00`.
A reproducible CI job should pin both toolchain pairs and digests and archive
byte-for-byte outputs.

## Outstanding decisions, risk and release recommendation

L7 has already approved ABI v9, compiler-recognized standard errors, the
absolute Root typed-error boundary, O2 wording correction, and the two narrow
Phase 20 deferrals. No new language semantic, ABI or numerical decision is
requested here. No mandatory acceptance criterion is waived. The final ABI-v9
contract remains subject to L7 acceptance before merging.

| Owner | Current recommendation | Basis |
| --- | --- | --- |
| Engineering L6 | **NO-GO at this checkpoint** | Final-SHA full validation and O1/O4/O8/early-release closeout are pending. |
| Independent Architect L6 | **Historic NO-GO at `5941d98`; no vote on ABI-v9 candidate** | The attributable review predates L7-approved v9 and A–F remediation. L7 will coordinate an exact-final-SHA independent retest. |
| L7 | **NO-GO for `main` until its acceptance gate** | Explicit merge approval has not been granted. |

The remaining disagreement is temporal: engineering has implemented repairs
to the Architect's observed ABI-v8 blockers, while the Architect has not yet
reviewed the new implementation. Keep both records visible. After all required
engineering gates pass, publish the reproducible exact SHA and proof packet to
L7 for independent architectural evaluation. Stop before merging into `main`.
