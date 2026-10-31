# Phase 27.4 report (in progress)

PASS optimizer reader sites = 0
PASS text-path calls in functional_optimizer.cpp = 0
PASS -O differences: 0, all adjudicated
PASS zero unadjudicated differences vs phase27-3-m1 = 0

## Iteration 1 — implementation and bulk differential

Base: `phase27-3-m1`, merge `ce5f836e06ce93909c358e6e6455402417d14991`. The optimizer now resolves plans and expressions through owning EST handles and ExprIds. Existing source strings and `functional_pipeline_ids` remain and are dual-written. Pattern work is out of scope.

- Optimizer reader lint: `python3 tests/tooling/check_phase27_string_debt.py --require-zero optimizer` — PASS, zero readers.
- Mechanical forbidden-call scan in `src/functional_optimizer.cpp` (`est::parse_view`, fingerprint lookup, `tree_expression_text`, `word_occurrences`, `plain_identifier`, `split_top_level`, `functional_pipeline_ids`, `callable_expression`) — zero matches.
- Full source corpus, baseline off vs candidate off and candidate shadow: 2,226 sources; zero verdict/diagnostic differences and zero incomplete runs.
- Default and `-O` Rust emission: 1,424 common accepted sources, 2,848 profile comparisons; zero hash mismatches among 2,826 successful matched emissions, 22 identical paired emitter failures, zero unpaired failures/timeouts.
- Top-level examples: 31; zero check, Fast Debug, default Rust, or optimized Rust output differences; zero incomplete runs. Three shared Fast Debug nonzero cases matched their paired status/output.
- Three-engine conformance gate: baseline and candidate both pass; outputs and callback count match, count 3.

Raw results and command logs: `tests/phase27/adversarial/phase27-4-iteration-1/`. The bulk harness reused the committed Phase 27.3 corpus, Rust emission, and examples parity scripts against the exact `phase27-3-m1` baseline.

Full `make check` off/shadow, `.mossi` parity, idle first-parent performance, and the single fresh Agent 2 review remain pending. No review verdict or final gate is claimed at this checkpoint.
