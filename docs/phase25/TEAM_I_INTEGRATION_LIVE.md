# Team I live integration record

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

ABI worker stopped at L7 instruction. Sole L4 inherits its frozen work serially:
strict PR #6 `ccf2ca7c6628882617ad5f830fce19b463bcce23`, source-free PR #8
`9f1ca499f57f935d34b9462c252f12043cdc677c`. Historical two-L5 concurrency
below describes earlier observed work, not current staffing authorization.


L7 authorized execution on 2026-10-18. Target `phase25-integration`; initial
SHA `fbd2f729caf1a96c3ffe184068e88ba6b09288cc`. No main merge or ABI production
cutover. Coordinator also serves as L6 PR Reviewer; independent milestone
adversarial review remains separate.

## Intake and dependency DAG

Verified after `git fetch origin`: H/T `2d457245061c75f3a4652a8851134322f1fa8445`,
G `2938d257bf10bbb11bd221fa14237ddcd1e0702b`,
S `53d04fbf48c007d912a47836959c488bcbb6c450`,
B `755f13172c37447bca981d1fefbb885ec46f3a59` match the execution plan.
H/T intake first, followed by distinct G, S, B deliveries on fresh branches
from the reviewed target. Historical prep rehearsal is not current evidence.
H1/H2/H3 and all release gates remain open until executable integrated proof.

## File leases

| Owner | Exclusive write scope | Branch/worktree |
| --- | --- | --- |
| L5 compiler | `src/moss.cpp`, `src/ast.hpp`, `src/type_ir.hpp`; compiler intake and associated tests | `phase25-i-h-intake`, `tmp/phase25-i-compiler` |
| L5 ABI/harness | ABI headers, runtime, `tests/phase25/integration/**`; initially audit and independently justified harness fixes | `phase25-i-abi-preparation`, `tmp/phase25-i-abi` |
| L6 Coordinator | root status, organizational guidance, this record, integration/review provenance | root checkout |

Whole-history H intake includes ABI files; runtime/harness worker must avoid
editing those files until their baseline interface is handed off. No concurrent
compiler writers. Each worker owns its feedback workstream. Two actual L5
agents instantiated; no optional third worker.

## Evidence and gates

Startup bootstrap returned `moss-0.1`. GitHub connector repository access
confirmed; `gh` CLI absent, connector supports draft PR and workflow inspection.
AGENTS already explicitly permits authorized normal push on the fetched branch;
no prohibiting rule remains. Push must preserve published history.

No PR review, integration, regression PASS or final acceptance is claimed yet.
Required CI runs four isolated groups; semantic gates missing before intake are
blocked, never green. Final strict 62-gate release is distinct from PR checks.

## Initial live findings

- H/T merge reports no compiler conflict; documentation conflicts are current
  status and three architecture documents. These still require deliberate
  reconciliation and regression evidence before integration.
- ABI worker identified the strict CLI bypassing `validate_gate_evidence`; a
  bounded harness integrity repair is in progress. Cross-crate source-removal
  coverage requires additional production-path work; existing status is not E3.

## Contract interpretation

The execution plan assigns typed ABI payloads, runtime linkage, Gate 10 and
source-free specialization to Team I. Historical D3/D4 ownership questions do
not authorize deferral and do not require a new language-design decision.
ABI-v10 production default cutover awaits L7 final acceptance; staging must
exercise the actual v10 emitter/importer/linker with source removal. Fail-closed
rejection is required for negative cases but is not positive H3/E3 evidence.

## Open integration issues

| ID | First evidence / owner | Acceptance consequence |
| --- | --- | --- |
| I-001 | L5 ABI audit: strict CLI bypasses evidence validator | Repair + CLI fault regression; no narrative-only strict acceptance |
| I-002 | L5 ABI audit: cross-crate scenarios 2–5 remain source-backed, 6 lacks execution, 7 checks name substring | Replace with real source-removal, link/run and exact provenance evidence before E3 |
| H1–H3 | Inherited L7 execution-plan findings | Team I owns structured requirements, stale AST projection and true generic source-free proof |

These are open implementation/validation defects, not architectural escalation.

## Inherited H2 live reproduction

Coordinator verified against H/T intake header before any authority fix:
construct each `Param`, `Method`, `TraitMethod`, `Handler`, `Function`; bind
`Type::primitive("int")` via its `bind_type`, `bind_return_type` or
`bind_reply_type`; erase the string projection (`clear()`, optional `reset()`
or assign empty string); call `assert_consistent()` and count `logic_error`.
Built with `g++ -std=c++17 -O0 -I tmp/phase25-i-compiler/src`.
Observed `rejected erased projections: 0/5`, exit 1 (expected 5/5).
Source cause: assertions guard on nonempty/present projection before comparing
against bound TypeRef. This verifies inherited L7 H2; no new discovery credit.
Focused regression and semantic/ABI boundary enforcement remain required.

## Submission ledger

| Submission | Exact implementation SHA | State | Review |
| --- | --- | --- | --- |
| [H/T intake PR #5](https://github.com/kuttyb/moss/pull/5) | `5cfdc1630d67506146e4adcc52628e508ffdc7c5` | Draft; automated workflow 39165091036 running all four isolated jobs | Not requested; CI pending |

Worker-local optimized build/bootstrap and G1/G2/G3 checks passed on H intake;
these do not substitute for PR checks. Coordinator confirmed four separate CI
jobs in progress. No target merge yet.

## Recovery checkpoint policy

L7 requested periodic crash/token recovery on 2026-10-18. Coordinator commits
and pushes `.codex/CURRENT_STATUS.md` at meaningful boundaries (PR submission,
review, integration or new validated blocker). Worker branches are committed and
pushed independently; target remains unchanged until required CI/L6 approval.
PR #6 latest recovery head is `ccf2ca7c6628882617ad5f830fce19b463bcce23`
(feedback-only follow-up to `afcf693`); exact-head CI is pending.

## Source-free harness recovery handoff

[PR #8](https://github.com/kuttyb/moss/pull/8), exact head
`ac317c65caecac0d9916d58318106fc13225939e`, makes source removal and
consumer execution real; baseline release is **0 PASS / 1 BLOCKED / 10 FAIL**,
exit 1. It is not integrated/reviewed. ABI worker now owns
`phase25-i-cross-crate` in its existing isolated worktree. Frozen strict #6 remains
`ccf2ca7c6628882617ad5f830fce19b463bcce23`. Its current semantic CI failure is
the accurately blocked missing pre-H G1 suite. H #5 has three green checks,
core still running at checkpoint. Coordinator [PR #7](https://github.com/kuttyb/moss/pull/7)
contains recovery/policy plus full-history CI checkout change; awaiting H target.

## H2 scratch recovery

Sole L4 pushed `0574de41d3c13d6af0448a80ad3708ce7a91e402` on
`phase25-i-h2-erased-projections` (`tmp/phase25-i-h2`). Local full compiler
build/bootstrap and G1/G2/G3 pass; direct regression rejects all 36 empty/reset
boundary cases. No PR/review/integration yet. H #5 remains unchanged while
core CI runs. After H integration, refresh H2 from approved target and open
its draft PR; all new automated gates still required. Actual source details
and next steps are committed in `docs/phase25/H2_DECLARATION_PROJECTIONS.md`.
