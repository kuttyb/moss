# Phase 27 EST shadow divergences

The zero-count table below is the pre-27.1a placeholder. The Phase 27.1a triage,
bank rulings, and final construction result supersede it; the current bank status
is recorded after those rulings.

The shadow corpus gate classifies each structural disagreement as an EST bug,
an existing-compiler inconsistency needing Bank adjudication, or Unexplained.
Each entry must include a minimal reproducer and the checker/string-path result.

| Label | Count | Reproducers |
| --- | ---: | --- |
| EST bug | 0 | None recorded yet |
| Existing-compiler inconsistency | 0 | None recorded yet |
| Unexplained | 0 | None recorded yet |

This table is provisional until the complete shadow differential runs. Zero
entries here do not constitute a parity result.


## Phase 27.1a triage — shadow corpus at 933ddd49 (2026-10-28)

Shadow corpus: 2,061 tracked `.moss` sources, 1,341 accepted. The EST trace records 403,436 slot attempts, 361,260 successful builds, and 42,176 failed builds. These are construction attempts across all parser invocations in the corpus pass; repeated compilation can count the same source occurrence more than once. Per-slot raw counts and the grouped classifications below sum exactly.

Every listed failure group has one label. DEFERRED groups count as deferred, never as built. `NEEDS BANK` is a routing label for unresolved scope/splitter questions; the forms remain unmodified.

| Form | Slot counts | Count | Label | Reproducer |
|---|---|---:|---|---|
| Pipelines | Condition 683; Initializer 832; ReturnValue 2,722; Iterable 10; Argument 1,053; RhsValue 4,599; RaisedValue 18; Expression 2 | 9,919 | DEFERRED-27.1b | `tests/phase27/est/repro/pipeline.moss` |
| Collection literals | Initializer 696; ReturnValue 238; Iterable 40; Argument 2,905; RhsValue 3,568 | 7,447 | DEFERRED-27.1b | `tests/phase27/est/repro/collection_literal.moss` |
| Collection constructors | Initializer 28; RhsValue 2 | 30 | DEFERRED-27.1b | `tests/phase27/est/repro/collection_constructor.moss` |
| Record/enum constructors and constructor argument fragments | Condition 2; Initializer 1,384; ReturnValue 4,889; Receiver 1; Argument 853; LhsPlace 34; RhsValue 13,771; Scrutinee 4; RaisedValue 1,757; Expression 17 | 22,712 | DEFERRED-27.1b | `tests/phase27/est/repro/constructor.moss` |
| Message expressions nested or standalone | Condition 4; Argument 1,437; ReturnValue 150; RhsValue 54 | 1,645 | NEEDS BANK | `tests/phase27/est/repro/message_expression.moss` |
| Bitwise and shift operators | Condition 15; Initializer 80; ReturnValue 50; Argument 142; RhsValue 115 | 402 | NEEDS BANK | `tests/phase27/est/repro/bitwise_shift.moss` |
| Unsupported `**` and `in` spellings | Condition 2; ReturnValue 2 | 4 | NEEDS BANK | `tests/phase27/est/repro/unsupported_exponent.moss`; `tests/phase27/est/repro/membership_operator.moss` |
| Symbolic Boolean `&&` and `||` | Condition 4 | 4 | NEEDS BANK | `tests/phase27/est/repro/symbolic_boolean.moss` |
| Incomplete `2 +` expression | Argument 2 | 2 | NEEDS BANK | `tests/phase27/est/repro/incomplete_expression.moss` |
| Standalone multiline fragments (`]`, `)`) | ReturnValue 2; Expression 5 | 7 | NEEDS BANK | `tests/phase27/est/repro/split_fragment.moss` |
| Numeric literal followed by member access | Argument 4 | 4 | EST BUG | `tests/phase27/est/repro/numeric_literal_member.moss` |

No `.mossi`-loaded expression failures occurred in this source corpus; the corpus runner enumerates tracked `.moss` files, not `.mossi` artifacts. No Pattern-slot construction failures occurred.

