# Phase 26 handoff after W2 closeout and W3 effects

## Current continuation status

W2 closeout and W3 implementation are complete. PR #18 merged into
`phase26-moss-speedup` as `5595b8e7d66f90284b03805ad647e3a1251d7890`; PR #17
now carries W1–W3 to `main`. `.codex/CURRENT_STATUS.md` has the current state. Read `W2_RESULTS.md` and `W3_RESULTS.md` for evidence and
`MossPhase26.md` for the final stop decision. W2's per-wave project counters,
legacy full-loop oracle, three extra convergence exits, W3 memoization, and W5
timer-key fix are present. W4 type-spelling interning is dropped. Local repository gates and feedback validation passed. Architect GO is for
exact implementation SHA `c4048d8cf8d743e101e8adcdaf46cc976bc60c3e`; the
docs-only successor is `11a1f8ba88ab8342076721f5650aaea48011ec4b`. Hosted run
#16 passed all PR #18 gates. Its integration merge `5595b8e` retains the same
source/test tree. PR #17 is the Phase 26-to-main review and its hosted run #17
has compiler performance and optimized examples green; core regressions remain
in progress. Merge to `main` only through PR #17 after its gates are green.

The Team is two actual agents: Coordinator/implementer/reviewer and the
independent adversarial Architect. Architect GO is for exact implementation SHA
`c4048d8cf8d743e101e8adcdaf46cc976bc60c3e`; the documentation-only closeout
commit may advance the delivery head without changing the reviewed implementation. There were no leaf implementation workers.
The Architect was asked to review at the frozen final integration checkpoint,
not on individual edits. Record unknown usage values as unknown in the team
closeout; do not infer agent counts or metrics.

## Current ownership and branch

W2/W3 is integrated into `phase26-moss-speedup` at merge commit
`5595b8e7d66f90284b03805ad647e3a1251d7890`; source/test tree matches the
Architect-reviewed implementation SHA `c4048d8cf8d743e101e8adcdaf46cc976bc60c3e`.
[PR #18](https://github.com/kuttyb/moss/pull/18) is merged.
[PR #17](https://github.com/kuttyb/moss/pull/17) now carries W1–W3 to `main`.
Hosted PR #17 run #18 passed core regressions, optimized examples, and compiler
performance. PR #17 merged to `main` at
`4f5788fd1176c22452ff3a8eb2893f3c62f571b0`. Phase 26 is complete; move next to
Graph DSL. See `W1_RESULTS.md`, `W2_RESULTS.md`, and
`W3_RESULTS.md` for the complete measurements and caveats.

The checker file has a single-writer lease. W2 closeout's counter/oracle and
loop-audit additions are in `src/semantic_checker.cpp` and
`tests/tooling/check_phase26_w2_inference_worklist.py`. W3 memoization is also
in that file. W5's timer-key constructor fix is in
`src/compiler_stage_timer.hpp` and is folded into this delivery. Do not start
W4: the post-W3 attribution leaves type spelling below 15% on all workloads.

## W2 implementation and evidence

The initial inference wave preserves source order; changed signatures schedule
callers. Shared object/domain/handler state changes and call-graph changes force
a conservative all-function wave. Unresolved local callable uses, pipeline
callbacks, sort callbacks, and callable formals are handled conservatively.
Zero-function source programs continue to converge on shared signature changes.
The existing `inference_rounds` counts are preserved; separate aggregate and
per-wave function-visit/signature-change counters show actual work.

Local gates at checkpoint `44b86de3af469c750ba0601389dee82291d83561`:

- `moss-dev` and `moss-opt` build succeeded.
- W2 regression passed on both builds: W5 output/counters, forward call,
  domain-only shared signature (asserted to analyze zero functions), pipeline
  callback, negative diagnostic.
- Differential parity passed all 2,060 tracked `.moss` files against W1:
  1,322 accepted; zero diagnostic/verdict changes; zero emitted-Rust hash
  changes.
- Three alternating W5 W100 optimized timing pairs: W1 10.9169 s, W2 0.3834 s
  median (96.5% lower); exact check JSON matched.
- Architect checkpoint 2.1 returned conditional GO with no remaining confirmed
  blocker. Final Architect review is GO for implementation SHA
  `44b86de3af469c750ba0601389dee82291d83561`.

PR #18 is merged into the Phase 26 integration branch after hosted run #16
passed the W2 closeout and performance gates. PR #17 brings the combined phase
to `main`; see
`docs/phase26/W2_RESULTS.md` for validation scope and limitations.

## W3 results and next work

W3's mechanism was chosen from measured D18 counts: 2,621,451 baseline calls
for 37 distinct keys, with effects fixed point at 1/60 rounds. The memoized
candidate reports 38 computations for 19 distinct inputs across the two
independent passes and lowers D18 Callgrind Ir by 72.5%. The eight-workload
attribution and small-fixture latency are in `W3_RESULTS.md`. The decision is
to stop Phase 26 after this work, drop W4, retain the discipline rules, and
move the next effort to Graph DSL. PR #17 is the integration path to `main`;
its core regression, optimized-example, and compiler-performance gates are
green. It merged through PR #17; move to Graph DSL.

## Prior W1 references

W1 implementation and measurements are recorded in
`docs/phase26/W1_RESULTS.md`; PR #17 targets `main` with the combined W1–W3
result. W1's expression caches
are per-Checker, bounded, syntax-only, and preserve parser output-argument side
effects. Keep those contracts intact when working on W2 and later workstreams.

## Local compiler performance runner follow-up

`make compiler-perf` now runs the local B1 timing suite with three repetitions,
isolated artifacts under `tmp/compiler-perf/<run-id>/`, and a median-based
comparison report. By default it includes all 54 torture fixtures plus the
attribution set. To compare against a revision, run
`MOSS_PERF_BASELINE=<git-ref> make compiler-perf`. Add
`MOSS_PERF_INCLUDE_REAL_WORLD=1` to include all 23 curated checkout workloads;
set `MOSS_PERF_REPS` only when a smaller/longer local run is intentional. The
comparison records the 8 attribution workloads, total sum of per-fixture
medians, small-fixture latencies, and every fixture slower by more than 5%.
See `docs/phase25/PERMANENT_COMPILER_PERFORMANCE_REGRESSION_TARGET.md` for
commands and interpretation. This is benchmark/tooling work; it makes no
compiler behavior change.

Worklist complexity debt: simplify to a plain convergence check unless a real
package shows partial waves.
