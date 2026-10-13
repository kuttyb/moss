# Moss Team organization efficiency tracking

**Scope:** All multi-agent engineering Teams. **Owner:** L6 Coordinator.
**Purpose:** Measure the engineering value of leaf-agent delegation, Coordinator integration review, and independent review without creating a second bureaucracy.
This is **not** `agent-feedback/` (Moss developer experience) or SWARM feedback.

## Operating rules

- Reuse Git/PR/worktree records, existing review notes, issue IDs, test reports, and **provider-reported** usage. Do not require leaf-agent status reports, per-prompt logging, new telemetry, or a PR workflow solely for metrics.
- Record at most one compact row per actual submission and per independent review event; reconstruct cheaply at closeout when existing records suffice.
- **No evidence ≠ zero.** Use `unknown` for unobservable counts or costs, `0` only when records substantiate no events, and `not applicable` for inapplicable fields. Distinguish plans and role labels from observed activity.
- The Integrator preserves Teams' reports, but is not required to repeat their organizational audit. Historical Teams need not retroactively reconstruct unavailable information.

## 1. Roles and headcount: do not double-count

| Role | Count as leaf agent? | Count its review where? |
| --- | --- | --- |
| **L6 Coordinator / Manager** | **No**, even if the Coordinator directly fixes code | Coordinator reviews only |
| **Independent L6 Architect** | **No**, even if it runs tests or suggests patches | Architect review events |
| **Independent adversarial reviewer** | **No** when tasked as an independent reviewer | Adversarial review events (separate from Architect) |
| **Leaf implementation, testing, benchmarking, or oracle-writing sub-agent** | **Yes** only when an actual separately instantiated worker carried out that leaf assignment | Its submissions; leaf reviews only if explicitly assigned reviewer duties |
| **L7 user/ChatGPT or external L7 peer reviewer** | **No** | L7 reviews and L7-origin defects, never Coordinator or Architect |
| **Team I / external integrator** | **No** for the source Team | Integration review, separate from source-Team Coordinator |

**Count distinct actual agent instances that executed work**, not planned slots (e.g., `N1–N8`), task names, simultaneous worktrees, messages, or the Coordinator/Architect. Give a short evidence basis (agent-platform session IDs, worktree handoff records, or `unknown`); avoid private IDs in public reports. If one instance performs multiple workstreams, count it once. If separate instantiated replacements take over, count each actual instance.

**Peak concurrent leaf agents** means the greatest number of such leaf workers demonstrably active at the same time. The Coordinator and reviewers never inflate leaf concurrency. If active intervals cannot be established, report `unknown`; do not equate role slots or sequential submissions with concurrency.

## 2. Submission, review, and repair units

- **Actual PR:** a GitHub pull request with a real PR number/URL. Count once per PR, even with multiple commits and review rounds.
- **Non-PR submission:** a distinct worktree/branch/commit handoff explicitly requesting Coordinator review or integration. Count once per requested handoff/review unit, not per push, commit, worktree creation, or feature branch lifetime.
- Classify each submission as `PR` or `non-PR handoff`, **never both**. Report counts separately, plus integrated/rejected/deferred outcomes. A final direct branch delivery is at most one handoff unless evidence identifies earlier distinct submissions.
- **Coordinator review:** an actual inspection with a recorded review decision or material feedback by the L6 Coordinator. An automatic merge, L7 review, Architect review, or merely mentioning a review is not evidence that the Coordinator reviewed.
- **Review-and-repair round:** one Coordinator change request followed by a revised submission for that same review unit. Count `0` when accepted without revisions, `unknown` when the history is missing. Architect/L7 feedback loops are counted **separately**; do not label them Coordinator cycles.
- **Architect event:** one distinct review session or milestone intervention with an identifiable scope and reviewed commit SHA. A multi-message review is one event. Distinguish planned vs unplanned and initial NO-GO vs later GO. Count separate adversarial-reviewer and L7 events in their own categories.
- **Coverage:** reviewed submissions / all submissions. Unknown review status stays in the denominator and is disclosed; do not silently treat unknowns as either reviewed or unreviewed.

A minimal event ledger is sufficient:

**Submission:** PR number/link or handoff SHA | actual leaf agent/workstream (or Coordinator-authored) | PR/non-PR | Coordinator reviewed yes/no/unknown | Coordinator change-request/resubmission rounds | outcome.

**Review:** reviewer role (Coordinator / Architect / adversarial / L7) | planned/unplanned | source commit SHA and scope | verdict | **new** confirmed defect IDs | independently verified earlier issues | duplicates/false positives | actual usage if available.

Do not create extra review events from documentation updates.

## 3. Defect discovery, attribution, and verification

Treat a **defect** as a unique, actionable issue with a minimal reproducer or equivalent source/test evidence. Keep one issue identity across repeated reports and repair cycles.

For each material defect, record:

`ID | severity | first discoverer (leaf / Coordinator / Architect / adversarial / L7 / test/tool / unknown) | first evidence/review SHA | confirmed? | fixed commit | regression/verification evidence | status`

