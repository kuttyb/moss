# Phase 21 final independent review

**Recommendation: NO-GO for merging into `main`.** This is the release-candidate
review record, not a merge approval. Independent reviewers inspected the compiler
and runtime, supplied counterexamples, and repaired defects without changing the
frozen Phase 21 ABI v8. Two mandatory acceptance issues remain: the source-free
interface is not bound to its provider rlib, and canonical Float formatting has
not been pinned across another Rust/C++ toolchain combination. The separate
general early-release proof in the Phase 21 specification also remains open.

## Exact baseline and scope

| Item | Revision |
| --- | --- |
| Implementation checkpoint challenged | `8d8a9cc6f842e11701dbedc56e3d2d2d3da9852a` |
| Current `main` and `origin/main` reconciled into integration | `d15e968778fa1cff3cd5085911c505592af54a30` |
| Integration merge of that `main` | `7eb192b` |
| Reviewed implementation commit | `c89f5644785dad60c108f80609c8692364a8c190` |
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
| **O1**: normal handler effects cover every reachable `recover`, nested arm, and helper. | `src/moss.cpp`: `analyze_effect_block`, `specialized_handler_effects`, `build_synchronization_plan`. | `check_phase21_recovery_semantics.py` checks variant sets, catches, and invalid bare raise. | Nested FileIO recovery and semantic lock query pass. Argument assumes effect walking and static call summaries are sound for all reachable callees. | **PASS**, conditional. |
| **O2**: `h$fail` has separate effects, class-signature column, and ClassSet; its accesses never enter `ClassSet(h)`. | `src/moss.cpp`: `specialized_handler_failure_effects`, `build_synchronization_plan`; `src/handler_lowering.inc`: failure wrapper. | `check_phase21_handler_failure_native.py` distinguishes normal and failure plans. | New `Fail`/`Fail$fail` semantic query shows disjoint class sets in a real Root. Assumes class derivation consumes both columns without conflation. | **PASS**, conditional. |
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
commit `c89f5644785dad60c108f80609c8692364a8c190`, after every review
fix. The report and status commit adds no compiler or runtime edits.

| Gate | Combined-tree result |
| --- | --- |
| `TMPDIR="$PWD/tmp" make check` | **PASS**, exit 0, `all Moss v0.1 tests passed`. This includes the focused Phase 21 source-free ABI, D7 capture, recovery checker/native, Root failure, injected FileIO/lock, Float IEEE, numeric conversion, borrowed-view, O9 branch-error, and chunk/FileIO suites. |
| Strict Phase 20 review, run by `make check` | **PASS**: 36 bug probes, 30 guard probes, 2 pre-existing probes; 8 design proposals remain informational. |
| Source-free provider execution, run by `make check` | **PASS**: ABI-v8 raised provider, owned capture and provider trailer with Moss source removed; concrete cross-crate Range rejection and valid source-free generic Range call. |
| Native/Fast Debug parity, run by `make check` and independent numeric review | **PASS** on tested cases: typed recovery and capture, fixed Float/IEEE corpus, numeric parsing/conversion. Separate 2,048-pattern/2,070-input/535-input oracle probes passed at native O0/O3 and Fast Debug. |
| `TMPDIR="$PWD/tmp" make examples` | **PASS**, exit 0, default and optimized example generation. |
| `g++ -std=c++17 -O2 -Wall -Wextra -Werror -pedantic -fsyntax-only src/moss.cpp` | **PASS**, exit 0. |
| `git diff --check` | **PASS** on the final staged report and status changes. |

Reviewer 3's actual concurrent native O9 fixture ran 64 times with four
simultaneous Roots. The FileIO/lock gate used 45 concurrent failure Roots
and 45 concurrent NotFound Roots. Those executions demonstrate the tested
schedules; they do not exhaust all scheduler interleavings. The stronger
recomputed-hash `.mossi` forgery still builds and aborts, so the green gates
must not be read as a pass of the source-free authenticity requirement.

The host is Linux 6.18.33.2-microsoft-standard-WSL2 x86_64, Rust 1.98.1
(LLVM 22.1.8), GCC 13.3.0, and Python 3.12.3. Independent numeric probes
matched native O0/O3 and Fast Debug for 2,048 finite Float bit patterns, 2,070
`parse_float` inputs, and 535 `parse_int`/`to_int` inputs. The existing fixed
270-value Float corpus and targeted IEEE edge suite also pass on this host.

No second Rust or C++ toolchain is installed here. CI should cross a pinned
current Rust release and an adjacent supported Rust release with GCC and Clang
on supported Linux targets. Each job should run the fixed corpus and independent
Python oracle against default/optimized native output and Fast Debug, fail on
any byte difference, and archive exact compiler versions and corpus digest.
That matrix is a proposed completion path, not validation already performed.

## Outstanding approval questions and recommendation

1. Approve a provider-rlib-bound contract mechanism, with the required native
   ABI/version disposition, or explicitly narrow the source-free falsification
   acceptance criterion. The current recomputed-hash pattern swap is a concrete
   build-success/runtime-abort counterexample.
2. Run the additional Float toolchain matrix, or explicitly approve a waiver
   of the specification's cross-toolchain pin. No waiver is assumed here.
3. Close the Phase 21 §16 general early-release argument and the incomplete
   O4/O8 invariant audits to the standard required for implementation acceptance.
4. Track the shared cross-crate `Range` ABI as separate Phase 20 work; the
   release candidate only makes the unsupported concrete call fail closed.

**NO-GO:** do not merge this branch into `main` until these items receive the
required human decisions or verification. The branch can be reviewed and
revised without changing the approved Phase 21 language semantics.
