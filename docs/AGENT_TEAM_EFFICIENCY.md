# Moss Team organization efficiency tracking

**Scope:** Every Moss multi-agent engineering Team, starting with the next Team assignment.
**Owner:** The L6 Coordinator. **Purpose:** Measure whether sub-agent delegation, Coordinator reviews, and independent Architect reviews deliver engineering value relative to their overhead.
This is separate from `agent-feedback/`, which tracks the experience of using Moss itself.

## Lightweight operating contract

- Use existing PR metadata, Git commits, worktree handoffs, issue/reproducer links, and agent-platform usage data. **Do not require leaf agents to write additional status reports** or log prompts.
- Maintain at most one terse record per accepted/rejected submission and one per Architect review event. Batch-fill from existing records at milestones when cheaper.
- **A submission** is a PR or a worktree/commit handoff requesting Coordinator integration; report real PRs and non-PR handoffs **separately**. Do not manufacture PRs merely to improve tracking.
- **A Coordinator review** counts only when the Coordinator actually inspects a submission and records an acceptance, request for changes, or material feedback. An automatic merge alone does not establish review.
- **One review-and-repair round** is feedback followed by a revised submission; count zero if accepted unchanged. Record count per submission, including repeated rounds.
- **An Architect intervention** is one distinct planned milestone review or unplanned consultation/review, not each message or comment.
- **A new Architect finding** is a distinct, evidence-backed issue not already identified by tests, the Coordinator, or another agent. Distinguish it from duplicate findings, design advice, false positives, and unresolved hypotheses.
- A finding is **fixed** only when a linked commit/change and appropriate verification establish the fix. Track severity and whether a regression test was added.
- Capture observed tokens/cost only when reliable tool/provider usage records exist. Do not estimate or back-solve unavailable tokens. Use `unknown`, not zero, for missing evidence.
- Avoid repeated exhaustive audits solely to collect metrics. If tracking becomes costly, reduce the detail rather than the actual engineering work.

## Minimum records

The Coordinator can use a small table in the final report; a separate machine-readable event log is optional.

**Each submission:** ID/link or commit SHA | leaf agent/workstream | PR or worktree | Coordinator reviewed? (yes/no/unknown) | review-and-repair rounds | merged/rejected/deferred | real defects found by Coordinator (with evidence).

**Each Architect event:** date or milestone | planned/unplanned | reviewed SHA/scope | new confirmed defects with IDs/severity | duplicates/false positives/unconfirmed concerns | fixed with commit/test links | actual tokens/cost if known.

When an issue was independently found earlier, do not attribute its discovery to the Architect. A later Architect review may still verify the fix, but count verification separately.

## Final Team closeout

Create `docs/<phase>/agent-patterns/<team-name>.md` (for example `docs/phase24/agent-patterns/team-n-numerics.md`) and report:

| Measure | Definition |
| --- | --- |
| Unique leaf agents | Distinct implementation/validation sub-agents; exclude Coordinator and Architect |
| Peak concurrent leaf agents | Maximum simultaneous active leaf agents, or `unknown` if not observable |
| PRs / worktree submissions | Two separate counts; also state merged/rejected/deferred |
| Coordinator review coverage | Number reviewed / number submitted, with unknowns explicit |
| Review-and-repair rounds | Mean and maximum per submission; do not count unreviewed submissions as reviewed |
| Architect review events | Planned vs unplanned counts |
| Architect net-new findings | Confirmed actionable defects by severity; separately count duplicates and false positives |
| Architect defect fixes | Number resolved with linked code/test evidence; outstanding number |
| Other review findings | Distinct Coordinator-discovered defects, to avoid over-crediting Architect |
| Usage/cost | Measured tokens and cost by agent role when available; otherwise `unknown` |
| Value assessment | Brief evidence-based statement: useful reviews, redundant reviews, unexpected rework, potential savings |

The assessment should distinguish **discovery**, **verification**, and **duplicate review**. Do not rank Teams solely by number of findings; fewer issues may reflect cleaner implementation. When measured usage exists, compare review cost against confirmed high-impact fixes and avoided rework, not a fabricated return on investment.

The Integrator preserves these reports but does not need to independently audit them. The policy is required for **future Team closeouts**, not retroactive reconstruction of earlier Teams. No new CI gate, telemetry infrastructure, PR workflow, or additional review ceremony is required.
