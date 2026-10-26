# Phase 26 W1: checker parse cache

## Implementation and scope

W1 is on `phase26-moss-speedup`, with `1252eaa6afbfcf9c917ac71ab93a3785790d9a63`
as its first pushed code checkpoint. One `Checker` owns a bounded syntax cache.
`split_binary` keys by exact text and the ordered operator list;
`split_top_level` keys by text and delimiter. Successful simple and member
calls, and redundant-outer-parenthesis stripping, reuse immutable results
through copied returns. Failed call parses still execute the shared parser so
partial output-argument mutations stay identical. A 32,768-entry cap bounds
retained parse results per check. Profiling emits `parse_cache_hits` and
`parse_cache_misses`.

`trim` is not cached in this candidate: its short-string operation may cost
less than a hash lookup. W1's measured target is repeated expression parsing,
especially `split_binary`. This scope decision remains subject to the final
performance and architecture review.

## Preliminary measurement

Both binaries were built with the repository's `moss-opt` -O2 flags on the
same machine. The pre-edit binary was built from code identical to `main`
`c66fb76ca0012b8125d70d404adbff13daa1cf4e`; Phase 26's plan-only
commit was the sole branch difference. Three serial paired trials alternated
baseline and W1 binaries, with identical `check <fixture> --json` commands.

| Workload | Baseline median | W1 median | Change |
| --- | ---: | ---: | ---: |
| W3 repeated specialization N32 | 2.5343 s | 1.8371 s | 27.5% lower |
| W5 wide inference W100 | 10.8669 s | 9.0077 s | 17.1% lower |

The full B1 benchmark runner then completed 77 fixtures in each of timing and
profiling modes for both binaries (154/154 successful trials per binary,
one repetition per mode). Its untraced -O2 timing results on the same six
large workloads were:

| Workload | Main | W1 | Change |
| --- | ---: | ---: | ---: |
| W2 distinct specialization N32 | 3.332 s | 2.719 s | 18.4% lower |
| W3 repeated specialization N32 | 2.353 s | 1.760 s | 25.2% lower |
| W4 deep inference D18 | 26.206 s | 24.741 s | 5.6% lower |
| W5 wide inference W100 | 10.763 s | 8.980 s | 16.6% lower |
| Regex package | 19.655 s | 18.803 s | 4.3% lower |
| Datastructures SDK | 21.375 s | 19.681 s | 7.9% lower |

The sum of the 77 untraced workload timings was 94.172 s on main and
85.881 s on W1 (8.8% lower). This is a one-repetition benchmark sum, not
an end-to-end elapsed time or an uncertainty interval. Twenty-one individual
fixtures measured slower, mostly in the 1–6 ms range; the three-trial paired
W3/W5 results above are the more reliable target-workload measurements.
The timing-mode peak RSS changed from 13,900 to 16,112 KiB on W5, 25,160
to 28,008 KiB on regex, and 21,728 to 23,604 KiB on datastructures. The
cache trades a few MiB on these workloads for shorter checks.

An earlier single pass across eight representative workloads returned
identical exit codes and exact stdout/stderr SHA-256 hashes for baseline and
W1 on all eight: W2 N32, W3 N32, W4 D18, W5 W100, regex,
datastructures, rolling window and checkout. The W3 profiling run reported
9,350,195 hits and 1,782,615 misses. These counters count checker cache
lookups, including misses after the cap and failed call parses.

## Gates and limitations

The B1 runner's report-only pass succeeded after correcting a preexisting
aggregation bug that read the specialization count from a timing row. The
checker cache regression fixture passes with Fast Debug output and the expected
negative diagnostic. Full corpus output parity, regression suites, optimized
examples, ABI-v10 gates and final Architect review are in progress. Callgrind
is unavailable on this machine; the eight-workload
instruction attribution requested by the Phase 26 plan is currently blocked.
No merge to `main` is authorized by this W1 record.
