# Team L7 Phase 25.1 regression stabilization

| Category | Report |
| --- | --- |
| **Leaf agents** | 0 in this stabilization turn; only the L6 coordinator wrote code. Evidence: no leaf assignments or submissions in this turn. Historical phase-wide count: unknown. |
| **Peak leaf concurrency** | 0 for this stabilization turn; historical phase-wide peak: unknown. |
| **Non-leaf reviewers/managers** | Coordinator: 1. Existing independent adversarial architect: 1 assigned, exact-SHA review pending. |
| **Submissions** | PRs: 0 observed. Non-PR handoffs: 1 branch checkpoint, pending review/integration outcome. |
| **Coordinator reviews** | Coverage and repair rounds: unknown; no separate Coordinator review event was recorded for this direct coordinator-authored checkpoint. |
| **Other review events** | Architect: one planned exact-SHA adversarial review, pending. Adversarial reviewer: 0 separate events observed. L7: one stabilization order, not counted as a review event. |
| **Defects** | Confirmed baseline regressions corrected: expression field-dot parsing; static/member receiver diagnostic traversal; assert diagnostic precedence; nullary open result instantiation; nested returned-call occurrence identity; typed scheme/body completeness conflation; private-chain producer selection/overlap. First discoverer per defect: test/tool or coordinator evidence; exact attribution unknown. All have regressions and final suite evidence. No open stabilization defect identified. |
| **Test evidence** | `tests/run_parallel.py` unique declared job count: 132. Branch: 132 pass, 0 fail, 0 skip. Isolated main: 132 pass, 0 fail, 0 skip. Focused checks: M2b2 source-free corpus, H3 chain, M2b1 provenance, ABI-v10 adversarial shard, typed API, checked-body IR, statement/call occurrence roles, teaching diagnostics, baseline regressions, negative shard, feedback validator. Modes and logs: [stabilization report](../PHASE_25_1_REGRESSION_STABILIZATION.md); full logs are under disposable `tmp/`. Historical supplied main 131/132 was not reproduced. |
| **Review provenance** | Exact implementation SHA and verdict: pending architect review. Delivery SHA: pending commit. Intervening-diff classification: not yet applicable. |
| **Usage** | Tokens and billed costs by role: unknown; no provider report available. |
| **Value** | The paired source-built suites show all 132 jobs pass on the branch and isolated `main`, with focused tests reproducing the repaired parser, diagnostic, typed-fact, and producer boundaries. The historical reported main failure did not reproduce and remains explicitly disclosed. The exact-SHA independent review is still required before calling the stabilization complete. |
