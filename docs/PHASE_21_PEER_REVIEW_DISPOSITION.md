# Phase 21 peer-review findings and disposition

**Engineering validated implementation (2026-10-11):**
`phase-21-integration` at `c1ef3ebb13d7c74c3ed86a45d45e536d527161da`
contains the approved ABI
v9 successor, compiler-defined standard errors, source-free warning
provenance, cross-crate standard-error WRITE transport, and reconstructed
external addendum A–F repairs. The complete `make check` and examples gates,
strict GCC/Clang C++17 builds, strict Phase 20 review, and four-pair Float
portability matrix passed on this exact source. Engineering recommends **GO
for independent Architect acceptance**; merging into `main` remains **NO-GO**
until L7 approval. No independent Architect L6 vote exists for ABI v9.

The independent Architect L6's **NO-GO** in
`PHASE_21_ARCHITECT_REVIEW.md` applies to exact implementation `5941d98`,
before L7 authorized ABI v9 and the later repairs. Its original findings and
attribution remain intact below. No engineering retest changes that historic
vote or constitutes a new Architect disposition.

This register reconciles the external review of `8d8a9cc6f842e11701dbedc56e3d2d2d3da9852a`
with the subsequent independent integration review and the remediation branch.
It is an acceptance ledger, not an assertion that a green test suite proves
the language contract. The external archive `p21_review_repros.zip` was not
present in the repository (including ignored repository-local files) when
this review began. Reproducers named below were reconstructed independently
from the review descriptions and are committed as permanent fixtures.

**Disposition vocabulary:** FIXED, CONFIRMED, DEFERRED-APPROVED,
DESIGN-DECISION-REQUIRED, NOT-REPRODUCIBLE. FIXED on the *new* implementation
requires a final-SHA independent Architect retest. The historic Architect
matrix records its independent `5941d98` decisions; the table immediately
below records later engineering evidence without relabeling those decisions.

## Post-`5941d98` engineering reconciliation

Both `p21_review_repros.zip` and `p21_open_at_7775049.zip` are unavailable
in this workspace. L7 approved reconstruction. The committed A–F programs
retain the expressions, input bytes and expected output in the written review,
but are not represented as byte-identical originals. The original review
archive remains an independent verification need if obtained later. The six
addendum cases were reported against `7775049`; baseline results independently
observed where practical are recorded in the fixture README and gates.

