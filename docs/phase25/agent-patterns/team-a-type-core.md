# Team A type core: efficiency checkpoint

This record covers the active Phase 25 Team A branch `phase25-g-type-core`.
It is a restart checkpoint, not a READY verdict. Evidence is the Git history,
isolated worktree handoffs, test output, and exact-SHA Architect review.
Historic agent usage and all handoff counts are not fully reconstructable;
those fields remain `unknown`.

| Category | Report |
| --- | --- |
| **Leaf agents** | Exact unique count `unknown` across resumed work. Recorded roles include A1 identity, A2 solver/compatibility/type environment/signature/reply, A3 requirements/specialization, A4 open summaries/body IR, and compiler-path tests. Worktrees and commits do not establish unique agent count. |
| **Peak leaf concurrency** | At least 3 during A2, A3, and A4 isolated work; exact historic peak `unknown`. Coordinator and Architect excluded. |
| **Non-leaf reviewers/managers** | 1 L6 Coordinator. Independent Architect instance count `unknown`; at least one exact-SHA review at `2c6772ff452a4f7d33f6bc7e389267ed5eee13d5`. No independent adversarial review evidenced. |
| **Submissions** | 0 PRs evidenced. Non-PR handoff count `unknown`; validated handoffs listed in the feature-branch history were integrated. A4 follow-up `feaf1136` remains isolated WIP. |
| **Coordinator reviews** | Integrated handoffs received focused validation or inspection; exact reviewed/submitted coverage and Coordinator repair-round average/max `unknown` across resumed history. The mini-application trait regression was returned to A3 and repaired by `e17ed1c`. |
| **Other review events** | Independent Architect reviewed exact `2c6772ff452a4f7d33f6bc7e389267ed5eee13d5`: **NO-GO**; G-A-3 closed, G-A-2 function keys preserved but selected method target ID defective, G-A-1 and A4 open. Selected method ID was repaired later in `6cd535a` and requires re-review. Earlier interim Architect NO-GO at `ec0c0d6` is preserved in `.codex/CURRENT_STATUS.md`. No attributable L7 implementation review yet. |
| **Defects** | Confirmed compiler-path defects include cross-body TypeRef leakage into sdk/tables (fixed `bfaef60`, direct Margo build and later oracle passed), trait-formal summary mismatch in mini-application (fixed `e17ed1c`, examples passed), message-result monotype overwrite (fixed `5bf0ae4`, negative probe passed), and generic-callee bypass of a declared Int parameter (fixed `b323e94`, isolated focused probe passed). G-A-1 string authority and A4 body coverage remain open. Exact net-new discoverer totals and duplicate counts `unknown`; Architect verification is not counted as discovery. |
| **Test evidence** | On checkpoint implementation through `b323e94`: `make`, `make examples`, Team A compiler-path suite in declared modes, generic specialization 40/40, built-in typing 129/129, Type IR, checked-body hash, typed API, nominal A–F source-free identity, feedback validation, and G2 Gates 1–9 and 11 passed. Gate 10 is pending joint integration. A prior `make check` on `2c6772f` passed Phase 20/21, Team A, runtime, foundations, and Text & Encoding before intentional stop at the advanced-data oracle to integrate changes. No final full-suite pass yet. Unique Team A case count and mode executions need recount from final frozen output. |
| **Review provenance** | Reviewed implementation: `2c6772ff452a4f7d33f6bc7e389267ed5eee13d5`, **NO-GO**. Checkpoint delivery SHA is in the final status commit and remote branch. Later implementation changes include exact selected method ID, retained body walk scope, partial checked body IR, and generic declared-parameter checking; a new Architect review is required. |
| **Usage** | Tokens and billed cost by role `unknown` (provider data unavailable). |
| **Value** | Isolated compiler-path tests found failures that standalone TypeSolver tests missed: cross-function state leakage, nominal trait-formal binding, and a generic call that bypassed its declared parameter. The independent Architect found a display-derived selected method ID and confirmed that semantic string decisions still prevent Finding 1 closure. |

Team A remains NOT READY. It retains ownership until full TypeRef authority,
complete checked body IR, full validation, and exact-SHA Architect GO.
