# CP-E fresh adversarial review

Frozen candidate: `53025f4d618d84403da6ba1e64b250229af3b57e`.
Comparison baseline `phase27-2-m1`: `04be1a1bbbc7a4fd8fe2e4090b4b5bad5dd573e`.
The candidate worktree was detached at the frozen SHA; the comparison compiler was built in a separate worktree. No `src/` files were modified.

## Exhaustive checker/emitter inventory

The line/field appendix [cp_e_fresh_inventory.tsv](cp_e_fresh_inventory.tsv) enumerates all 1,154 checker/emitter field-access occurrences matching `.a`, `.b`, `.text`, `.args`, or `.result_expression` across the nine source files in the lint's checker/emitter scope. It records source path, line, field, classification, rationale, and source line. Categories are assigned to each occurrence by exact lexical containment in a recognized API/comparison call; `other` includes field transport, metadata, serialization, and operations outside those reader/comparison forms. These are source-use occurrences, so repeated accesses on a line are separate entries.

| Classification | Occurrences |
| --- | ---: |
| Expression reader (registered parser/classifier argument or `trim`) | 41 |
| Allowed NAME/keyword/binding comparison | 49 |
| Structural AST equality operand | 2 |
| Other field use | 1,062 |
| **Total** | **1,154** |

Cross-checks against the lint's stricter comparison inventory: **39** raw literal NAME/keyword comparisons and **9** dynamic binding-name comparisons. The checker and emitter each have **0** remaining registered string-reader sites under `--require-zero`; the three type-string `split_top_level` calls are explicitly exempted. There are **10** calls to `tree_expressions_structurally_equal` in checker code (excluding its definition); those are tree-based expression-equivalence checks rather than rendered-string equality.

A whole-source `git grep -n 'tree_expression_text' -- src` found **0 occurrences**. The lint's call-form scan also found zero checker/emitter matches.

## Lint and exemptions

Ran `python3 tests/tooling/check_phase27_string_debt.py --require-zero checker`, `... emitter`, `... reach_walk`, and `... fast_debug`; all four returned 0. The normal lint also passes and reports exactly 39 NAME comparisons and 9 binding comparisons.

Each exemption is a single `split_top_level` call in a named checker function over canonical type arguments: `phase20_is_nontransferable_capability`, `builtin_equality_type`, and `contains_float_type`. The code uses the split pieces as nested type spellings and recursively validates them; none receives a source expression slot. The lint keys exemptions by path/function/API, rejects missing/ambiguous functions, stale keys, and any count other than one. This is narrow and fails closed for the registered exemption sites.

Lint blind spots: the source scope is a fixed `CONSUMER_PATHS` filename list, so a new checker/emitter file must be added there to enter the audit. The text-reader inventory is API-name based and cannot detect an unregistered helper or an expression field copied through an alias before classification. Raw comparison guards recognize specific regex shapes rather than parsing C++; parenthesized fields, helper predicates, and nonliteral comparison forms outside the dynamic-binding pattern could evade them. The independent whole-`src` identifier search and complete field-use appendix reduce, but do not eliminate, that static-lint limitation.

## Paired behavior probes

The probe source fixtures are embedded in [check_cp_e_fresh.py](../../../tests/phase27/adversarial/cp-e-fresh/check_cp_e_fresh.py). The script runs `moss check --json` and Fast Debug under baseline `MOSS_EST=off`, candidate `MOSS_EST=off`, and candidate `MOSS_EST=shadow`; it compares verdicts, structured diagnostic code/message/line, and execution status/output.

| Case | Phase 27.2 off | Frozen off | Frozen shadow |
| --- | --- | --- | --- |
| Parenthesized enum constructor passed to `unwrap`, then matched | check 0; run 0, prints `7` | check 0; run 0, prints `7` | **check -6; run -6**, assertion abort |
| Dotted string path `text.ascii_lower().length()` | check/run 0 | check/run 0 | check/run 0 |
| Invalid dotted path `text.ascii_lower().not_a_method()` | check 1, `TYPE_MISMATCH`, line 3 | same | same |
| Unreachable valid `dormant` function | check/run 0; prints `closure-ok` | same | same |
| Reachable `dormant` function | check/run 0; prints `dormant` | same | same |

The constructor probe is a migration defect in the shadow parity assertion: the legacy `parse_enum_case_expression` projection and EST `project_enum_case` projection disagree for the valid parenthesized constructor, and the candidate aborts at `src/est/expression_query.cpp:910` (`assert(legacy == result)`). This is not merely a diagnostic drift: the frozen checker/runtime abort while the baseline and candidate legacy mode accept and execute the same program. It is a blocker.

## Reproduction commands and results

From the repository root, the two compilers were built independently with `make --jobs="$(nproc)"` in `tmp/cp-e-base` (Phase 27.2) and the candidate worktree. Then, from the candidate worktree:

```sh
python3 tests/tooling/check_phase27_string_debt.py --require-zero checker
python3 tests/tooling/check_phase27_string_debt.py --require-zero emitter
python3 tests/tooling/check_phase27_string_debt.py --require-zero reach_walk
python3 tests/tooling/check_phase27_string_debt.py --require-zero fast_debug
python3 tests/phase27/adversarial/cp-e-fresh/build_inventory.py
./tests/phase27/adversarial/cp-e-fresh/check_cp_e_fresh.py
```

The first four commands returned 0; inventory returned 0 and wrote the TSV appendix. The paired probe returned **1** after preserving all case results, because candidate shadow aborts on the parenthesized constructor. Its stderr names `project_enum_case` and the `legacy == result` assertion. Probe output captured in `tmp/` is disposable; the source, exact expected cases, inventory appendix, and this review are durable evidence.

## Verdict

CP-E verdict: GO-with-defects
