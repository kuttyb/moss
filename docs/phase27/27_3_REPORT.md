# Phase 27.3 report (in progress)

## Corrections and independent reproductions

| Classification | Finding and correction | Oracle | Reproducer |
| --- | --- | --- | --- |
| MIGRATION BUG | Checker effect/ownership walks split on the final dot in FileIO path strings, passing truncated fragments to recursive expression checking. Both now project EST `Member` nodes. | Phase 27.2 compiler at `04be1a1`; current off and shadow produce the same successful check result. | `tests/tooling/fixtures/phase20_fileio_runtime_integration.moss:29`; CP-E paired dotted-member probes. |
| MIGRATION BUG | `inferred_expr_core` split a member receiver at the final dot in the string argument `"tmp/phase21c-concurrent-a.data"`. It now projects member access from the EST node. | Phase 27.2 compiler at `04be1a1`; current off and shadow agree on successful checking. | `tests/tooling/fixtures/phase21c_concurrent_roots.moss:57`; calls `a.Run("tmp/phase21c-concurrent-a.data", 0, 1)`. |
| MIGRATION BUG | The `project_enum_case` shadow oracle parsed parenthesized case source without first removing redundant grouping, while EST projects the underlying constructor node. Shadow parity now invokes the unchanged legacy parser on the normalized child source. | `tests/phase27/adversarial/cp-e-fresh/check_cp_e_fresh.py` compares Phase 27.2 off, candidate off, and candidate shadow; all check and execution results agree for `unwrap((Choice.Value(value: 7)))`. | The parenthesized enum constructor case in that committed CP-E probe. |
| LEGACY MISPARSE | `split_binary` sees `==` inside the rejected statement-shaped expression `if ...: ... else: ...`; EST correctly has no expression root. Shadow comparison narrowly accepts only this `if`/colon/`else:` shape. | Phase 27.2 off, candidate off, and candidate shadow report the same `INVALID_EXPRESSION`; the independent oracle is the checker diagnostic. | `invalid_if_expression` in `tests/phase27/adversarial/cp-e-fresh/check_cp_e_fresh.py`; also `x = if 1 == 1: 1 else: 2` in `tests/tooling/check_phase25_s2_char_string.py`. |
| MIGRATION BUG | Fresh CP-E found raw punctuation and prefix tests on assignment places and vector initializers, plus a source-text search for a member-call requirement origin. These now use EST index/member/vector projections and a node walk for matching member calls. FileIO call reconstruction also uses an identifier projection. | Phase 27.2-m1, candidate off, and candidate shadow agree on the indexed/member assignment runtime fixture; the imported-generic and typed semantic API diagnostics pass. | `tests/indexed_field_assignment.moss`; CP-E's four unchanged source probes; `tests/tooling/check_phase26_1_t1_generic_imports.py`; `tests/tooling/check_phase25_typed_semantic_api.py`. |

Focused evidence from the current tree: `tmp/phase27-3/cp-e-after-enum-and-if-fix.json`, `tmp/phase27-3/phase21c-phase27-2.json`, and `tmp/phase27-3/fileio-phase27-2.json`. Full gates at pushed `53025f4d` are recorded in `.codex/CURRENT_STATUS.md`; they predate the fixes in this report and must be rerun at a new checkpoint.

## Remaining sections

## Fresh CP-E checkpoint

Agent 2 reviewed frozen candidate `068eb9aa130e51778441f222b1a449a48247bef9` against phase27-2-m1 `04be1a1bbbc7a4fd8fe2e4090b4b5bad5dd573e4` and returned **CP-E verdict: NO-GO**. Evidence commit: `88b2b006f8c97adfd3f1e71e0bb7421eb01bb249` (recorded on the phase branch as `c5741773`). Four independent probes matched verdicts and diagnostics in baseline, candidate off, and candidate shadow. The review found expression-shape reads missed by the widened lint: bracket/dot checks on assignment targets; bracket-prefixed initializer checks for vector classification in checker and emitter; and a dot/call-suffix assignment-target classification. Full inventory and exact locations: `docs/phase27/adversarial/cp_e_agent2_068eb9aa_inventory.tsv`; report: `docs/phase27/adversarial/cp_e_agent2_068eb9aa_review.md`; probes: `tests/phase27/adversarial/cp-e-agent2-068eb9aa/`. These are unconverted migration reads; no semantic delta was reported. The exhaustive inventory and full corpus closure differential are still open.

