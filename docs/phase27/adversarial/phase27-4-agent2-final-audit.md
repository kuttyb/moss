# Phase 27.4 Agent 2 final audit

**Verdict: GO**  
**Frozen candidate SHA:** `e80812291c32454fece6c840f0369acb61c87b7e`  
**Baseline:** `ce5f836e06ce93909c358e6e6455402417d14991` (`phase27-3-m1`)  
**Independent worktree:** `tmp/phase27-4-agent2-audit`

## Findings

- No direct or indirect optimizer source-text reader was found in `functional_optimizer.cpp` or its helper interface. It obtains EST roots from `Stmt`/function-owned handles and traverses nodes. Searches found no `est::parse_view`, fingerprint lookup, `tree_expression_text`, `word_occurrences`, `plain_identifier`, `split_top_level`, `functional_pipeline_ids`, or `callable_expression` use in the optimizer. `source_expression` is retained in traversal metadata and emitted in IR diagnostics; it is not inspected to make decisions. Type spelling parsing and optimization-note string searches operate on type/note data, not Moss expression source.
- Checker plan creation records the source occurrence's exact `ExprSlot` and argument index, selects a handle by that exact pair, validates its fingerprint only for staleness, and stores arena index plus ExprId. No order-based or first-compatible pairing remains. Optimizer ownership lookup validates slot, argument, arena, and ExprId. Result plans use the `ReturnValue` slot and argument 0.
- Dependency collection marks missing required handles incomplete and emits an internal invariant diagnostic in debug builds. Cross-binding fusion requires complete dependencies; shared traversal exits when dependency coverage is incomplete. Message collection requires the complete `Receiver` tree, so separate Argument roots cannot be mistaken for complete coverage. Injecting a missing Message Receiver produced `incomplete expression dependency collection` and blocked fusion.
- Decision parity covered 2,232 source paths (1,419 accepted in all three modes). There was one decision difference, candidate off and shadow matching: `tests/phase27/conformance/field_name_not_binding_use.moss`. The baseline's word scan counts field `p.total` as a use of binding `total`, while the candidate's Identifier-node walk does not. This is the expected **LEGACY MISPARSE**. The independent Fast Debug/native/optimized-output checks passed with callback count 3 on both paths. No other optimized-decision difference was observed.
- Corpus verdict parity: 2,226 sources; baseline-off vs candidate-off differences 0; candidate-off vs candidate-shadow differences 0; incomplete 0. Rust emission parity: 1,424 eligible sources, 2,848 comparisons, 0 mismatches (22 paired emitter failures). Examples parity: 31 examples, 0 output mismatches. The three-engine gate passed for baseline and candidate with callback invocations 3.
- All six targeted expression-use regressions passed. Their expected decisions and callback counts were: identical pipelines in one statement (6), field-name non-use (3), rebound binding (9), Message argument use (3), callback reading the binding (3), and injected missing dependency root (3). The identical-plan probe showed distinct pipeline IDs and ExprIds for argument indices 0 and 1. For all six, Fast Debug, baseline/candidate default emission, and baseline/candidate optimized emission produced matching outputs and callback counts.

## Evidence

Raw independent outputs are committed unchanged under `tests/phase27/adversarial/phase27-4-agent2-final/`. They include corpus, optimized-decision, Rust-emission, examples, three-engine logs, and the Message fallback injection diagnostic. The six source fixtures are existing tracked conformance files; no source files were changed by this audit.

The decision parity normalizer replaces transient `%<integer>` IDs and excludes generated ExpressionPlan probe lines and `DataflowGroup Source` formatting lines. It preserves the remaining decision, rewrite, materialization, effect/barrier, and cardinality output exactly. The ExpressionPlan ownership probe is checked independently by the targeted regression script.
