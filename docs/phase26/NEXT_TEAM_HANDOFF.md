# Phase 26 handoff after W2 closeout and W3 effects

## Current continuation status

W2 closeout and W3 implementation are committed and pushed on
`phase26-w2-inference` at `c4048d8cf8d743e101e8adcdaf46cc976bc60c3e`;
`.codex/CURRENT_STATUS.md` has the current delivery state. Read `W2_RESULTS.md` and `W3_RESULTS.md` for evidence and
`MossPhase26.md` for the final stop decision. W2's per-wave project counters,
legacy full-loop oracle, three extra convergence exits, W3 memoization, and W5
timer-key fix are present. W4 type-spelling interning is dropped. Local repository gates, feedback validation, exact-SHA Architect review, and
hosted run #15 all passed. PR #18 remains open and unmerged; it targets
`phase26-moss-speedup`. Its implementation SHA has Architect GO. The next
integration action is merging the PR into that Phase 26 branch under the normal
review policy; do not bypass this with a direct main-branch merge.

The Team is two actual agents: Coordinator/implementer/reviewer and the
independent adversarial Architect. Architect GO is for exact implementation SHA
`c4048d8cf8d743e101e8adcdaf46cc976bc60c3e`; the documentation-only closeout
commit may advance the delivery head without changing the reviewed implementation. There were no leaf implementation workers.
The Architect was asked to review at the frozen final integration checkpoint,
not on individual edits. Record unknown usage values as unknown in the team
closeout; do not infer agent counts or metrics.

## Current ownership and branch

W2 implementation is on `phase26-w2-inference`, checkpoint
`44b86de3af469c750ba0601389dee82291d83561`, and pushed to `origin`. Draft
[PR #18](https://github.com/kuttyb/moss/pull/18) targets
`phase26-moss-speedup`, currently at W1 base
`b8871297619cf51187457b1fc3e55d833d661146`. Do not merge directly to `main`.
Final delivery head `7e40f007fb74efec5b42127475a795714473bfa2` passed hosted
run #11: core regressions, optimized examples, and W2 same-runner performance.
PR #18 is ready for review and remains unmerged. The full 77-fixture
B1 timing/profiling suite passed 154/154 trials for W1 and W2, and the
eight-workload Callgrind attribution completed; details and the disclosed W4
wall-time regression are in `docs/phase26/W2_RESULTS.md`. Read `.codex/CURRENT_STATUS.md`,
`docs/phase26/W2_RESULTS.md`, and this handoff before continuing.

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

The PR #18 automated checks are green on corrected delivery head `5d1cd6b6`;
PR #18 is ready for review and remains unmerged. See
`docs/phase26/W2_RESULTS.md` for validation scope and limitations. PR #17/W1
remains separate and has no main merge implied here.

## W3 results and next work

W3's mechanism was chosen from measured D18 counts: 2,621,451 baseline calls
for 37 distinct keys, with effects fixed point at 1/60 rounds. The memoized
candidate reports 38 computations for 19 distinct inputs across the two
independent passes and lowers D18 Callgrind Ir by 72.5%. The eight-workload
attribution and small-fixture latency are in `W3_RESULTS.md`. The decision is
to stop Phase 26 after this work, drop W4, retain the discipline rules, and
move the next effort to Graph DSL. Follow the repository PR gates; do not merge
directly to `main`.

## Prior W1 references

W1 implementation and measurements are recorded in
`docs/phase26/W1_RESULTS.md`; its PR #17 targets `main`. W1's expression caches
are per-Checker, bounded, syntax-only, and preserve parser output-argument side
effects. Keep those contracts intact when working on W2 and later workstreams.