### NEEDS BANK evidence

- **Message expressions (1,645):** The string path accepts and checks the reproducer with no diagnostics. Shadow EST reports trailing expression tokens because its grammar has no `message` primary. `docs/LANGUAGE_SYNTAX.md` §Domain communication describes `message` as a synchronous blocking handler invocation and shows it used as a value. Keep the form unresolved for the bank.
- **Bitwise and shifts (402):** The string path accepts and checks the reproducer. EST reports trailing tokens or an expected parenthesis. The form lies outside this task's explicit in-scope list, while §Bitwise and shift operators documents `&`, `|`, `^`, `~`, `<<`, and `>>` as supported Int operators. Keep the scope decision for the bank.
- **Unsupported operators (4) and symbolic Boolean operators (4):** The string path reports `MOSS_COMPILE_ERROR` for `**` and `UNSUPPORTED_EXPRESSION_OPERATOR` for `in` and `&&`; `docs/LANGUAGE_SYNTAX.md` §Operators are closed, not overloadable excludes operators outside the closed set, and §Boolean operators specifies word operators. The shadow failures do not change those diagnostics. Keep the parser-error versus EST-failure policy for the bank.
- **Incomplete expression (2):** The string path reports `MOSS_COMPILE_ERROR: invalid arithmetic expression`; the EST logger records a missing operand. §Functions and expressions describes complete value expressions. Keep whether invalid source contributes a construction failure for the bank.
- **Standalone fragments (7):** The reduced multiline-list reproducer returns `MOSS_COMPILE_ERROR: indentation jumps more than one block level` from the string path; the shadow trace separately records `[`/`]` fragments in expression slots. §Functions and expressions describes a block's final expression, while §Built-in collections documents collection operations without defining independent closer fragments. Keep this splitter/tree boundary for the bank.

### In-scope EST bug

`(796935229243298.2).to_string()` is accepted by the string path with no diagnostics but fails EST parsing with trailing expression tokens. The literal/member/parentheses combination is in scope. The minimal reproducer is `tests/phase27/est/repro/numeric_literal_member.moss`. No splitter or tree behavior was changed during triage.

### Classified per-slot view

The columns below are successful builds / deferred forms / remaining failed forms, computed from the raw pass for triage. They are not the requested C counter implementation; C was not started.

| Slot | Built | Deferred 27.1b | Remaining failures |
|---|---:|---:|---:|
| Condition | 32,664 | 685 | 25 |
| Initializer | 20,370 | 2,940 | 80 |
| ReturnValue | 47,840 | 7,849 | 204 |
| Iterable | 1,190 | 50 | 0 |
| Receiver | 48,494 | 1 | 0 |
| Argument | 82,077 | 4,811 | 1,585 |
| LhsPlace | 74,281 | 34 | 0 |
| RhsValue | 52,206 | 21,940 | 169 |
| Scrutinee | 284 | 4 | 0 |
| Pattern | 0 | 0 | 0 |
| RaisedValue | 1,808 | 1,775 | 0 |
| Expression | 46 | 19 | 5 |
| **Total** | **361,260** | **40,108** | **2,068** |

**Stop rule applied:** item B produced multiple NEEDS BANK groups. Item B is recorded; work stops before item C pending bank classification. No language semantics, `.mossi` format, or ABI decision was made.

## Bank rulings on item B (2026-10-28)

The NEEDS BANK labels in the initial triage table are historical and are
superseded by the ruling results below. No NEEDS BANK group remains.

