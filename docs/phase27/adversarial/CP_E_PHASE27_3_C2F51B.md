# CP-E independent review — Phase 27.3 FileIO-open freeze

CP-E verdict: GO-with-defects

Candidate SHA: `c2f51b291c72125dcb0716cf30d22d9a4bf74f7b` (`phase27-3-work`)
Baseline SHA: `04be1a1bbbc7a4fd8fe2e4090b4b5bad5dd573e4`

The candidate was reviewed in a detached, separate worktree. Bootstrap reports
`language_version: moss-0.1`. The freshly compiled candidate compiler SHA-256 is
`759e978b3a78d47d61ed15f5c26cdd615df16422939227ca47a2cc375fd7a509`; the freshly
compiled baseline compiler SHA-256 is
`4a334c71d9b5ea5b2925c311f7a462abd046e479fc4b52df4c0453e612ceb286`. No `src/`
file was edited and no performance work was run.

## Defect: source-free numeric helpers are omitted from generated Rust

A clean, isolated `sdk/tables` package build fails with the candidate and passes
with the baseline. The reproduction script and complete Margo JSON are in
[`tests/phase27/adversarial/cp-e-c2f51b/`](../../../tests/phase27/adversarial/cp-e-c2f51b/).

From the candidate checkout, the command is:

```sh
MOSS="$PWD/build/compiler/moss-dev" \
MARGO_HOME="$PWD/tmp/cp-e-c2f51b/margo-tables-candidate" \
  ../../margo build --json
```

Run from the frozen candidate worktree’s `sdk/tables` directory. It exits 1
with Rust `E0425`: generated `tables.rs` calls `__moss_parse_int` at line 4891
and `__moss_parse_float` at line 4939, but the candidate output defines neither
helper. The baseline compiler, run by the same candidate `margo` driver against
the exact same frozen source tree and a separate `MARGO_HOME`, exits 0. The paired
complete logs are in `native-build-reproduction.json`;
`repro_native_sourcefree.py` reproduces both.

The definitions are present in `src/numeric_runtime.cpp` at lines 176 and 181.
`src/native_generator.cpp` emits that runtime only when
`program_uses_numeric_builtins()` returns true (line 226). The candidate's
source-free generated Rust has two helper references and zero definitions;
the baseline has the same two references and both definitions. The function's
candidate check only scans expression arenas (`native_generator.cpp:318`), so
numeric calls in the package's imported/source-free function closure are missed.
Evidence is in `native-symbols.json`. Treat this as a confirmed candidate
migration bug requiring a fix before the native source-free gate is green.

## Checker/emitter reader inventory

`stmt-slot-inventory.tsv` enumerates 996 checker/emitter accesses to `.a`, `.b`,
`.text`, and `.args` across the checker and emitter files used by the widened
reader lint. It includes source line and a lexical consumer classification;
`slot-inventory-summary.json` records counts. Whole-`src` search found zero
`tree_expression_text(...)` calls.

The widened string-debt lint passed in normal mode and with `--require-zero` for
`checker`, `emitter`, `reach_walk`, and `fast_debug`. It reports and enforces 40
literal NAME/keyword comparisons and 9 binding-name comparisons. Full sites are
in `name-comparisons.json` and `binding-comparisons.json`; exact per-name counts
and rationale are in `docs/phase27/27_3_STRING_READER_EXEMPTIONS.md`.

The only three function/API-keyed reader exemptions are
`phase20_is_nontransferable_capability / split_top_level`,
`builtin_equality_type / split_top_level`, and
`contains_float_type / split_top_level`. They split canonical type spellings,
not source expressions. The exemption list keys source, function, and API and
fails closed on missing/stale/duplicate sites; the active lint passes.

The bounded direct-string taint review follows six trimmed Stmt-field aliases
into their same-function uses (`bounded-taint-direct.json`). These aliases feed
EST/tree queries, checked semantic queries, or emitter expression lowering;
none feeds a legacy text parser. This complements the direct API lint and is a
bounded source review, not whole-program C++ dataflow proof.

## Corpus and checked-declaration closure

Fresh diagnostics on all tracked `.moss` inputs: 2,148 files, 1,409 accepted by
baseline-off, candidate-off, and candidate-shadow; zero timeouts. There were no
acceptance differences and one diagnostic-code difference, preserved in
`corpus.jsonl` and summarized in `corpus-summary.json`.

The sole difference is `sdk/advanced-data-acceptance/fixtures/source_free_consumer.moss:57`:
without package provider context, baseline returns
`RECOVERY_PATTERN_REQUIRES_VARIANT`, while candidate off/shadow return
`UNKNOWN_ERROR_TYPE` for `ndarray.NdArrayError`. All three reject the standalone
input. With provider interfaces and a package manifest, all three accept the
same source-free consumer. An invalid imported-function probe returns the same
`UNKNOWN_SYMBOL_OR_TYPE` in package mode for all three. The package/standalone
oracle, including raw diagnostics, is `source-free-oracle.json`. Classify the
standalone diagnostic change as benign project-context diagnostic drift.

On the 1,409 sources accepted by all three compiler/mode pairs, `moss symbols`
returned identical declaration sets for 1,398 sources: 12,250 declarations per
pair, with zero set differences. Eleven sources produced the same query errors
in all three runs (six `TEST_ASSERTION_CONFIGURATION_ERROR`, five
`UNSUPPORTED_STRING_ORDERING`) despite per-file `check` acceptance; their 13
source-level declaration fallbacks are enumerated in
`closure-query-fallback.json`. Raw query results and summary are
`closure.jsonl` and `closure-summary.json`. The query limitation is explicit;
no missing source is counted as a declaration-set pass.

## FileIO.open fix

The frozen change recognizes `FileIO.open` as a static namespace member through
`tree_root_member_access_named(source, "FileIO", "open")`, replacing the raw
source-string comparison in the pinned-ownership path. The existing
`tests/phase20_fileio_helper_parameter.moss` check accepts with baseline-off,
candidate-off, and candidate-shadow. The full
`check_phase20_fileio_semantics.py` positive, negative, effect, and warning
suite passes on the candidate. This verifies the intended EST-based
classification and FileIO behavior.

## Validation record

- `./moss agent bootstrap --json`: pass; `moss-0.1`.
- `make --jobs="$(nproc)" moss-dev` on candidate and baseline: pass.
- `check_phase27_string_debt.py` normal and `--require-zero` for checker,
  emitter, reach walk, and Fast Debug: pass.
- `check_phase20_fileio_semantics.py`: pass.
- 2,148-file baseline/off/candidate-off/candidate-shadow corpus: zero acceptance
  deltas, one standalone project-context diagnostic difference, zero timeouts.
- Checked-declaration symbol census: zero declaration-set deltas on 1,398 query
  successes; 11 query-failure sources explicitly preserved with fallbacks.
- `sdk/advanced-data-acceptance/check_source_free.py --abi v9 --profile debug`:
  baseline pass; candidate fails in `sdk/tables` Rust compilation for the
  missing numeric runtime helpers above.
- No performance run during this review.