| Finding | Later implementation and permanent counterexample | Current engineering evidence and independent status |
| --- | --- | --- |
| EXT-01, EXT-18, INT-02 | Checked FileIO operations, cleanup, errno mapping, and fresh Root failure locks remain covered by `check_phase21_lock_fileio_adversarial.py`, `check_phase21_fileio_errors.py`, and `check_phase21_fileio_close_full.py`. `98391aa` introduced compiler `std.FileError`; `838aae6` migrated the adversarial identity fixture. | Exact-source `make check` includes the fault-injection, cleanup and concurrent Root gates; Architect retest pending. |
| EXT-02, EXT-06 | Absolute Root typed-error boundary, including synchronous, Executor and source-free ingress, is covered by `check_phase21_root_totality.py`. | Exact-source `make check` includes Root totality and native/Fast Debug/source-free controls; Architect retest pending. |
| EXT-03, INT-11; addendum **A** | `6aa69be` repairs implicit final-expression method raises and exact local/generic specialization effects. `check_phase21_addendum_a_c.py` includes unhandled and handled trailing methods and a generic negative; preexisting unary/projection, collection and helper gates remain. | The reconstructed baseline `7775049` counterexample was accepted with a missing raise. The permanent gate passes rejection, exact `effects --json`, native O0/O3 and Fast Debug on `c1ef3eb`. The separate 13-gate proof audit closes engineering O1 under accepted/fail-closed syntax; Architect retest pending. |
| EXT-04, INT-03 | `check_phase21_peer_effects.py` and `check_phase21c_branch_errors.py` retain 4097-byte panic/shadowing, indexed precedence and concurrent bounded-K cases. | Exact-source `make check` reran the native and concurrent Branch gates after A/D/E; Architect review pending. |
| EXT-05, EXT-14, INT-05 | ABI v9 `09a4d2a`, `93ce266`, `4711a47`, and `4e917d6` bind canonical `.mossi` bytes to the selected provider `.rlib`, preserve method effects and reject stale v8; `2b6e94f` adds a swapped but individually valid provider-artifact negative. `c1ef3eb` distinguishes historical v8 rebuild diagnostics from malformed v9 digests, with a removed-source migration gate. `check_phase21_source_free_abi.py`, `check_phase21_imported_effects.py`, `repro_phase21_mossi_pattern_forgery.py`, `check_phase21_source_free_cache_binding.py`, `check_phase21_abi_v9_external_cache.py`, and `check_phase21_abi_v9_migration.py` are permanent gates. | Exact-source `make check` rejects recomputed-hash forgery, warm-cache substitution and selected-artifact mismatch before execution, runs removed-source methods/functions, and passes the v8 rebuild-versus-damaged-v9 migration gate. Architect retest pending. Historical v8 failure remains valid, not a v9 limitation. |
| EXT-07, INT-09, INT-10, INT-12 | ABI v9 `4711a47` carries checked warning witnesses for direct/nested/generic/method/on_fail paths; `check_phase21_sourcefree_noop_warning.py` includes protected-state positives and local negative controls. | Exact-source `make check` passes source-backed and source-removed warning-code parity; Architect verification pending. |
| EXT-08, EXT-09, EXT-11, EXT-13, EXT-16, INT-01, INT-08 | Prior recovery, interpreter, parser and Root repairs and gates remain; `check_phase21_peer_recovery.py`, `check_phase21_recovery_semantics.py`, `check_phase21_on_fail_capture.py`, and Root keyword/identifier fixtures apply. | Historic Architect verified the earlier concrete cases; exact-source `make check` reran native/Fast Debug gates after ABI v9. New Architect assessment pending. |
| EXT-10; addendum **D/E** | `4fc0936`, `36982ef`, `e37046f`, and `95e7d85` propagate checked `file.chunks(2) |> map(checked(_)) |> reduce(0, add)` and raising `reduce(seed(flag), add)` initial values. `check_phase21_peer_open_pipelines.py` writes isolated `p4.bin=ABABC` and `p6.bin=ABCD`; `check_phase21_pipeline_try_placeholder.py` covers lexical recovery and Root capture. | The reconstructed D/E cases must yield `-1` via `on_fail` and exact `FormatError.Bad`/`SetupError.Bad` effects. Exact-source `make check` passes D/E exact effects and native O0/O3 output; file-chunk Fast Debug is outside its supported surface. Architect retest pending. |
| EXT-12; addendum **B/F**, INT-06 | `6a08830` repairs computed Float expression formatting and concrete `sqrt` local lowering. `check_phase21_float_ieee.py` pins B/F source and output plus expression corpus digest `4a14b7bee3f4f391105be90ddda1e4c01eac629ac0f6184fc0e703c8c5e23c00`; the 270-bit-pattern corpus digest remains `91553f7482c35cfef6c108c429c7bdb54fba4cc8d4959c4d0a7bd3faf1ca28ef`. | At `7775049`, B passed check but native printed noncanonical integer-like text; F passed check but rustc failed E0689. Exact-source gate checks B/F output at native O0/O3 and Fast Debug; GCC/Clang × Rust 1.97/1.98 matrix passed all four combinations byte-for-byte on `c1ef3eb`. Architect retest pending. |
| EXT-15, INT-13 | `98391aa`, `838aae6`, `ebd213a`, `c2793ce`, `60cd8ec`, `67bda5a`, and `726b987` define stable `std.FileError`/`ParseError`/`ConversionError`, preserve imported identity, and bridge cross-crate WRITE on normal and raised exits. `check_phase21_standard_error_namespace.py`, `check_phase21_source_free_standard_write_recovery.py`, and `check_phase21_standard_write_order.py` are permanent. | Exact-source `make check` passes native O0/O3, Fast Debug and source-removed provider gates; Architect retest pending. |
| EXT-17; addendum **C** | `6aa69be` narrows grouped bare re-raise to the actually reachable selected variant. `9af6d79` excludes effects of a well-formed but unreachable recovery arm; `f4519fd` excludes that arm from ownership successors while still checking its body. `check_phase21_addendum_a_c.py` includes A only, B only, both, a Root lock-plan case, unreachable-arm explicit raise and consume positives, and a reachable consume negative. | Reopen the historic NOT-REPRODUCIBLE disposition: it rested on a weaker reconstructed case. The L7-described original shape was rejected at `7775049`; focused gates now require `outer A`, exact `raise_set`, no impossible D7 consume, and rejection of the reachable consume at Moss checking. Native O0/O3 and Fast Debug pass for accepted cases in exact-source `make check`; reachable consume fails at Moss checking. Architect retest pending. |
| EXT-19, INT-04 | L7 approved narrow Phase 20 deferrals. Failed create may leave a directory entry; retry `sync()` does not prove that earlier name durable. Concrete cross-crate `Range` remains fail-closed `FILEIO_CROSS_CRATE_ABI`; legal same-module and consumer-specialized generic uses remain valid. | **DEFERRED-APPROVED** for full Range interoperability and failed-create durability expansion only. Exact-source `make check` passes descriptor cleanup, negative diagnostic and valid controls; full deferred functionality remains unimplemented by approval. |
| EXT-20 | Deterministic two-Root barrier in `check_phase21c_branch_errors.py` retains quiescence and zero-token assertions. | Exact-source `make check` passes the 64-run concurrent native gate; Architect retest pending. |
| INT-07 | `docs/PHASE_21_FINAL_PROOF_AUDIT.md` and the final review below track O1/O4/O8 and §42 early-release implementation arguments, new expression-edge and imported capture counterexamples, and conservative iterator rejection. `a10d8bb` records that an unreachable recovery state write may still conservatively widen lock and warning footprints. | The independent engineering proof reviewer ran 13 focused O1/O4/O8/early-release/ABI gates on `c1ef3eb`; all passed. Engineering closes these claims under documented assumptions and accepted/fail-closed syntax. Historic Architect classified the earlier implementation incomplete; new Architect assessment pending. |

**Exact-source engineering validation:** `TMPDIR="$PWD/tmp" make check` exited 0
with `all Moss v0.1 tests passed` on `c1ef3eb`. It included the source-free
ABI-v9 adversarial, external A–F, Root/Branch/FileIO, standard error, and
strict Phase 20 gates (36 bug, 30 guard, two pre-existing PASS; eight design
INFO). `make examples` built 32 positive examples. Strict GCC 13.3 and Clang
18.1 C++17 `-O2 -Wall -Wextra -Werror -pedantic` passed. GCC/Clang × Rust
1.97/1.98 Float native O0/O3 and Fast Debug output matched both pinned corpora
byte-for-byte. The full environment and gate matrix are in the final review.
Independent architectural acceptance remains pending L7 coordination.

## Historical issue record through `5941d98`

