# Local compiler performance suite follow-up

| Category | Report |
|---|---|
| **Leaf agents** | 0; coordinator implemented in the shared checkout. |
| **Peak leaf concurrency** | 0. |
| **Non-leaf reviewers/managers** | Coordinator: 1; Architect: 1; independent adversarial reviewer: 0. |
| **Submissions** | One PR (#20) against `main`; open and ready. No integration outcome yet. |
| **Coordinator reviews** | Three coordinator reviews at major implementation checkpoints; repaired each Architect NO-GO finding before final review. |
| **Other review events** | Architect checkpoint 1 returned NO-GO on `a8016f14d6be94d53534bfcc0ff64c24ae9f8009`, finding missing no-baseline workload reporting, misleading nested B1 reports, stale Make help text, and trailing blank line; those were repaired. Checkpoint 2 returned NO-GO on `6817b59ab37711fd4e1ec8c942e07652a25d5b23`: certification accepted partial runs. The guard now requires c66fb76c, 77 fixtures, and three repetitions. Final review at `93ed4bdd` returned NO-GO because absent CPU/RSS/counters for expected-negative fixtures rendered as zero. Parsing/rendering repair is underway; the 2 ms non-overlap floor follows the user's explicit request. |
| **Defects** | Five harness defects/limitations found: timing-only reports lacked counters; isolated output self-copied; old baseline runner lacked the new CLI; certification accepted partial runs; GNU time's failure preamble hid resource values for expected-negative inputs and missing values rendered as zero. Repairs are in progress for the last issue. CPU has 10 ms resolution and pre-Phase 26 counters are sparse; these limits are disclosed. No compiler behavior defects. |
| **Test evidence** | Certified command completed run `20261026-111935`: 77 fixtures; 231 timing and 77 profile trials per compiler; 97.078 s baseline vs 14.877 s candidate. Candidate checkout was clean and source matched remote `main` at `bdf973af651deb2d3a4deac2b6c4bcc60893a776`. Architect accepted wall-time arithmetic, certification coverage and gate status but rejected report accuracy for nine negative fixtures. Updated run.py parser and n/a rendering are being validated; the full run must be repeated before accepting the committed resource/counter data. Hosted PR #20 run #25 passed core regressions, optimized examples, and perf smoke on prior SHA `93ed4bdd`. |
| **Review provenance** | Earlier implementation SHA `abe436d9036b6f81774b2c55df3cf36874c3d220` received GO; later checkpoint/final reviews found certification and report-accuracy issues, now being repaired. |
| **Usage** | Unknown. |
| **Value** | The local target avoids overwriting historical B1 reports and gives developers paired medians, resources, and per-fixture work counters. The certified full corpus establishes a durable Phase 26.1 reference; the historical counter and CPU timer limits are explicit. |
