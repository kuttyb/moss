# CP-E full blind review — Agent 2

**Verdict: NO-GO**

The full corpus and package matrices preserve acceptance verdicts across the
baseline and frozen candidate. The expression-reader acceptance goal is still
not met: three checker decisions compare expression-bearing source strings
directly to other strings. Each is a **MIGRATION BUG**. The two standalone
line-57 diagnostic changes are BENIGN after the required Margo package-context
oracle: both source-free consumers pass in all four baseline/candidate and
off/shadow configurations.

## Freeze and scope

- Candidate source freeze: `75ff852287f9f845af89175607b512af38f8fdc0`.
- Baseline: `04be1a1bbbc7a4fd8fe2e4090b4b5bad5dd573e4`.
- Review branch: `review/cp-e-full-75ff8522`.
- Startup build completed with `make --jobs="$(nproc)"`; bootstrap reported
  `language_version: moss-0.1`.
- No files under `src/` were modified.
- Four scrubbed tracked `.moss` inputs and four scrubbed tracked `Moss.toml`
  inputs were restored from the candidate freeze without opening their text.
  Prior reports, scripts, and raw findings were not opened.

## Findings

### M1 — Three raw expression-to-string comparisons remain

These reads are outside the existing lint's semantic classification. The
checker should project identifier names or occurrences from EST nodes and use
statement kind/binding identity where the value is a binding slot.

1. [`semantic_checker.cpp:9905`](../../../../src/semantic_checker.cpp:9905),
   `check_closed_body_types`: `stmt.a == name` chooses an error line for an
   unresolved local type. The loop has no statement-kind guard, so `Stmt.a`
   can be an expression spelling (for example on `Raise`, `Return`, or `If`),
   not only a declaration name. A coincidental spelling match can affect the
   diagnostic location. The lint counted this among its nine dynamic
   `Stmt.a`/binding comparisons without classifying each statement kind.

2. [`semantic_checker.cpp:19851`](../../../../src/semantic_checker.cpp:19851),
   `phase21_raise_set` → `phase21_raise_set_block`: `Raise.a ==
   selected_binding` chooses whether a raised outcome reuses the selected
   recovery `RaiseSet`. `Raise.a` is an expression. This is a semantic
   raise-set decision based on exact source spelling.

3. [`semantic_checker.cpp:20056`](../../../../src/semantic_checker.cpp:20056),
   `phase21_failure_capture_reads`: `tree_plain_identifier(expression)` is
   followed by `expression == statement.a` to filter self-assignment from
   failure captures. The EST kind check does not replace the final raw text
   equality; a grouped identifier spelling is treated differently.

All three are **MIGRATION BUGS**. No source fix is included in this review.
Minimal paired regressions should compare bare and redundantly grouped
identifiers, plus a distinct same-typed identifier: (a) `raise err` versus
`raise (err)` in a bound recovery arm, checking outer recovery reachability;
(b) `x = x` versus `x = (x)` in the failure-capture path, checking the capture
set; and (c) an unresolved local whose `Stmt.a` expression happens to spell an
existing binding, checking the diagnostic source line. Run each against both
compiler revisions and both EST modes.

The widened linter's `RAW_FIELD_VARIABLE_COMPARE` only matches a field on the
left side. It therefore misses item 3, where the expression string is the left
operand. Items 1 and 2 are counted by its dynamic comparison pattern but are
misclassified as binding-name reads because it does not inspect statement
kind. The complete direct-field comparison listing and the three reviewed
sites are in [`reader-audit.json`](../../../../tests/phase27/adversarial/cp-e-full-75ff8522/reader-audit.json).

### M2 — Standalone line-57 diagnostics are benign in package context

The standalone full-corpus matrix has four diagnostic-only differences (two
source files × off/shadow), all rejected in both revisions:

- `sdk/advanced-data-acceptance/fixtures/source_free_consumer.moss:57`
- `tests/phase27/adversarial/cp-e-phase27-3-22a9709a/source_free_consumer.moss:57`

