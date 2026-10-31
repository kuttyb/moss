# Phase 27.4 independent Agent 2 audit

- Frozen implementation SHA: `7dd0468540359447c5c1fff55e5eadaa2f66f174`
- Baseline: `phase27-3-m1` (`ce5f836e06ce93909c358e6e6455402417d14991`)
- Review worktree: `tmp/phase27-4-agent2`
- Source policy: no implementation edits; no existing implementation evidence was consulted.

## Verdict

GO

## Optimizer expression-text audit

Mechanically searched `src/functional_optimizer.cpp` for `est::parse_view`, fingerprint APIs, `tree_expression_text`, `word_occurrences`, `plain_identifier`, `split_top_level`, `functional_pipeline_ids`, `callable_expression`, and direct `Stmt` fields `a`, `b`, and `text`: no expression text reader remains. The repository string-debt lint passes with `--require-zero optimizer`.

Manual direct/indirect taint classification:

- `Stmt` expression values reach `statement_expression_roots` through owning `est::Handle` slots; all subsequent use counting and shape comparisons receive `ExpressionRef`/`ExprId` and walk EST nodes.
- Binding names are obtained only from the LhsPlace root `Identifier`; binding uses compare `Identifier.name` values in the expression tree. Member names are not counted as variable uses.
- Pipeline source structure is read via the owned plan and pipeline expression handle. The `FunctionalPipeline::source_expression` string is copied to `FunctionalTraversalGroup::source_expression` and rendered by `dump_functional_ir` only; it does not feed a semantic branch or optimization decision.
- `Stmt::args.size()` is used only to enumerate argument handle slots. No argument text is inspected.
- Callable/capture strings are semantic names/metadata. Callable identity is rendered only; captures are compared as already-resolved binding names against result binding names. Neither is parsed as expression text.
- `ScopeOccurrence::expression` and local `result_expression` are `ExpressionRef` values, not strings. Functional plan lookup uses context-keyed plans and their arena index/ExprId handles; no legacy `functional_pipeline_ids` lookup remains.

No additional text-path helper or copied expression-text taint was found.

## Differential probes

- Built the frozen candidate and baseline independently with `make --jobs="$(nproc)" moss-dev` in separate worktrees.
- Candidate `tests/tooling/check_phase27_three_engine.py`: PASS; identical Fast Debug/native/-O stdout and callback marker count 3.
- Baseline `tests/tooling/check_phase27_three_engine.py`: PASS; identical stdout and callback marker count 3.
- Independently compared `-O` Rust emission for all 2,226 tracked `.moss` files, baseline versus candidate. There were zero generated-output hash mismatches among sources that emitted successfully in both. Twelve rows were paired non-comparisons: one paired 20-second timeout for `w2_spec_distinct_n032.moss`, plus eleven sources whose baseline and candidate emission both failed on the same unrelated package/source diagnostic. No one-sided failure was observed. These paired rows were excluded from output equality, not treated as optimizer differences.
- Optimizer string-debt lint: PASS (`audited raw Stmt NAME comparisons: 42`, `audited raw Stmt binding comparisons: 7`; `--require-zero optimizer` returned success).

The independent command record and machine-readable result are in `tests/phase27/adversarial/phase27-4-agent2-audit.json`. The scratch scan script/logs are disposable and were not consulted as implementation-team evidence.

## Findings

No remaining direct or indirect expression-text reader, unclassified -O output difference, or Fast Debug/native/-O behavior or callback-count mismatch was found.
