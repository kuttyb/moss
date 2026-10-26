# Phase 26.0.1 C3 — Negative call-parse cache

## Decision from baseline counts

Baseline was `274f848502df08f62b69c54c614ad9f05a90a3ec`. Miss classification used bounded distinct-failure-spelling sets, with the existing cache cap of 32,768. None of the measured checkers approached that cap, so it remains unchanged.

| Workload / checker | Total parse misses | Successful cache inserts | Failed simple-call misses | Failed member-call misses | Post-cap misses | Distinct failed simple/member spellings |
|---|---:|---:|---:|---:|---:|---:|
| W4 D18 | 17,866,873 | 834 | 11,555,975 | 6,310,064 | 0 | 23 / 41 |
| regex (per checker) | 540,689 | 13,423 | 283,610 | 243,656 | 0 | 643 / 689 |
| datastructures (per checker) | 293,371 | 9,088 | 162,613 | 121,670 | 0 | 425 / 511 |

These counts justify caching failed simple-call and member-call parses. The cache stays per Checker, shares the original entry budget, and does not increase the cap. A failed simple-call parse does not mutate outputs. A failed member-call parse can write receiver and member before rejecting an empty side; the cached result records whether those outputs were written and replays them, while leaving arguments as the parser did.

## Candidate counter result

| Workload | Baseline misses | Candidate misses | Candidate entries / cap | Candidate failed simple/member parser executions | Negative cache hits simple/member |
|---|---:|---:|---:|---:|---:|
| W4 D18 | 17,866,873 | 898 | 898 / 32,768 | 23 / 41 | 11,555,952 / 6,310,023 |
| regex (two checker instances, aggregate) | 1,081,378 | 29,510 | 14,755 / 32,768 per checker | 1,286 / 1,378 | 565,934 / 485,934 |
| datastructures (two checker instances, aggregate) | 586,742 | 20,048 | 10,024 / 32,768 per checker | 850 / 1,022 | 324,376 / 242,318 |

All profiled inputs stayed below the cache cap. The current cap therefore covers the observed positive and negative key set; raising it is not justified. The aggregate regex/datastructures rows combine two independent Checker profiles; the per-checker measurements are identical for these parser counters.

In the candidate, `parse_cache_hits` includes negative-cache hits; it counts cache lookups served, not only successful parser results.

## Validation and performance

- Full tracked-corpus differential: 2,064 `.moss` sources, 1,325 accepted and 739 rejected; zero verdict, diagnostic-code, or full SHA-256 generated-Rust differences against 274f848.
- Fast Debug corpus parity: passed.
- `make check`: 141/141 jobs passed, including the new negative-cache replay/profile regression.
- `MOSS_PERF_BASELINE=274f848 TMPDIR="$PWD/tmp" make compiler-perf`: run `20261026-150611`, 57 fixtures, 3 interleaved timing repetitions and one profiling sample. Aggregate fixture-median sum 14.645 s → 14.118 s (-3.6%). W4 D18 improved from 7.190 s to 6.828 s (-5.0%). No fixture exceeded the suite's slowdown noise floor. W8 C10 was +7.1% / +0.62 ms and remained below the absolute noise threshold.
- Eight attribution workloads (baseline → candidate wall medians):

| Workload | Baseline | Candidate | Change |
|---|---:|---:|---:|
| example checkout | 3.99 ms | 3.81 ms | -4.4% |
| regex | 1,480.76 ms | 1,421.68 ms | -4.0% |
| rolling window | 98.50 ms | 99.70 ms | +1.2% |
| datastructures | 562.77 ms | 546.80 ms | -2.8% |
| W2 distinct N32 | 1,909.49 ms | 1,901.85 ms | -0.4% |
| W3 repeated N32 | 988.31 ms | 958.03 ms | -3.1% |
| W4 deep D18 | 7,190.03 ms | 6,828.18 ms | -5.0% |
| W5 wide W100 | 133.76 ms | 133.11 ms | -0.5% |

The performance run used this WSL2 host (16 logical CPUs); load varied during the run, so small percentage changes are not treated as gains. The targeted D18 improvement and repeated-work counters support keeping the change.
