# Phase 21 independent architectural review

**Reviewer:** Architect L6, independent of the implementation reviews.

**Normative baseline:** `MOSS_PHASE_21_ERROR_HANDLING.md` v4.5.2 and its implementation handoff.

**Exact reviewed implementation SHA:** `5941d98c824f31550f520891235c8b7ac96f3992`.
**Independent recommendation:** **NO-GO**. At this SHA, a forged source-free failure pattern still builds and aborts, and a source-free helper still loses a required state-write recovery warning. ABI-v8 object methods still lack exported effect metadata. Sections below retain the earlier preliminary evidence; the exact-SHA results and disposition matrix at the end supersede its old results.

**Earlier preliminary SHA:** `f6f11c4e990d5a85a9e246f39004d4b0c1b7cbc7`. Its A6 `recover err` and stale-fixture failures were repaired before the exact-SHA retest.

This review uses the repository-local Moss skills and live `./moss agent bootstrap --json` (`language_version: moss-0.1`). The compiler was built from the pinned SHA in an isolated worktree. The original external P21-EXT-01..20 archive was unavailable; the cases below are reconstructed from the coordinator's text summary and identified permanent regressions. A committed test is evidence only where it was run successfully on this SHA.

## Architectural findings

### A1. Source-free ABI v8 does not authenticate a failure pattern against its provider arm

A source-free `.mossi` interface can be changed so that the application selects a provider arm for the wrong raised variant. Recomputing the interface hashes makes `moss build --json` accept and link the unchanged provider rlib; the resulting executable aborts. The current regression in `check_phase21_source_free_abi.py` swaps callable **symbols** and checks their canonical spelling. It does not prevent a same-symbol **pattern** swap. This is a release blocker for the requested source-free provider ABI authenticity claim.

Exact reproducer at the SHA above, in `tmp/architect-l6/forged-pattern` in the review worktree:

```moss
module service
export enum E:
  A
export enum F:
  B
export domain Worker:
  count: Int
  fn Run(which: Int) -> Int:
    if which == 1:
      raise E.A
    raise F.B
  on_fail E.A:
    count = count + 1
    reply count
  on_fail F.B:
    count = count + 2
    reply count
```

The consumer constructs `service.Worker(count: 0)` and echoes `message worker.Run(1)` and `message worker.Run(2)`. The source-backed build prints `1\n3\n`. After renaming `src/service.moss` to `service.removed`, change the two occurrences of each record in `build/debug/service.mossi`:

```text
handler_failure_arm "Run" 0 "enum:service__E:A" ...
handler_failure_arm "Run" 1 "enum:service__F:B" ...
```

to the same records with the **pattern identities exchanged**, preserving the arm indices, callable symbols, and all other fields. Recompute `concrete_interface_hash` and `interface_hash` over the `contract` section plus its trailing newline, and `generic_semantic_hash` over `semantic_exports`, using the FNV-1a 64-bit routine already in `check_phase21_source_free_abi.py::write_interface`. The hashes are unkeyed integrity values and are reproducible from the edited text. `moss build --json` then returns `ok: true` using only `src/main.moss`; running `build/debug/architect_forged_pattern` exits **134** without output. The intended invariant is that a selected failure arm's pattern, callable and compiled provider body agree; ABI-v8 currently validates each interface locally but does not establish that binding to the rlib. Resolve as an explicit ABI/design decision. Do not merely suppress the abort or weaken typed Root dispatch.

### A2. ABI v8 omits observable effects of exported object methods

`src/moss.cpp` writes and reads `object_method` return type and ownership modes, but no `ObservableEffects` record for the method. A source-backed consumer can call an exported method that raises or panics. With provider source removed, the current compiler correctly fails closed with `IMPORTED_METHOD_EFFECTS_UNAVAILABLE`, as `check_phase21_imported_effects.py` expects. This prevents a false empty effect summary, but it also leaves the Phase 21 source-free public method contract unusable. The source-free function and handler paths are separate and have positive evidence. An ABI extension or an explicit approved limitation is required before describing EXT-05 as fully closed.