Baseline reports `RECOVERY_PATTERN_REQUIRES_VARIANT`; candidate reports
`UNKNOWN_ERROR_TYPE` for `ndarray.NdArrayError`. Following the package-context
adjudication rule, these rows are BENIGN: Margo package consumers for both
inputs pass in all eight runs (two sources × baseline/candidate × off/shadow)
against their matching `.mossi` dependency closures. The standalone error
delta does not survive package context. See
[`source-free-adjudication.json`](../../../../tests/phase27/adversarial/cp-e-full-75ff8522/source-free-adjudication.json)
and [`source-free-margo-oracle.json`](../../../../tests/phase27/adversarial/cp-e-full-75ff8522/source-free-margo-oracle.json).

## Expression-reader inventory and taint audit

The permitted Phase 27.3 pattern inventory records 578 pre-migration call
sites: 454 checker and 124 emitter. The full pattern breakdown is in
[`27_3_PATTERN_INVENTORY.md`](../../27_3_PATTERN_INVENTORY.md).

| Historical reader pattern | Checker | Emitter | Total |
| --- | ---: | ---: | ---: |
| Pipeline normalization/detection | 12 | 2 | 14 |
| Redundant grouping | 20 | 9 | 29 |
| Bare-name recognition | 77 | 10 | 87 |
| Identifier occurrence scans | 0 | 7 | 7 |
| Trimmed source equality | 75 | 23 | 98 |
| Direct call shape | 54 | 17 | 71 |
| Member call shape | 40 | 8 | 48 |
| Bound method callable | 8 | 2 | 10 |
| Named argument shape | 36 | 6 | 42 |
| Binary operator/operand extraction | 56 | 15 | 71 |
| Index extraction | 25 | 10 | 35 |
| Vector literal extraction | 11 | 2 | 13 |
| Delimited child-list splitting | 23 | 9 | 32 |
| Enum case extraction | 7 | 2 | 9 |
| FileIO call/chunk source extraction | 5 | 2 | 7 |
| Executor-chain extraction | 2 | 0 | 2 |
| Assignment/colon scanning | 3 | 0 | 3 |
| **Total** | **454** | **124** | **578** |

Current-source audit covered 10 checker/emitter translation units and included
files. It found zero calls to legacy expression-reader APIs in either consumer,
zero `tree_expression_text(...)` reads, and zero raw punctuation, prefix, or
first-character expression-shape tests on `Stmt.a`, `.b`, or `.text`. It found
42 raw literal comparisons, all audited NAME/keyword/operation slots matching
the exact counts in [`27_3_STRING_READER_EXEMPTIONS.md`](../../27_3_STRING_READER_EXEMPTIONS.md),
and nine field-left dynamic comparisons. Of the latter, seven are binding or
callee-name slots and the two at lines 9905 and 19851 above inspect
expression-bearing slots. The reverse-oriented expression comparison at line
20056 is the third raw expression comparison. Seventy-two `.empty()` checks
are recorded individually as optional-field/presence checks; they do not
inspect expression punctuation or token shape.

All three function/API exemptions passed fail-closed mutation probes for a
removed reader site, a duplicate reader site, and a removed function (9/9).
Evidence: [`exemption-fail-closed.json`](../../../../tests/phase27/adversarial/cp-e-full-75ff8522/exemption-fail-closed.json).

The normal `check_phase27_string_debt.py --require-zero checker` and
`check_phase27_string_debt.py --require-zero emitter` invocations could not reach their checks because the scrubbed checkout lacks
`docs/phase27/STRING_DEBT_BASELINE.md`. That failed invocation is preserved in
[`string-debt-cli-attempts.json`](../../../../tests/phase27/adversarial/cp-e-full-75ff8522/string-debt-cli-attempts.json).
The module's `sites()` scanner was run directly without loading that baseline
document; the raw-field counts, tree-text scan, and mutation checks were also
run independently. No baseline report was read to work around the scrub.

## Full tracked `.moss` corpus

The candidate freeze contains 2,218 tracked `.moss` files (the baseline's own
tracked count is 2,118). Per instruction, baseline and candidate compilers both
checked the same 2,218 candidate source inputs. Each input was checked with
`MOSS_EST=off` and `MOSS_EST=shadow` by both compilers: 8,872 checks total.