### Original release blockers at the ABI-v8 review

### P21-EXT-01 · Blocker · Ordinary FileIO typed errors

- **Guarantee / original case:** direct and indirect FileIO open/read/write/sync/close failures are typed `FileError` raises, including within `try`, handlers, `on_fail`, expressions and source-free providers. Cleanup preserves the original cause; misuse remains a panic.
- **Reproducer provenance:** independently reconstructed from the external description; `tests/tooling/check_phase21_lock_fileio_adversarial.py` and the new peer FileIO gate cover native fault injection and source-removed provider execution.
- **At `8d8a9cc`:** CONFIRMED: ordinary operations still used aborting Phase 20 calls.
- **Since checkpoint / root cause / fix:** `2384900` introduced checked FileIO lowering; `e828490` corrected module-qualified `FileError` identity and close-time ENOSPC/Full mapping, and `9be2c7f` kept ownership diagnostics reachable when a negative fixture omits a declaration.
- **Permanent gates / current modes / Architect:** `check_phase21_fileio_errors.py`, `check_phase21_lock_fileio_adversarial.py`, `check_phase21_fileio_close_full.py`, and `check_phase21_source_free_abi.py` pass on the combined tree. The last executes a source-removed provider. FileIO Fast Debug is outside the approved Phase 21 runtime (§18). Exact-final-SHA independent verification remains pending; disposition is provisionally CONFIRMED pending that review.

### P21-EXT-02 · Blocker · Executor silently discards a raised Root outcome

- **Guarantee / original case:** `executor.invoke(worker.Run())` where `Run` raises `WorkError.Failed` without `on_fail` must fail Moss checking. `join()` is drain-only and cannot silently consume a typed failure.
- **Reproducer provenance:** reconstructed `tests/tooling/fixtures/phase21_executor_unhandled_raise.moss`; a corresponding total handler and a nonraising handler appear in `phase21_executor_root_total.moss`.
- **At `8d8a9cc` and `1ae239b`:** CONFIRMED: `moss check --json` accepted the source; generated Rust used `let _ = ...Run_shared()` and the executable exited 0 without reporting `WorkError.Failed`.
- **Since checkpoint / root cause / fix:** `f70196c` adds post-inference Root totality checking and a native codegen guard. The check reports `ROOT_UNHANDLED_RAISE` with `enum:WorkError:Failed`; positive submitted Roots use `on_fail` and normal Roots need none.
- **Permanent gate / modes:** `check_phase21_root_totality.py` rejects the original case through check and lowering, and executes positive O0/O3 native and Fast Debug cases. Source-free imported effects remain subject to P21-EXT-05. Architect disposition pending.

### P21-EXT-03 · Blocker · Incomplete exact raise-set inference

- **Guarantee / original cases:** parentheses, list elements, method exit calls and duck-typed helpers must preserve exact typed variants in `check`, `effects`, native and Fast Debug, including across packages.
- **Reproducer provenance:** independently reconstructed grouped, vector, method and duck-helper cases in `phase21_peer_effects.moss`, with `check_phase21_peer_effects.py` as a permanent gate.
- **At `8d8a9cc`:** CONFIRMED for grouped/vector/duck call forms; method-exit variant was already visible in the separately reconstructed direct case. An unreachable-arm warning masked reachable variants.
- **Root cause / fix / current status:** expression effect descent and concrete specialization gaps repaired in `26fcb9f`; eager pipeline Result propagation also changed there. The original grouped/list/method/duck gate passes `moss check --json`, `moss effects --json`, native O0/O3, and Fast Debug for its concrete cases. Independent follow-up found `return not fail()` and `return fail().value` dropped typed raises, falsely warned that recovery was unreachable, admitted an unhandled Root, and failed native lowering while Fast Debug caught the variant. Permanent regressions in `5c3f1ed` and `f49ca8a` pass after the recursive effect-walker repair `5cc76d0`. Method receiver/argument writes enter public `observable_effects.domain_write` after `11d62d2`. These are verified engineering fixes at `263ad67`; full O1 generality and independent final-SHA review remain open. Imported source-free effects remain tracked under P21-EXT-05.

### P21-EXT-04 · Blocker · Unsound speculative mapper eligibility

- **Guarantee / original cases:** no mapper with possible panic or unresolved imported behavior may execute in speculative Branches. Shadowed `case`/`for` binders must not inherit parameter bounds facts; an earlier typed raise must outrank a later speculative panic.
- **Reproducer provenance:** 4097-byte grouped-expression, case-shadow and for-shadow programs independently reconstructed in `phase21_peer_speculation.moss`, exercised by `check_phase21_peer_effects.py`.
- **At `8d8a9cc`:** CONFIRMED: functional IR reported these mappers eligible, and native execution aborted on a short `Range` index despite an earlier typed raise.
- **Root cause / fix / current status:** panic effect descent and name-based index proof repaired in `26fcb9f`; lexical shadowing invalidates the nonempty index proof. The 4097-byte native O0/O3 cases produce the earlier typed arm result without later panic. `check_phase21c_branch_errors.py` passes actual concurrent native O9 tests after valid Root fixture migration `a967f47`. O9 general argument and final Architect retest remain pending.

### P21-EXT-05 · Blocker · Imported function and method effects