### A3. The literal O2 prose overclaims unchanged normal lock count

The implementation handoff correctly records this disagreement with Part II §3 step 5 and Part III P10. Its exact point is: “Adding a pseudo-handler may split a class that the normal handler previously held exclusively into two or more classes that it must also hold exclusively.” `h_fail` must have a separate effect column and ClassSet, and its writes must not enter `h`'s per-leaf WRITE footprint. Yet adding its signature column may split an existing class and increase `h`'s **number** of exclusive acquisitions. A new writer can also add shared locks to readers that were previously lock-free. The shared ABI test exercises class repartitioning, but the normative text still says `on_fail` “never adds an exclusive class” to the normal handler. Retain the handoff's proposed corrected wording in the normative closeout; do not claim lock-count invariance.

### A4. O4, O8 and early-release proof scope remains narrower than the paper result

ABI-v8 exceptional edges carry the six consumer bits D7, ownership, guard cancellation, early acquisition/2PL, cleanup and codegen. `verify_exceptional_cfg` rejects a record omitting any bit, and focused tests exercise some D7/capture and native cleanup paths. A bitmask on a serialized edge is a declared consumer contract, not by itself an independent proof that every analysis uses the edge in every control-flow shape. The Phase 21 spec §16 explicitly leaves the §42 early-release reconciliation open, and the implementation handoff says the same. Theorem A2 proves release at a raised **exit**; it does not prove arbitrary mid-execution touched-guard release. The final O4/O8 evidence should include exception edges through nested recovery, loops, conditional continuation cancellation, protected borrows, and all Root-local FileIO scope exits. Until then, treat the paper results as conditional on O1–O9, exactly as the approved spec does.

### A5. Root totality is correctly stricter than two old regression fixtures

The §4 rule applies to synchronous `message` in `main` even if `main` lexically catches the handler's error. A top-level and a nested-`if` minimal probe both return `ROOT_UNHANDLED_RAISE` for a raising `Worker.Fetch` lacking `on_fail`. The focused Root-totality suite passes. At the pinned SHA, `check_phase21_source_free_abi.py` stops at its first raised-handler fixture because it places this forbidden lexical recovery around a Root; `check_phase21_peer_effects.py` stops at the same issue in its speculative-panic fixture. These are test migrations, not evidence that §4 should be relaxed. The coordinator reports that migrations are underway on integration; they are not part of this SHA's results.

### A6. Bound `recover err:` does not supply the approved enum type to `match`

The approved §2 rule says `recover err:` binds the single concrete error enum when all covered variants belong to that enum. The existing recovery fixture only echoes `err`. This independent minimal probe fails at `moss check --json` with `MATCH_REQUIRES_ENUM` and “found _selected_error”:

```moss
enum WorkError:
  Missing
  Broken

fn main():
  try:
    raise WorkError.Missing
  recover err:
    match err:
      case Missing:
        echo "missing"
      case Broken:
        echo "broken"
```

The probe is in `tmp/architect-l6/recover_err_match.moss` in the review worktree. This reproduces EXT-13 at the pinned SHA. Treat it as an unimplemented approved type rule, not as a reason to change the pattern grammar. The coordinator reports a repair and permanent native/Fast Debug regression in progress; retest only on the frozen integration SHA.

### A7. Failed `create` can leave a name without a surviving directory-sync obligation

`FileIO::open_checked(path, "create")` may create the name with `O_CREAT | O_EXCL`, then fail at `OpenAfterDescriptor`, `OpenFlagClear`, or `ParentDirectoryOpen`. It closes the descriptor and does not install an inode claim, but it leaves the name. A later `create` takes the existing-file path with `created=false`, so that later handle's `sync()` does not sync the parent directory. `check_phase21_fileio_errors.py` tests that such a path can be reopened and closed; it does not assert directory durability. The runtime behavior is therefore a concrete retry hazard for an application that interprets successful retry-plus-sync as proof that the newly created **name** is durable.

