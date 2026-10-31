# CP-E blind re-review — 2026-10-30

Candidate: `68a61a7b927b6ae9e651de9866d8a034405f95f9`  
Baseline: `04be1a1bbbc7a4fd8fe2e4090b4b5bad5dd573e4`  
Bootstrap reports `moss-0.1`.

**NO-GO**

## Direct reader lint and exhaustive direct inventory

The expanded checker and emitter lints both pass at zero reader sites. `tree_expression_text` has zero uses under `src/`. The raw Stmt comparison census has 42 NAME comparisons and 9 assignment/binding comparisons; all 51 are individually recorded in `reader-inventory.json` with reasons. The three type-argument exemptions are keyed by function plus API and each fails closed if the function vanishes or gains another reader site (6/6 mutation checks).

The zero direct-lint count does not establish zero readers. The bounded taint audit found the in-scope checker and emitter expression readers in `bounded-taint-audit.json`; each is classified MIGRATION BUG. They inspect expression characters after copying text into helper parameters/locals. These findings block CP-E GO.

## Differential and regressions

The corrected identical-source-archive corpus run covered 2,103 `.moss` files and 8,412 checks (baseline/candidate × off/shadow), with no timeout or non-JSON result. Two diagnostic-only differences occur on `sdk/advanced-data-acceptance/fixtures/source_free_consumer.moss:57`: both sides reject; baseline emits `RECOVERY_PATTERN_REQUIRES_VARIANT`, candidate emits `UNKNOWN_ERROR_TYPE`. The independent source-free package oracle passed in all four baseline/candidate × off/shadow modes and produced the expected consumer output. Classified BENIGN diagnostic refinement; no verdict change.

The earlier 3,770-pair closure result is **INVALID / SUPERSEDED**: the baseline was run against a different source archive. The corrected run used the identical candidate source archive for both compilers: 2,103 files, 8,412 runs, 7,624 complete traces, 3,812 complete paired cases, and 12 short-bound timeout records. Four closure additions are classified LEGACY MISPARSE with paired raw evidence and independent type/call oracle in `closure-adjudication.json`. Both empty-call-argument fixtures are rejected identically by baseline and candidate in off and shadow: line 5 column 1 `TYPE_MISMATCH` (empty argument has type `''`). The explicit callee is `add`; baseline closure misses its declaration and call edges, while candidate records them. No declaration or edge is lost by candidate. This is closure bookkeeping on rejected malformed syntax, with no verdict or diagnostic delta.

Two unique fixture contents are BOTH-TIMEOUT at the 600-second bound, with no output on either side, and are excluded under the agreed ruling: `examples/functional_pipeline.moss` (also identical to `benchmarks/compiler_torture/fixtures/examples/functional_pipeline.moss`) and `examples/frontend_syntax.moss`. Raw elapsed times, hashes, and side/mode are in `fast-debug-long.json`.

The named-argument migration regression passes 4/4 baseline/candidate × off/shadow checks with `CALL_NAMED_ARGUMENT_UNSUPPORTED` at line 5 and no shadow abort. The Phase 27.1 malformed/rejected-source regression sweep passes in off and shadow, including `**`, `in`, `&&`, `||`, incomplete expressions, and fragments. The focused EST suite and checker-authority suite pass; candidate build succeeds. The source-free package oracle passes in all four modes.

## Verdict basis

The only parity differences are the adjudicated BENIGN diagnostic refinement and the four adjudicated LEGACY MISPARSE closure additions; there is no verdict delta. However, the taint audit found active expression-text interpretation in both checker and emitter. Those are MIGRATION BUGS, so this CP-E verdict is NO-GO. No source was edited by this reviewer.

## Evidence map

Raw scripts, outputs, source probes, linter logs, closure traces, long-bound records, and checksums accompany this report under `tests/phase27/adversarial/cp-e-postfix-agent2-20261030/`. `closure.summary.json` is the corrected run; `closure-run-summary.json` is retained only as a raw artifact and must not be read as the earlier invalid 3,770 result. Sanitized deletions and `.codex/CURRENT_STATUS.md` are not part of the evidence commit.