Rules:

1. **First discovery gets credit**. A later Architect sign-off that validates a bug first found by L7 is **verification**, not Architect discovery. Similarly L7 review is not a Coordinator review.
2. An independent Architect discovery requires traceable new evidence, not a statement that the Architect "reviewed all fixes." Distinguish net-new Architect findings, verified preexisting findings, duplicates, false positives, and unconfirmed concerns. Record multiple independent discoverers only when independent discovery is evidenced; do not double-count the defect.
3. An issue is `confirmed` when evidence demonstrates the failure, and `fixed` only with a linked implementation change plus a passing regression/reproducer (or equally concrete verification). A planned fix, updated narrative, or approval alone does not mean fixed.
4. Preserve NO-GO and failed-review history. Do not rewrite earlier reviews into a single final GO.
5. Counts of "defects resolved by Team" and "defects first discovered by Architect" are **different measures**. Do not credit L7-provided findings to the Architect or Coordinator.

## 4. Test counts, benchmark claims, and exact-SHA sign-off

- Count **unique declared/executed test cases** per package or suite and calculate the total arithmetically from actual files/test-runner output. Separate unique tests from their **executions across modes**. For example, 9 + 9 + 9 + 3 = **30 unique tests**; running those in three modes does not create 90 unique cases. Do not mix package tests, differential-oracle scenarios, compiled-only acceptance, and repository-wide tests into an unexplained "total."
- In the closeout, show `suite | unique tests | Native Debug | Fast Debug | Native Release | source evidence`. If a count in a status report disagrees with source/test output, correct it before sign-off. Do not report "31" or "34" without traceable cases.
- Benchmark comparisons must state actual observed inputs/outputs, equivalent timed work, optimization/provider profile, Rust baseline version/source, and limitations. An allocation count or a `black_box(len)` alone may not force the payload computation. Mark unsupported ratios `unvalidated`, not as parity.
- **Every attributable Architect GO/NO-GO must state the full exact reviewed *implementation* SHA.** Never use "latest branch", "see final commit", or an unspecified moving ref. Include review scope and regression evidence.
- If delivery HEAD differs from the reviewed implementation SHA, record both and list the intervening commits. A docs/feedback-only change may preserve the exact-SHA implementation approval **only after a diff confirms no code/tests/benchmarks/ABI/semantics changed**. Any implementation or acceptance-gate change requires review of the new implementation SHA. Do not backdate a review or claim an Architect saw a later commit.

## 5. Required compact Team closeout

Create `docs/<phase>/agent-patterns/<team-name>.md` (e.g., `docs/phase24/agent-patterns/team-n-numerics.md`) with these **literal categories**, not prose substitutions:

| Category | Report |
| --- | --- |
| **Leaf agents** | Actual unique count; evidence; planned slots separately, if relevant |
| **Peak leaf concurrency** | Actual peak with evidence, or `unknown` |
| **Non-leaf reviewers/managers** | Coordinator count; Architect count; independent adversarial reviewer count |
| **Submissions** | Actual PR count; actual non-PR handoff count; integrated/rejected/deferred |
| **Coordinator reviews** | Reviewed/submitted coverage; Coordinator-only average and max repair rounds |
| **Other review events** | Architect planned/unplanned; adversarial; L7, **separately** |
| **Defects** | Net-new confirmed by first discoverer (Architect, Coordinator, adversarial, L7, other); duplicates/false positives; fixed vs open; links |
| **Test evidence** | Per-suite unique test arithmetic, modes run, actual outputs/links; no contradictory totals |
| **Review provenance** | Exact reviewed implementation SHA, review verdict, final delivered SHA, intervening-diff classification |
| **Usage** | Actual tokens and/or billed costs by role when supported, provider/report source; otherwise `unknown` |
| **Value** | 2–4 evidence-grounded sentences on unique useful review findings versus redundant review/rework |

Use the two compact event schemas in §2 and defect references in §3 only to the extent existing records support them. A single table plus a few linked records is preferable to a long narrative. **Completing this table is not permission to invent data**; unknowns are valid and expected.

### Closeout self-check (Coordinator, once)

Before publishing, confirm that:

- [ ] Coordinator, Architect, adversarial reviewers, L7, and leaf workers are classified separately; no planned role is counted as an actual leaf worker.
- [ ] PRs and non-PR handoffs are distinct; Coordinator rounds cannot include L7/Architect cycles.
- [ ] Each unique defect has an attributable first discoverer and fix evidence, or `unknown`; verification is not counted as discovery.
- [ ] Unique test totals equal the sum of the listed suites, with mode executions shown separately.
- [ ] Architect's reviewed implementation SHA and delivery SHA are explicit; any intervening changes are classified by diff.
- [ ] Unknown costs/headcounts/reviews remain `unknown`, not estimated or reported as zero.

No additional review ceremony, CI gate, issue tracker, or leaf-agent reporting is required solely for this policy.
