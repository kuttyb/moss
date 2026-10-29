# Phase 27 EST shadow divergences

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
