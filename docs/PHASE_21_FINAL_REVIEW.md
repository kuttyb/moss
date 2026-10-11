# Phase 21 final independent review

> **Validated engineering checkpoint, not architectural acceptance
> (2026-10-11):** implementation commit `5941d98` passes the full Moss suite,
> examples, strict GCC/Clang C++17, and the four-combination Float matrix.
> The independent Architect L6 retested the exact implementation SHA and
> issued a NO-GO recommendation. Two live ABI-v8 counterexamples and
> incomplete general proofs remain. Engineering and L7 also remain NO-GO.

**Recommendation: NO-GO for merging into `main`.** This is the release-candidate
review record, not a merge approval. Independent reviewers inspected the compiler
and runtime, supplied counterexamples, and repaired defects without changing the
frozen Phase 21 ABI v8. Source-free interface metadata remains unbound to its
provider rlib, source-free object methods lack ABI-v8 effect summaries, and the
general O4/O8 and early-release proof obligations remain open. A four-combination
Rust/C++ Float portability matrix passed on the frozen implementation SHA.

## Exact baseline and scope

| Item | Revision |
| --- | --- |
| Implementation checkpoint challenged | `8d8a9cc6f842e11701dbedc56e3d2d2d3da9852a` |
| Current `main` and `origin/main` reconciled into integration | `d15e968778fa1cff3cd5085911c505592af54a30` |
| Integration merge of that `main` | `7eb192b` |
| Reviewed implementation commit | `5941d98c824f31550f520891235c8b7ac96f3992`; later commits change reports and status only. |
| Release-candidate branch | `phase-21-integration`; no merge into `main` |

The implementation includes typed `raise` and lexical `try`/`recover`, variant
effect inference, handler `on_fail` with owned captures and a separate failure
lock plan, provider-owned source-free failure frames, checked FileIO failures,
ordered bounded Branch outcomes, and checked numeric parsing/conversion with
canonical binary64 text in native and Fast Debug.

## O1–O9 evidence matrix

`PASS` below means a reviewed implementation argument with named assumptions
and behavioral tests; it is not a machine-checked proof. `INCOMPLETE` means the
general invariant has not been justified sufficiently for acceptance even
when the observed fixtures pass.

