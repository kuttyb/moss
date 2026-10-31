# Agent 2 blind CP-E review — 81dc92d7

Frozen candidate: `81dc92d7ea99a4c89bf954bf8d6c96f932401077` (`origin/phase27-3-work`)

Baseline: `04be1a1bbbc7a4fd8fe2e4090b4b5bad5dd573e4`

## Verdict

CP-E verdict: NO-GO

Ten corpus inputs have candidate diagnostic changes: seven change
`TYPE_AMBIGUOUS_OPEN_RESULT` to `MOSS_COMPILE_ERROR`; three retain
`MOSS_COMPILE_ERROR` but change its message. These are MIGRATION BUGs against
the baseline diagnostic contract. The source-free standalone recovery
difference is a LEGACY MISPARSE; the independent source-free package oracle
passes in all four compiler/mode combinations.

## Reader inventory

The AST `Stmt` has four source-string fields that can hold expression text:
`a`, `b`, `text`, and `args`. By `Stmt::Kind`, `a` may be an assignment place,
binder, call receiver/name, match/return/reply expression, or message receiver;
`b` may be an expression slot, method/handler name, or `consume` keyword;
`text` may preserve a raw statement or complete call spelling; and `args` are
call/message/echo operands. Expression consumers also use `expression_handles`
(EST handles); `est_raw_form` and `est_pattern` carry parsed statement and
Pattern nodes.

Direct member-reference census across checker/emitter source files found 973
reads: `Stmt.a` 516, `Stmt.b` 295, `Stmt.text` 29, and `Stmt.args` 133. Per-file
counts are in `tests/phase27/adversarial/cp-e-blind-81dc92d7/field-inventory.json`.
The widened lint reports 40 literal NAME comparisons and 9 assignment-binding
comparisons, zero checker/emitter `tree_expression_text` reads, and zero
remaining legacy string-reader debt for either consumer
(`check_phase27_string_debt.py --require-zero checker|emitter`). Remaining
literal comparisons are method/handler/builtin names, effect keywords, `pass`,
and `_`; the nine variable comparisons are assignment/loop binders. Expression
shape decisions use EST queries.

Bounded aliases were followed: FileIO `call_text` enters tree-based lifecycle
queries; assembled emitter calls enter EST-aware lowering; `constrain_call`
and summary instantiation use `tree_parse_simple_call`; the temporary emitter
`method` alias is a method selector. No alias calls a legacy string classifier.

The only exemptions are three type-string `split_top_level` calls, each keyed
by source path, function, and API: `phase20_is_nontransferable_capability`,
`builtin_equality_type`, and `contains_float_type`. The normal lint verifies
each function and exactly one site. In-memory negative checks confirmed
fail-closed behavior for a missing function, stale API key, and duplicate site
(`exemption-fail-closed.json`).

## Corpus and checked-declaration closure

The sweep covers 1,972 `.moss` files under `tests`, `examples`, `benchmarks`,
and `sdk`, excluding `tests/phase27/adversarial/**` to avoid prior evidence.
It records baseline-off, baseline-shadow, candidate-off, and candidate-shadow
verdicts and first diagnostic signatures. Baseline accepted 1,307 files; there
were zero timeouts. Ten inputs have MIGRATION BUG diagnostic differences and
one source-free standalone difference is classified separately. Raw results
are in `corpus-raw.jsonl`, aggregate counts in `corpus-summary.json`, and exact
signatures in `diagnostic-differences.json`.

Accepted declaration tracing covered all 1,307 baseline-accepted sources.
Baseline-off equals candidate-off, and baseline-shadow equals candidate-shadow
exactly for all 1,307: zero paired same-mode closure mismatches and zero
timeouts. In 128 sources, off and shadow method edge spellings differ by
qualification; the same differences occur in both baseline and candidate, and
the selected declaration identities remain equal. Evidence is in
`closure-raw.jsonl` and `closure-summary.json`.

## Exact diagnostic differences

