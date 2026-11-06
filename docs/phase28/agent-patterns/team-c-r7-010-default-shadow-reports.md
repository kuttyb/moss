# Team C R7-010 Default/Shadow regression report closeout

Scope: evidence-only comparisons of the saved Default and Shadow regression
logs for source `d4aa51111d62917c4a1caa180266291862c5b88d` against the
`phase28-work` baseline `150ab36b41e45504aaa400aa717da241be7542fb`.

| Category | Report |
|---|---|
| **Leaf agents** | 0 observed; this report cycle was authored by the Team C coordinator, with no separately instantiated leaf worker evidenced in the branch activity. Planned slots: not applicable. |
| **Peak leaf concurrency** | 0 observed for this report cycle; no leaf worker was active. |
| **Non-leaf reviewers/managers** | Coordinator: 1 (Team C report author). Architect: 0 documented. Independent adversarial reviewer: 1 (Agent 2, as reported by L7). L7: 1, recorded separately below. |
| **Submissions** | PRs: 0. Non-PR handoffs: 1 report handoff with two report commits (`ad74557afd1bccbeb49a51bae2d3b8eae1daf30d`, `4dc7fbd5de563484434fd8656950e92e5a33efa7`). Outcome: delivered for L7 staging review; no implementation integration claimed. The later wording correction is a revision within this handoff, not an additional submission. |
| **Coordinator reviews** | 1/1 handoff checked by the coordinator for exact failure-ID set, counts, digests, and report formatting. Coordinator change-request/resubmission rounds: 0. The independent-review wording correction and L7 request are tracked separately below. |
| **Other review events** | Architect: 0 documented. Adversarial: 1 Agent 2 review event; GO on the two report commits, with a wording correction requested to separate the unused-local compile-hygiene failure from fixture SHA integrity failures. L7: 1 follow-up directing that correction and requesting this closeout record. |
| **Defects** | No compiler defect was discovered or repaired in this evidence-only cycle. One report-classification issue was identified by adversarial review and corrected: the unused `local_transfer` `-Werror` failure is compile hygiene, not a runner defect; fixture SHA mismatches are a separate integrity finding. No implementation issue is marked fixed here. |
| **Test evidence** | No builds or tests were launched by Team C in this cycle. The reports parse the completed saved logs, each containing 167 selected jobs. Default: candidate 90 PASS/76 FAIL/1 SKIP; baseline 166/1/0. Shadow: candidate 90/76/1; baseline 165/2/0. Exact added/removed IDs and log SHA-256 values are in the [Default report](../team-c/P28_R7_010_DEFAULT_REGRESSION_DELTA_D4AA5111.md) and [Shadow report](../team-c/P28_R7_010_SHADOW_REGRESSION_DELTA_D4AA5111.md). The two runner results are evidence inputs, not tests executed for this report submission. |
| **Review provenance** | No implementation SHA was reviewed or changed. Agent 2 GO covered report commits `ad74557afd1bccbeb49a51bae2d3b8eae1daf30d` and `4dc7fbd5de563484434fd8656950e92e5a33efa7`; the wording correction changes reports only. Final corrected report revision: `d62f2224ff4d2d87435ddfabb58fd1c2cf911fec`. This closeout is a subsequent documentation-only addendum and makes no claim of implementation approval. |
| **Usage** | `unknown`; no provider-reported token or cost evidence was available. |
| **Value** | Exact job-ID comparisons found 75 new Default failures and 74 new Shadow failures, with no removed failures in either comparison. The reports preserve the inherited failures, dependent skip, source/log identities, and runner differences, while the review correction keeps compile hygiene distinct from fixture integrity. These are preliminary regression checkpoints and do not replace the pending frozen-source oracle and provenance audit. |

