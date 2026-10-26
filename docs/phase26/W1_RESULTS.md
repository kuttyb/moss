# Phase 26 W1: checker parse cache

## Implementation and scope

W1 is on `phase26-moss-speedup`; implementation SHA
`f6c07e69742620119863aed0a335f93f1c4f8e8e` passed its automated gates. One
`Checker` owns a bounded syntax cache.
`split_binary` keys by exact text and the ordered operator list;
`split_top_level` keys by text and delimiter. Successful simple and member
calls, and redundant-outer-parenthesis stripping, reuse immutable results
through copied returns. Failed call parses still execute the shared parser so
partial output-argument mutations stay identical. A 32,768-entry cap bounds
retained parse results per check. Profiling emits `parse_cache_hits` and
`parse_cache_misses`.

`trim` is not cached: its short-string operation may cost less than a hash
lookup. W1 targets repeated expression parsing, especially `split_binary`.

## Wall-clock measurements

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

The dedicated alternating three-trial comparison measured W3 N32 at
2.3703 s on main and 1.7422 s on W1 (26.5% lower), and W5 W100 at 10.8333 s
and 8.9391 s (17.5% lower). The compiler-performance CI job repeated the
baseline/W1 comparison on the same hosted runner and passed its requirement
that both targets improve.

## Callgrind attribution

Callgrind `Ir` references were measured once for each baseline and W1
compiler on the same eight `moss check <source> --json` workloads. Both
binaries used the repository `moss-opt` `-O2` build; main was built from
`c66fb76ca0012b8125d70d404adbff13daa1cf4e`, and W1 is
`f6c07e69742620119863aed0a335f93f1c4f8e8e`. Profiles used Valgrind 3.22.0
with cache simulation and instruction dumps disabled. The table reports total
`Ir` references and the share attributed directly (exclusive/self cost) to
each named category:

| Workload | Main total Ir | W1 total Ir | Selected parse helpers | Type spelling helpers | String-keyed map table |
| --- | ---: | ---: | ---: | ---: | ---: |
| W2 distinct N32 | 56.361B | 43.840B | 30.80% → 17.18% | 2.61% → 3.36% | 1.24% → 3.30% |
| W3 repeated N32 | 41.728B | 29.897B | 37.56% → 22.73% | 2.46% → 3.43% | 1.18% → 4.35% |
| W4 deep D18 | 493.573B | 454.717B | 26.60% → 16.55% | 1.53% → 1.66% | 0.60% → 4.78% |
| W5 wide W100 | 190.110B | 146.599B | 39.43% → 24.00% | 1.63% → 2.12% | 1.50% → 5.22% |
| Regex | 279.552B | 252.263B | 16.14% → 8.72% | 6.82% → 7.56% | 2.59% → 3.93% |
| Datastructures | 305.652B | 248.257B | 27.85% → 14.84% | 5.13% → 6.31% | 2.01% → 4.41% |
| Rolling window | 2.112B | 1.810B | 25.23% → 14.54% | 5.40% → 6.30% | 1.57% → 4.19% |
| Checkout | 0.150B | 0.123B | 24.21% → 11.56% | 4.92% → 5.96% | 3.35% → 5.58% |

Total `Ir` references fell on all eight workloads, by 7.9%–28.4%. The
selected parse-helper direct-cost share fell on all eight, by 7.42–15.43
percentage points. The named-helper category includes direct costs for
`split_binary`, `split_top_level`, simple/member call parsing,
outer-parenthesis stripping, `trim`, and pipeline-stage splitting. It is a
lower bound on expression-text parsing: costs in inlined helpers and callers
are not included. The type-spelling category includes direct costs for
canonical type parsing and rendering helpers. The string-keyed map-table
category sums direct costs of `std::_Hashtable` functions with `std::string`
keys; other map implementations and inlined map work charged to callers are
excluded. Percentages are each
category's share of that binary/workload's total `Ir` references. They are
instruction-reference attribution, not CPU cycles or wall-clock shares.
The type-spelling category's absolute direct `Ir` count was unchanged between
main and W1 for every workload; its percentage rises because total `Ir` falls.

The new cache lookups increase direct string-keyed-map work, so those shares
rise as total references fall. On W3, direct named parse-helper references
fall by 8.881B while string-keyed-map references rise by 0.809B; total
references fall by 11.831B. On W4, the map category rises from 2.973B to
21.747B references, while total references still fall by 38.855B and the
untraced B1 wall time improves by 5.6%. The 32,768-entry per-checker cap and
observed RSS increases bound this tradeoff on the measured workloads.

An earlier single pass across eight representative workloads returned
identical exit codes and exact stdout/stderr SHA-256 hashes for baseline and
W1 on all eight: W2 N32, W3 N32, W4 D18, W5 W100, regex,
datastructures, rolling window and checkout. The W3 profiling run reported
9,350,195 hits and 1,782,615 misses. These counters count checker cache
lookups, including misses after the cap and failed call parses.

The isolated full corpus differential covered all 1,633 tracked `.moss`
files under `examples/` and `tests/`. Check exit status and exact stdout/stderr
hashes matched for all 1,633 files. All 626 rejected sources had matching
results; all 1,007 accepted sources emitted byte-identical Rust. A separate
Fast Debug run of the positive W1 fixture returned `7`, `8`, `9`, `2`. The five
tracked Moss test files under
`tests/` also produced identical Fast Debug exit codes, stdout and stderr
hashes on baseline and W1.

## Correctness and CI gates

The B1 runner's report-only pass succeeded after correcting a preexisting
aggregation bug that read the specialization count from a timing row. The
full corpus differential covered all 1,633 tracked `.moss` files under
`examples/` and `tests/`: exit status and exact stdout/stderr hashes matched
for all files, all 626 rejected files retained their diagnostic results, and
all 1,007 accepted sources emitted byte-identical Rust. Fast Debug output and
exit status matched on five tracked Moss test files. The new W1 check harness
passed its positive cache-counter check, interpreter output, and negative
`TYPE_MISMATCH` check; the separate Fast Debug run of its positive fixture
returned `7`, `8`, `9`, `2`.

Hosted Actions run [#4](https://github.com/kuttyb/moss/actions/runs/40271463458)
passed all three jobs on implementation SHA
`f6c07e69742620119863aed0a335f93f1c4f8e8e`: full core regressions,
optimized examples, and same-runner W1 performance. The core regression job
explicitly excludes the legacy Phase 25.4 fixed-machine timing threshold; the
dedicated W1 job compares main and W1 on the same runner instead. Full history
is fetched for tests that inspect old refs. The mold-only Make incremental
invalidation case is skipped only when `MOSS_LDFLAGS` selects the system
linker. The first CI attempts exposed and resolved those runner/workflow
assumptions, plus the requirement to build both compiler binaries before the
parallel regression runner.

The Callgrind attribution gate is complete for all eight named workloads.
The remaining limitation is methodological: named function self-costs do not
attribute every inlined or transitive parse/type/map operation. No merge to
`main` is authorized by this W1 record.