| Obligation and exact guarantee | Implementation inspected | Existing positive/negative coverage | New adversarial result and assumptions | Status |
| --- | --- | --- | --- | --- |
| **O1**: normal handler effects cover every reachable `recover`, nested arm, and helper. | `src/moss.cpp`: `observable_expression_effects`, `analyze_effect_block`, `specialized_handler_effects`, `build_synchronization_plan`. | Recovery semantics, `check_phase21_peer_effects.py`, and the imported-effects gate check concrete variant sets. | External grouped/list/duck cases were repaired in `26fcb9f`. Independent `not fail()` and `fail().value` counterexamples later lost exact raises, admitted an unhandled Root and failed native lowering; `5cc76d0` repairs those forms, and focused checker/effects/native/Fast Debug gates pass. `11d62d2` also repairs receiver/argument method writes in public effect summaries and warning analysis. Source-free helper warning provenance is separately unresolved under ABI v8. A path-complete structural induction over every accepted expression and import form has not been established. | **INCOMPLETE** for the general O1 guarantee. |
| **O2**: `h$fail` has separate effects, class-signature column, and ClassSet; its accesses never enter `ClassSet(h)`. | `src/moss.cpp`: `specialized_handler_failure_effects`, `build_synchronization_plan`; `src/handler_lowering.inc`: failure wrapper. | `check_phase21_handler_failure_native.py` distinguishes normal and failure plans. | A real Root query shows separate effect columns and lock sets. Adding the `h_fail` signature column can still repartition a class and increase the number of normal exclusive acquisitions without adding a per-leaf write; the approved Part II/P10 wording that says it never adds an exclusive class is overbroad. The rank proof is unaffected, but normative correction requires approval. | **PASS** for separate planning; prose correction **OPEN**. |
| **O3**: all own-domain recovery classes are acquired in rank order before their `try`/first observable action; no arm upgrade or late acquisition. | `src/moss.cpp`: `derive_path_placement`; `src/handler_lowering.inc`: `emit_path_sensitive_wrapper`. | Existing synchronization-plan and recovery-native suites exercise entry planning and warnings. | Lock hook observes old attempt before fresh failure acquisition under concurrent Roots. Bounded leading-condition placement hoists for rank and cancels only untouched classes. Assumes O1 summaries and closed route ranks. Broader §16 mid-execution early-release proof remains open separately. | **PASS** for implemented placement, conditional. |
| **O4**: remaining-path, guard-cancellation, and D7 analyses include each exceptional source edge and destination. | `src/moss.cpp`: `check_ownership_block`, exceptional-edge ABI emission/loader; `src/handler_lowering.inc`: carrier forwarding. | `check_phase21_on_fail_capture.py` includes preinit/consumed/borrowed-view negatives and source-site captures; `check_phase21_recovery_semantics.py` tests variant ownership. | New if/try/loop joins, branch mismatch, nested re-raise, and native/Fast Debug/source-free captures pass. The reviews did not establish an exhaustive invariant over every expression and CFG form. | **INCOMPLETE** as a general proof; tested paths pass. |
| **O5**: a tagged raise materializes an owned payload and releases exactly the wrapper's acquired guards after its last protected access. | `src/handler_lowering.inc`: typed wrapper result and reverse guard drops in `gen_planned_domain`. | Handler failure native and recovery suites cover normal/raised exits. | New runtime lock events show old guard release before failure acquisition. Assumes Rust frame/RAII semantics and no hidden guard in the owned carrier. | **PASS**, conditional. |
| **O6**: Root joins/quiesces, abandons local FileIO, releases old guards, acquires fresh failure locks, dispatches arm, materializes reply, and releases. | `src/handler_lowering.inc`: `emit_failure_root_wrapper`; `src/fileio_runtime.hpp`: `abandon_for_raise`; `src/file_chunk_codegen.inc`: joined window. | `check_phase21_fileio_root_cleanup.py`, `check_phase21_chunk_fileio_raise.py`. | New injected FileClose cleanup, FileSync failure in trailer, partial WriteAfterPrefix, inode-claim and lock-order witnesses, plus concurrent Roots pass. Branch quiescence depends on O9 and finite OS work. | **PASS**, conditional. |
| **O7**: nested wrappers propagate tagged raises without running a trailer; arms message only declared routes and cannot admit Roots. | `src/handler_lowering.inc`: `_shared` versus `_root` dispatch; `src/moss.cpp`: route/Root checks. | `check_phase21_handler_failure_native.py`, `check_phase21_source_free_abi.py`. | New Root-versus-nested native/Fast Debug case and source-free provider trailer pass. Assumes static route closure and authentic provider metadata. | **PASS**, conditional. |
| **O8**: body-to-trailer captures are owned and contain no guard, protected borrow, FileIO, Range, or RangeBatch. | `src/moss.cpp`: per-edge capture/capability checks; `src/interpreter_domains.inc`: fresh Fast Debug failure frame. | Owned String/payload positives and preinit, consumed, FileIO, borrowed-payload negatives in `check_phase21_on_fail_capture.py`. | New joined capture and mismatch probes pass; source-free native captures pass. Raised payload clone is bounded to heap-free scalar/enum data by `phase21_bounded_error_type`. General type/edge preservation is not mechanically proved. | **INCOMPLETE** as a general proof; tested paths pass. |
| **O9**: each bounded-K window is fully joined before any outcome read or parent fold; next window publishes only after ordered commit without a terminal event. | `src/file_chunk_lowering.inc`: eligibility; `src/file_chunk_codegen.inc`: publication/join/ordered commit; `src/executor_runtime.hpp`: scope join; `src/fileio_runtime.hpp`: read-borrow token. | `check_phase21c_branch_errors.py` covers indexed raise/EOF/short/fold precedence, huge/overflow, panic and source-free effect facts. | Four actual concurrent native Roots force publication overlap; 64 runs produce expected ordered outcomes and zero live scopes/read tokens after join. Generated structure has join before slot read and parent fold, then next-window publication. Assumes finite Branch/OS work, no speculative mapper panic, and scheduler progress; no scheduler model check. | **PASS**, conditional. |

## Independent findings and disposition