- **Guarantee / original cases:** source-removed providers preserve variant-precise raises and panic facts for functions, methods and generic specializations; unhandled imports fail at Moss checking and unsafe mappers stay sequential.
- **Reproducer provenance:** independently reconstructed source-removed provider functions/methods, Root call and mapper cases in `check_phase21_imported_effects.py`.
- **At `8d8a9cc` and `1ae239b`:** CONFIRMED: imported function summaries were reset during fixed-point inference; ABI-v8 `object_method` records lack an effects field. A raising source-backed method also failed native trait lowering.
- **Root cause / current engineering remedy:** preserve imported function summaries, repair native method Result lowering, and fail closed at source-free object-method calls whose effects cannot be proved. This v8-safe diagnostic is an interim compatibility restriction. Complete method metadata requires approved provider contract evolution.
- **Status / modes / Architect:** DESIGN-DECISION-REQUIRED. The combined gate passes source-free function/generic check and native execution, source-backed method native/Fast Debug, and expected source-free method rejection `IMPORTED_METHOD_EFFECTS_UNAVAILABLE`. The independent Architect confirmed ABI-v8 method metadata absence. No ABI-v8 schema edit is authorized.

### Major external findings at the ABI-v8 review

| ID | Severity and original guarantee | Reproducer and checkpoint state | Engineering disposition at `5941d98` | Independent Architect L6 |
| --- | --- | --- | --- | --- |
| P21-EXT-06 | Major: synchronous Root raises must not escape into `main`'s `try` (§4). | Reconstructed `phase21_sync_root_lexical_recover.moss` and grouped-expression variant; `8d8a9cc` and `1ae239b` accepted. | `f70196c` rejects both with `ROOT_UNHANDLED_RAISE`; positive `phase21_sync_root_total.moss` returns through exhaustive `on_fail`. Native O0/O3 and Fast Debug pass. Provisional pending independent review. | Pending exact-final-SHA verification. |
| P21-EXT-07 | Major: lock-inflation and state-writing empty catch-all warnings. | Reconstructed direct, loop-condition, WRITE-helper, transitive-helper, nested-call, and method-receiver cases. `8d8a9cc` emitted none of the required provenance warnings. | `b149a88`, `40d6704`, `a3ee83c`, `5b8aca6`, and `11d62d2` add `RECOVER_LOCK_INFLATION`, `RECOVER_EMPTY_AFTER_STATE_WRITE`, and `ON_FAIL_EMPTY_AFTER_STATE_WRITE`, with helper and method provenance. Focused checker/native/Fast Debug gates pass; `5d1c194` also preserves handler locals in warning analysis. Source-removed imported helper warning parity **fails** the permanent known-issue gate (INT-10) and needs a provider-contract decision. Overall status **DESIGN-DECISION-REQUIRED** despite local fixes. | Architect confirms source-free metadata lacks required catch-all provenance; exact-final-SHA retest pending. |
| P21-EXT-08 | Major: unhandled typed raise in a `test` fails only that test. | `check_phase21_peer_recovery.py` reconstructs native test continuation; at `8d8a9cc`/`f6f11c4`, native lowering rejected a raising test call before compilation. | `eda7533` gives each native test a typed outcome and reports the selected variant while continuing later tests. Engineering native/Fast Debug gate passed on its branch; final Architect retest pending. | Pending final-SHA verification. |
| P21-EXT-09 | Major: `raise e` must preserve the selected concrete enum variant. | `phase21_peer_raise_enum_value.moss` and `phase21_peer_re_raise_exact.moss`; at `f6f11c4`, enum-valued raises were inferred as a scalar enum identity. | `eda7533` enumerates possible enum variants and preserves an active recovery binding's selected identity. Engineering gate checks exact A/B arms in checker, native and Fast Debug; final Architect retest pending. | Pending final-SHA verification. |
| P21-EXT-10 | Major: raising callbacks in eager pipelines must propagate or fail Moss check, never rustc E0308. | Reconstructed map/filter/any/all and generic reducer cases in `check_phase21_peer_effects.py`. | `26fcb9f` propagates checked Result values through eager pipelines; engineering check/native O0/O3/Fast Debug gate passes. General callback-shape argument and Architect retest pending. | Pending final-SHA verification. |
| P21-EXT-11 | Major: Fast Debug `on_fail` and payload-pattern dispatch must match native. | `phase21_peer_on_fail_parity.moss`; reconstructed two-arm payload case aborts on old `8d8a9cc` Fast Debug. | Internal `95a3ae8` repaired interpreter trailer routing; `eda7533` adds exact variant parity gate. Engineering native/Fast Debug gate passes; Architect retest pending. | Pending final-SHA verification. |
| P21-EXT-12 | Major: Float expression text parity and IEEE division must not be marked panicking. | `check_phase21_float_ieee.py` reconstructs `echo 0.1 + 0.2`, `1.0 / 0.0`, `0.0 / 0.0`, nested Float division and an Int-division control. At `8d8a9cc`, Float expression formatting/effects differed from the approved IEEE contract. | `dba4daf` removes the false Float-division panic effect. The permanent gate checks `effects --json` (`divide` and `nested` nonpanicking, Int division panicking), expected native O0/O3 and Fast Debug bytes, IEEE edges and the pinned 270-value corpus. Engineering passes; final Architect retest pending. | Pending final-SHA verification. |
| P21-EXT-13 | Major: `recover err:` binding must have its concrete type and work in `match`. | Independent Architect reproduced `MATCH_REQUIRES_ENUM` for single-enum `recover err: match err` at `f6f11c4`; permanent `phase21_peer_recover_binding.moss` exercises native/Fast Debug. | `eda7533` binds the selected enum type when all alternatives share one enum; engineering gate passes checker/native/Fast Debug. | Independent defect confirmation; final-SHA fix verification pending. |
| P21-EXT-14 | Major: missing provider symbols and falsified `.mossi` must fail closed. | `check_phase21_source_free_abi.py` covers missing/canonical symbol rejection; permanent `repro_phase21_mossi_pattern_forgery.py` reconstructs a recomputed-hash failure-pattern swap. | Narrow hash/symbol checks in `382e361`; the pattern forgery still checks/builds and exits by abort (`-6` in Python) against unchanged rlib. **DESIGN-DECISION-REQUIRED** for artifact binding; no v8 schema change made. | Architect independently reproduced the same pattern-swap class at preliminary SHA; final-SHA retest pending. |
| P21-EXT-15 | Major: `FileError`/`ParseError` shape, availability, and nominal identity must match built-in operations. | At `8d8a9cc`, a manually declared `FileError.IO(code: Int)` with FileIO use passed `moss check` then failed rustc E0533; `check_phase21_builtin_error_contract.py` reconstructs this. A package probe gives a consumer its own `app__FileError` and an unrelated provider namesake: old checker acceptance led to rustc E0425 or a `FileError.NotFound` recovery falling into the catch-all. | `dba4daf` validates the six/two/two tag-only declarations; `16859f0` scopes shape checks by source and rejects ambiguous multiple `FileError` candidates as `FILEIO_ERROR_IDENTITY_UNSUPPORTED`. `263ad67` preserves valid single-module qualified provider recovery; the focused package gate builds and runs it with the correct arm. The ambiguous package negative fails at Moss checking, while declared-shape/native numeric/Fast Debug gates pass. Implicit built-in availability is not established by recorded approval: normative examples omit declarations while the practical guide requires them. Overall **DESIGN-DECISION-REQUIRED**; no suffix alias or ABI-v8 identity change was made. | Architect independently confirmed nominal identity risk; exact-final-SHA disposition pending. |

