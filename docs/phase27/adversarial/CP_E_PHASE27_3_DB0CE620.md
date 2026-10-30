# CP-E fresh exhaustive review — `db0ce620`

CP-E verdict: NO-GO

Frozen candidate: `db0ce6203197726d3cd8eca01a0366d1d8283295`

Baseline: `04be1a1bbbc7a4fd8fe2e4090b4b5bad5dd573e4`

Candidate executable SHA-256: `c4bf711bba8cf3f52acfb632c22636e1de95250cac1e4196d5be155d0a826fca`

This review made no source edits. The direct reader lint passes at zero checker and emitter sites, and `tree_expression_text` has zero uses in those readers. The three retained type-argument exemptions are keyed by function and API, each resolves to exactly one site, and remain the documented `split_top_level` uses in `phase20_is_nontransferable_capability`, `builtin_equality_type`, and `contains_float_type`. The raw NAME/binding comparison lint reports 40 NAME and 9 binding comparisons; the exemption inventory classifies them as method, handler, builtin, effect, keyword, or binding names. These direct lint results do not cover the expression-string taint paths below.

## Exhaustive direct and bounded-taint findings

The bounded audit started at reads of expression-bearing `Stmt` fields and `tree_expression_text`, followed values into helper parameters/locals, and classified helpers that inspect characters to make a semantic, validation, lowering, or code-generation choice. These are residual readers and prevent a GO:

| Reader and source location | Taint/call path and classification |
|---|---|
| `Checker::proven_safe_integer_divisor`, `src/semantic_checker.cpp:11413` | Binary-expression child (`binary->second`) → raw text passed to this helper → trims/unpacks grouping/signs, scans digits/leading zeroes. Reader: decides divisor safety from expression characters. |
| `tree_numeric_literal_spelling`; `root_literal_class` / `decimal_integer_literal` in `src/est/expression_query.cpp`; decoder callers in `src/semantic_checker.cpp:5754,20765` and `src/native_generator.cpp:1615,3251,3469` | EST literal node → raw spelling → character parsing/`decode_numeric_literal_expression` to recover literal class/value. Reader: numeric semantic facts are still derived from text rather than node facts. |
| `reject_untyped_fields`, `src/semantic_checker.cpp:15110+` | `TypeEnvVisitor` supplies `statement.a`, `.b`, `.text`, `.args`, and result-expression strings → helper scans `receiver.member` and call/generic punctuation → emits `UNTYPED_FIELD_ACCESS`. Reader. |
| `has_unresolved_local_callable_call`, `src/semantic_checker.cpp:18870+` | `record_signature_call_dependencies(expression)` → helper trims/unwraps and scans names/captures in raw text (alongside recursive EST checks) → changes unresolved-local-call decision. Reader. |
| message-prefix checks, `src/semantic_checker.cpp:~18000,~19067` | Expression strings → `starts_with("message ")` before further parsing/collection. Reader: syntax form detection. |
| `juxtaposed_operands`, `src/semantic_checker.cpp:25739+` | `check_expression(original)` → raw scanner walks quotes, separators, words, and grouping → rejects adjacent operands. Reader: expression validation. |
| `check_expression`, `src/semantic_checker.cpp:~26522,~26755` | `original` → `starts_with("match ")` and leading `!` character checks → rejects unsupported expression forms/operators. Readers. |
| `check_expression` / `inferred_expr_core`, `src/semantic_checker.cpp:~11654,~26381` | Original expression strings → equality with `_` → placeholder handling. Reader; should use Placeholder node identity. |
| range step validation, `src/semantic_checker.cpp:24138` | Parsed call argument `args[2]` → optional-plus/digit scan and comparison with `0`/`+0` → validates positive literal step. Reader. |
| `functional_pipeline_from_est`, checker `src/semantic_checker.cpp:~25898`, emitter `src/native_generator.cpp:1734` | Expression strings → `find("|>")` prefilter → decides whether to query/lower pipeline. Reader/precondition. |
| `tree_parse_member_call`, checker `src/semantic_checker.cpp:~11118` | Failed EST parse text → trim/leading-dot check and reparsing with a synthetic receiver → recovery decision. Reader. |
| `program_uses_numeric_builtins`, emitter `src/native_generator.cpp:316+` | `body_uses` forwards statement fields/args/result strings → `contains(value)` searches `parse_int` and related names plus following punctuation → chooses runtime builtin/helper inclusion. Reader. |
| checked-body literal IR builder, `src/semantic_checker.cpp:4418+` | `occurrence.source_expression` → text equality for `true`/`false`, digit scan and `stoll` for integers → classifies/populates literal IR. Reader. |
| `native_generator::expr`, `src/native_generator.cpp:3159+` | EST expression is accompanied by original `e`; late rewrites scan and splice module-qualified paths, identifiers, `self.field` versus numeric zero, write-through parameter names, word operators, and member names. String-concat operands and index expressions are also tested for quote delimiters. These alter emitted semantics/code from source characters: reader. |
| `obvious_expr_type` (`src/semantic_checker.cpp:10352`) and `generated_expr_type` (`src/native_generator.cpp:~1840`) | Raw expression used as a type-environment key without first proving it is an Identifier node. Possible reader; exact-name lookup is benign only when node kind is guaranteed. Current call paths do not establish that guarantee. |