The off-mode full gate was started at `068eb9aa` and interrupted after the CP-E NO-GO, so there is no full-gate result. Shadow full gate was not started on that checkpoint. After the raw-shape fixes, `make moss-dev`, the EST focused harness, Phase 25 S2 char/string suite, shadow Phase 20 FileIO semantics, four unchanged CP-E probes against baseline/off/shadow, indexed/member assignment runtime output against baseline/off/shadow, imported-generic checks, typed semantic API checks, all four zero-reader lint invocations, and feedback validation pass. Fresh exhaustive CP-E is still pending on a pushed checkpoint. CP-F, semantic-fact audit, full parity/sanitizer/performance gates, merge SHA, and tag remain outstanding.

Full off gate at `6b926fc7`: **FAIL**, 157/159 jobs passed, zero skips; log `tmp/phase27-3/check-off-6b926fc7.log`. `check_run_parallel_timeout` hit a `ProcessLookupError` while reading the child process stat; `check_phase25_4_performance_baseline` exceeded w2 (1216 ms against 1150 ms) and w3 (946 ms against 900 ms). CP-E was concurrently running a full corpus differential, so repeat the performance-sensitive gate without that load. No waiver or final performance disposition is made.

## Escalation blocker — semantic/package verdict change

CP-E verdict: NO-GO

Frozen candidate: `6b926fc7dd3ed42dedddb0f2f8f60f931b106a68`

Baseline `phase27-2-m1`: `d4446b7250f651e60e978740661e6fb20ff6ef10`

Evidence payload: `c8cb5ae60e5f1017dfcebb56f97256ca6e6160ff` (review branch HEAD `aa0dc86ea4751df5e5cbe901bed0e9b3841e3559`; payload and follow-up are recorded on this branch as `8687e1a2` and `21e74a74`).

On `tests/phase27/est/repro/incomplete_expression.moss` and the identical benchmark source `fn main(): echo 2 +`, baseline off rejects with `MOSS_COMPILE_ERROR:2`, candidate off accepts, and candidate shadow aborts with SIGABRT before JSON. A paired mini-package `margo build --check` reproduces baseline rejection and candidate acceptance. **Classification: SEMANTIC**, and a package-mode verdict changed. The source-free SDK fixture also changes standalone diagnostic from `RECOVERY_PATTERN_REQUIRES_VARIANT:57` to `UNKNOWN_ERROR_TYPE:57`; its actual provider-interface package result is unverified. Exact per-file evidence is in `docs/phase27/adversarial/cp_e_rawshape_6b926fc7_verdict_diff.tsv`, paired/package tables, report, inventory, and probes under `tests/phase27/adversarial/cp-e-rawshape-rerun/`.

The exhaustive field inventory contains 1,192 line-level occurrences: 113 EST/expression-reader calls, 96 raw-string-operation review hits, 58 literal-comparison review hits, 484 expression-value uses, and 441 mixed name/expression uses. `tree_expression_text` has zero checker/emitter uses; there are 39 audited NAME comparisons, 9 binding comparisons, and three canonical-type split exemptions. CP-E demonstrates the widened lint still has an alias/helper blind spot: it catches direct Stmt-field shape tests but misses equivalent tests after copying expression text to a local string or helper parameter. Remaining indirect raw readers are enumerated and classified in the committed review.

The off verdict-corpus comparison completed: baseline had 2,118 sources and 1,394 accepted; candidate had 2,124 sources and 1,398 accepted; 2,118 paths were common, with three verdict changes and six candidate-only sources. The checked-declaration closure differential timed out at 90 seconds on a specialization workload; its 300-second retry was stopped before producing a report. No closure pass is claimed.

## L7 ruling and correction — incomplete expression

L7 ruled that acceptance of malformed REJECTED-SOURCE input is a MIGRATION BUG, not SEMANTIC. The required behavior is identical diagnostics on both paths. The incomplete-expression mismatch is therefore a fix-and-rerun finding, not a stop condition.

The EST incomplete-trailing-operator query now identifies incomplete operator tails. Checker validation emits the baseline MOSS_COMPILE_ERROR / invalid arithmetic expression before semantic interpretation; shadow binary-split parity avoids asserting on the legacy partial pair for that rejected input. Regression coverage in tests/tooling/check_phase27_est.py checks the 27.1 rejected-source fixtures for **, in, &&, ||, incomplete expressions, and all six files containing the seven isolated delimiter fragments. It requires the baseline diagnostic signature in off and shadow. The incomplete-expression repro is also checked in standalone and package modes. The focused EST regression passes.

The prior Agent 2 review's NO-GO still stands due the indirect expression-reader inventory and the incomplete checked-declaration closure comparison. The reviewer identified helper/local aliases that the direct-field lint does not follow. These are the next bounded taint-audit and migration items; no semantic stop is active under the L7 ruling.
