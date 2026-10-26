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
99.74% (17,866,873 to 46,207). The fixed-point count remains one. This supports
the selected cache-window fix; the timing result and all required parity gates
are pending.
