# Phase 25 Team I — Final Integration Execution Plan

## L7 Team I staffing override — 2026-10-18

For this integration task, use exactly two active agents: one combined L6
Coordinator/Reviewer and one L4 implementation worker. Work is serial through
that sole worker, preserving exclusive compiler ownership. The independent L6
adversarial reviewer runs once at the end, after implementation is complete;
idle the L4 worker during that review to keep at most two agents active.
This L7 instruction supersedes the default L5 headcount and intermediate
adversarial checkpoints below. Keep draft PRs, automated isolated regression
checks, exact-SHA L6 review, normal integration commits, and no main merge.
Minimize repeated chat, duplicate CI polling and redundant review work.


**Authorized target:** `phase25-integration`, branched from the frozen prep commit `a6e706bf99d3a96389cb63af87a3788bfe9056ac`. No direct main merge or production ABI-v10 cutover without L7 final acceptance.

Read `AGENTS.md` and `docs/AGENT_TEAM_OPERATING_MODEL.md`. This file is the Team I mission; do not reprompt organizational instructions. One L6 Coordinator, one L6 code-review role (Coordinator may fill it), **two L5 workers by default, maximum three active implementation workers only on independent file/task areas**, plus an independent L6 adversarial reviewer at major frozen milestones. L5 worker opens draft PR to integration, automated CI tests, requests L6 review **only after all required checks pass**; no manual ritual scripts or per-test LLM prompting.

## A. Exact source branches: verify before merging

| Input | Branch | Pinned SHA | Owned content |
| --- | --- | --- | --- |
| Team I prep | `phase25-integration-prep` | `a6e706bf99d3a96389cb63af87a3788bfe9056ac` | 62-gate ledger, 11 cross-crate scenarios, SDK/differential harness, merge-playbook |
| Team H and T | `phase25-checked-body-ir` | `2d457245061c75f3a4652a8851134322f1fa8445` | TypeRef/checked IR/callable IDs/typed declaration authority |
| Team S | `phase25-semantics-engines` | `53d04fbf48c007d912a47836959c488bcbb6c450` | checker parity, numeric, Char/Unicode, key semantics |
| Team G | `phase25-generic-types` | `2938d257bf10bbb11bd221fa14237ddcd1e0702b` | separate generic/compiler changes |
| Team B | `phase25-g-abi-runtime` | `755f13172c37447bca981d1fefbb885ec46f3a59` | ABI-v10 records, runtime, linker and proof tests |

Team T `9ed6830` is already contained in H; do not redundantly merge it. Team G and Team B **diverged** from H: H does not contain their entire branch history. I prep is divergent from H and S. The old rehearsal conflict ledger is historical and must be rerun. Reconfirm SHA/ancestry immediately before starting merges. Preserve original commit ancestry, reconcile semantic overlaps, use normal merge commits where possible; no blind cherry-pick, wholesale ours/theirs, rebase/force push of shared branches or unreviewed main merge.

## A1. Executable Git intake recipe (Coordinator/assigned single-file writer)

The following is a **template**, not permission to blindly merge conflicting code. Repeat on a fresh per-input PR branch, in the dependency order above, substituting the exact source ref and a new target-based branch name. Never use this template to push directly to `main`.

```sh
git fetch origin --prune
git switch phase25-integration || git switch --track origin/phase25-integration
git pull --ff-only origin phase25-integration
git status --porcelain
git rev-parse HEAD
git rev-parse origin/phase25-checked-body-ir
git rev-parse origin/phase25-semantics-engines
git rev-parse origin/phase25-generic-types
git rev-parse origin/phase25-g-abi-runtime

# Example: Team H/T intake (only the assigned compiler writer).
git switch -c phase25-i-h-intake
git merge-base --all HEAD origin/phase25-checked-body-ir
git diff --stat HEAD...origin/phase25-checked-body-ir
git merge --no-ff --no-commit origin/phase25-checked-body-ir
# At conflicts: inspect both implementations and frozen semantics.
# Resolve in the owned worktree, record actual provenance, build and commit.
git status --short
git diff --check
git commit -m "Phase 25 integrate reviewed H/T compiler baseline"
git push --set-upstream origin phase25-i-h-intake
# Open DRAFT PR to phase25-integration. CI starts automatically.
# Only after CI green and L6 review approval, merge PR into target.
```

