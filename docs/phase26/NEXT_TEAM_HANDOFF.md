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

`make compiler-perf` runs three untraced timing repetitions plus one separate
profiling sample per fixture, writing isolated artifacts under
`tmp/compiler-perf/<run-id>/`. With a baseline, its three timing samples are
interleaved adjacent per fixture and the first side alternates across fixtures
and repetitions. Set `MOSS_PERF_BASELINE=<git-ref>` to compare; set
`MOSS_PERF_INCLUDE_REAL_WORLD=1` to include the full 77-fixture set (54 torture
plus 23 curated real-world workloads). The report includes wall medians, CPU time, peak RSS, all
eight attribution workloads, per-fixture work counters, and slowdowns exceeding
5% plus either 20 ms absolute or non-overlapping timing ranges with a >2 ms
absolute increase. Set `MOSS_PERF_CERTIFY=1` to require a clean working tree and
compiler sources matching `origin/main`; default runs accept any revision. Metadata
records the candidate's dirty status. This is a
benchmark/tooling change and makes no compiler behavior change. See
`docs/phase25/PERMANENT_COMPILER_PERFORMANCE_REGRESSION_TARGET.md` for details.

Certification mode additionally requires `MOSS_PERF_BASELINE=c66fb76c`, the
full 77-fixture corpus, and exactly three timing repetitions. The certified
Phase 26 run on 2026-10-26 is recorded in `MossPhase26.md`, with the full report,
candidate medians, work counters, and host/commit metadata under
`benchmarks/phase25_4/cpp_baseline/results/`. Historical baseline work counters
are sparse because pre-Phase 26 revisions do not emit the complete structured
counter set; missing entries mean unavailable, not zero.

Worklist complexity debt: simplify to a plain convergence check unless a real
package shows partial waves.

## Phase 26.1 T1 closeout handoff — 2026-10-27

T1 implementation and local gates are complete on branch `phase26-1-m1` at
`18c04f2ea965fc71c6b835dd6b8690310e514656`, pushed to
`origin/phase26-1-m1`. Architect GO is for this exact implementation SHA.
Delivery head `6ec0561d` adds only closeout docs/feedback/status. T1
defers imported generic body checks until concrete instantiation and removes
the obsolete dependency-role bit from Margo package fingerprints. The focused
regression verifies that a current geolib interface is skipped on the first
consumer graph after being built as root.

Evidence: `docs/phase26/T1_RESULTS.md` and
`docs/phase26/PHASE_26_1_EXIT.md`. Exact-SHA `make check` passed 146/146,
including Fast Debug parity. The full-hash corpus differential against C1
`7bc75969` covers 2,064 sources and found exactly three intended
`generic_poisons_import` acceptance changes. Package-perf (70/70) and corrected
C1 corpus gates (438 roots, G-a 660/660, G-b 67/67) passed on the production-
code-identical parent `af9674af`. Compiler-perf guard run `20261027-104141`
shows 77 fixtures, three repetitions, 5.438 s -> 5.415 s aggregate median sum
(-0.4%) and no slowdown beyond the noise floor.

Remaining handoff: certification mode requires a clean candidate checkout
whose compiler source is exactly `origin/main`; no such certified run can be
produced from this review branch. After integration is authorized and lands,
run `MOSS_PERF_CERTIFY=1 MOSS_PERF_INCLUDE_REAL_WORLD=1 MOSS_PERF_BASELINE=c66fb76c make compiler-perf` from clean `main`, record the
report/metadata and host in the Phase 26.1 exit report, and update status to
`26.1 complete; awaiting Bank review`. The branch currently has the complete
implementation and review evidence but does not claim certification.
