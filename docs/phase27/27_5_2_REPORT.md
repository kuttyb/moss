# Phase 27.5.2 — EST default retention and compiler-state handoff

## Scope and result

Phase branch: `phase27-5-2-work`, implementation SHA `9aa85e8877987155e6fd4f50bd734150e4665a7a`. The baseline binary was built from `phase27-5-1-m1` (`e7a9f707`), SHA256 `e07941624dff52907512bfb0011498d82b75659b6dd0f6a15ab479dd7501054d`.

Default mode now retains expression handles and per-expression checker facts without legacy parser cross-checks. `MOSS_EST=shadow` adds those checks; `off` is rejected. Consumers continue reading the same decisions. Specialized-body facts are recorded under their specialization contexts. The semantic-fact store uses lookup-only hashed keys; no consumer-visible ordering exists.

## Mode gates

All 39 original `Mode::Shadow` gates are enumerated and classified in [`COMPILER_STATE.md`](../COMPILER_STATE.md): **5 RETENTION** gates are unconditional and **34 CROSS-CHECK** gates remain shadow-only. The inventory records every original site and its role.

## S4 measurements

On 1,822 source files and 337 package manifests, paired fallback-parse totals were **5,364,705 baseline → 4,929,605 default-on** (−435,100; −8.11%). Site-level counts, residual causes, and checker fact coverage by node kind are in [`COMPILER_STATE.md`](../COMPILER_STATE.md). Coverage totals were PRESENT=23,452 and ABSENT=259,216. The report-only instrumentation is behind `MOSS_TEST_BUILD` and `MOSS_EST_COVERAGE_STATS=1`; it does not alter generated Rust.

## Parity

The baseline/candidate source matrix covered 1,822 sources, 1,130 successful default Rust emissions, 1,130 successful `-O` emissions, and 763 Fast Debug runs. It found **zero differences**. The only paired Fast Debug timeouts were `examples/frontend_syntax.moss` and `examples/functional_pipeline.moss` on both compilers. Evidence: `tmp/phase27-5-2-parity-sources/summary.json`.

Package parity covered 337 manifests: build statuses and generated Rust matched with zero Rust diffs. Twelve raw Fast Debug error signatures differed only in the absolute compiler executable path printed by Margo; replacing both executable paths with a common marker left zero residual differences. Evidence: `tmp/phase27-5-2-parity-packages/summary.json`.

The `.mossi` identity harness recomputed toolchain-derived package fingerprints and execution-body hashes; the remaining library/app bytes were identical. Evidence: `tmp/phase27-mossi-baseline/1793487120198349400/report.json`. The explicit three-engine harness passed with identical stdout and three callback invocations.

## Full suite and performance

`make --jobs="$(nproc)" check` passed in default and shadow modes: 165/165 jobs, 0 failures, 0 skips in each mode. Full log: `tmp/phase27-5-2-make-check-final.log`.

The final 77-fixture, three-repetition performance run compared against first parent `origin/main=32a80a46220f49ac69f6ec68a7bd676b5941f27e`. Aggregate wall time was 5.585s → 5.698s (**+2.0%**). No `rw_*` fixture exceeded the noise rule. `w2_spec_distinct_n032` was 2046.50ms → 2095.08ms (**+2.4%**) with peak RSS 60,392 → 61,532 KiB (**+1.9%**). Across the 77 individual fixture peak-RSS maxima, the sum was 997,316 → 1,008,596 KiB (+1.13%); this sum is a comparison aid, not a concurrent-process RSS measurement. Per-fixture wall time, ranges, CPU time, and peak RSS are recorded in `tmp/compiler-perf/20261031-155208/phase26_candidate_medians.csv`; the full report is `tmp/compiler-perf/20261031-155208/LOCAL_REPORT.md`. One non-`rw_*` workload, `w5_infer_wide_w100`, exceeded the runner's per-fixture noise rule (+10.7%, +13.27ms); the phase tripwire is aggregate +5% or an `rw_*` beyond-noise slowdown, so it did not block.

## Review and delivery

The Agent 2 adversarial review is pending on the exact implementation SHA above, after the full suite. Merge and `phase27-5-2-m1` tag are pending that review. No delivery merge has occurred yet.

Agent 2 reviewed implementation SHA `9aa85e8877987155e6fd4f50bd734150e4665a7a` and returned **GO**. It confirmed no active Off path, the 5/34 gate split, test-build-only coverage counters, and three sampled parity results; it checked the state handoff against cited implementation lines. It also called out `w5_infer_wide_w100` (+10.7%, +13.27ms) as a non-blocking performance concern under the phase's stated tripwire. Review detail is summarized in [`agent-patterns/phase27-5-2.md`](agent-patterns/phase27-5-2.md). The implementation was merged as `a2b308fd30409b54b2e4bb0d8dfe27ef860d63d6`, with first parent `32a80a46220f49ac69f6ec68a7bd676b5941f27e` and second parent `46be7ef29ca3674a9d098382506f061a517ecb96`. Annotated tag `phase27-5-2-m1` targets the merge. Delivery was pushed to `origin/main` and `origin` tag.
