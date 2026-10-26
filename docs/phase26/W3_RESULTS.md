# Phase 26 W3: specialization effects memoization

## Mechanism selected from counts

The W4 D18 baseline before memoization made 2,621,451 calls to
`concrete_function_observable_effects` for 37 distinct function/concrete-type
inputs, with recursive maximum depth 19. This is a strong memoization case:
computations greatly exceeded distinct keys. The observable-effects fixed
point itself ran once (0 changing rounds, limit 60), so it was not a hidden
whole-program convergence loop.

The cache key combines semantic callable identity and canonical concrete
argument types. Cache entries are scoped to effect-summary epochs and cleared
when summaries are recomputed. The two specialization-effects passes remain
independent: the cache is invalidated between pass one and pass two, then
disabled after pass two. Profiling counters separate function evaluations,
actual computations, hits, and distinct inputs for those two passes.

## Evidence

The focused D18 regression reports 110 specialization effect evaluations,
38 actual computations, 72 cache hits, and 19 distinct inputs. Thus each key
is computed once in each of the two independent passes. Effects convergence
remains 1 round out of 60. The optimized D18 wall median was 7.354 s across
three runs (7.354, 7.359, 7.325 s), compared with the earlier W2 single-run
measurement of 26.469 s. This is directional, not a paired same-session
comparison.

The eight-workload Callgrind attribution compares W2 and W3 `moss-opt` builds.
Callgrind 3.22.0 total instruction references fell on all eight workloads:

| Workload | W2 Ir | W3 Ir | Change |
| --- | ---: | ---: | ---: |
| W2 distinct N32 | 35.704B | 27.673B | 22.5% lower |
| W3 repeated N32 | 21.810B | 15.274B | 30.0% lower |
| W4 deep D18 | 453.374B | 124.695B | 72.5% lower |
| W5 wide W100 | 4.935B | 1.882B | 61.9% lower |
| Regex | 29.663B | 19.753B | 33.4% lower |
| Datastructures | 18.425B | 7.066B | 61.7% lower |
| Rolling window | 1.330B | 1.181B | 11.2% lower |
| Checkout | 0.114B | 0.0324B | 71.6% lower |

The W3 attribution ran before the W5 timer-key constructor change. Type
spelling direct self-cost is 1.96% on D18, 8.64% on regex, 4.19% on
datastructures, 6.65% on rolling window, and 4.04% on checkout; every measured
workload remains below the Phase 26 plan's 15% threshold. W4 type-spelling
interning is therefore dropped. The timer key change is folded into this
delivery.

For additional main-relative performance evidence, hosted PR #17 measured W3
repeated N32 at 4.1563 s on main and 1.7534 s on the full Phase 26 candidate
(57.8% lower), and W5 W100 at 18.9773 s on main and 0.2250 s on the candidate
(98.8% lower).

Small-fixture check latency after the timer change, using ten optimized
`check --json` runs per fixture: rolling window median 0.0944 s (range
0.0909–0.0997 s), checkout median 0.0033 s (range 0.0032–0.0045 s). These
measurements close the small-file reporting gap; they are not a paired
before/after claim.

The signature-worklist corpus oracle completed 2,064 tracked sources, with
1,325 accepted and no mismatch against the legacy full-scan signature loop.
A normal corpus pass after adding convergence exits also completed 2,064
sources / 1,325 accepted. Local `make check` passed all 140 jobs. Hosted PR run #16 passed
`core-regressions`, `optimized-examples`, and `w2-inference-performance`. The
combined Phase 26 PR to main run #17 passed core regressions, optimized examples,
and compiler performance. The
hosted same-runner W5 median was 19.2254 s on W1 and 0.2663 s on the candidate
(98.6% lower), with identical check JSON. PR #18 merged into `phase26-moss-speedup` as
`5595b8e7d66f90284b03805ad647e3a1251d7890`. PR #17 run #18 passed all
applicable gates and merged W1–W3 to `main` at
`4f5788fd1176c22452ff3a8eb2893f3c62f571b0`. Phase 26 is complete.

## Decision after W3

W3 is justified by counts and improves D18 and all seven other attribution
workloads. W4 is dropped because type spelling is below the agreed 15% share.
W5 is included as a small timer-key optimization. The Phase 26 results support
stopping after W3: preserve the compiler discipline rules, park the expression
representation refactor, and move the next effort to Graph DSL. No direct merge
to `main` is implied; deliver through the Phase 26 integration branch and its
review gates.
