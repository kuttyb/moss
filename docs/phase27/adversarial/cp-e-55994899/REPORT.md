# CP-E blind review — 55994899

**Candidate:** `55994899539532c763d6489b16b859f419b12205`
**Comparison baseline:** `04be1a1bbbc7a4fd8fe2e4090b4b5bad5dd573e4`
**Verdict: GO**

## Reader and taint audit

The checker, emitter, reach-walk, and Fast Debug zero-reader lints all pass. The lint reports 42 reviewed raw `Stmt` NAME comparisons and seven checker binding-name comparisons; no expression-shaped `Stmt` classifier or `tree_expression_text` read remains. I followed the surviving expression-bearing `Stmt.a`, `.b`, `.text`, and `.args` values into helper and local flows: expression parsing and shape checks go through EST projections (`tree_parse_*`, `tree_*_identifier`); emitted type queries go through `generated_expr_type`, which starts from EST grouping and projections. Remaining literal comparisons are the audited method, handler, keyword, and binding-name slots documented by the inventory and exemption file.

All three function/API-keyed type-argument exemptions were mutated in an isolated `tmp/` copy. For each exemption, a missing target, stale key, and duplicate reader site failed closed (nine rejections total). The mutation harness is `tests/phase27/adversarial/cp-e-55994899/exemption_fail_closed.py`.

## Full source and declaration matrix

The phase27-2 dev compiler binary was SHA-256 verified as `4a334c71d9b5ea5b2925c311f7a462abd046e479fc4b52df4c0453e612ceb286`. Baseline-off and candidate-off checked all 2,225 candidate-frozen `.moss` inputs:

- No accepted/rejected verdict changes.
- No checked-ID closure differences across the 661 inputs with complete scope traces (661 complete traces on each compiler); no trace-completion differences.
- Two diagnostic-only changes, both for `source_free_consumer.moss` checked without its provider interfaces. The baseline stops at `RECOVERY_PATTERN_REQUIRES_VARIANT`; the candidate parses the qualified variant and reports `UNKNOWN_ERROR_TYPE` because `ndarray` is not available in this standalone invocation. Both the baseline and candidate pass the actual compiled-only ABI-9 source-free package gate with the generated `.mossi` provider interfaces supplied. I classify these standalone diagnostic refinements as **BENIGN**, not source behavior changes.

Raw rows and the compact summary are in `tests/phase27/adversarial/cp-e-55994899/baseline-candidate-matrix.jsonl` and `docs/phase27/adversarial/cp-e-55994899/baseline-candidate-matrix-summary.json`.

A separate candidate off/shadow matrix also found no verdict or diagnostic differences among all 2,225 inputs. Its 661 complete traces have four checked-ID differences, all on identically rejected malformed roots where off also checks the unrelated `heap__Entry.key_of` method. The remaining 159 reach-edge differences change only target display spelling; edge owner/kind topology is identical. These did not appear in the baseline-off/candidate-off checked-ID closure comparison.

## Focused gates

- Rejected-source and EST structural controls: `python3 tests/tooling/check_phase27_est.py ./moss` — PASS.
- Functional tree and Fast Debug output: `python3 tests/tooling/check_phase27_est_functional_tree.py ./moss` — PASS.
- Raw `.mossi` off/shadow parity, generic interface body, and imported execution body: `python3 tests/tooling/check_phase27_est_mossi.py ./moss` — PASS.
- Source-free package and runtime with ABI-9 interfaces: `sdk/advanced-data-acceptance/check_source_free.py --abi v9 --profile debug` — PASS on baseline and candidate.
- Reachability named-form controls: `python3 tests/tooling/check_phase27_reach_named_forms.py ./moss ./moss` — PASS.
- Checker, emitter, reach-walk, and Fast Debug reader lints: PASS with zero readers on every surface.

No Moss source files were edited. The only staged paths for this review are the two CP-E evidence directories.
