# CP-E: Phase 27.3 final migration at `ce924341`

Candidate: `ce924341c8db7d80ef09509f786711b0c304d62d`
Baseline: `04be1a1bbbc7a4fd8fe2e4090b4b5bad5dd573e4`
Review: independent worktree, candidate source frozen; no `src/` edits.
Evidence: `tests/phase27/adversarial/cp-e-final-migration/`.

## Findings

1. **One bounded-taint reader remains.** In `src/semantic_checker.cpp:22910–22914`, the checker copies expression-bearing `s.b` into local `source` with `trim`, then classifies it using `source != "FileIO.open"`. The direct-field linter misses this alias flow. This is the only raw expression-shape comparison found in the bounded local/helper trace. Candidate and baseline accept both direct and parenthesized `FileIO.open(...)` probes in EST off; candidate shadow agrees. The full corpus has no associated verdict or diagnostic difference. This is a migration/lint-coverage defect, without a demonstrated behavior regression.

2. **The direct reader gates pass.** `tree_expression_text(...)` has zero source hits. The widened string-debt checks pass with checker=0 and emitter=0 expression-text parser/classifier sites; their audited raw NAME and binding comparisons remain 40 and 9. The three function/API-keyed exemptions are present once each and accepted by the fail-closed lint: `phase20_is_nontransferable_capability/split_top_level`, `builtin_equality_type/split_top_level`, and `contains_float_type/split_top_level`.

3. **Checked declarations match.** For all 543 corpus sources accepted by baseline and candidate off, `symbols --source` queries succeeded in both compilers and the normalized source declaration sets matched: zero query failures and zero differences. A separate unreachable-declaration probe confirms that the unused function remains listed, while an unreachable body with `missing_name` is rejected as `UNKNOWN_SYMBOL_OR_TYPE` in baseline off, candidate off, and candidate shadow.

4. **Off/shadow corpus parity passes.** The candidate worktree's 944 `tests/**/*.moss` inputs produced 543 accepted sources under each configuration. Baseline off vs candidate off had zero verdict/diagnostic differences; candidate off vs shadow had zero differences; there were no timeouts.

5. **Standalone/package source-free diagnostics match.** With a compiled-only provider artifact and the provider source removed, direct `moss check` and package `margo build --check` both report `SOURCE_FREE_PROVIDER_SOURCE_MISSING` at line 0. The diagnostic code, line, and message match between modes and between baseline/candidate. This oracle checks the source-free missing-provider diagnostic path; it does not claim successful provider loading without freshness source.

## Enumeration and artifacts

`field-census.tsv` preserves a conservative, line-by-line census of expression-slot-like `a`, `b`, `text`, and `args` field accesses plus EST handle/pattern and binding metadata reads across checker/emitter consumer files. It intentionally includes ambiguous receivers as a superset. `bounded-taint.tsv` traces field-to-local/helper flow and labels the residual classifier above. `reader-gates.json` records lint output, hit counts, and all exemptions. The corpus and closure JSONL files preserve per-source results. Probe sources and the standalone/package oracle script are unchanged under the same directory.

The audit ran no performance work. It did not modify compiler source.

CP-E verdict: GO-with-defects
