# Local compiler performance suite follow-up

| Category | Report |
|---|---|
| **Leaf agents** | 0; coordinator implemented in the shared checkout. |
| **Peak leaf concurrency** | 0. |
| **Non-leaf reviewers/managers** | Coordinator: 1; Architect: 1; independent adversarial reviewer: 0. |
| **Submissions** | One PR (#20) against `main`; open. No integration outcome yet. |
| **Coordinator reviews** | One self-review of the candidate diff; no repair rounds. |
| **Other review events** | Architect checkpoint 1 returned NO-GO on `a8016f14d6be94d53534bfcc0ff64c24ae9f8009`, finding missing no-baseline workload reporting, misleading nested B1 reports, stale Make help text, and trailing blank line. These were addressed; final Architect review of the updated candidate is pending. Independent adversarial reviewer: 0. |
| **Defects** | Three B1 harness issues found during execution (timing-only report lacked counters; isolated output self-copy; old baseline runner lacked the new CLI), fixed by timing-aware reporting/output suppression and staging the current benchmark-only runner into the temporary baseline worktree. Architect also found two report-accuracy issues and two documentation issues, all repaired. No compiler behavior defects. |
| **Test evidence** | Updated `make compiler-perf`: 57 fixtures × 3 timing repetitions, 171/171 successful. Updated paired `MOSS_PERF_BASELINE=HEAD MOSS_PERF_REPS=1 make compiler-perf`: 57 successful trials per side; report includes eight attribution medians, small-fixture latency, and >5% rows, with no nested historical reports. Python syntax, comparison-report assertions, feedback validation, and `git diff --check` passed. Main-PR core, optimized-example, and local performance gates are now configured; hosted results pending. No compiler source changed. |
| **Review provenance** | Architect review NO-GO is recorded above for exact SHA `a8016f14d6be94d53534bfcc0ff64c24ae9f8009`; updated implementation SHA, final verdict, delivery SHA, and hosted CI are pending. |
| **Usage** | Unknown. |
| **Value** | The new local target avoids overwriting certified B1 reports and gives developers a paired median comparison with raw trials. The historical runner exposed two timing-only/report-path assumptions, and the paired smoke run caught an older CLI compatibility gap before delivery. |