1. **Message expressions (1,645): EST BUG, in scope.** Parse `message target.Handler(args...)` as a primary expression. Its target, handler name, and arguments are explicit; every argument is a distinct occurrence. Message expressions are valid wherever a value is valid.
2. **Bitwise and shift operators (402): EST BUG, in scope.** Implement `& | ^ ~ << >>` using the precedence and associativity in `docs/LANGUAGE_SYNTAX.md` §Bitwise and shift operators. The positive regression cites this section.
3. **`**`, `in`, `&&`, `||`, and incomplete `2 +` (10): REJECTED-SOURCE.** The string path rejects these spellings. Count them separately as rejected source, not failures; preserve identical diagnostics in off and shadow. Do not extend the EST grammar to accept them.
4. **Standalone `]` / `)` fragments (7): source audit required.** Inspect each occurrence's source. If the source is rejected, classify it as REJECTED-SOURCE. If any is from accepted source, fix the shadow hook to provide the complete logical expression; do not change the expression parser to accept a fragment.
5. **Numeric literal member access (4): EST BUG.** The string path accepts `(796935229243298.2).to_string()`; repair EST tokenization.

The C counters classify unique `(owner, slot, argument_index)` occurrences and separately retain total attempts.

### Bank ruling applied: standalone fragments

All seven isolated delimiters came from rejected source files; none came from an accepted source. The result is REJECTED-SOURCE for all seven (five `]` fragments in Expression, two `)` fragments in ReturnValue):

- `examples/swarm/control_flow_2026/iterative_planner/failed_attempts/01_multiline_task_literal.moss` — `MOSS_COMPILE_ERROR`, indentation jumps more than one block level.
- `examples/swarm/expression_surface_2026/numerical_tool/failed_attempts/01_len_method.moss` — `UNTYPED_FIELD_ACCESS`.
- `examples/swarm/expression_surface_2026/parser_evaluator/failed_attempts/01_multiline_vector_literal.moss` — `MOSS_COMPILE_ERROR`, indentation jumps more than one block level.
- `examples/swarm/expression_surface_2026/validation_engine/failed_attempts/01_multiline_constructor.moss` — `UNTYPED_FIELD_ACCESS`.
- `examples/swarm/expression_surface_2026/validation_engine/failed_attempts/02_multiline_vector.moss` — `MOSS_COMPILE_ERROR`, indentation jumps more than one block level.
- `examples/swarm/expression_surface_2026/validation_engine/failed_attempts/15_multiline_test_vector.moss` — `MOSS_COMPILE_ERROR`, indentation jumps more than one block level.

### Item C construction result

The fresh run after applying the rulings covered 2,061 sources (1,341 accepted). Per-slot totals below are attempts / unique / built / deferred 27.1b / rejected source / failed. Attempts and unique match because each occurrence is visited once in each compile invocation. The corpus pass had zero failed occurrences.

| Slot | Attempts | Unique | Built | Deferred | Rejected | Failed |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| Condition | 33,374 | 33,374 | 32,683 | 685 | 6 | 0 |
| Initializer | 23,390 | 23,390 | 17,082 | 6,308 | 0 | 0 |
| ReturnValue | 55,893 | 55,893 | 47,916 | 7,973 | 4 | 0 |
| Iterable | 1,240 | 1,240 | 1,190 | 50 | 0 | 0 |
| Receiver | 48,495 | 48,495 | 48,480 | 15 | 0 | 0 |
| Argument | 88,473 | 88,473 | 83,621 | 4,850 | 2 | 0 |
| LhsPlace | 74,315 | 74,315 | 74,281 | 34 | 0 | 0 |
| RhsValue | 74,315 | 74,315 | 49,611 | 24,704 | 0 | 0 |
| Scrutinee | 288 | 288 | 284 | 4 | 0 | 0 |
| RaisedValue | 3,583 | 3,583 | 1,808 | 1,775 | 0 | 0 |
| Expression | 70 | 70 | 46 | 19 | 5 | 0 |

