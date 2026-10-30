# CP-E independent raw-shape rerun

CP-E verdict: NO-GO

## Frozen identities

- Baseline: `d4446b7250f651e60e978740661e6fb20ff6ef10` (`phase27-2-m1`).
- Candidate: `6b926fc7dd3ed42dedddb0f2f8f60f931b106a68`.
- Evidence commit: recorded after this report and its probes are committed; it is a separate review branch whose parent is the candidate SHA above.
- Candidate source was not modified. The review and probe files were added only in this separate worktree.
- Bootstrap reported `moss-0.1`. Candidate `make --jobs="$(nproc)" moss-dev` passed.

## Inventory scope and result

`tests/phase27/adversarial/cp-e-rawshape-rerun/inventory.py` scans the checker and emitter consumer files used by the widened lint. Its TSV contains 1,192 line-level occurrences of `.text`, `.a`, `.b`, `.args`, and `.result_expression`, with a first-pass use classification for every hit: 113 EST/expression-reader calls, 96 raw-string-operation review hits, 58 literal-comparison review hits, 484 expression-value uses, and 441 mixed name/expression uses. The lexical inventory intentionally leaves type resolution of each base to the source review; in particular it includes 108 `result_expression` references as related expression-bearing values.

The independent source review found no `tree_expression_text(...)` call in the checker/emitter consumer set. The widened lint passes for both consumers and reports exactly 39 literal NAME comparisons and nine dynamic binding-name comparisons. Its three keyed exemptions are type-argument splitting (`phase20_is_nontransferable_capability`, `builtin_equality_type`, `contains_float_type`); these inputs are canonical type spellings. The assertion-only legacy vector parser is ignored by the lint as a shadow parity oracle, not as a semantic classifier. The remaining NAME comparisons inspected are method/keyword/binding names documented by the exemption inventory.

The lint does catch direct field operations such as `statement.a.find('[')`, `starts_with(trim(statement.b), "[")`, and `statement.text.front()`. The committed `lint_coverage_probe.py` demonstrates that it misses the same operations after a field is copied into a local string. That is the material coverage defect: the linter constrains direct field syntax but does not follow expression text through local aliases or helper parameters.

## Remaining expression-text interpretations

These are checker/emitter paths, not merely stored diagnostic text. They receive expression-bearing statement slots, aliases, or recursively split source operands and then infer shape from punctuation/prefixes. The current lint does not count these raw-shape tests because the parameter/local name is no longer a `Stmt` field.

| Consumer path | Reader and source flow | Remaining interpretation | Classification |
| --- | --- | --- | --- |
| `src/static_bounds.inc` | `static_statement_mutation_roots` selects `statement.a`, `.b`, `.text`; calls `static_mark_possible_mutation` | strips `echo`/`reply`/`return`/`message` prefixes, dispatches on `not ` and `-`, finds member boundary with `rfind('.')`, and detects vector literals with `[`/`]` | Indirect raw syntax reader |
| `src/static_bounds.inc` | `static_bindings_block` passes `statement.b` to bound queries | `static_collection_entries` recognizes a vector by `starts_with(value, "[")` and `ends_with(value, "]")`; `static_payload_byte_bound` recognizes strings by quote characters at `front()`/`back()` | Indirect collection/literal classifier |
| `src/fileio_semantics.inc` | statement walk passes selected `.a`/`.b`/`.text` and call arguments into lifecycle expression analysis | `fileio_lifecycle_expression` recurses into list contents using `[`/`]` prefix/suffix checks | Indirect collection-shape reader |
| `src/fileio_semantics.inc` | selected expression fields and nested values reach `fileio_blocking_expression` | scans `message ` and wrapper prefixes, then recognizes vector contents by `[`/`]` | Indirect prefix and collection reader |
| `src/executor_invoke_lowering.inc` | `phase21_validate_root_ingress_totality` iterates `statement_expressions(statement)` | scans every character for the `message` keyword, checks token boundaries, and finds the following call parenthesis with `find('(')` | Manual expression parser |
| `src/semantic_checker.cpp` | expression walkers receive statement fields and recursive operands | checks `not ` and unary `~`/`+`/`-` using prefixes/front characters in effect capture, local-call collection, callable-call validation, ownership, and expression checking | Indirect unary-shape readers |
| `src/semantic_checker.cpp` | `legacy_spawn_expression` is applied to expression text | detects retired `spawn ` by prefix | Legacy syntax classifier |
| `src/semantic_checker.cpp` | `tree_split_binary` shadow assertion receives arbitrary expression text | uses `if ` plus colon/`else:` substrings for one rejected-statement diagnostic exception | Shadow-oracle exception; not a checker semantic decision |
| `src/native_generator.cpp` | expression type helpers receive statement expressions and call arguments | generated type inference dispatches on `not `, leading signs, and `~`; `is_pure_state_read_expression` tests `message ` | Indirect emitter expression classifier |
| `src/native_generator.cpp` | `write_call_place` receives assignment targets and write-effect arguments, including statement fields | after EST index parsing fails, splits a member place at raw `rfind('.')` and checks the suffix as an identifier | Indirect member-place shape reader |