The `FileIO` mode comparisons (`ro`, `rw`, `create`, `dataonly`, checker near `src/semantic_checker.cpp:27135`) are a separate candidate exemption: they appear to be designated bare mode keywords at call sites. The implementation currently compares argument strings, so it should be retained as a legitimate NAME/keyword exemption only after the caller proves Identifier shape. Type-string parsing, canonical type/name handling, and legacy splitter/oracle implementation were inspected separately and are not counted as active expression-reader migrations.

## Differential and closure evidence

The frozen corpus sweep covered 2,146 sources. Baseline, candidate-off, and candidate-shadow each accepted 1,409, with zero timeouts. Candidate off versus shadow had zero differences. Baseline versus candidate-off had one diagnostic difference at `sdk/advanced-data-acceptance/fixtures/source_free_consumer.moss:57`: baseline `RECOVERY_PATTERN_REQUIRES_VARIANT`; candidate `UNKNOWN_ERROR_TYPE`. This remains unadjudicated for the standalone corpus result. The source-free package oracle was run independently with both compiler binaries and passed for both (5 Team A modules and 14 provider interfaces); logs are in `tests/phase27/adversarial/cp-e-phase27-3-db0ce620/logs/source-free-{baseline,candidate}.log`. Exact standalone diagnostics are preserved alongside them.

Checked-declaration closure was evaluated for all 2,146 sources. There are 661 entries with `TRACE_COMPLETE` on both paths and 1,485 without complete traces. The latter are classified in `closure-context-exclusions.json`: 1,212 standalone entries with no project/local-import context; 92 outside the project source/test/bench roots under the harness context rules; and 181 project-target entries rejected as `MOSS_COMPILE_ERROR` on both paths before the trace-complete point. These are audit-coverage exclusions, not closure equality claims. For the 661 complete traces, four entry contexts show a true per-entry checked-declaration difference (not duplicate rows inside one context); all four omit the same declaration identity on the candidate side:

`method:heap__Entry.key_of@9` in `examples/swarm/modules/algo_chain/heapkit/src/heap.moss:9`.

The affected entry contexts are `examples/swarm/modules/algo_chain/heapkit/src/order.moss`, `examples/swarm/modules/algo_chain/heapkit/tests/order_test.moss`, and the corresponding `natural_chain/heapkit/src/order.moss` and `natural_chain/heapkit/tests/order_test.moss`. In each context baseline closure contains the method row and candidate closure does not. Both paths have complete traces and reject identically with `INVALID_BARE_IDENTIFIER_STATEMENT` at line 35. The contextual `order.moss` imports `heap`; its `order_by_key` calls local generic `key_of(item)`, while `heap.moss:100-101` has the heap method call `e.key_of()`. The reject location is the `if x < lo:` in `clamp` at `order.moss:35`; a causal relation to the omitted method has not been established. Raw exact per-context rows are in `closure.jsonl`.

The closure edge census has 1,546 missing and 1,546 added raw labels across 160 entries. Stripping the target's generated owner prefix normalizes both multisets to zero differences. This is a BENIGN label normalization, separate from the checked-declaration omission above.

## Evidence files

- `tests/phase27/adversarial/cp-e-phase27-3-db0ce620/corpus.py`, `corpus.jsonl`, `corpus-summary.json`
- `tests/phase27/adversarial/cp-e-phase27-3-db0ce620/closure.py`, `closure.jsonl`, `closure-summary.json`, `closure_contexts.py`, `closure-context-exclusions.json`
- `tests/phase27/adversarial/cp-e-phase27-3-db0ce620/logs/` (standalone and package source-free oracle outputs)

The verdict is NO-GO because expression-text semantic readers remain and the closure census has a checked-declaration difference. The standalone diagnostic difference also remains unresolved. No performance tests were run, as instructed.