| Compiler/mode cell | Accepted | Rejected |
| --- | ---: | ---: |
| Baseline / off | 1,419 | 799 |
| Baseline / shadow | 1,419 | 799 |
| Candidate / off | 1,419 | 799 |
| Candidate / shadow | 1,419 | 799 |

There are zero accept/reject verdict differences and zero JSON parse failures.
The four standalone diagnostic changes above are recorded in
[`corpus-diffs.json`](../../../../tests/phase27/adversarial/cp-e-full-75ff8522/corpus-diffs.json);
their package-context disposition is M2. Exact commands are reproducible from
[`corpus.py`](../../../../tests/phase27/adversarial/cp-e-full-75ff8522/corpus.py).
The 2,218 input paths and content hashes are in
[`corpus-input-manifest.jsonl`](../../../../tests/phase27/adversarial/cp-e-full-75ff8522/corpus-input-manifest.jsonl).

## Isolated package and declaration closure

The authoritative package run used one shared 2,645-file tracked input manifest
and four fresh source stages, one per baseline/candidate × off/shadow cell.
The manifest contains all 2,218 `.moss` inputs, the tracked `.mossi` fixture,
and all 426 tracked `Moss.toml` files. This isolates generated interfaces by
compiler and EST mode; the earlier per-root-clean matrix is retained only as
`package-matrix-preliminary-superseded.jsonl` and is not authoritative.

[`package-matrix-isolated.jsonl`](../../../../tests/phase27/adversarial/cp-e-full-75ff8522/package-matrix-isolated.jsonl)
records 1,704 serial Margo checks (426 roots per cell). Each cell has 350
accepted and 76 rejected package roots. Across every accepted root, the closure graphs represent 267 distinct
package-name nodes; Margo checked all declarations reachable from each accepted root.
Accepted closure graphs, root verdicts, and normalized rejected-source
diagnostics match across baseline/candidate and off/shadow (zero differences).
The per-root accepted closures and rejected diagnostics are summarized in
[`package-closure-summary.json`](../../../../tests/phase27/adversarial/cp-e-full-75ff8522/package-closure-summary.json).

## Package-context `.mossi` checks and remaining harness gates

Both source-free consumers were copied into isolated temporary Margo consumer
packages with the matching `moss.ndarray`, `moss.binary`, and related path
dependencies. Each consumer was checked against its own stage's generated
interfaces. All eight Margo checks passed, with no diagnostics. The invocation
and per-cell results are in
[`source_free_margo_oracle.py`](../../../../tests/phase27/adversarial/cp-e-full-75ff8522/source_free_margo_oracle.py)
and [`source-free-margo-oracle.json`](../../../../tests/phase27/adversarial/cp-e-full-75ff8522/source-free-margo-oracle.json).

The additional Phase 25 cross-crate ABI harness controls did not pass: scenario
8 failed in all four compiler/mode cells because its provider did not emit
ABI-v10 records; scenario 9 was BLOCKED in all four cells because ABI-v9
source-free rebuild enforcement is not implemented. These are accurately
recorded in [`mossi-controls-summary.json`](../../../../tests/phase27/adversarial/cp-e-full-75ff8522/mossi-controls-summary.json)
and the eight per-run files. Both outcomes are identical for baseline and
candidate and do not account for the CP-E NO-GO.

## Reproduction and checksums

The compiler cells are identified by source commit and binary hash:

| Cell | Source commit | `moss-dev` SHA-256 |
| --- | --- | --- |
| Baseline | `04be1a1bbbc7a4fd8fe2e4090b4b5bad5dd573e4` | `4a334c71d9b5ea5b2925c311f7a462abd046e479fc4b52df4c0453e612ceb286` |
| Candidate | `75ff852287f9f845af89175607b512af38f8fdc0` | `0c470dda783260986128da0cba6a8dfd68cfcfaa5be902ac872e21fbe3df9b05` |

Commands and JSON evidence are in the `tests/phase27/adversarial/cp-e-full-75ff8522/`
directory. The final evidence checksums are recorded in
[`CHECKSUMS.sha256`](CHECKSUMS.sha256).