The type-string bracket handling in `fileio_contains_scoped_type` and `fileio_forbidden_collection_type` was inspected separately: those inputs are canonical type strings, so it is not an expression-reader finding. The direct `Stmt` name comparisons and type-split exemptions above are not part of the defect.

The regex coverage probe proves the linter's exact boundary: direct field search/prefix/front operations are reported, while the equivalent `auto value = statement.b; starts_with(value, "[")` form is not. Thus the claim that widened lint rejects all raw expression-shape interpretations is not supported by its implementation.

## Differential evidence

Full tracked-source verdict comparison:

- Baseline `phase27-2-m1`: 2,118 `.moss` sources, 1,394 accepted.
- Candidate in EST off: 2,124 sources, 1,398 accepted.
- The candidate archive adds six probe files. Across all 2,118 common source paths, three verdict differences remain:
  - `benchmarks/agent/phase22_3_ab/tasks/P223017_simple_arithmetic/starter/main.moss` and `tests/phase27/est/repro/incomplete_expression.moss` contain identical `fn main(): echo 2 +` text. Baseline off rejects both with `MOSS_COMPILE_ERROR:2`; candidate off accepts both; candidate shadow aborts on both (exit `-6`, no JSON). The committed mini-package reproducer confirms the package-mode verdict changed too: baseline `margo build --check` rejects with `MOSS_COMPILE_ERROR:2`, candidate accepts. **Classification: SEMANTIC acceptance change**, with an additional shadow-mode abort. The minimal paired source and package are under `tests/phase27/adversarial/cp-e-rawshape-rerun/`.
  - `sdk/advanced-data-acceptance/fixtures/source_free_consumer.moss`: baseline off reports `RECOVERY_PATTERN_REQUIRES_VARIANT:57`; candidate off and shadow report `UNKNOWN_ERROR_TYPE:57`. Both standalone checks reject because the fixture's imported package interfaces are absent in this invocation. **Classification: MIGRATION diagnostic change in the isolated-source corpus; package-mode result is UNVERIFIED.** The coordinator-directed stop came before running the provider-interface package harness, so no package-mode acceptance claim is made for this fixture.
- The candidate archive has six added `.moss` sources; the complete path list and paired statuses are in [the TSV differential](cp_e_rawshape_6b926fc7_verdict_diff.tsv) and [paired verdict table](cp_e_rawshape_6b926fc7_paired_verdicts.tsv). Their statuses are listed in `tmp/cp-e-rawshape-candidate-off.tsv` generated by the exact command in the evidence log; they are not baseline source changes.

Three unchanged shape probes committed with this review (`probe_vector_index.moss`, `probe_constructor_member.moss`, and `probe_punctuation_calls.moss`) each returned `(ok=true, no diagnostic)` under baseline off, candidate off, and candidate shadow. The paired malformed-expression probe separately demonstrates the acceptance and shadow-abort differences. The shape probes do not clear the helper-alias lint gap.

The full **verdict** corpus differential completed in EST off: baseline 2,118 sources / 1,394 accepted; candidate 2,124 sources / 1,398 accepted; 2,118 common paths, with three common-path verdict changes and six candidate-only sources. The full **checked-declaration closure** differential did not complete. `tests/tooling/check_phase27_reach_differential.py` was run on the 2,118-source baseline archive with baseline off and candidate shadow. The first attempt used 90s per source and timed out on `benchmarks/compiler_torture/fixtures/specialization/w2_spec_distinct_n032.moss`. A retry at 300s per source was stopped at the coordinator's direction after about three minutes. This harness only writes its report after all sources complete, so there is no exact completed-entry count or closure-difference total; no full closure pass is claimed.

## Required disposition

The frozen candidate is NO-GO: the raw-shape lint has an alias/helper blind spot, several in-scope expression shape readers remain, and the full verdict comparison found a SEMANTIC acceptance change with a shadow abort. Package-mode behavior changed for the malformed-expression reproduction. The source-free fixture has a diagnostic-only standalone difference; its package-mode result remains unverified.
