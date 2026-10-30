# CP-E independent review: Phase 27.3

Frozen candidate: `c211da42` (`phase27-3-work`)
Baseline: `04be1a1bbbc7a4fd8fe2e4090b4b5bad5dd573e4` (`phase27-2-m1`)

## Verdict

CP-E verdict: **NO-GO**.

The candidate has no paired corpus verdict or diagnostic changes in the checks below. The remaining blocker is an incomplete checker expression-tree migration: `src/semantic_checker.cpp:20601-20604` copies the `For` iterable expression from `statement.b` into `iter_expr`, then classifies the raw spelling with `starts_with("range(")`, `starts_with("[")`, and `ends_with("]")`. This is a semantic expression-shape read: it sets `effects.may_diverge`, which feeds checker effect facts. The checker lint reports zero because it does not propagate taint through this local alias or recognize these string predicates on that alias. Replace the classification with EST-backed iterable queries and rerun CP-E. This is a migration gap, not an observed baseline behavior change.

## Reader inventory

`field_inventory.tsv` and its unchanged generator enumerate 975 line-level reads of the expression-capable legacy `Stmt` fields (`a`, `b`, `text`, `args`, and `result_expression`) across checker and emitter consumers. Reviewed uses route expression content to EST-backed `tree_*` queries or checked expression/type data; statement-dependent identifier, receiver, handler, binding, and keyword slots are name uses; diagnostic text and code-generation forwarding are transport. The literal-comparison inventory classifies the 40 values as `push`, `consume`, `invoke`, `pop`, `pop_front`, `assert`, `join`, `insert`, `sort`, `pass`, `FileIO`, `_`, `delete`, `assertEqual`, `write`, `open`, `sync`, and `close`; these occupy method, builtin, keyword, or capability-name slots. The other 9 comparisons compare binding-name identities. Exact locations are in `raw_comparison_inventory.tsv`. The raw source-shape alias at line 20601 is the exception and is the NO-GO finding; `check_alias_blindspot.py` reproduces that the checker gate exits 0 while the alias sink is present.

A direct source search of the checker/emitter files found zero `tree_expression_text` references. The widened gate passed `--require-zero checker` and `--require-zero emitter`, each reporting 40 audited raw NAME comparisons and 9 binding comparisons. The 40 literal comparisons are method names, built-in names, keywords, or binding protocol spellings; the 9 variable comparisons compare binding names. These gate results do not detect the iterable alias above. Separate gates show optimizer and parser still have their listed text-reader counts (exit 1), while reach walk and Fast Debug return zero (exit 0). Per scope, those four results are reported separately and are not included in the CP-E verdict.

## Paired corpus and closure

`run_corpus_parity.py` and `corpus_raw.jsonl` compare every `tests/**/*.moss` file as a standalone check using `MOSS_EST=off` on both binaries. Result: 924 files; 533 accepted and 391 rejected on each compiler; zero verdict or structured diagnostic mismatches. Malformed rejected-source cases in the test corpus are included in the 391 rejects. The script is retained unchanged for reproduction.

`closure_enum_recovery.moss` tests grouped enum construction/matching and typed recovery. Its declaration census contains 15 source symbols, including 8 callable declarations. The main call closure reaches 6 callables; `unused_leaf` and `unused_middle` are unreachable. All 8 callable declarations, including both unreachable ones, resolve with `type_facts.origin=checked_declaration` on both compilers. Check diagnostics, call closure, declaration census, and each callable status match exactly; raw results are in `closure_raw.json` and the summary is in `closure_summary.json`.

The paired package-mode fixture has a local library dependency with reachable and unreachable exports. `margo build --check --json` succeeds on candidate and baseline with identical structured result. Reproduction script, package sources, raw output, and summary are under `tests/phase27/adversarial/cp-e-c211da42/`.

## Reproduction

From the review worktree at `c211da42`, after building both pinned checkouts:

```sh
python3 tests/phase27/adversarial/cp-e-c211da42/run_corpus_parity.py \
  --candidate "$PWD/moss" --baseline "$PWD/tmp/cp-e-phase27-2/moss" \
  --root "$PWD" --out "$PWD/tests/phase27/adversarial/cp-e-c211da42"
python3 tests/phase27/adversarial/cp-e-c211da42/run_closure_census.py \
  "$PWD/tmp/cp-e-phase27-2/moss" "$PWD/moss"
python3 tests/phase27/adversarial/cp-e-c211da42/run_package_parity.py \
  "$PWD/tmp/cp-e-phase27-2/moss" "$PWD/moss"
python3 tests/tooling/check_phase27_string_debt.py --require-zero checker
python3 tests/tooling/check_phase27_string_debt.py --require-zero emitter
```

No compiler performance or Phase 27.3 gate was run concurrently with the review. No source files were edited.
