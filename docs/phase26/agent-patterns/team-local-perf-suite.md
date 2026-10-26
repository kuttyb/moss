# Local compiler performance suite follow-up

| Category | Report |
|---|---|
| **Leaf agents** | 0; coordinator implemented in the shared checkout. |
| **Peak leaf concurrency** | 0. |
| **Non-leaf reviewers/managers** | Coordinator: 1; Architect: 1; independent adversarial reviewer: 0. |
| **Submissions** | One PR (#20) against `main`; open and ready. No integration outcome yet. |
| **Coordinator reviews** | Three coordinator reviews at major implementation checkpoints; repaired each Architect NO-GO finding before final review. |
| **Other review events** | Architect checkpoint 1 returned NO-GO on `a8016f14d6be94d53534bfcc0ff64c24ae9f8009`, finding missing no-baseline workload reporting, misleading nested B1 reports, stale Make help text, and trailing blank line; those were repaired. Checkpoint 2 returned NO-GO on `6817b59ab37711fd4e1ec8c942e07652a25d5b23`: certification accepted partial runs. The guard now requires c66fb76c, 77 fixtures, and three repetitions. Final review at `93ed4bdd` returned NO-GO because expected-negative CPU/RSS/counters rendered as zero. GNU time parsing and n/a rendering are repaired, and the full certification run has been repeated. The 2 ms non-overlap floor follows the user's explicit request; final review pending. |
| **Defects** | Five harness defects/limitations found: timing-only reports lacked counters; isolated output self-copied; old baseline runner lacked the new CLI; certification accepted partial runs; GNU time's failure preamble hid resource values for expected-negative inputs and missing values rendered as zero. All are repaired. CPU has 10 ms resolution and pre-Phase 26 counters are sparse; those limits are disclosed. No compiler behavior defects. |
| **Test evidence** | Repaired certified run `20261026-115031`: 77 fixtures; 231 timing and 77 profile trials per compiler; all succeeded; 98.437 s baseline vs 15.071 s candidate. CPU/RSS rows are populated for every fixture; expected-negative work counters are explicitly n/a. Candidate checkout was clean and source matched remote `main` at `bdf973af651deb2d3a4deac2b6c4bcc60893a776`. `py_compile`, feedback validation, diff check and focused expected-negative resource/counter probes pass. Hosted run #26 was in progress on parser fix SHA `6deefa96`; exact final-artifact gates pending. |
| **Review provenance** | Earlier implementation SHA `abe436d9036b6f81774b2c55df3cf36874c3d220` received GO; the certification guard and resource-report corrections addressed subsequent NO-GO findings. Final review of refreshed artifacts remains pending. |
| **Usage** | Unknown. |
| **Value** | The local target avoids overwriting historical B1 reports and gives developers paired medians, resources, and per-fixture work counters. The certified full corpus establishes a durable Phase 26.1 reference; the historical counter and CPU timer limits are explicit. |