### Minor external findings at the ABI-v8 review

| ID | Severity and original issue | Reproducer / checkpoint status | Engineering disposition at `5941d98` | Independent Architect L6 |
| --- | --- | --- | --- | --- |
| P21-EXT-16 | Minor: `recover`/`on_fail` prefix parsing rejects identifiers such as `recovered`. | `phase21_peer_keyword_prefix.moss`; engineering reproduced rejection at `8d8a9cc` and `f6f11c4`. | `eda7533` requires a keyword boundary. Engineering checker/native/Fast Debug gate passes; Architect retest pending. | Pending final-SHA verification. |
| P21-EXT-17 | Minor: grouped `recover A, B: raise` may re-raise the wrong alternative. | `phase21_peer_grouped_reraise.moss` exercises both A and B with outer-arm dispatch. | Engineering ran old `8d8a9cc` and current checker/native/Fast Debug; reconstructed case passes both. Provisional **NOT-REPRODUCIBLE** pending original archive or Architect challenge. | Pending independent confirmation. |
| P21-EXT-18 | Minor: ENXIO/ENODEV and close-time ENOSPC mapping under the six-variant FileIO contract. | `check_phase21_fileio_errors.py` now enumerates Linux ENXIO/ENODEV and actual/injected close ENOSPC; `check_phase21_fileio_close_full.py` exercises a Moss `FileError.Full` arm. | `e828490` maps absent-device errors to IO and close storage exhaustion to Full. Engineering native gates pass; Architect retest pending. | Pending final-SHA verification. |
| P21-EXT-19 | Minor: failed `create`/retry cleanup and parent-directory synchronization obligation. | `check_phase21_fileio_errors.py` injects post-descriptor and parent-open failures, then reopens the path. The independent Architect inspected `open_checked(create)` against Phase 20 §12.8/R10 and Phase 21 §6. | **CONFIRMED contract limitation:** the first failed create closes its descriptor and registry claim but can leave a name. A later `create` takes the existing-file path (`created=false`) and carries no parent-directory sync obligation. The approved contract discards that obligation at close and promises no rollback, so this is not yet an implementation violation. A successful retry plus `sync()` must not be advertised as proof that the new name is durable. An unlink or obligation-transfer remedy would change race and failure semantics and requires a separate decision. | Architect independently confirmed the code path and restricted contract; final-SHA rerun pending. |
| P21-EXT-20 | Minor: Branch quiescence assertion may be timing-dependent. | `check_phase21c_branch_errors.py` previously gave a second Root ten million scheduler yields to publish a Branch, then panicked if it had not run. This could fail under load without a quiescence violation. | `d5976de` uses a two-Root barrier at the first distinct Branch publications. The subprocess timeout remains a deadlock witness; the test still requires both Root publications and zero live Branch scopes/read tokens after `join()`. Its 64-run native gate passes on the combined tree; final Architect retest pending. | Pending final-SHA verification. |

### Later independent integration-review findings at `5941d98`