The H intake above is an illustration. If `git merge` is conflict-free it still uses `--no-commit` and requires deliberate code/test review. If it stops on conflicts, resolve and stage intended files before committing. Do not execute subsequent `git commit` while the tree still contains unmerged paths. In the compiler intake branch, never allow another writer of `src/moss.cpp`.

For S, G and B intakes, start **new PR branches based on the latest reviewed/merged integration target** (not on each other's stale feature branches). Check ancestry and exact changed-file overlap first. Do not merge all branches into a single unreviewed mega-commit. If a feature branch already contains a source commit, preserve it once; record that it was subsumed rather than merging it again. Every PR must pass automated gates before L6 review; an intake cannot declare PASS when required suites are missing.

## B. Work allocation, leases and DAG

**Coordinator:** owns DAG and `src/moss.cpp` single-writer lease; reconciles input branches in disposable worktrees, reviews approved PRs and publishes consolidated `phase25-integration`. Build the DAG from actual file conflicts. Begin from Team H/T, bring in distinct G, S, then B while preserving all prep fixtures. Dependency order may be revised if exact ancestry/interface comparisons warrant it; record reason and SHA. Team I owns all integration fixes. Coordinate API boundary, not dual-writer compiler sessions.

**L5 Worker 1 (compiler writer):** only compiler writer. Own `src/moss.cpp`, `src/ast.hpp`, `src/type_ir.hpp`, callable specialization, checked-IR and compiler-facing ABI loader, G/S/H reconciliation. Preserve lexically scoped monotypes, nominal identity, inference/requirement closure, static dispatch, Char, numeric precision, Fast Debug/native diagnostics and semantics. Never silently drop edits from one branch.

**L5 Worker 2 (ABI/runtime/integration harness):** own nonoverlapping `src/abi_v10_records.hpp`, `src/source_free_abi_v10.hpp`, `src/moss_runtime_linkage.hpp`, `runtime/moss_runtime/**`, `tests/phase25/integration/**` and cross-crate scenarios. Deliver ABI-v10 typed record emission/import, exact dependency hashes, provenance, provider-owned nominal layouts, source-free linking, runtime cache invalidation and nominal MapKeyProof integration. Coordinate typed interface changes with Worker 1; do not concurrently edit `src/moss.cpp`.

**Optional L5 Worker 3:** independent test/oracle/CI/SDK work exclusively, if disjoint from Workers 1 and 2. Otherwise stick to two or one. Automated CI handles parallel test execution: do not create agents to launch test commands. Each worker has its own branch/worktree; submit draft PR and have CI run before requesting L6 code review. Coordinator may perform L6 review and must record the fact that the review was combined, not independent.

## C. Three inherited Team H acceptance blockers

Team H's G1–G4 report is useful implementation evidence, **not unconditional end-to-end acceptance**.

**H1: open semantic requirements.** Inventory compiler-critical uses of `_generic:`, `_element:`, `_field:`, `_method_value`, `callable:` and related markers. Separate legitimate output/diagnostic strings from semantic decisions. On live generic source, show that member, index, method and callable requirements actually flow through structured `TypeRef`/`MemberDispatchRequirement` with receiver/argument/result type relations, access, origin, and closure. A hand-constructed C++ requirement record or 0 diagnostic leaks is insufficient evidence. Preserve the rule that a completely unknown receiver cannot imply a structural record.

**H2: stale AST projection.** A bound `TypeRef` with an emptied projection currently evades `assert_consistent()`, because it checks only nonempty string mismatches. Reproduce and fix (or make impossible to express) on Param, Method, TraitMethod, Handler and Function; enforce at semantic/ABI boundaries. No silently divergent checked authority. Add focused negative test.

**H3: true source-free generic specialization.** Existing Team H test uses standalone provider source, not removed-source importing consumers. Prove *real* provider compile -> `.mossi` + `.rlib` -> physically remove `.moss` provider tree -> two independently typed consumers compile and execute via provider artifact. Cover a multi-statement generic and nested private helper, and require either adequate exported typed summaries/lowering recipes or checked body IR as needed by consumer specialization. Mark missing required facts/unspecializable cases **FAIL**, not PASS for benign fail-closed rejection. Preserve no fabricated semantic hashes and exact dependency validation. Gate 10 / binary transport belongs to Team I.

Use `docs/phase25/TYPE_IR_CONTRACT.md`, `ABI_V10_DESIGN.md`, `TEAM_A_TYPED_SEMANTIC_API.md`, `TEAM_S_TEAM_G_INTERFACE_AGREEMENT.md` and `TEAM_B_LINKAGE_INTEGRATION_SPEC.md` as frozen source of truth.

## D. Integrated Phase 25 exit requirements

**E1 Integration:** all distinct H/T, G, S, B deliveries reconciled with traceable SHAs/PRs and no dropped semantics; one compiler writer.

**E2 ABI-v10 and custom keys:** complete **Gate 10** using provider-defined `isequal` / `hash`, nominal MapKeyProof coherence and proper effect/visibility/ownership constraints, no automatic record `==` and no Phase 25.5 opaque/ownership features.

**E3 Source-free:** all 11 real cross-crate scenarios in `tests/phase25/integration/cross_crate_harness.py` with actual source removal, both consumer types, full link and execution, negative effect/provenance, tampered metadata fail-closed, v9-to-v10 migration behavior, cache invalidation.

**E4 Machine-checkable:** `tests/phase25/integration/run_acceptance.py --strict` must show **62/62 PASS** with real same-SHA evidence; BLOCKED/NOT IMPLEMENTED/fake evidence means unfinished. No source-free or compiler path accepted merely because unit records exist.

**E5 Regression:** existing full `make check`, `make examples`, Phase 25 suites, Phase 24 SDK compatibility for 19 packages, native debug/release/Fast Debug parity, 29 adversarial fixtures and differential oracles. No result masking or changes to expected diagnostics merely to pass.

## E. Automated PR CI and final verification

`.github/workflows/phase25-team-pr.yml` supplies parallel isolated CI jobs: core regression, optimized examples, Phase 25 tests + prep-harness self-tests. Each job independently builds using `make --jobs="$(nproc)"`. The preexisting `tests/run.sh` uses shared outputs and is sequential **inside its checkout**; do not claim `make -j` parallelizes it internally. Never launch interfering suites simultaneously in one worktree; parallelize at isolated CI job/worktree level.

Every L5 worker waits for all required green PR checks on the *latest head SHA* before requesting L6 review. L6 Reviewer inspects changed code and failing counterexamples, not mundane test-command instructions. Coordinator reruns combined-branch CI after every merge. At the frozen final candidate, push an integration commit whose message contains `[phase25-final]` (or dispatch the workflow where available) to trigger the full cross-crate release acceptance and `run_acceptance.py --strict`. A green PR is **not** a green Phase release.

## F. Adversarial schedule and completion

Independent L6 adversarial reviewer is invoked at **only 2–3 substantial frozen checkpoints**: (1) combined compiler/semantics+ABI interfaces, (2) true source-free Gate 10/cross-crate, (3) final all-62-gate freeze; coalesce adjacent checkpoints. It must try to falsify the most important contracts and submit concrete reproducers. No reviewer polling or unconditional boilerplate PASS.

Coordinator maintains the existing compact `docs/AGENT_TEAM_EFFICIENCY.md` closeout, including actual workers, PR rounds, reviewer defects, exact reviewed SHA and actual reported token usage (unknown if unavailable). Report failures honestly. Do not reopen Team H; Team I owns the integration and inherited gaps.

**DONE means:** consolidated pushed `phase25-integration`, all PR regression checks green, all 62 final gates PASS, source-free and MapKeyProof Gate 10 executable, SDK/differential green, H1–H3 closed with evidence, independent final reviewer at exact SHA, full issue ledger, and L7 review requested. Never merge to main. Only escalate a concrete frozen-contract conflict with a minimal reproducer.
