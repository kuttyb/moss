# CP-E independent Agent 2 review — Phase 27.3

CP-E verdict: GO-with-defects

## Frozen inputs and isolation

- Candidate: `d28c63a845d8da1fba5cb1d9ef0164cb4d60cbae` (`origin/phase27-3-work`).
- Baseline: `04be1a1bbbc7a4fd8fe2e4090b4b5bad5dd573e4`.
- Review ran in a dedicated candidate worktree and a separate baseline worktree. No file under `src/` was modified.
- Candidate bootstrap reports `language_version: moss-0.1`. Both compilers built with `make --jobs="$(nproc)"`.

## Checker/emitter reader audit

The independent source inventory records 1,006 direct reads of legacy
expression-bearing `Stmt` slots across the checker/emitter consumers: `a` 534,
`b` 301, `args` 139, and `text` 32. Every read is listed with its file, line,
nearest function signature, and source line in
`tests/phase27/adversarial/cp-e-independent-d28/reader_inventory.txt`.

There are zero `tree_expression_text(...)` calls anywhere under `src/`. The
widened string-debt lint passes `--require-zero checker` and
`--require-zero emitter`. Its remaining 40 literal comparisons are Stmt NAME
slots (method, builtin, effect, or keyword names); its nine dynamic comparisons
are binding-name reads. The three reader exemptions are each keyed by file,
function, and API, and each covers one canonical type-spelling
`split_top_level` call:

- `phase20_is_nontransferable_capability`
- `builtin_equality_type`
- `contains_float_type`

The in-memory fail-closed probes confirm the lint fails when an exempted
function disappears and when the function gains a second `split_top_level`
site. Fourteen simple local aliases initialized from Stmt slots were found;
the bounded local-use scan found zero raw string-shape operations on those
aliases. Expression recognition at audited callers goes through EST query
helpers. Raw inventory, lint outputs, and mutation probes are committed under
the test evidence directory.

## Corpus and checked-declaration comparisons

The common tracked corpus has 2,118 `.moss` roots; the candidate has 30
candidate-only probe files and the baseline has no baseline-only files. Across
baseline off, baseline shadow, candidate off, and candidate shadow, all four
modes accept 1,438 roots and reject 680. Off/shadow verdicts and diagnostics
match within each checkout on all 2,118 roots.

The baseline/candidate corpus comparison has one diagnostic-only difference
when `sdk/advanced-data-acceptance/fixtures/source_free_consumer.moss` is run as
a standalone file without its provider `.mossi` context. Baseline reports
`RECOVERY_PATTERN_REQUIRES_VARIANT`; candidate reports `UNKNOWN_ERROR_TYPE`.
This direct-file invocation lacks the provider module context required for
that fixture, so it is not treated as a package result. The actual compiled-only
source-free consumer gate, with provider interfaces and no provider source
roots, passes in all four baseline/candidate off/shadow runs (ABI-9 debug, 14
provider interfaces).

The semantic symbols census covers all 2,118 common roots in all four modes:
all declaration IDs, kinds, names, source locations, and enclosing identities
match exactly (zero missing or added declarations). The checked-call closure
trace completed in all four modes for 705 roots. Of those, 639 are accepted in
all modes; candidate off and shadow closures match for all 639, with zero
baseline checked identities missing from candidate shadow. The other 66
trace-complete roots are rejected in all modes. Four rejected roots show a
single legacy-only `heap__Entry.key_of` checked identity; baseline off/shadow
already differs identically there, and each root fails with
`INVALID_BARE_IDENTIFIER_STATEMENT` before accepted-program closure applies.
The four roots are `examples/swarm/modules/algo_chain/heapkit/src/order.moss`,
`examples/swarm/modules/algo_chain/heapkit/tests/order_test.moss`,
`examples/swarm/modules/algo_chain/repro/natural_chain/heapkit/src/order.moss`,
and `examples/swarm/modules/algo_chain/repro/natural_chain/heapkit/tests/order_test.moss`.
The other 1,413 roots have no complete C1 trace (799 accepted roots: 754
standalone and 45 project; 614 rejected roots: 433 standalone and 181
project). These roots remain outside the instrumented closure comparison;
their full symbol declaration census is included. Raw per-root outputs are in
`closure-diff.jsonl` and `symbol-closure.jsonl`.

## Package builds and malformed-expression probes

`moss.tables` builds natively in baseline off/shadow and candidate off/shadow.
The advanced-data compiled-only source-free consumer also passes in all four
modes. Machine outputs are in the evidence `packages/` directory.

Seven fresh probes cover a valid grouped/indexed expression, incomplete binary,
call, member, and vector expressions, an unsupported operator, and an invalid
bare statement expression. Candidate off/shadow return identical verdicts and
diagnostics for all seven. Baseline and candidate also match exactly on five.
For the two incomplete forms below, both compilers reject the input, but the
diagnostic differs:

| Probe | Baseline off/shadow | Candidate off/shadow |
| --- | --- | --- |
| `incomplete_member.moss` | `MOSS_COMPILE_ERROR` | `TYPE_AMBIGUOUS_OPEN_RESULT` |
| `incomplete_vector.moss` | `MOSS_COMPILE_ERROR` | `TYPE_AMBIGUOUS_OPEN_RESULT` |

This is a **MIGRATION BUG (diagnostic-only)**, not a semantic acceptance
difference: both inputs are incomplete and rejected. The syntax oracle is
`docs/LANGUAGE_SYNTAX.md`, §“Functions and expressions” and §“Local bindings”:
expression operands must form a complete expression, and incomplete
collection/member expressions are not legal v0.1 syntax. No source-free or
corpus accepted-program diagnostic delta remains after providing the required
module interfaces.

The exact probe sources and four-mode results are under
`tests/phase27/adversarial/cp-e-independent-d28/probes/` and
`probe-matrix.jsonl`. Corpus verdict/diagnostic results and the full reader
inventory are preserved alongside them.

## Required verdict

CP-E verdict: GO-with-defects

GO for the audited checker/emitter expression-reader migration and accepted
program declaration closure. The two incomplete-expression diagnostic changes
are the remaining migration defect. This review does not approve unrelated
Phase 27 gates.
