# Team A — Phase 28 Round 3 selector argument effect authority

| Category | Report |
| --- | --- |
| **Leaf agents** | 0 actual; Coordinator implemented directly. |
| **Peak leaf concurrency** | 0; no leaf agents. |
| **Non-leaf reviewers/managers** | Coordinator: 1. Architect: 0. Independent adversarial reviewer: 1. |
| **Submissions** | PR: 0. Non-PR handoff: 1 (`phase28-r3-team-a-effect-facts`, implementation `325ff2ef62f384684fd693ca38d0c3b6474f5a8e`); integrated: pending; rejected: 0; deferred: 0. |
| **Coordinator reviews** | 1/1 implementation self-reviewed; repair rounds: 0. |
| **Other review events** | Architect: none. Adversarial: 1 review of exact implementation SHA `325ff2ef62f384684fd693ca38d0c3b6474f5a8e`, GO, no findings. L7: none. |
| **Defects** | Net-new confirmed defects: 0; review findings: 0. Abstract trait dispatch remains explicitly unsupported when checker has no concrete checked argument-effect vector. |
| **Test evidence** | Selector effect harness: PASS default and EST shadow. Ownership harness: PASS default and EST shadow. Take fixtures: 16/16. Specialized ownership contract: PASS. String-debt gate: PASS (42 names / 7 binding comparisons). Feedback checker and `git diff --check`: PASS. No full integration suite run. |
| **Review provenance** | Agent 2 reviewed implementation SHA `325ff2ef62f384684fd693ca38d0c3b6474f5a8e`; GO. Subsequent changes are status/feedback/team-efficiency records only. |
| **Usage** | Tokens and billed cost: unknown; no provider report. |
| **Value** | Existing checked argument effects are now retained under the primary EST context and exact argument occurrence used by selector consumers, including specialization isolation. Unknown abstract trait-call effects remain absent rather than fabricated. |

## Team B integration

Use `SemanticFacts::inferred_effect` on the exact argument `ExprId` from the same EST arena as the selector call. Query with the primary semantic context (`TypedTakeFactV1::est_fact_context` for take selectors). The checker verifies the call's retained target in that context before publication. For static generic callees, the call specialization is carried in the retained call facts and the checked callee effect vector is selected by that exact checker specialization identity. Do not fall back to the secondary `source:` context for supported selector calls. If the primary fact is absent, treat it as missing/unsupported and fail closed.

Cherry-pick `325ff2ef62f384684fd693ca38d0c3b6474f5a8e` onto the active Team B Round 3 branch. This change modifies the checker, checker fixtures/harness registration, compiler contract/status and feedback records; it does not modify consumer files.