This is not yet an established violation of the approved contract: Phase 20 §12.8 says the parent-directory obligation belongs to the open that created the file, is discarded at close, and is not inherited by a later reopen. Phase 21 §6 retains that rule, allows partial effects and promises no rollback. The failed attempt's descriptor cleanup is analogous to closing an unsynced creator. If the intended guarantee instead covers a failed open that created a name, it needs an explicit design change; unlinking a created path or transferring an obligation across attempts has different race and durability consequences. Record the retry caveat in closeout and do not silently claim that a later `sync()` always establishes the name's durability.

## O1–O9 evidence and open proof work

| Gate | Independent assessment at `f6f11c4` |
| --- | --- |
| O1 | Exact raise sets through local functions, handlers and imported functions have focused evidence; `check_phase21_imported_effects.py` passed. Source-free object methods still lack effect metadata (A2). |
| O2 | Separate `h_fail` plan and repartitioning test passed. Preserve the lock-count wording correction (A3). |
| O3 | Adversarial native Root lock/FileIO test passed. No proof from this test alone for all leading-conditional and nested-recovery shapes. |
| O4 | Shared ABI edge verifier test passed; full cross-analysis consumer argument remains to be supplied (A4). |
| O5 | Native handler failure/class-repartition and Root guard handoff tests passed; this review did not exhaust every raised exit. |
| O6 | Adversarial Root lock/FileIO test passed, including old guard release before fresh failure lock acquisition. Branch join and every local file lifetime remain broader proof obligations. |
| O7 | Focused Root-totality suite passed; nested `on_fail` rejection has permanent fixtures. Recheck source-free fixture after migration. |
| O8 | Owned capture proof and native handoff test passed; cross-crate typed Range now fails at Moss check with `FILEIO_CROSS_CRATE_ABI`, while same-crate and source-free generic controls pass. This is fail-closed evidence, not full capability-escape proof. |
| O9 | The Branch suite and ordering tests exist, but were not rerun by this reviewer at this SHA. The old speculative-panic fixture stops at A5, so its later runtime assertions are unexecuted here. |

## Reconstructed external counterexample inventory

These descriptions are from the coordinator's summary, not a recovered original archive. “Present” means a corresponding code path or regression exists; it does not mean this reviewer ran or proved that entire case.

| ID | Independent disposition at preliminary SHA |
| --- | --- |
| EXT-01 FileIO open/read/write/sync/close abort | Phase 21 typed FileIO error test passed; it includes cause mapping and injected failure sites. |
| EXT-02 Executor Root silently discards raised outcome | Root totality suite passed for `executor.invoke`; unhandled raise is rejected. |
| EXT-03 expression and duck-helper raise-set loss | Reconstructed peer-effects suite contains cases; its later assertions were blocked by A5. Imported function effects suite passed. |
| EXT-04 hidden panic admitted to 4097-byte speculation | Reconstructed peer-speculation fixture exists; blocked by A5 at this SHA. |
| EXT-05 imported function/method effects | Function effects pass source-free tests; method metadata gap A2 remains. |
| EXT-06 synchronous Root raise caught in `main` | Top-level and nested `if` probes reject with `ROOT_UNHANDLED_RAISE`. |
| EXT-07 lock inflation and no-op catch-all warnings | Warning paths and focused regression are present; not independently rerun. O2 wording A3 remains. |
| EXT-08 unhandled test raise aborts suite | `check_phase21_recovery_semantics.py` passed, including its unhandled typed test outcome. |
| EXT-09 `raise e` loses enum variant | Recovery semantics and native local-recovery suites passed; exact external source was unavailable. |
| EXT-10 raising pipeline callback Rust E0308 | Native recovery regression exists; not independently rerun. |
| EXT-11 Fast Debug `on_fail`/payload dispatch | Interpreter/regression code exists; not independently rerun. |
| EXT-12 Float text/division effects | `check_phase21_float_ieee.py` passed debug native, optimized native and Fast Debug with rustc 1.98.1 and repo-local 1.97.0. Its independent Python corpus checks 270 finite bit patterns. |
| EXT-13 bound `recover err` unusable in match | **Reproduced:** approved single-enum binding fails `match` with `MATCH_REQUIRES_ENUM` (A6). |
| EXT-14 provider symbol and weak `.mossi` metadata | Shared ABI test passed, but source-free suite stops at A5 and pattern-swap authenticity fails as A1. |
| EXT-15 unchecked FileError/ParseError shape | FileIO and numeric tests exist; `check_phase21_numeric_parse.py` passed. |
| EXT-16 keyword-prefix identifier | No focused permanent `recovered` identifier regression was located in this review; unresolved. |
| EXT-17 grouped recovery re-raises wrong alternative | Native recovery regression exists; not independently rerun. |
| EXT-18 ENXIO/ENODEV and close ENOSPC mapping | FileIO error test passed; it does not explicitly enumerate ENXIO/ENODEV or close ENOSPC, so those exact mappings remain unverified here. |
| EXT-19 failed-create cleanup and parent sync | FileIO error and Root cleanup tests passed for descriptor/claim cleanup. The directory-durability retry caveat remains (A7); the tests do not establish name durability after a failed creator. |
| EXT-20 timing-dependent Branch quiescence | Deterministic Branch tests exist; not independently rerun. |

