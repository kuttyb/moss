# CP-E adversarial review — Agent 2

Frozen candidate: `068eb9aa130e51778441f222b1a449a48247bef9`  
Comparison baseline (`phase27-2-m1`): `04be1a1bbbc7a4fd8fe2e4090b4b5bad5dd573e4`

## Verdict

CP-E verdict: NO-GO

This review found no verdict or diagnostic delta in its four independent probes, and the EST focused-check harness passed. The audit is still incomplete for the requested exhaustive checker/emitter read inventory. The zero-reader lint inventories calls to a fixed list of expression readers plus selected comparisons; it does not capture all expression-shape tests on raw `Stmt` fields. The concrete missed readers below require adjudication or structural conversion before an exhaustive GO can be claimed.

## Source-reader findings

| Site | Raw field | Classification | Review result |
| --- | --- | --- | --- |
| `src/semantic_checker.cpp:13491-13494` | Assign `statement.a` | `find('[')` gates indexed-place handling; `tree_parse_index` then extracts the structure | Residual expression-shape text test. Semantics are extracted structurally, but the branch still classifies an expression-bearing field textually. |
| `src/semantic_checker.cpp:13537` | Assign `statement.a` | `find('.')` gates member-place handling; `known_expression` then retrieves the projected field | Residual expression-shape text test. |
| `src/semantic_checker.cpp:18015` | Assign `statement.a` | `rfind('.')` classifies a writable member place before calling `analyze_effect_expression` | Residual expression-shape text test. |
| `src/semantic_checker.cpp:27899, 27921` | Let/Var/Assign `statement.b` | `starts_with(trim(...), "[")` classifies a vector literal when inferred type is absent | Expression-text interpretation; a vector-literal tree query is available in this checker. No exemption was evidenced for this case. |
| `src/native_generator.cpp:6352` | Let/Var initializer `s.b` | First character `[` selects the fallback generated type `vector` | Expression-text interpretation in the emitter's fallback type decision. |
| `src/semantic_checker.cpp:28035-28036` | Assign `statement.a` | `rfind('.')` plus `find('(', dot)` distinguishes member place from a call-shaped suffix | Raw syntax classification; needs an explicit tree-backed classification or documented narrow exemption. |

These are not `tree_expression_text` calls. They demonstrate why a search for named reader APIs alone does not prove zero expression-text interpretation. The exact machine-readable list is in `cp_e_agent2_068eb9aa_inventory.tsv`.

The exact spelling `tree_expression_text(` has zero call sites in the checker/emitter consumer files searched by `check_phase27_string_debt.py`. That checker also passed `--require-zero checker` and `--require-zero emitter`, reporting 39 raw NAME comparisons and 9 binding-name comparisons. Those results do not cover the raw shape tests listed above.

## Differential probes

The unchanged probe sources are under `tests/phase27/adversarial/cp-e-agent2-068eb9aa/`. Each was run with the phase27-2-m1 compiler and the frozen candidate in off and shadow modes. Verdict, diagnostic code/message, line/column, rule, and cause matched in all cases:

- `probe_valid.moss`: accepted on all three paths; covers calls, vector construction/indexing, grouping, conditional branches, `for`, and `map` pipeline lowering.
- `probe_invalid_operator.moss`: same `MOSS_COMPILE_ERROR` / `invalid arithmetic expression`.
- `probe_invalid_unbound.moss`: same `UNKNOWN_SYMBOL_OR_TYPE` / `unknown identifier 'missing'`.
- `probe_invalid_statement.moss`: same `UNKNOWN_SYMBOL_OR_TYPE` / `unknown identifier 'missing'`.

`python3 tests/tooling/check_phase27_est.py ./build/compiler/moss-dev` passed its focused checks (four syntax dumps, raw places, copy/rebuild, off-mode expression arena, stale guard). The frozen compiler build `make --jobs="$(nproc)" moss-dev` completed. This does not substitute for a full tracked-source verdict/diagnostic/checked-closure differential, which was not run in this isolated adversarial review.

## Limits

The lexical reader inventory was restricted to the review scope: checker/emitter consumer files and expression-bearing legacy `Stmt` slots (`text`, `a`, `b`, `args`). It classified the remaining named-reader calls and the raw comparison categories above, but did not create a per-read inventory for every use of those fields. The listed missed textual shape tests and that unclosed inventory make NO-GO the only supportable verdict for an exhaustive CP-E review.