| ID | Severity and guarantee | Reproducer / checkpoint status | Engineering disposition at `5941d98` | Independent Architect L6 |
| --- | --- | --- | --- | --- |
| P21-INT-01 | Major: D7 joined captures and nested bare re-raise must lower to owned native values. | Internal reviewer fixtures added after `8d8a9cc`; original branch-join and E0382 cases failed. | Repaired by `95a3ae8` and `e200d69`; native/Fast Debug/source-free behavior to be retested at final SHA. | Pending. |
| P21-INT-02 | Major: checked FileIO propagation and Root lock/cleanup order. | Injected FileIO and concurrent Root gate added after checkpoint. | `2384900` plus P21-EXT-01 peer follow-up; general proof remains conditional. | Pending. |
| P21-INT-03 | Major: `Range.slice` effect lookup and actual concurrent O9 execution. | `check_phase21c_branch_errors.py` four-Root, 64-run case. | `82dd956` repaired effect lookup; O9 to be rechecked after EXT-03/04/05. | Pending. |
| P21-INT-04 | Major, Phase 20: concrete typed cross-crate `Range` fails native E0308. | `check_phase20_cross_crate_range_abi.py`: source-backed and source-free negative, legal generic/same-module controls. | `382e361` and `c89f564` fail closed at Moss check. Full native interoperability is separate Phase 20 work, not claimed fixed. | Pending. |
| P21-INT-05 | Blocker: internally consistent but forged `.mossi` can mismatch provider rlib. | `repro_phase21_mossi_pattern_forgery.py`: swap failure-arm pattern identities, recompute hashes, retain canonical symbols; check/build succeed, runtime SIGABRT. | DESIGN-DECISION-REQUIRED: proposed artifact-bound contract or provider-owned dispatch in this register; ABI-v8 evolution may be required. | Architect independently reproduced and objects to acceptance at preliminary SHA; final-SHA retest pending. |
| P21-INT-06 | Blocker: canonical Float portability matrix initially had one Rust/GCC combination only. | Original 2,048 bit-pattern, 2,070 parse_float and 535 parse_int/to_int probes used Rust 1.98.1/GCC 13.3. The permanent 270-pattern Float corpus has expected-output SHA-256 `91553f7482c35cfef6c108c429c7bdb54fba4cc8d4959c4d0a7bd3faf1ca28ef`. | Engineering installed Rust 1.97.0 and Clang 18.1.3 under repository `tmp/` without privilege changes. On frozen `5941d98`, GCC 13.3 and Clang 18.1.3 builds of Moss crossed with Rust 1.97.0 and 1.98.1 all passed `check_phase21_float_ieee.py`, comparing native O0/O3 and Fast Debug byte-for-byte. The exact matrix, versions and digest are in the final report; reproducible CI is proposed. No waiver assumed. | Architect independently confirmed both Rust versions with GCC at preliminary SHA; final-SHA retest pending. |
| P21-INT-07 | Major: general O4/O8 and mid-execution early-release arguments remain incomplete. | Reviewed implementation and focused fixtures in `PHASE_21_FINAL_REVIEW.md`; no exhaustive invariant proof. | Precise restricted guarantee and Architect L6 proof review required. | Pending. |
| P21-INT-08 | Major: Root ingress post-inference scan mistakes an ordinary field named `message` for a Root message. | Full `make check` stopped at `tests/phase15_15_enum_positive.moss:40` with `ROOT_MESSAGE_TARGET_UNRESOLVED` for `Outcome.Failure(message: "helper")`; the focused `phase21_root_message_identifier.moss` reconstructs the same token ambiguity. | `a61c728` requires a receiver member-call shape before interpreting the token as a Root message. `moss check --json` accepts the old enum fixture; targeted Root native O0/O3 and Fast Debug and complete `make check` pass on `5941d98`. Architect verification pending. | Pending. |
| P21-INT-09 | Major: a method's no-op recovery after protected receiver state write emits no warning, and the public handler effect query omits that write. | Independent reviewer committed direct and helper-wrapped method fixtures/gate as `21679ca` and `6fe83c4`. At integration `5b8aca6`, check emitted no warning; native/Fast Debug printed `2`; synchronization said `write_set=[cell.value]` while public `domain_write=false`. | `11d62d2` maps receiver and writable argument effects to protected state and emits exact `cell.value` provenance for both call shapes. The permanent method checker/effects/native/Fast Debug gate and complete `make check` pass on `5941d98`; Architect exact-SHA retest pending. | Architect classified the original as substantive O1 public-effect loss; fix verification pending. |
| P21-INT-10 | Blocker: source-free imported helper loses the required no-op recovery warning. | Independent `check_phase21_sourcefree_noop_warning.py` (known-issue gate `8d6f717`) builds `provider.swallow(target)` with a write followed by raise and no-op catch. Source-backed consumer `swallow(value)` warns, but after provider source removal the rebuilt consumer warns no more while native execution still leaves state `2`. | **DESIGN-DECISION-REQUIRED.** ABI v8 carries parameter WRITE effects but no helper recovery-body/provenance summary. A provider-source warning cannot know whether the future caller's argument is protected state. Precise source-free parity needs approved provider metadata evolution or an explicitly approved limitation/fail-closed contract. The red reproducer is preserved outside green gates. | Architect independently agrees this is a public diagnostic-contract gap; final-SHA retest pending. |
| P21-INT-11 | Blocker: unary `not` and object-field projection hide a typed raise from O1 analysis. | Independent `check_phase21_unary_not_effects.py` and `check_phase21_field_projection_effects.py` reconstruct `return not fail()` and `return fail().value`, plus unhandled Root uses. At `5b8aca6`, check falsely warned recovery unreachable and admitted Root, effects lost `Error.Bad`, native lowering failed internally, Fast Debug caught it. | `5cc76d0` recursively descends both accepted expression forms. Both permanent gates now pass exact `check --json`/`effects --json`, native and Fast Debug, and are in `tests/run.sh`. The separate 4097-byte unary mapper O9 gate `e192f14` passes native O0/O3 with `may_panic=true` and speculation ineligible. O1 general coverage and final-SHA Architect review remain open. | Architect independently reproduced unary `not` on the earlier SHA; final retest pending. |
| P21-INT-12 | Major: warning provenance analysis lacked a handler local type and raised an internal synchronization invariant. | Interim full `make check` stopped at `phase21_capture_slot_combined.moss` during `check_phase21_on_fail_capture.py`: the recovery body uses `item.length()`, but the warning-only leaf-effect walk treated `item` as an unresolved concrete method target. | `5d1c194` carries checked local and selected-error binding types into recovery-arm lock-inflation analysis. D7 capture, warning, method, O9, built-in and complete `make check` gates pass on `5941d98`. | Pending independent final-SHA verification. |
| P21-INT-13 | Major: declared `FileError` presence check rejected Phase 20 FileIO sources that use the legacy aborting API without a typed enum. | Full `make check` on `263ad67` failed `check_phase20_fileio_semantics.py` at `root_read` with `UNKNOWN_ERROR_TYPE`; the suite contains positive and negative legacy FileIO controls. | `5941d98` skips presence validation only when no FileError enum is declared. Declared malformed and ambiguous contracts still fail, and typed recovery selectors still require a declaration. The Phase 20 FileIO suite, Phase 21 built-in contract gate and complete `make check` pass on the new implementation SHA. Architect exact-SHA review pending. | Pending. |

