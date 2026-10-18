# Moss multi-agent engineering operating model

**Status:** Mandatory repository-level Team policy for new work. **Scope:** All engineering Teams, including Phase 25 Team I. **Precedence:** Complements `AGENTS.md`, the frozen Moss design contracts, and `docs/AGENT_TEAM_EFFICIENCY.md`. This is organizational policy, not authority to change language semantics or merge `main`.

## 1. Default team and scaling

| Role | Responsibility | Count |
| --- | --- | --- |
| L6 Coordinator | Own task DAG, acceptance criteria, file-ownership map, PR integration, delivery, evidence and status. May be the hands-on L6 Reviewer. | 1 |
| L6 Reviewer | Review each green PR for correctness, tests, contract compliance, regression risk, maintainability. May be the Coordinator; label it transparently as combined review. | 1 role, optionally combined |
| L5 implementation workers | Independently implement bounded tasks, self-test, prepare draft PRs, fix requested issues. | **2 by default**, fewer when work is coupled |
| Independent L6 Adversarial Reviewer | Falsify architectural invariants at **major frozen milestones**, independently of normal PR approval. No continuous participation or rubber-stamping. | 1 on-demand |

**Hard concurrency cap:** At most **3 active implementation workers**, regardless of platform, across a Team. This includes a Coordinator while it is personally acting as an additional implementation worker. A reviewer who is solely reviewing does not count as a worker; do not relabel an implementation worker as a reviewer to evade the cap. Default two L5 workers; add a third only for demonstrably independent edits and tasks. Never spawn multiple workers just for appearance. If the task is one shared compiler file, use **one writer** and let the other worker own independent tests/runtime/docs on nonoverlapping files, or idle that slot.

Each implementation worker gets a dedicated worktree and feature branch. L6 owns integration order and avoids simultaneous shared-file changes. Explicit handoffs include branch/ref, commit SHA, changed files, PR URL, contract invariants and known blockers. Team I holds the sole `src/moss.cpp` writing lease after Team H releases it.

## 2. PR-first implementation, automated tests, and review

1. Coordinator divides the mission into small, independently verifiable *outcomes*, with disjoint file owners and a dependency DAG. Name the frozen contract, concrete acceptance program(s), review boundary and non-goals. Do not supply scripts of mundane test commands in LLM prompts.
2. L5 worker builds the change in its own worktree, writes new behavioral and negative regression tests, performs its own code/diff check, pushes its branch, and creates a **draft PR** to the Team integration branch. If PR permissions/platform are unavailable, publish an evidence-bearing branch handoff and record the exception explicitly; do not call it a PR.
3. The PR's **automated regression workflow** starts immediately and runs independent test groups concurrently on isolated checkouts/runners. Builds use all available cores with `make --jobs="$(nproc)"` (or the platform-equivalent CPU count). Do not start mutually interfering suites concurrently in the same worktree without isolated artifacts/cache directories. Observe actual outcomes; a missing job, pending status, or BLOCKED acceptance gate is not green.
4. **Only after all required PR regression checks pass** does the worker mark the PR ready/request the L6 Reviewer. The L6 Reviewer reads code, verifies behavioral evidence and the change's invariants, and either approves or requires a focused repair. It is not the reviewer's job to tell the worker to run routine test commands.
5. Coordinator integrates an approved green PR to the Team integration branch, runs the automated combined-branch gates, and checks cross-PR interactions. Reviewer and Coordinator may be the same agent but the review verdict and review SHA must still be recorded; no self-approval claims masquerading as independent review.
6. Any edit after review invalidates its old green status/approval until the new SHA is validated. No merge to `main` without L7 approval.

**Review quality rule:** `make check` and source count alone do not prove architectural compliance. A gate passes only when its specified semantic invariant is exercised by a real executable path. Unit-testing a record's existence is not evidence that production compiler paths populate or consume it. A source-backed interpreter/native parity test is not a source-free provider/consumer proof.

### Automated testing rules