| Finding | Resolution or remaining risk |
| --- | --- |
| D7 rejected a definitely initialized owned capture after branch joins; nested bare re-raise accepted by `moss check` failed rustc E0382; Fast Debug missed Root `on_fail`, typed recovery payload bindings, and per-test unhandled-raise continuation. | Repaired with joined binding identities, original carrier forwarding, and interpreter Root/test handling. New positive, negative, native, Fast Debug and source-free probes pass. |
| FileIO operations in Phase 21 result contexts used panicking Phase 20 lowering, including attempt, trailer `try`, and expression positions; source-free provider arms could omit FileIO runtime. | Checked open/read/batch/write/sync/close propagation and runtime inclusion repaired. Injected fault, cleanup, lock handoff, 45 concurrent failure Roots, 45 concurrent NotFound Roots, and source-free provider execution pass. |
| `Range.slice` in an ordinary mapper caused an internal effect-target error. | READ effect resolution repaired; a panicking indexed mapper still falls back to sequential execution. New concurrent O9 gate passes. |
| Concrete typed `Range` across generated module crates passed checking but failed native rustc E0308. | A precise `FILEIO_CROSS_CRATE_ABI` call-site diagnostic is the interim fail-closed remedy. A shared native FileIO `Range` ABI or ownership-preserving conversion is still a separate Phase 20 redesign. Same-module and consumer-specialized generic Range helpers remain valid. |
| `.mossi` accepted same-shape failure-arm symbol swapping and built an aborting executable. | Existing section hashes and canonical symbol names are now verified, rejecting missing/incompatible/unadjusted tampering. A forged pattern swap with recomputed hashes still checks and builds, then aborts: `.mossi` is not bound to the provider rlib. Full rejection needs an approved cross-artifact contract change; **not resolved** under frozen ABI v8. |

## Validation and environment

All results below are from the reconciled integration tree at implementation
commit `5941d98c824f31550f520891235c8b7ac96f3992`. Its Phase 20 FileIO
compatibility adjustment was followed by a complete rerun. Later commits add
reports only; they do not change compiler or runtime source.

| Gate | Combined-tree result |
| --- | --- |
| `TMPDIR="$PWD/tmp" make check` | **PASS**, exit 0, `all Moss v0.1 tests passed`. This includes the focused Phase 21 source-free ABI, D7 capture, recovery checker/native, Root failure, injected FileIO/lock, Float IEEE, numeric conversion, borrowed-view, O9 branch-error, and chunk/FileIO suites. |
| Strict Phase 20 review, run by `make check` | **PASS**: 36 bug probes, 30 guard probes, 2 pre-existing probes; 8 design proposals remain informational. |
| Source-free provider execution, run by `make check` | **PASS**: ABI-v8 raised provider, owned capture and provider trailer with Moss source removed; concrete cross-crate Range rejection and valid source-free generic Range call. |
| Native/Fast Debug parity, run by `make check` and independent numeric review | **PASS** on tested cases: typed recovery and capture, fixed Float/IEEE corpus, numeric parsing/conversion. Separate 2,048-pattern/2,070-input/535-input oracle probes passed at native O0/O3 and Fast Debug. |
| `TMPDIR="$PWD/tmp" make examples` | **PASS**, exit 0, default and optimized example generation. |
| `g++ -std=c++17 -O2 -Wall -Wextra -Werror -pedantic -fsyntax-only src/moss.cpp` | **PASS**, exit 0. |
| `clang++ 18.1.3 -std=c++17 -O2 -Wall -Wextra -Werror -pedantic` | **PASS**, produced the independent Clang-built compiler used in the Float matrix. |
| Recomputed-hash `.mossi` pattern forgery | **KNOWN RED**: checker/build accept the source-removed provider; linked executable exits `-6` after selecting a mismatched provider arm. |
| Source-free imported-helper empty-catch warning | **KNOWN RED**: source-backed check warns, source-removed consumer rebuilds and executes, but `warning_codes() == []`. |
| `git diff --check` | To rerun on the final staged report and status changes. |

Reviewer 3's actual concurrent native O9 fixture ran 64 times with four
simultaneous Roots. The FileIO/lock gate used 45 concurrent failure Roots
and 45 concurrent NotFound Roots. Those executions demonstrate the tested
schedules; they do not exhaust all scheduler interleavings. The stronger
recomputed-hash `.mossi` forgery still builds and aborts, so the green gates
must not be read as a pass of the source-free authenticity requirement.

The host is Linux 6.18.33.2-microsoft-standard-WSL2 x86_64, GCC 13.3.0,
Clang 18.1.3, Rust 1.97.0 (LLVM 22.1.6), Rust 1.98.1 (LLVM 22.1.8), and
Python 3.12.3. Earlier independent numeric probes matched native O0/O3 and
Fast Debug for 2,048 finite Float bit patterns, 2,070 `parse_float` inputs,
and 535 `parse_int`/`to_int` inputs. The current `make check` reruns the
permanent numeric parity and IEEE suites, not those larger disposable probes.