### Authoritative independent disposition at `5941d98`

The independent Architect L6's exact-SHA report in
`PHASE_21_ARCHITECT_REVIEW.md` records the individual commands and distinguishes
rerun tests from code/spec inspection. `FIXED` below applies to each
reconstructed counterexample, not a claim that all accepted Moss programs
satisfy the broader invariant. EXT-17 cannot be fully resolved without the
unavailable original archive. INT-13 was found after the architect's matrix
was drafted; its exact-SHA Phase 20 and Phase 21 gates were independently
rerun in the architect report's final section.

| ID | Independent disposition | Verification and remaining boundary |
| --- | --- | --- |
| EXT-01 | FIXED | Typed FileIO injection, cleanup and source-free gates rerun. |
| EXT-02 | FIXED | Unhandled Executor Root rejected; positive Root executes. |
| EXT-03 | FIXED | Group/list/helper/method, unary and projection reproducers pass; O1 general proof remains open. |
| EXT-04 | FIXED | 4097-byte panic/shadowing/precedence cases pass; O9 still assumes complete O1. |
| EXT-05 | DESIGN-DECISION-REQUIRED | Imported functions/generics pass; source-free object methods fail closed under ABI v8. |
| EXT-06 | FIXED | Synchronous Root lexical catch is rejected; exhaustive `on_fail` succeeds. |
| EXT-07 | DESIGN-DECISION-REQUIRED | Local warning gates pass; source-free imported-helper warning still disappears. |
| EXT-08 | FIXED | Native/Fast Debug unhandled test fails individually and later test runs. |
| EXT-09 | FIXED | Enum-valued and selected bare re-raises dispatch exact A/B variants. |
| EXT-10 | FIXED | Reconstructed eager raising callbacks pass check, native O0/O3 and Fast Debug. |
| EXT-11 | FIXED | Native/Fast Debug payload `on_fail` variant dispatch agrees. |
| EXT-12 | FIXED | IEEE division effects and Float text pass two Rust versions independently; coordinator verified Clang pairs. |
| EXT-13 | FIXED | `recover err: match err` passes native/Fast Debug. |
| EXT-14 | DESIGN-DECISION-REQUIRED | Missing/canonical symbols reject, but recomputed-hash pattern forgery builds and aborts. |
| EXT-15 | DESIGN-DECISION-REQUIRED | Declared shapes and unambiguous qualified recovery pass; implicit availability/nominal cross-module identity unsettled. |
| EXT-16 | FIXED | `recovered` keyword-prefix source passes checker/native/Fast Debug. |
| EXT-17 | NOT-REPRODUCIBLE | Reconstructed grouped A/B bare re-raise passes; original archive unavailable. |
| EXT-18 | FIXED | ENXIO/ENODEV and close-time ENOSPC mappings pass focused gates. |
| EXT-19 | CONFIRMED | Failed-create name durability is a bounded contract limitation; cleanup passes, retry `sync()` is not proof of durable name. |
| EXT-20 | FIXED | Deterministic Branch barrier and 64 concurrent native runs pass. |
| INT-01 | FIXED | Joined captures and nested bare re-raise pass focused native/Fast Debug/source-free gates. |
| INT-02 | FIXED | Tested Root FileIO/lock release and fresh acquisition order passes; general O6 assumes path completeness. |
| INT-03 | FIXED | `Range.slice` and concurrent Branch cases pass. |
| INT-04 | CONFIRMED | Concrete cross-crate Range remains unsupported but fails closed at Moss check; legal controls pass. |
| INT-05 | DESIGN-DECISION-REQUIRED | Exact source-free pattern forgery remains build-success/runtime-abort. |
| INT-06 | FIXED | Architect reran GCC with Rust 1.97/1.98; coordinator completed exact-SHA Clang pairs. |
| INT-07 | CONFIRMED | O4/O8 and §42 early-release general proof gaps remain after code/spec inspection. |
| INT-08 | FIXED | Ordinary `message` field no longer triggers Root scan. |
| INT-09 | FIXED | Method receiver write reaches effect query and exact `cell.value` warning. |
| INT-10 | DESIGN-DECISION-REQUIRED | Source-free imported-helper warning gate still fails with `[]`. |
| INT-11 | FIXED | Unary/projection raise inference and unary speculative mapper regressions pass; O1 generality remains open. |
| INT-12 | FIXED | Handler-local recovery no longer trips synchronization warning invariant. |
| INT-13 | FIXED | Architect independently reran Phase 20 FileIO semantics/runtime/executor and Phase 21 declared-error/ABI gates. |

### Historical acceptance layers and decisions then open