## Numeric and source-surface decisions

The approved §13.3 grammar now explicitly accepts `.5`, `1.`, uppercase `E`, exponent signs and leading zeroes, and restricts nonfinite tokens to `inf`, `-inf`, and `NaN`. It also freezes ASCII digit and six-whitespace-character rules. These are approved inputs, not reviewer-invented spellings. `check_phase21_numeric_parse.py` passed. §13.2 freezes shortest-round-trip Float text and tie handling; the Float regression passed with an independent Python oracle using both rustc 1.98.1 and repo-local 1.97.0. This is a two-toolchain check on the preliminary SHA, not proof for arbitrary future rustc versions.

## Commands and exact preliminary results

All commands below used the isolated worktree compiler built from `f6f11c4` with `make moss -j2`. All generated artifacts used repository-local `tmp/`.

| Command | Result |
| --- | --- |
| `./moss agent bootstrap --json` | `moss-0.1` confirmed. |
| `check_phase21_root_totality.py ./moss` | Pass. |
| `check_phase21_shared_abi.py tmp/architect-l6/shared-abi` | Pass. |
| `check_phase21_imported_effects.py ./moss tmp/architect-l6/imported` | Pass, with imported method calls intentionally rejected. |
| `check_phase21_lock_fileio_adversarial.py ./moss` | Pass. |
| `check_phase21_handler_failure_native.py ./moss` | Pass, including class repartitioning. |
| `check_phase21_on_fail_capture.py ./moss` | Pass. |
| `check_phase20_cross_crate_range_abi.py ./moss` | Pass, including fail-closed cross-crate typed Range. |
| `check_phase21_float_ieee.py ./moss` | Pass. |
| `PATH=<repo-local rustc 1.97.0 bin>:$PATH check_phase21_float_ieee.py ./moss` | Pass, including the same independent Float corpus. |
| `check_phase21_numeric_parse.py ./moss` | Pass. |
| `check_phase21_fileio_errors.py` | Pass. |
| `check_phase21_fileio_root_cleanup.py ./moss` | Pass. |
| `check_phase21_recovery_semantics.py ./moss` | Pass; its bound error fixture only echoes the binding. |
| `check_phase21_recovery_native.py ./moss` | Pass. |
| `check_phase21_source_free_abi.py ./moss tmp/architect-l6/sourcefree` | **Fail:** stale main-level Root recovery fixture; later ABI assertions not reached. |
| `check_phase21_peer_effects.py ./moss` | **Fail:** stale main-level Root recovery in peer speculation fixture; later speculation assertions not reached. |
| Independent `.mossi` pattern-swap/source-free build | **Build passes; executable aborts (134)**. |
| Independent one-enum `recover err: match err` probe | **Fail:** `MATCH_REQUIRES_ENUM`, internal `_selected_error` type. |

