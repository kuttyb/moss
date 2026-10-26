# Phase 26.0.1 C1: concrete effect traversal

## Baseline evidence (main `274f848`)

Certified Phase 26 work counters show that W4 deep inference has few distinct
concrete effect inputs but exponentially many evaluations. The fixed-point
summary pass already converges in one unchanged round; the repeated work is in
the cache-disabled calls.

| Fixture | Evaluations | Distinct inputs | Cache-disabled calls | Cache-enabled calls | Parse misses |
|---|---:|---:|---:|---:|---:|
| W4 D5 | 110 | 11 | 68 | 42 | 8,820 |
| W4 D10 | 2,139 | 21 | 2,057 | 82 | 86,301 |
| W4 D15 | 65,672 | 31 | 65,550 | 122 | 2,258,943 |
| W4 D18 | 524,451 | 37 | 524,305 | 146 | 17,866,873 |

D18 also has 90 concrete-effect cache hits and 19 retained entries, so the
existing cache works for its enabled window. The disabled-call count grows as
`2^(depth+1) + depth - 1` for these fixtures. This rules out memoizing
additional concrete input keys as the first fix.

`concrete_function_observable_effects` recursively evaluates a callee while
walking its body and exceptional effects. After the fixed-point summaries are
stable, `infer_observable_effects` disables the cache before its authoritative
exceptional walk. That walk must still visit each actual body so diagnostics and
CFG annotations are produced, but nested concrete summaries can safely be
memoized for this stable window. C1 therefore enables the existing per-Checker
cache only across that authoritative walk, with a fresh epoch, and disables it
before leaving the pass.

## Validation

An initial profile of the candidate on the same four fixtures shows linear
growth and no cache-disabled evaluations:

| Fixture | Evaluations | Distinct inputs | Cache-disabled calls | Cache hits | Parse misses |
|---|---:|---:|---:|---:|---:|
| W4 D5 | 58 | 11 | 0 | 30 | 7,272 |
| W4 D10 | 113 | 21 | 0 | 60 | 18,507 |
| W4 D15 | 168 | 31 | 0 | 90 | 34,417 |
| W4 D18 | 201 | 37 | 0 | 108 | 46,207 |

The D18 evaluation count fell 99.96% (524,451 to 201), and parse misses fell
99.74% (17,866,873 to 46,207). The fixed-point count remains one. Timing run
`20261026-132734`, using `MOSS_PERF_BASELINE=274f848 make compiler-perf`,
confirmed the targeted gain on the same host:

| Fixture | Baseline median | Candidate median | Change |
|---|---:|---:|---:|
| W4 D5 | 6.34 ms | 6.34 ms | -0.1% |
| W4 D10 | 39.99 ms | 12.28 ms | -69.3% |
| W4 D15 | 910.04 ms | 20.79 ms | -97.7% |
| W4 D18 | 7,152.46 ms | 28.92 ms | -99.6% |

The complete default suite measured 57 fixtures (54 torture plus the selected
curated workloads). Sum of per-fixture medians fell from 14.660 s to 6.435 s
(-56.1%). None of the 57 fixtures crossed the slowdown noise floor. Seven
fixtures were more than 5% slower but below the floor; their median increases
were 0.12–5.18 ms.

| Attribution workload | Baseline median | Candidate median | Change |
|---|---:|---:|---:|
| Checkout | 3.66 ms | 3.63 ms | -0.8% |
| Regex | 1,484.71 ms | 1,488.78 ms | +0.3% |
| Rolling window | 96.48 ms | 95.59 ms | -0.9% |
| Datastructures | 584.91 ms | 562.97 ms | -3.8% |
| W2 N32 | 1,916.21 ms | 1,859.70 ms | -2.9% |
| W3 N32 | 991.59 ms | 926.15 ms | -6.6% |
| W4 D18 | 7,152.46 ms | 28.92 ms | -99.6% |
| W5 W100 | 132.80 ms | 129.04 ms | -2.8% |

The `274f848` corpus differential checked all 2,064 tracked `.moss` sources:
1,325 accepted and 739 rejected on both compilers, with zero verdict,
diagnostic-code, or full SHA-256 generated-Rust-hash changes. The Fast Debug
parity shard passed, including project closure and native/Margo parity checks.
`make check` passed all 140 jobs with zero failures.