This register uses two distinct layers. **Formal implementation obligations**
are O1–O9 in the approved specification and ask whether the compiler/runtime
argument covers every relevant control-flow and ownership path. Their detailed
locations, assumptions, tests and status are in `PHASE_21_FINAL_REVIEW.md`.
**Public language semantics** ask whether ordinary syntax, helpers, methods,
generics, module imports, Executor Roots and Fast Debug expose those promises.
The layers are not interchangeable: a positive fixture is behavioral evidence,
while general correctness needs a path-complete argument.

| Public surface | Current acceptance result | Blocking distinction |
| --- | --- | --- |
| Local expressions, helpers and generic effects | Focused native O0/O3, Fast Debug, `check --json` and `effects --json` cases pass after `26fcb9f` and `5cc76d0`. | ABI-v8 source-free warning provenance and path-complete expression coverage remain unresolved. |
| Root ingress and recovery | Unhandled synchronous/submitted Roots fail checker; positive `on_fail`, typed bindings and variant dispatch pass focused modes and the complete suite on `5941d98`. | Independent exact-SHA architectural verification remains pending. |
| Source-backed and source-free packages | Source-free function effects, raised provider frames and owned captures have positive execution evidence. | Object-method effects are absent from ABI v8 and fail closed; forged same-symbol `.mossi` patterns still build and abort. |
| FileIO, locks and Branch execution | Injected failures, concurrent Roots, ordered Branch outcomes and zero terminal read tokens pass focused native gates. | The O4/O8 general proof and §42 early-release reconciliation remain incomplete; failed-create name durability has a contract caveat. |
| Numeric conversions and Float output | Four Rust/C++ combinations pass the pinned native O0/O3 and Fast Debug corpus on `5941d98`; the complete numeric suite passes. | Pin the versions and corpus digest in reproducible CI. |

The independent statuses above apply to exact implementation `5941d98`.
Mode-by-mode engineering results and the Engineering L6, Architect L6 and L7
votes are in `PHASE_21_FINAL_REVIEW.md`; the architect's directly observed
results are in `PHASE_21_ARCHITECT_REVIEW.md`.

Human approval is required before changing §4 Root ingress, numerical grammar
or canonical Float tie-breaking, O2 proof wording, the frozen source-free
provider contract, or waiving a mandatory proof/portability gate. The agreed
numeric grammar already accepts `.5`, `1.`, uppercase `E`, leading zeros and
signed exponents, and the chosen Float tie-breaking is nearest, ties to even;
these choices are recorded normatively in
`docs/MOSS_PHASE_21_ERROR_HANDLING.md` §13.2–13.3 and are exercised by the
numeric and Float gates. No new numerical decision is proposed.

### Historical ABI-v9 proposal before L7 approval

The threat is modification, replacement, or accidental mismatch of a `.mossi`
file while its provider `.rlib` remains unchanged. ABI-v8 section hashes detect
accidental edits that leave the stored hash stale; they do not authenticate
metadata against compiled provider behavior. In a concrete two-arm provider,
swapping only the two `handler_failure_arm` pattern identities, retaining the
canonical symbols, and recomputing both interface hashes makes a source-free
consumer pass `moss check` and link against the original `.rlib`. Execution
then aborts when provider-owned dispatch and consumer interpretation disagree.
The independent Architect L6 reconstructed this case under repository-local
`tmp/architect-l6/forged-pattern` at `f6f11c4`. Commit `4fb1663` preserves the
equivalent source and falsification steps in the permanent standalone known-issue
reproducer `tests/tooling/repro_phase21_mossi_pattern_forgery.py`. It is kept
outside green acceptance gates until a contract decision enables expected
checker rejection.

**Recommended contract:** a versioned provider-owned digest over the canonical
effect, method, failure-arm, capture, and symbol metadata, embedded in the
compiled provider artifact and checked by the consumer compiler against the
`.mossi` contents before accepting source-free imports. The digest must be
derived from the provider build, not a second editable interface field.
Provider-owned dispatch is an alternative if the consumer never interprets
failure patterns independently. Either design needs a precise representation
and independent adversarial review. A content hash alone does not establish a
publisher identity if an attacker can replace both artifacts; this proposal
protects the consistency boundary between a selected rlib and its interface,
assuming the build system selects the intended provider artifact.

ABI v8 has no artifact-bound proof field and no object-method effects record.
Adding those records is a provider-contract change and requires approval of a
versioned successor, likely ABI v9. Rebuilt providers can emit the new record;
source-free v8 providers cannot be retroactively authenticated. The proposed
transition fails closed for source-free v8 imports at the new strict boundary
or uses an explicit, approved compatibility mode. It does not claim that the
current fail-closed method diagnostic or section hashes fully resolve
P21-EXT-05/P21-EXT-14/P21-INT-05.

### Historical O2 proposal before L7 approval

Current Part III O2 says trailer effects never enter `ClassSet(h)` and P10 C1
says a trailer never adds exclusive locks to `h`. The first statement is about
separate effect columns; the latter is too broad. A new `h_fail` signature
column can split an existing class of two leaves both written by `h`, so `h`
may acquire two exclusive classes after previously acquiring one even though
its per-leaf write footprint is unchanged. The recommended wording is:
"Fresh `on_fail` effects are not unioned into the original handler's state
effect footprint and do not increase its per-leaf write requirements. As with
any new signature column or writer, they may repartition synchronization
classes and alter normal-path lock counts, including exclusive counts."
This corrects the class-count claim without changing ranked acquisition or the
deadlock proof. It requires an approved specification correction, not an
unreviewed compiler change.
