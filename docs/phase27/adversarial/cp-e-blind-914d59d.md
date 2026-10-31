# CP-E blind review — frozen 914d59d

CP-E verdict: NO-GO

## Freeze and method

- Candidate: `914d59dcb0d4825f1ae652b6a23a7db742a5b3fd` (`origin/phase27-3-work` at review start).
- Baseline: `04be1a1bbbc7a4fd8fe2e4090b4b5bad5dd573e4`.
- Isolated candidate worktree: `tmp/cp-e-blind-914d59d`; baseline worktree: its `baseline/` child. Both bootstrapped as `moss-0.1`; candidate compiler was built because the worktree had no `./moss` binary.
- No `src/` files were edited. The common-corpus runner checked 909 shared standalone `.moss` files (all `tests/` and `sdk/` sources except phase27 EST repros and adversarial directories) in both `off` and `shadow`: 1,818 runs per revision, zero timeouts/invalid JSON. Raw outputs are in `tests/phase27/adversarial/cp-e-blind-914d59d/corpus-{baseline,candidate}-off-shadow.json`.
- After normalizing source paths, the only diagnostic-code/message delta is the context-dependent `sdk/advanced-data-acceptance/fixtures/source_free_consumer.moss`, which cannot be checked as a standalone file because its imported module interfaces are not on `MOSS_MODULE_PATH`. Baseline reports `RECOVERY_PATTERN_REQUIRES_VARIANT`; candidate reports `UNKNOWN_ERROR_TYPE` for `ndarray.NdArrayError`. The repository advanced-data source-free acceptance gate passes in package context on both revisions (ABI-9 debug; 5 Team A modules and 14 source-backed provider interfaces), so this standalone diagnostic delta is not treated as a semantic regression.
- `python3 tests/tooling/check_phase27_est.py ./moss` passes on candidate and baseline. `python3 tests/tooling/check_phase27_string_debt.py` passes on candidate.

## Expression-bearing statement reader inventory

`Stmt` carries four legacy text slots: `text`, `a`, `b`, and `args`. The parser initializes `text` to the full source line; the expression-tree binder then assigns expression slots by statement kind:

| Slot | Expression-bearing cases | Bounded non-expression cases |
|---|---|---|
| `text` | Raw statement expression / assignment text; Call statement expression | Original source/provenance for other kinds |
| `a` | If/while condition, match subject, assignment place, return/reply/raise value | Binding/receiver/handler names, for-loop binder, case/recovery pattern |
| `b` | For iterable; var/assignment RHS | Member/handler name for calls/messages |
| `args` | Call, message, and echo value arguments | Case payload binder names |

Function/method result expressions and state/object initializers are separate expression-bearing roots with their own handle vectors. EST binds condition, iterable, scrutinee, initializer, LHS/RHS, return, raised-value, receiver, expression, argument, and pattern slots. Expression children, including constructor fields, named-argument values, member/index operands, collection items, and pipeline stages, are bounded aliases derived from those roots through EST projections.

The compiler-side inventory in `check_phase27_string_debt.py` found zero remaining legacy expression-reader API sites in checker, emitter, reach-walk, or Fast Debug consumers. The parser has 23 counted lexical/parser helper sites, compiler frontend 48, and optimizer 10; these are not checker/emitter expression-semantic readers. The fail-closed raw-slot audit reports 40 classified NAME comparisons, 9 classified binding comparisons, no unclassified comparison/shape checks, and no `tree_expression_text` reads. There are 3 keyed exemptions; each is keyed by source path, function, and API. The audit rejects malformed, duplicate, or empty entries, stale entries, and exemptions that match multiple sites. The script completed successfully.

`tree_split_binary` caches by expression string and operator set and returns cached entries before shadow comparison; shadow parity is therefore checked once per cache key, not separately per source occurrence or specialization context. No performance run was performed.

## Fresh malformed-source probes

Fresh probes are in `tests/phase27/adversarial/cp-e-blind-914d59d/probe-sources/`; outcomes for baseline/candidate in both modes are in `probe-results.json`.

- Empty call slots (`sink(1,,2)`, `sink(,2)`, `sink(1,)`) and repeated collection commas (`[1,,2]`, `[1,[2,,3],4]`) are rejected by both revisions. Incomplete binary and parenthesized expressions also remain rejected by both.
- **MIGRATION BUG:** bare unary `not`, an unclosed collection `[1, 2`, and a trailing member access `item.` are rejected by the baseline but accepted without diagnostics by candidate in both modes.
- **MIGRATION BUG:** `1 + * 2` is rejected by baseline in both modes and accepted by candidate `off`. Candidate `shadow` aborts with `tree_split_binary` assertion `result == legacy` (`semantic_checker.cpp:11383`). This is a fail-closed violation in shadow mode as well as an invalid-expression acceptance in off mode.
- `items[` is accepted by both baseline and candidate in both modes; classify this shared malformed-input behavior as **LEGACY MISPARSE**, not a candidate-only migration regression.

The candidate's focused EST regression harness passing does not cover these fresh counterexamples. The invalid-expression acceptance and shadow process abort independently require NO-GO.

## Declaration closure, native SDK, and source-free package oracle

- `margo build --json` for `sdk/tables` succeeds in both revisions in default, `off`, and `shadow` modes. Each build resolves/checks the six-package closure: `moss.core`, `moss.text`, `moss.fmt`, `moss.parse`, `moss.csv`, and `moss.tables`. Captured JSON is under the evidence directory.
- `margo test` for `sdk/tables` fails on both revisions at `sdk/tables/tests/tables_test.moss:151` with the same `TYPE_MISMATCH` on binding `ignored`; this is not a candidate-only failure.
- The repository advanced-data source-free acceptance gate passes on both revisions in ABI-9 debug (5 Team A modules, 14 current source-backed provider interfaces). A separate fresh consumer importing `tables` using generated `tables.mossi` module interfaces (with `MOSS_MODULE_PATH` restricted to build/debug interface directories) passes `moss check`, native `margo build --moss-json`, and execution on both revisions; output is `0`. Consumer and raw results are under `package-oracle/`.

## Evidence and recommendation

The source-free declaration/package path and shared corpus are broadly consistent, but the four candidate-only malformed-expression acceptances and one shadow abort violate the parser migration's rejection/parity contract. Fix the EST handling and ensure shadow checks cannot abort on these inputs, then rerun CP-E against this same freeze family before integration.
