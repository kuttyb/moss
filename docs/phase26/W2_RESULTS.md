# Phase 26 W2: signature inference worklist

## Scope and implementation

W2 replaces repeated whole-program function-signature inference scans with a caller worklist in `src/semantic_checker.cpp`. The first wave remains source ordered. Later waves visit callers of functions whose signatures changed. Since handlers, `main`, tests, benchmarks, or shared object/domain signatures can affect inference outside a known function edge, the implementation fingerprints all function and shared signatures and falls back to a full wave when shared signatures or the dependency graph change. Unresolved local callable uses conservatively trigger full scans. The existing run-level `inference_rounds` and pass boundaries retain their meanings.

New profiling counters report functions re-inferred and signatures changed, both aggregate and by wave. The existing B1 inventory reports the new aggregates.

## Correctness evidence

- `moss-dev` and `moss-opt` compiled successfully with `make --jobs="$(nproc)" moss-dev moss-opt`.
- `tests/tooling/check_phase26_w2_inference_worklist.py` passed on both binaries. It covers W5's result and convergence counters, a forward call, a domain-only shared-signature path with zero analyzed functions, a pipeline callback, and a negative diagnostic.
- The full tracked-source differential checked 2,060 `.moss` files at base `b8871297619cf51187457b1fc3e55d833d661146`: 1,322 accepted, zero verdict changes, and zero generated-Rust hash changes. The run used isolated source exports and `--emit`.
- `git diff --check` passed. Hosted PR run #8 passed `core-regressions`, `optimized-examples`, and `w2-inference-performance`; the W1-only `compiler-performance` job was skipped by its head/base condition. The run is attached to the implementation SHA above.

## Performance evidence

Three alternating same-runner `moss-opt` W5 W100 check pairs produced median times of 10.9169 s on W1 and 0.3834 s on W2, a 96.5% reduction. Exact check JSON output matched in every pair. The W2 profile retained the existing `inference_rounds=5`; 1,200 function visits were reported over six worklist waves for this workload, with 200 functions in the initial wave. This separates run-level passes from inference worklist waves.

Hosted same-runner performance also passed on run #8: W1 9.5943 s, W2 0.3287 s median, 96.6% lower, with matching check JSON. This confirms the direction on a separate runner; absolute timings differ from local as expected.

### Full B1 run

The B1 runner completed all 77 fixtures in timing and profiling modes, one repetition per mode, for both W1 and W2: 154/154 successful trials on each binary, zero timeouts. Runs were sequential on the same host, with isolated W1/W2 source worktrees and per-worktree report outputs. W1 binary SHA-256 was `b50d5afff67fc6f4c83264fb19bd42ea6ee2e22d214500fe67d1080d4d3b5501`; W2 used implementation SHA `44b86de3af469c750ba0601389dee82291d83561`, binary SHA-256 `6f297d555e213f8c2334e0d5d3e5b6dc9a6255f2ad60a4d178e548d6022ed74f`.

| Workload | W1 timing | W2 timing | Change |
| --- | ---: | ---: | ---: |
| W2 distinct specialization N32 | 2.795 s | 2.378 s | 14.9% lower |
| W3 repeated specialization N32 | 1.844 s | 1.407 s | 23.7% lower |
| W4 deep inference D18 | 25.820 s | 26.469 s | 2.5% higher |
| W5 wide inference W100 | 9.906 s | 0.315 s | 96.8% lower |
| Regex package | 19.427 s | 2.244 s | 88.4% lower |
| Datastructures SDK | 20.298 s | 1.508 s | 92.6% lower |
| Rolling window | 0.138 s | 0.109 s | 21.3% lower |
| Checkout example | 0.011 s | 0.011 s | 7.4% lower |

The one-repetition sum across all 77 timing fixtures fell from 89.554 s to 40.144 s. W2 was faster or equal on 67 fixtures; 10 were slower, with the largest absolute increase 0.555 ms (nested collection K1). The W4 D18 increase was 0.648 s and is disclosed here; W2's target W5 W100 and the two large packages improved substantially. One-repetition per-fixture figures are directional and not a confidence interval.

### Callgrind attribution

Callgrind 3.22.0 `Ir` profiles used the W2 `moss-opt` binary and the same eight workloads reported in W1. The W1 column reuses the previously completed W1 attribution profiles; W1 implementation source at `f6c07e69742620119863aed0a335f93f1c4f8e8e` is unchanged in the W2 base `b8871297619cf51187457b1fc3e55d833d661146`. W2 profiles were run sequentially with cache simulation and instruction dumps disabled. The table gives total instruction references and selected direct function self-cost categories as shares of total:

| Workload | W1 total Ir | W2 total Ir | Total change | Named parse helpers | Type spelling helpers | String-keyed maps |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| W2 distinct N32 | 43.840B | 35.704B | 18.6% lower | 17.18% → 14.44% | 3.36% → 3.80% | 3.30% → 3.21% |
| W3 repeated N32 | 29.897B | 21.810B | 27.0% lower | 22.73% → 20.25% | 3.43% → 4.16% | 4.35% → 4.39% |
| W4 deep D18 | 454.717B | 453.374B | 0.3% lower | 16.55% → 16.54% | 1.66% → 1.66% | 4.78% → 4.78% |
| W5 wide W100 | 146.599B | 4.935B | 96.6% lower | 24.00% → 20.63% | 2.12% → 1.85% | 5.22% → 5.22% |
| Regex | 252.263B | 29.663B | 88.2% lower | 8.72% → 11.32% | 7.56% → 8.14% | 3.93% → 3.65% |
| Datastructures | 248.257B | 18.425B | 92.6% lower | 14.84% → 16.05% | 6.31% → 5.59% | 4.41% → 4.22% |
| Rolling window | 1.810B | 1.330B | 26.5% lower | 14.54% → 13.96% | 6.30% → 6.78% | 4.19% → 3.99% |
| Checkout | 0.123B | 0.114B | 7.5% lower | 11.56% → 11.42% | 5.96% → 5.91% | 5.58% → 5.56% |

Total `Ir` fell on all eight workloads. The named parse-helper share rises on regex and datastructures even though its absolute direct `Ir` falls there; inference work shrinks faster, so the remaining parsing slice is a larger fraction. These are direct self-cost categories and do not include all inlined/transitive work. Raw profiles and complete machine-readable summaries are disposable under `tmp/phase26-w2/callgrind/`; the summary is not a standalone substitute for the source code or workload definitions.

## Review and delivery

- Implementation checkpoint: `44b86de3af469c750ba0601389dee82291d83561`.
- Architect checkpoint 2.1 reviewed the pre-final candidate and returned conditional GO, with no remaining confirmed blocker. The suggested zero-function assertion was added before this implementation checkpoint.
- Draft PR #18 targets `phase26-moss-speedup`; no merge to `main` is requested. Hosted run #8 passed all three applicable jobs. The Architect final exact-SHA review is pending.