GCC and Clang builds of the exact implementation commit were each crossed
with Rust 1.97.0 (`2d8144b78`) and 1.98.1 (`48a229cea`). All four runs of
`check_phase21_float_ieee.py` passed native unoptimized, native optimized,
and Fast Debug output byte-for-byte against the 270-pattern oracle with SHA-256
`91553f7482c35cfef6c108c429c7bdb54fba4cc8d4959c4d0a7bd3faf1ca28ef`.
The four logs are in repository-local `tmp/phase21-float-{gcc,clang}-rust{197,198}.log`.
Reproducible CI should pin these versions and the corpus digest and archive
the output bytes; the local matrix satisfies the requested additional
toolchain acceptance check without a waiver.

## Public-language acceptance layer

The formal O1–O9 matrix above concerns specific implementation invariants.
Ordinary language use also crosses helpers, methods, packages, Executor Roots,
and Fast Debug. The complete external and later integration issue register is
`PHASE_21_PEER_REVIEW_DISPOSITION.md`.

| Surface | Behavioral evidence | Acceptance limit |
| --- | --- | --- |
| Local expressions, helpers, methods and pipelines | Exact `check --json`/`effects --json` variants, native O0/O3 and Fast Debug cases pass, including reconstructed `not fail()` and `fail().value`. | O1 lacks a path-complete argument for every accepted expression and specialization. |
| Root ingress and recovery | Unhandled synchronous and Executor Roots fail checking; typed `on_fail`, captures, exact re-raise and test continuation pass focused native/Fast Debug gates. | Authentic provider metadata is assumed at a source-free boundary. |
| Source-free packages | Removed-source ordinary/generic providers, owned captures and failure arms execute; unsupported imported methods fail closed at Moss checking. | ABI v8 cannot carry exported method effects or catch-all warning provenance, and a forged `.mossi` can disagree with its rlib. |
| FileIO, locks and Branches | Fault injection, concurrent Roots, ordered 4097-byte Branch cases and terminal read-token checks pass. | O4/O8 and §42 early-release proofs remain incomplete; failed-create name durability has a documented caveat. |
| Numeric conversion and Float text | Permanent parse/conversion suite and four-toolchain Float output matrix pass. | Preserve pinned corpus and compiler versions in CI. |

## Outstanding approval questions and recommendation

1. Approve a provider-rlib-bound contract or provider-owned dispatch, with a
   versioned successor to frozen ABI v8, or explicitly approve a narrower
   source-free contract. Recomputed-hash pattern forgery currently builds and
   aborts; exported object-method effects and helper warning provenance are
   also absent from ABI v8. Rebuilding providers would be required for a new
   contract. No ABI change or waiver has been made.
2. Approve the O2 wording correction proposed in the peer register: separate
   trailer effects do not add per-leaf normal writes, but class repartition
   can change the count of normal exclusive locks. The rank proof is unchanged.
3. Complete the general O1/O4/O8 audits and §42 mid-execution early-release
   reconciliation. The current evidence establishes only the restricted tested
   cases. No proof waiver is assumed.
4. Preserve the approved §4 no-per-ingress-exception rule. The recorded
   `.5`, `1.`, uppercase `E`, exponent-sign and leading-zero grammar, and
   nearest-ties-to-even Float rule are already normative and tested; no new
   language-semantics decision is requested.
5. Track concrete cross-crate `Range` interoperability as Phase 20 work. The
   current `FILEIO_CROSS_CRATE_ABI` diagnostic is fail-closed, not an ABI fix.

| Owner | Recommendation | Basis |
| --- | --- | --- |
| Engineering L6 | **NO-GO** | Green implementation gates coexist with ABI-v8 counterexamples and incomplete general obligations. |
| Independent Architect L6 | **NO-GO** | Independently retested `5941d98`, reproduced the forged-pattern failure and missing source-free warning, and recorded O2/O4/O8 objections in `PHASE_21_ARCHITECT_REVIEW.md`. |
| L7 | **NO-GO** | The independent exact-SHA review confirms live release blockers; contract and proof decisions remain open. |

Engineering's passing fixtures and the Architect's broader source-free and
proof objections are intentionally both retained. No merger into `main` is
authorized. The branch is a reviewable NO-GO candidate awaiting human
architectural decisions.