- The workflow at `.github/workflows/phase25-team-pr.yml` is the default Phase 25 PR evidence source. Three independent groups (core regressions, examples, Phase 25/integration smoke) run concurrently on **separate checkout instances** and each build with available cores.
- The existing `tests/run.sh` has shared artifact paths and runs serial suites. `make --jobs=N check` still runs this script serially; do not claim internal parallelism. Concurrency is across isolated CI jobs or isolated worktrees. Split further only after auditing fixed paths, cache writes, build outputs, and toolchain CPU/memory contention.
- `run_acceptance.py --strict` is the **Phase 25 release gate**, not a substitute for PR unit/regression tests. It must reject missing, blocked, stale, or mocked evidence. Final full ABI-v10 source-removal, cross-crate, SDK, native/Fast Debug/differential tests are release-only where not yet supported; their pending state must be explicit during development.
- The regression suite includes negative reproducers with exact Moss diagnostic codes, source-backed/native parity where relevant, source-free import without provider `.moss` sources where required, and stale/tampered metadata fail-closed behavior.
- Worker reports should link machine-produced test results and PR checks, not paste manually constructed PASS summaries. A red result means fix first, request review later.

## 3. Independence and adversarial checkpoints

L6 Reviewer is a production gate on each ready, tested PR. Independent L6 Adversarial Reviewer **is not** continuously active or a mandatory review on each PR.

Invoke it at a few frozen milestones only:
1. After compiler/type/semantics and ABI/runtime branches are reconciled sufficiently for an architectural integration baseline.
2. After the genuinely source-free ABI-v10 / custom MapKeyProof Gate 10 is implemented and exercised end to end.
3. At final Phase acceptance of all 62 gates and SDK multi-engine parity.

The Coordinator may coalesce milestones 1 and 2 if they coincide. Each review independently attempts to **falsify** 3–6 high-risk invariants with live probes, records precise SHA, minimal reproducers, confirmed new defects vs previously known ones, and a PASS / FAIL / PASS WITH DEFINED REMAINING SCOPE verdict. An unchanged re-review without new evidence is not mandatory. A PASS WITH DEFINED REMAINING SCOPE is not an unconditional completion signoff.

## 4. Compiler ownership and merge safety

The Coordinator maintains an explicit file lease table. For `src/moss.cpp`, **one writer at a time**; parallel work must use disjoint files and must not bypass lease via cherry-picks. ABI/header/test workers may work independently, but synchronize API interfaces before merge. Check exact upstream branch SHA and ancestry, not only branch name. Preserve reviewed Team branches, no force-pushes or whole-file "ours/theirs" conflict resolution for semantic overlap. Preserve diagnostic, ownership, effect, optimizer and Fast Debug behavior while merging.

## 5. Definition of done

**Worker PR done:** exact commit SHA; executable positive/negative tests; automated green PR checks; L6 code review approval; integration merge reference. A PR is not complete because a worker says "all tests pass."

**Team milestone done:** acceptance gate ledger with concrete executable evidence on the merged branch; known-blocker and deferred-owner list; implementation SHA and independent milestone reviewer verdict where scheduled; no unowned compiler contract gap; push consolidated branch; no direct main merge.

Use `docs/AGENT_TEAM_EFFICIENCY.md` for one lightweight closeout record: actual worker count, PRs, reviewer loops, net-new proven defects, time to green and provider-reported tokens/cost if available. Unknown remains unknown. Do not create status bureaucracy or fabricate metrics.

## 6. Avoid repetitive prompts

Every new Team session reads `AGENTS.md`, then this file. Phase-specific Teams read their single execution file and live `.codex/CURRENT_STATUS.md`. A kickoff needs only the branch, execution-file path and target mission. LLM prompts should describe desired code behavior, acceptance contracts, and decision boundaries; scripts and CI define routine build/test invocations.

When the Coordinator/Reviewer agent is shared, record which review was self-review. An independent adversarial reviewer is still required at the major checkpoints above.