No `make check`, `make examples`, or full Branch suite has been claimed for this SHA. Retest the exact frozen integration SHA after the coordinator signals code freeze; record that SHA, all gate results, and a revised GO/NO-GO here. Until A1, A6 and any approved method-contract disposition are settled, keep **NO-GO** even if general test suites pass.

## Exact-SHA independent retest: `5941d98c824f31550f520891235c8b7ac96f3992`

This section supersedes all earlier test results in this report. I checked out the commit in a separate detached worktree, built `./moss` there, and reran `./moss agent bootstrap --json` (`moss-0.1`). Every gate below used that compiler and repository-local scratch space. The original external archive remains unavailable; EXT cases are reconstructed from the recorded descriptions and permanent regressions. “FIXED” below means the reconstructed concrete counterexample passes at this SHA; it does not certify every possible expression or exceptional path. “CONFIRMED” denotes a live defect or a real, bounded contract limitation. “DESIGN-DECISION-REQUIRED” denotes a live gap whose full remedy or accepted restriction requires a change to the approved contract. “NOT-REPRODUCIBLE” is confined to the available reconstructed case.

### Release-blocking counterexamples and architectural vote

1. `repro_phase21_mossi_pattern_forgery.py ./moss` prints `REPRODUCED NO-GO: forged ABI-v8 pattern builds, then exits -6`. The provider source is removed; swapping only two failure patterns, preserving symbols, and recomputing interface hashes makes the consumer build against the unchanged rlib and abort on execution. The test process exits zero because it successfully witnesses the defect. **EXT-14 / INT-05 remain open.** ABI-v8 hashes establish local text integrity, not provider artifact authenticity.
2. `check_phase21_sourcefree_noop_warning.py ./moss` fails its final assertion: after removing the provider source, `RECOVER_EMPTY_AFTER_STATE_WRITE` is absent (`[]`), although the source-backed consumer warns and the source-free native consumer still mutates protected state. The provider `.mossi` has a WRITE parameter but no recovery-body or empty-catch provenance. **EXT-07 / INT-10 remain open.** This is a public diagnostic-contract gap across a supported source-free import, not just an untested proof case.
3. `check_phase21_imported_effects.py` passes its expected `IMPORTED_METHOD_EFFECTS_UNAVAILABLE` rejection. That is safe fail-closed behavior for current ABI-v8 records, which omit object-method observable effects, but it does not deliver the stated source-free method surface. **EXT-05 needs an approved limitation or provider-contract evolution.**

Accordingly my exact-SHA vote is **NO-GO for Phase 21 acceptance or merge**. General `make check`, examples, strict builds, and the Float matrix can add confidence to repaired behavior; they cannot convert the two observed source-free counterexamples into passing semantics. The O2 wording correction and O4/O8/§16 proof scope also need explicit closeout. I have not edited the implementation or accepted engineering status labels as evidence.

### EXT-01 through EXT-20 disposition