| Form | Label | Count | Reproducer |
| --- | --- | ---: | --- |
| Pipelines, placeholders, constructors, collection literals/constructors | DEFERRED-27.1b | 40,108 (attempts) | `tests/phase27/est/repro/pipeline.moss`, `collection_literal.moss`, `constructor.moss` |
| Message expressions | EST BUG, fixed | 1,645 (triage) | `tests/phase27/est/message_expression.moss` |
| Bitwise and shifts | EST BUG, fixed | 402 (triage) | `tests/phase27/est/bitwise_shift.moss` |
| Unsupported operators and incomplete expression | REJECTED-SOURCE | 10 (triage) | `tests/phase27/est/repro/unsupported_operators.moss` |
| Isolated `]` / `)` fragments | REJECTED-SOURCE | 7 (triage) | `tests/phase27/est/repro/split_fragment.moss` |
| Numeric literal member access | EST BUG, fixed | 4 (triage) | `tests/phase27/est/numeric_member.moss` |

There are no remaining NEEDS BANK groups. The raw shadow failure file was not created because the failed count was zero. Corpus artifacts: `tmp/phase27-c2-shadow.tsv` and `tmp/phase27-c2-est-coverage.csv`.

## Phase 27.1b/c final construction and ruling status (2026-10-28)

This section supersedes the earlier deferred totals above. The final corpus pass
covered 2,078 tracked sources and 1,351 checker-accepted sources. Attempts equal
unique occurrences within the per-Program arena scopes; this aggregate equality
does not establish deduplication across separate compiler invocations (see CP-A).

| Slot | Attempts | Unique | Built | Deferred 27.1b | Rejected source | Failed |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| Condition | 22,029 | 22,029 | 22,023 | 0 | 6 | 0 |
| Initializer | 16,953 | 16,953 | 16,951 | 0 | 2 | 0 |
| ReturnValue | 32,785 | 32,785 | 32,767 | 0 | 18 | 0 |
| Iterable | 744 | 744 | 744 | 0 | 0 | 0 |
| Receiver | 32,942 | 32,942 | 32,941 | 0 | 1 | 0 |
| Argument | 59,115 | 59,115 | 59,080 | 0 | 35 | 0 |
| LhsPlace | 47,936 | 47,936 | 47,936 | 0 | 0 | 0 |
| RhsValue | 47,936 | 47,936 | 47,923 | 0 | 13 | 0 |
| Scrutinee | 147 | 147 | 147 | 0 | 0 | 0 |
| RaisedValue | 3,024 | 3,024 | 3,024 | 0 | 0 | 0 |
| Expression | 68 | 68 | 42 | 0 | 26 | 0 |
| **Total** | **263,679** | **263,679** | **263,578** | **0** | **101** | **0** |

| Form | Phase 27.1a count | Final status | Reproducer |
| --- | ---: | --- | --- |
| Pipelines, placeholders, constructors, collection literals and named arguments | 40,108 | Built; no deferred occurrences | `tests/phase27/est/syntax_27_1b.moss` |
| Message expressions | 1,645 | EST BUG fixed and built | `tests/phase27/est/message_expression.moss` |
| Bitwise and shift operators | 402 | EST BUG fixed and built | `tests/phase27/est/bitwise_shift.moss` |
| `**`, `in`, `&&`, `||`, incomplete `2 +` | 10 | REJECTED-SOURCE; the string checker rejects these | `tests/phase27/est/repro/unsupported_operators.moss` |
| Isolated `]` / `)` fragments | 7 | REJECTED-SOURCE; all source occurrences were rejected by the checker | `tests/phase27/est/repro/split_fragment.moss` |
| Numeric literal member access | 4 | EST BUG fixed and built | `tests/phase27/est/numeric_member.moss` |

Final construction labels: deferred 27.1b = 0, failed = 0, rejected source = 101
unique slot occurrences (including the 17 ruled rejected-source cases above),
and no unresolved NEEDS BANK groups. The 101 rejected-source total is grouped by
slot in the preceding table; it includes corpus contexts beyond the original
17-case ruling table. ABI-v9 generic bodies and Section 2 execution bodies are
covered separately by `tests/tooling/check_phase27_est_mossi.py`.
