# Architect L6 exact-SHA retest of the nested Root expression repair

**Reviewed candidate:** `19cf731ae08291500e7488c8a03df123aac4f9aa` on `phase-21-integration`; implementation fix `3a964dc9aa22bfc7d85cccaa57515c532ee839b2`.

**Independent Phase 21 recommendation: GO at this exact SHA, within the approved Phase 21 acceptance scope.** The Root expression defect that blocked `96eef9e` is repaired in the original forms, native O0/O3, Fast Debug and source-removed ABI-v9 execution. I found no unsafe suppression in the nested-handler or trailer challenges below. This is an Architect L6 technical vote, not authorization to merge. The earlier [96eef9e review](PHASE_21_ARCHITECT_V9_REVIEW.md) remains an unchanged, attributable NO-GO for that older SHA.

I used a separate repository-local worktree and built its `./moss` from `19cf731` with `make moss -j2`. I read both repository skills and ran `./moss agent bootstrap --json`, confirming `moss-0.1`. All scratch programs and outputs were kept under this worktree's `tmp/`. Only `src/moss.cpp` changes implementation code between `96eef9e` and this candidate; the other differences add tests and reports.

## Exact counterexample retest

The original minimal `Worker.Fetch() -> Int` raises `E.Bad` and has `on_fail E.Bad: reply 7`. I copied the original review's probe files without changing their semantics and ran `./moss check ... --json`, native lowering with `-O0` and `-Oshared-memory`, `rustc --edition=2021 -Dwarnings -Copt-level=0/3`, the produced executables and `./moss run --interp`:

| Probe | Exact result |
| --- | --- |
| `echo 1 + message worker.Fetch()` | Check passes; native O0/O3 and Fast Debug print `8`. |
| `echo identity(message worker.Fetch())` | Check passes; all three execution modes print `7`. |
| Direct `echo message worker.Fetch()` and grouped `echo (message worker.Fetch())` | Check passes; all three modes print `7`. |
| Nonraising `Fetch` with the binary expression | Check passes; all three modes print `8`. |
| Exhaustive trailer plus redundant `main` `try`/`recover` control | Check passes; all modes print `8`; the catch arm is not entered. |
| `values = [message worker.Fetch()]; echo values[0]` | Check passes; all three modes print `7`. This tests the vector element's Root effect and executable value path. |
| Handler without `on_fail`, nested binary message inside `main` `try` | Check rejects with `ROOT_UNHANDLED_RAISE`; lexical `main` recovery cannot receive an unhandled Root. |

I separately built a provider with ABI-v9 `.mossi`/`.rlib`, removed its Moss source, then changed only the consumer. `./margo build --json` compiled binary, vector-value and ordinary-call-argument Root expressions; its executable printed `8\n7\n7\n`. A source-removed `provider.Bare.Fail()` without a trailer, used inside `1 + message ...` under `main` `try`, rejected with `ROOT_UNHANDLED_RAISE` and exact `enum:provider__E:Bad` provenance.

## Unsafe-suppression challenge and code path

The fix carries the `root_ingress` flag through the recursive expression forms in `observable_expression_effects`, including vector elements, binary/index operands, function and method arguments, constructor payloads and pipeline source/stages. `phase21_raise_set_block` also passes the statement's Root flag for `raise`, `if`, `while`, `for` and `match` expression sites. The flag is set only on `main` statements during checking. Native `gen_block` scopes `root_ingress_statement_` to that statement, and nested `message` expressions select the Root wrapper only when that scope is true. I inspected these paths in `src/moss.cpp`; the effect recursion now agrees with the existing lowering choice for the tested syntax.

For an independent non-main challenge, `Outer.Run` uses `reply 1 + message leaf.Fail()`; `Leaf.Fail` has its own `on_fail` but is invoked **nested**, so that trailer must not run. `./moss effects Outer.Run --json` retains `raise_set=[enum:E:Bad]`; the outer Root's `on_fail` returns `9` in native O0/O3 and Fast Debug. Removing the outer trailer rejects the Root. A nested `message leaf.Fail()` inside `Outer.Run`'s `on_fail` arm rejects with `ON_FAIL_MUST_HANDLE_ERRORS`, proving the new suppression does not silently make a trailer's nested call total. Two message calls nested as `message worker.Fetch(message bare.Fail())` also reject the inner no-trailer Root; adding lexical recovery in `main` still yields `ROOT_UNHANDLED_RAISE` for `Bare.Fail`.

The permanent `check_phase21_root_expression_ingress.py` and existing `check_phase21_absolute_root_boundary.py` both passed on this SHA. The former covers binary, vector-value, call argument, conditional and grouped expressions, the no-trailer negative, native O0/O3, Fast Debug and source-removed ABI-v9 provider execution. The latter covers direct synchronous/Executor boundaries, nested calls and source-free imports.

## Adjacent exact-SHA gates and limits

I independently reran these focused scripts with the new compiler; each exited zero: `check_phase21_root_totality.py`, `check_phase21_peer_effects.py`, `check_phase21_expression_ownership_edges.py`, `check_phase21_source_free_proof_edges.py`, `check_phase21_source_free_cache_binding.py`, `check_phase21_original_af.py`, `check_phase21_imported_effects.py`, `repro_phase21_mossi_pattern_forgery.py`, `check_phase21_sourcefree_noop_warning.py`, `check_phase21_early_condition_raise.py`, `check_phase21_early_placement_proof.py`, `check_phase21_on_fail_capture.py`, `check_phase21_handler_failure_native.py`, `check_phase21_source_free_standard_write_recovery.py`, `check_phase21_source_free_abi.py` and `check_phase21c_branch_errors.py`. Source-free authenticity remains conditional on the approved ABI-v9 trust boundary: a trusted provider build emits matching interface and object; it is not publisher authentication. The O4/O8 and bounded early-placement tests are witnesses to the reviewed invariants, not an exhaustive machine proof or a general touched-guard early-release claim.

I did not independently rerun the full `make check`, `make examples`, strict GCC/Clang builds or four-pair Float matrix on this SHA. Engineering's recorded results for those gates are separate evidence and were not substituted for the independent counterexample, code-path and adjacent-gate review here. The external original A–F ZIP remains unavailable for byte comparison; the committed attributed transcription passed its hash-pinned permanent gate.

### Separate, pre-existing vector echo defect

The *literal* earlier scratch probe `echo [message worker.Fetch()]` now passes `moss check`, but generated Rust fails with `E0277`: `Vec<i64>` lacks `Display`. Minimal `fn main(): echo [1]` has the same check-pass/native-rustc failure on both `96eef9e` and `19cf731`; Fast Debug prints `[1]`. This is a real, reproducible general checker/native echo discrepancy, not introduced by the Root fix. The practical guide shows scalar vector element output, and the approved Phase 21 contract does not require direct whole-vector echo. I therefore classify it as a separate Moss v0.1 compiler defect rather than a Phase 21 Root/ABI/O1 release blocker; it should be tracked and fixed or rejected at Moss checking in its own scope. The executable vector-element Root form above validates the Phase 21 semantics without relying on whole-vector formatting.

**Final disposition at `19cf731`:** the specific `96eef9e` NO-GO condition is closed in supported Phase 21 forms, the no-trailer and nested-handler safety boundaries remain enforced, and the independently rerun ABI/O1/ownership/locking gates pass. My Phase 21 Architect L6 vote is **GO** for this exact implementation SHA, subject to ordinary L7 merge authorization and without claiming broader vector echo correctness.