| ID | Independent status at exact SHA | Evidence and limit |
| --- | --- | --- |
| EXT-01 | FIXED | **Reran** `check_phase21_fileio_errors`, `check_phase21_fileio_close_full`, `check_phase21_lock_fileio_adversarial`, and source-free ABI gates; injected ordinary failures are typed in the tested native paths. |
| EXT-02 | FIXED | **Reran** `check_phase21_root_totality`; `executor.invoke` with unhandled typed Root is rejected and positive Roots execute. |
| EXT-03 | FIXED, concrete forms | **Reran** peer effects, unary `not`, field projection, method warning/effect and imported-effects gates. Group/list/helper/method, `not fail()`, and `fail().value` preserve tested raises. A path-complete O1 argument remains open. |
| EXT-04 | FIXED, concrete forms | **Reran** peer effects, unary 4097-byte speculative mapper, and Branch gates; tested hidden panic and shadowing cases are ineligible for speculation, and earlier typed raise wins. General O9 relies on O1 completeness. |
| EXT-05 | DESIGN-DECISION-REQUIRED | **Reran** imported-effects gate: source-free function/generic effects pass; source-free object methods fail closed because ABI-v8 lacks their effects. Full imported-method support remains unavailable. |
| EXT-06 | FIXED | **Reran** Root-totality gate including lexical `main` catch, nested form, and positive `on_fail` cases; §4 applies at handler ingress. |
| EXT-07 | DESIGN-DECISION-REQUIRED | **Reran** local, transitive, nested and method warning gates: pass. **Reran** source-free warning gate: fails after source removal despite native mutation. The cross-provider warning promise remains false. |
| EXT-08 | FIXED | **Reran** peer-recovery gate's unhandled-test continuation through native/Fast Debug; later test executes. |
| EXT-09 | FIXED | **Reran** peer-recovery gate's enum-valued raise and selected bare re-raise arms; exact A/B dispatch passes native/Fast Debug. |
| EXT-10 | FIXED, concrete forms | **Reran** peer-effects gate's raising eager callback forms through checker, native O0/O3 and Fast Debug. |
| EXT-11 | FIXED | **Reran** peer-recovery payload `on_fail` parity gate in native and Fast Debug. |
| EXT-12 | FIXED, pinned environments | **Reran** Float IEEE/text gate with default Rust 1.98.1 and repository-local Rust 1.97.0 using the same GCC-built Moss; native O0/O3, Fast Debug, division effect and 270-pattern oracle pass. Clang pairs are coordinator-run, not independently rerun here. |
| EXT-13 | FIXED | **Reran** peer-recovery gate including the one-enum `recover err: match err` case that failed at the preliminary SHA; native and Fast Debug pass. |
| EXT-14 | DESIGN-DECISION-REQUIRED | **Reran** source-free ABI gate: missing/canonical symbols reject. **Reran** pattern forgery: locally consistent false pattern still builds and aborts. |
| EXT-15 | DESIGN-DECISION-REQUIRED | **Reran** built-in error contract and numeric gates: shape and single-module qualified FileError arms pass; ambiguous namesakes fail closed. The approved text does not settle implicit built-in availability or a general cross-module nominal identity contract. |
| EXT-16 | FIXED | **Reran** peer-recovery gate's `recovered` keyword-prefix fixture; native and Fast Debug pass. |
| EXT-17 | NOT-REPRODUCIBLE | **Reran** reconstructed grouped A/B bare re-raise fixture in peer-recovery gate; both selected alternatives dispatch correctly. Original archive absent, so this cannot disprove its original report. |
| EXT-18 | FIXED | **Reran** FileIO error and close-Full gates; Linux ENXIO/ENODEV and close ENOSPC cases have tested six-variant mappings. |
| EXT-19 | CONFIRMED contract limitation | **Reran** FileIO error, Root cleanup and Phase 20 runtime gates; descriptor/claim cleanup passes. **Code inspected** failed-create path: a created name can survive an injected failed open and retry as `created=false`, so retry `sync()` does not prove directory-name durability. Approved Phase 20 §12.8 and Phase 21 §6 do not promise rollback/obligation transfer; record the caveat, do not call it a fixed durability guarantee. |
| EXT-20 | FIXED, bounded stress | **Reran** `check_phase21c_branch_errors.py`; barrier-based quiescence and the 64-run concurrent native case pass. |

### INT-01 through INT-12 disposition