| Source and line | Baseline off/shadow | Candidate off/shadow | Classification |
| --- | --- | --- | --- |
| `examples/swarm/modules/domain_services/repro/test_diag_location/single/tests/bad.moss:2` | `TYPE_AMBIGUOUS_OPEN_RESULT` — `unbound open result type for variable 'm'; heterogeneous or unresolved collection element type` | `MOSS_COMPILE_ERROR` — same message | MIGRATION BUG |
| `examples/swarm/modules/domain_services/repro/test_diag_location/split/tests/bad.moss:6` | `TYPE_AMBIGUOUS_OPEN_RESULT` — `unbound open result type for variable 'm'; heterogeneous or unresolved collection element type` | `MOSS_COMPILE_ERROR` — same message | MIGRATION BUG |
| `tests/negative/unresolved_field_value.moss:5` | `TYPE_AMBIGUOUS_OPEN_RESULT` — `unbound open result type for variable 'value'; heterogeneous or unresolved collection element type` | `MOSS_COMPILE_ERROR` — same message | MIGRATION BUG |
| `tests/phase25/g2/conformance/unpinned_local_without_echo.moss:2` | `TYPE_AMBIGUOUS_OPEN_RESULT` — `unbound open result type for variable 'values'; heterogeneous or unresolved collection element type` | `MOSS_COMPILE_ERROR` — same message | MIGRATION BUG |
| `tests/phase25/integration/fixtures/adversarial/family02_empty_closure/02_unpinned_empty_vector_neg/case.moss:2` | `TYPE_AMBIGUOUS_OPEN_RESULT` — `unbound open result type for variable 'v'; heterogeneous or unresolved collection element type` | `MOSS_COMPILE_ERROR` — same message | MIGRATION BUG |
| `tests/phase25/key_protocol/09b-empty-map-get-never-inserted/case.moss:3` | `TYPE_AMBIGUOUS_OPEN_RESULT` — `unbound open result type for variable 'm'; heterogeneous or unresolved collection element type` | `MOSS_COMPILE_ERROR` — same message | MIGRATION BUG |
| `tests/phase25/key_protocol/09e-empty-map-never-used/case.moss:3` | `TYPE_AMBIGUOUS_OPEN_RESULT` — `unbound open result type for variable 'm'; heterogeneous or unresolved collection element type` | `MOSS_COMPILE_ERROR` — same message | MIGRATION BUG |
| `tests/phase27/est/repro/incomplete_vector.moss:2` | `MOSS_COMPILE_ERROR` — `heterogeneous or unresolved collection element type` | `MOSS_COMPILE_ERROR` — `unbound open result type for variable 'value'; heterogeneous or unresolved collection element type` | MIGRATION BUG |
| `tests/phase27/est/repro/package_open_collection/src/main.moss:2` | `MOSS_COMPILE_ERROR` — `heterogeneous or unresolved collection element type` | `MOSS_COMPILE_ERROR` — `unbound open result type for variable 'value'; heterogeneous or unresolved collection element type` | MIGRATION BUG |
| `tests/phase27/est/repro/unresolved_collection_result.moss:2` | `MOSS_COMPILE_ERROR` — `heterogeneous or unresolved collection element type` | `MOSS_COMPILE_ERROR` — `unbound open result type for variable 'value'; heterogeneous or unresolved collection element type` | MIGRATION BUG |
| `sdk/advanced-data-acceptance/fixtures/source_free_consumer.moss:57` | `RECOVERY_PATTERN_REQUIRES_VARIANT` — `recovery patterns must name a qualified enum variant` | `UNKNOWN_ERROR_TYPE` — `unknown error enum 'ndarray.NdArrayError'` | LEGACY MISPARSE |

The last pair is a direct standalone check without package providers. The
independent source-free ABI-9 release oracle builds and runs the 14-provider
consumer in all four baseline/candidate × off/shadow combinations; all pass.
Thus the standalone diagnostic difference is not a source-free package
failure. Raw results are in `source-free-oracle.json`.

## Package and native checks

The `sdk/tables` native release build passes in all four compiler/mode
combinations, with `native_checked` including `moss.tables` and no skips
(`tables-native-matrix.json`). Its `margo test --json` result is identical in
all four combinations: `TYPE_MISMATCH` at
`sdk/tables/tests/tables_test.moss:151`, `assignment type mismatch for binding
'ignored'` (`tables-test-matrix.json`).

The package-form open-collection probe was copied under each worktree's
disposable `tmp/` and checked with `margo build`. All four modes reject at
`src/main.moss:2` with `MOSS_COMPILE_ERROR`: baseline message
`heterogeneous or unresolved collection element type`, candidate message
`unbound open result type for variable 'value'; heterogeneous or unresolved
collection element type`. Standalone signatures are in
`open-collection-diagnostics.json`; package outcomes are in
`open-collection-package-matrix.json`.

## Reproduction

Both compiler builds used `make --jobs="$(nproc)" moss-opt` in separate
worktrees. Candidate bootstrap returned `language_version: moss-0.1`. Runner
scripts and raw data are under
`tests/phase27/adversarial/cp-e-blind-81dc92d7/`.
