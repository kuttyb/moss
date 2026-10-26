# Phase 26 handoff after Team W1

## Current branch and ownership

Team W1 works on `phase26-moss-speedup` with draft [PR #17](https://github.com/kuttyb/moss/pull/17)
against `main`. The Coordinator is the sole writer for
`src/semantic_checker.cpp` and `src/fileio_semantics.inc`; the second agent
is an independent adversarial Architect. Do not merge directly to `main`.
Read `.codex/CURRENT_STATUS.md` and `docs/phase26/W1_RESULTS.md` for the
latest exact SHA, validation and known limits. The Phase 26 plan is
`docs/phase26/MossPhase26.md`.

W1 caches context-free checker syntax only. Its `split_binary` key includes
the ordered operator list; `split_top_level` includes the delimiter.
Successful call parses return copies, while failed call parses still use the
shared parser because they may partially change output arguments. The cache
belongs to one `Checker` and has a 32,768-entry cap. `trim` remains uncached
pending a measured benefit. Keep those contracts when working on later
workstreams. The new regression is
`tests/tooling/check_phase26_w1_parse_cache.py`.

## W1 evidence that a receiving team should check

The B1 benchmark ran the same 77 fixtures in timing and profiling modes on
main and W1, one repetition per mode; all 154 trials passed on each binary.
Three alternating paired trials confirmed W3 N32 and W5 W100 time gains.
`docs/phase26/W1_RESULTS.md` holds exact numbers, memory tradeoffs and the
scope limit. Baseline/W1 differential passed all 1,633 tracked `.moss` files
under examples and tests; the 1,007 accepted sources emitted byte-identical
Rust. Fast Debug exit status and output matched on all five Moss test files
under `tests/`.

PR #17's Phase 26 workflow has independent regression, optimized-example and
same-runner performance jobs on separate checkouts. The regression job
explicitly omits the legacy fixed-machine timing threshold; the performance
job builds `main` and W1 on one runner and requires gains on W3 N32 and W5
W100. The Make incremental test now runs its mold-version invalidation case
only when mold is selected. The initial hosted run failed at link because the
runner lacks mold; the workflow uses the Makefile's empty `MOSS_LDFLAGS`
override. A later run must prove this revised workflow.

The local machine lacks callgrind, so W1's requested eight-workload
instruction attribution is blocked here. Wall-clock timing and stage timers
cannot establish the instruction-share categories in the plan. A receiving
team with an instruction profiler should run that gate on the same eight
baseline workloads before claiming full W1 acceptance.

## Next workstream

W2 is the next independent Phase 26 change. Start it from W1's accepted
integration SHA after PR #17's required gates and final Architect verdict,
or preserve a separate branch while W1 remains pending. W2 owns the
signature-inference convergence loop in `src/semantic_checker.cpp`: re-infer
a caller when a callee's inferred signature changes, preserve the existing
result order and diagnostics, and expose functions changed per round alongside
`inference_rounds`. W5 wide inference W100 is the target workload. Keep W2
performance and correctness evidence separate from W1 so each result can be
attributed or reverted independently. Maintain one writer lease on the
checker file, and leave W3–W5 outside W2's PR.
