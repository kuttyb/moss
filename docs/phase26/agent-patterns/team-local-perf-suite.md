# Local compiler performance suite follow-up

| Category | Report |
|---|---|
| **Leaf agents** | 0; coordinator implemented in the shared checkout. |
| **Peak leaf concurrency** | 0. |
| **Non-leaf reviewers/managers** | Coordinator: 1; Architect: 1; independent adversarial reviewer: 0. |
| **Submissions** | One PR (#20) against `main`; open and ready. No integration outcome yet. |
| **Coordinator reviews** | Two coordinator reviews at major implementation checkpoints; repaired both Architect NO-GO findings before final review. |
| **Other review events** | Architect checkpoint 1 returned NO-GO on `a8016f14d6be94d53534bfcc0ff64c24ae9f8009`, finding missing no-baseline workload reporting, misleading nested B1 reports, stale Make help text, and trailing blank line; those were repaired. Architect checkpoint 2 returned NO-GO on `6817b59ab37711fd4e1ec8c942e07652a25d5b23`: certification accepted partial workloads/repetitions without a baseline. The repair now enforces c66fb76c, 77 fixtures, and three repetitions. The 2 ms non-overlap floor follows the user's explicit threshold request. Final review is pending; independent adversarial reviewer: 0. |
| **Defects** | Four harness defects/limitations found: timing-only reports lacked counters; isolated output self-copied; old baseline runner lacked the new CLI; certification could label a partial run. The first three and certification gap are repaired. CPU attribution is rounded to 10 ms, and pre-Phase 26 baseline counters are sparse; both limits are disclosed. No compiler behavior defects. |
| **Test evidence** | Certified command `MOSS_PERF_CERTIFY=1 MOSS_PERF_INCLUDE_REAL_WORLD=1 MOSS_PERF_BASELINE=c66fb76c make compiler-perf` completed run `20261026-111935`: 77 fixtures; 231 timing and 77 profile trials per compiler, all successful; 97.078 s baseline vs 14.877 s candidate. Candidate checkout was clean, and its compiler source matched remote `main` at `bdf973af651deb2d3a4deac2b6c4bcc60893a776`. Python syntax, feedback validation, and `git diff --check` pass after current edits. Hosted PR #20 run #24 was still in progress at report time. No compiler source changed. |
| **Review provenance** | Architect GO for earlier implementation SHA `abe436d9036b6f81774b2c55df3cf36874c3d220`; current final review pending the repair and green hosted run. |
| **Usage** | Unknown. |
| **Value** | The local target avoids overwriting historical B1 reports and gives developers paired medians, resources, and per-fixture work counters. The certified full corpus establishes a durable Phase 26.1 reference; the historical counter and CPU timer limits are explicit. |