| ID | Independent status at exact SHA | Evidence and limit |
| --- | --- | --- |
| INT-01 | FIXED | **Reran** `check_phase21_on_fail_capture.py`: joined owned captures and nested bare re-raise pass native/Fast Debug/source-free focused cases. |
| INT-02 | FIXED, concrete forms | **Reran** adversarial Root lock/FileIO, handler failure native, FileIO Root cleanup and checked error gates; tested release/acquisition order passes. General O6 still depends on complete path analysis. |
| INT-03 | FIXED, concrete forms | **Reran** `check_phase21c_branch_errors.py`; typed `Range.slice` and actual concurrent O9 paths pass. |
| INT-04 | CONFIRMED limitation | **Reran** `check_phase20_cross_crate_range_abi.py`: typed concrete cross-crate `Range` fails closed at Moss checking; native cross-crate support itself remains unavailable. Generic/same-module controls pass. |
| INT-05 | DESIGN-DECISION-REQUIRED | **Reran** forged `.mossi` pattern reproducer; accepted source-free build aborts against unmodified provider rlib. |
| INT-06 | FIXED for independently sampled matrix | **Reran** pinned Float gate with GCC-built compiler and Rust 1.97.0/1.98.1, native O0/O3 and Fast Debug. Did **not** independently rerun Clang compiler build; coordinator's four-pair result is separate evidence. |
| INT-07 | CONFIRMED proof gap | **Code/spec inspected**, with shared ABI, capture, FileIO and Branch gates rerun. Exceptional-edge consumer bits and focused paths do not establish general O4/O8 invariants or §42 mid-execution early-release reconciliation. |
| INT-08 | FIXED | **Reran** Root-totality gate, including ordinary `message` field fixture; false Root detection no longer occurs. |
| INT-09 | FIXED, concrete forms | **Reran** method no-op warning gate; protected receiver write now appears in public `observable_effects.domain_write` and warns with native/Fast Debug state parity. |
| INT-10 | DESIGN-DECISION-REQUIRED | **Reran** source-free no-op warning gate; assertion fails with no warning after provider source removal. ABI-v8 lacks warning provenance. |
| INT-11 | FIXED, concrete forms | **Reran** unary `not`, field projection and unary speculation gates; exact raises, Root rejection and bounded mapper behavior pass. O1 structural coverage is not proven by three fixtures. |
| INT-12 | FIXED | **Reran** `check_phase21_on_fail_capture.py` with handler-local recovery and `item.length()`; warning provenance no longer trips the synchronization invariant in this fixture. |

### Additional exact-SHA checks and restricted guarantees

The exact-SHA `check_phase21_shared_abi.py` passed exceptional-edge consumer-bit verification and `h_fail` class repartition cases; `check_phase21_handler_failure_native.py` passed native guard handoff. These support separate normal/failure plans, owned raised exits and guarded cleanup for tested paths. They do **not** justify the literal O2 claim that adding `on_fail` never increases the normal handler's count of exclusive classes: class repartition can increase that count without adding any normal write. I preserve the disagreement in A3 and request a normative correction, with the approved semantic consequence unchanged.

O4/O8 remain conditional: the edge schema enumerates six consumers, but a bit being present is not a proof that each consumer analysis traverses every edge and respects every owned live value, nested recovery, loop join and cancellation. Theorem A2 establishes release at a raised exit. It does not settle §16's arbitrary touched-guard mid-execution early-release question. The final claim should be restricted to the implemented and exercised paths until a general argument is reviewed.

The only implementation change after provisional `263ad67` is the four-line FileIO checker compatibility adjustment at `5941d98`: with no declared `FileError`, a Phase 20 aborting FileIO program may continue to check; a typed recovery selector still needs the declaration. I **reran** `check_phase20_fileio_semantics.py`, `check_phase20_fileio_runtime.py`, and `check_phase20_executor_fileio.py`; all passed. The Phase 21 declared-error, qualified single-module, six/two/two shape, FileIO injection and source-free gates also passed. This validates that compatibility adjustment for the tested forms; it does not resolve EXT-15's broader nominal identity/availability decision.

The numeric grammar approvals are already recorded in §13.3 (`.5`, `1.`, uppercase `E`, exponent signs, leading zeros, ASCII digits and six whitespace characters, exact `inf`, `-inf`, `NaN` spellings). The exact-SHA numeric gate passed. The Float gate passed with both independently used Rust versions and its pinned Python oracle. No new grammar or tie-breaking choice is proposed.

This independent worktree did not run `make check`, `make examples`, strict GCC/Clang builds, or the Clang Float pairs. The coordinator is running those separately; their green result would not change the two observed source-free failures or the **NO-GO** vote at this SHA. All tests above were run at `5941d98`; a later implementation edit invalidates this exact-SHA acceptance record until it is rerun.
