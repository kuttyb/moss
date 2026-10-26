# Moss Phase 26: Compiler Performance Surgery

Proposal · Oct 25, 2026 · @Bank kus

## Summary

Phase 26 cuts Moss check time on the current C++ compiler without changing its architecture. Expression text stays text; the checker, emitter and Fast Debug keep their current shapes. The work is five targeted fixes, each measured before and after with the existing compiler benchmark.

The target is the cost we have already measured: on the -O2 profiles, re-parsing expression text takes 36–58% of compiler instructions, and the B1 report's algorithmic hotspots (inference rounds, effects analysis) dominate the slowest workloads. Both can be attacked with caches and convergence fixes, not a representation change.

The expression-tree refactor stays deferred. Phase 26 ends with a fresh attribution: if string-type handling and string-keyed maps are still large after the cheap fixes, that number is the case for the refactor; if not, we stop.

## Measured baseline

Re-parsing expression text is 36–58% of compiler instructions on every profiled workload; the typed TypeSolver work is under 5%.

&#91;embedded content: callgrind on the -O2 compiler (moss-opt), main 23f5fc25 · disjoint outermost-call attribution · 8 workloads\]

The single largest helper is `split_binary`, at 10.6% on its own. These shares are lower bounds: string internals inlined into callers are billed to library code, not to the helper.

The B1 report's slowest cases are algorithmic, not parsing: W4 d18 spends 36.6 s in effects analysis and W5 w100 spends 15.4 s in inference rounds. End-to-end check times for the heavy packages are regex 26.8 s, datastructures 27.7 s and time 7.2 s. The B1 stage timers cannot see the parsing cost above because it is spread across every stage.

## Workstreams in priority order

Each workstream lands as its own change with its own before/after numbers, so a regression or a non-win is attributable. W2 closeout extended the project counters and added an opt-in legacy-loop oracle; W3 then selected memoization from measured call/key counts. The post-W3 decision drops W4 and folds W5 into the W3 delivery.

| # | Workstream | Where | What it attacks | Risk |
| --- | --- | --- | --- | --- |
| W1 | Parse cache for checker expression helpers | `split_binary`, `parse_simple_call`, `parse_member_call`, `split_top_level`, `strip_redundant_outer_parentheses`, `trim` in semantic\_checker.cpp | Re-parsing the same expression text across rounds and call sites; `split_binary` alone is 10.6% | Low: parsing is context-free |
| W2 | Inference convergence (P1) | Round loop at semantic\_checker.cpp:13800, bounded by `functions × 3 + 3` | Whole-program re-inference until nothing changes; W5 w100 spends 15.4 s here | Medium: order of results must not change |
| W3 | Effects memoization (P2) | Effects analysis per call site | Re-deriving effects for the same callee and types; W4 d18 spends 36.6 s here | Medium: key must include concrete types |
| W4 | Intern type spellings ↔ TypeRef | `Type::parse_canonical`, `to_canonical_string`, `parsed_type` | Round-tripping type strings on every lookup | Low: pure function of the spelling |
| W5 | Timer key by reference | `ScopedOpTimer(const char*, std::string key)` in compiler\_stage\_timer.hpp | String copies on hot paths even with timing off | Very low |

Notes per workstream:

1. **W1 parse cache.** Key on the exact expression string; return an immutable parsed result (or a copy where callers mutate). No semantic decisions in the cache, only syntax. Start with `split_binary`, then extend to the other helpers in order of profile share.
2. **W2 inference convergence.** Replace "re-run every function every round" with a worklist: re-infer a function only when a callee's inferred signature changed. Add an `inference_rounds` counter and a per-round changed-functions count so the win is visible in the benchmark output.
3. **W3 effects memo.** Cache effects per (function, concrete specialization), invalidated when that function's inference result changes. Add an `effects_calls` counter (computed vs cache hits).
4. **W4 interning.** One table from canonical spelling to TypeRef and back. The 8 `InferredExpr{nullptr, "..."}` fallbacks stay as they are; this only removes repeated parsing of the same spelling.
5. **W5 timer.** Take the key by `const std::string&` or build it lazily only when timing is on.

Optional, only if the post-W4 profile shows it: duplicate project/snapshot passes on import (`project_imported_h3_checked_bodies` rendering checked IR back to text).

## Measurement and correctness gates

Every workstream must pass all correctness gates and show a measured gain before it merges. A cache that changes output is a bug, not a speedup.

**Correctness (must be identical before and after):**

- Generated Rust is byte-identical across the full example and test corpus.
- Accept/reject results and diagnostic codes are identical for every checked file.
- Fast Debug stdout and exit status are identical on the unit-test corpus.
- Existing suites stay green: Type IR, ledger, G1, G3, and the ABI v10 import/export tests.

**Performance (before vs after, same machine, same build flags):**

- The existing compiler benchmark (B1 profiler and torture suite via the compiler-perf / check-perf targets) runs on main before W1 and after each workstream.
- Wall-clock timing uses the -O2 build (moss-opt); day-to-day development builds use moss-dev.
- A callgrind attribution run on the same 8 workloads as the baseline, with expression-text parsing, string-type handling and string-keyed maps reported as their own categories. Stage timers alone cannot see this cost; that is why the B1 report missed it.
- New counters in the benchmark output: `inference_rounds`, functions re-inferred per round, `effects_calls` computed vs cached, parse-cache hits vs misses.

**Rule:** a workstream that is correct but shows no measurable gain on its target workload is reverted, not kept "for later".

## Discipline rules for new work

These rules apply from Phase 26 onward so the eventual expression-tree refactor gets easier, not harder.

1. **No new expression-text parsing in the emitter or Fast Debug.** New features read the checker's results. The checker is the single semantic authority; Fast Debug runs only after check passes and executes the checker's decisions.
2. **Caches are context-free or keyed by function/specialization.** Parsing caches key on text. Anything semantic (types, effects, dispatch) keys on function and specialization, never on expression text alone, because the same text means different things in different scopes.
3. **No per-expression decision caches keyed by text.** They paper over the identity problem that already forces the `…_by_context` maps and occurrence counters.
4. **Each DSL is structured internally and lowers through one narrow interface into core Moss.** Graph DSL lowers fully to core Moss; embedded Moss expressions stay text slots; the lowering emits already-qualified names so the module rewriter has nothing to rename.

One consequence to accept: with Fast Debug no longer a full peer, the native-vs-Fast-Debug differential stops catching checker bugs. Checker coverage has to come from checker tests and the accept/reject corpus.

## Out of scope

- The expression-tree refactor (estimated Phase-25-sized, touching roughly 35–45K of 89K lines).
- The Java port (phase25-3-java-j2 stays paused) and the compiler daemon.
- GPU, Graph DSL and File IO Advanced (moved to Phase 30).
- Changes to generated Rust shape or rustc build time.
- Language changes of any kind.

## Exit criteria and the decision point after Phase 26 — complete

Phase 26 is complete: W1–W3 show measured gains, W4 was dropped by the profiler criterion, W5 was folded into W3, and PR #17 merged to `main` after all gates passed.

- [x] W1–W3 measured; W4 dropped because type spelling stays below 15%; W5 timer-key fix folded into W3
- [x] Expression-text/type spelling/map shares re-measured on the 8 baseline workloads
- [x] B1 hotspots re-attributed, including W5 W100, W4 D18, regex, datastructures, rolling window, and checkout
- [x] Final attribution and small-fixture latency published in `W3_RESULTS.md`
- [x] W1–W3 merged to `main` through PR #17 after core regressions, optimized examples, and compiler performance passed

**Decision point after W3.** The measured type-spelling direct share is below 15% on every workload, while total Callgrind Ir fell on all eight. Phase 26 is stopped after W3. The discipline rules remain, the expression representation refactor stays parked, and Graph DSL is next. Reopen representation work only if later attribution shows a material share.

Open question: what share counts as "large"? Proposed threshold: 15% of compiler instructions on any baseline workload.

## Certified full-corpus result — Phase 26.1 reference

Run `20261026-115031` used `MOSS_PERF_CERTIFY=1 MOSS_PERF_INCLUDE_REAL_WORLD=1 MOSS_PERF_BASELINE=c66fb76c make compiler-perf` on 2026-10-26. The clean candidate checkout was `6deefa9677db4feb3f85de9ad389c5e6863412e4`; its compiler source tree matched `origin/main` at `bdf973af651deb2d3a4deac2b6c4bcc60893a776`. The baseline resolved to `c66fb76ca0012b8125d70d404adbff13daa1cf4e`. Both compiler binaries were built on the same host.

Host: `DESKTOP-0T772KP`, WSL2 Linux x86_64, 16 logical CPUs, Ubuntu clang 18.1.3. Load averages (1/5/15 minutes) were 1.41/0.46/0.29 before timing and 1.01/0.93/0.61 after profiling. The benchmark used 77 fixtures, three untraced timing repetitions per side interleaved by fixture, and one separate profiling sample per side. Corpus hashes matched on both sides. Every trial succeeded: 231 timing and 77 profiling samples per compiler. The candidate working tree was clean.

The sum of per-fixture wall medians fell from **98.437 s to 15.071 s (-84.7%)**. None of the 77 fixtures exceeded the slowdown alert rule (>5% and either >20 ms absolute or non-overlapping ranges with at least a 2 ms increase). The everyday small-fixture checks also improved: rolling window 151.50 ms to 97.01 ms (-36.0%); checkout 10.11 ms to 3.50 ms (-65.4%). CPU time is GNU `/usr/bin/time` user plus system time at 10 ms resolution; reported `0.00 ms` values are below that resolution. Expected-negative fixtures have measured wall/CPU/RSS data; their work counters are n/a because they fail before checker instrumentation emits counters.

| Attribution workload | Baseline wall | Candidate wall | Change | Baseline CPU | Candidate CPU | Baseline peak RSS | Candidate peak RSS |
|---|---:|---:|---:|---:|---:|---:|---:|
| Checkout example | 10.11 ms | 3.50 ms | -65.4% | 0.00 ms | 0.00 ms | 9,296 KiB | 9,464 KiB |
| Regex | 20,454.59 ms | 1,513.26 ms | -92.6% | 20,430 ms | 1,500 ms | 25,180 KiB | 28,028 KiB |
| Rolling window | 151.50 ms | 97.01 ms | -36.0% | 150 ms | 90 ms | 14,364 KiB | 15,400 KiB |
| Datastructures | 22,671.08 ms | 574.11 ms | -97.5% | 22,660 ms | 560 ms | 21,748 KiB | 23,300 KiB |
| W2 N32 | 3,533.22 ms | 1,967.25 ms | -44.3% | 3,520 ms | 1,950 ms | 26,524 KiB | 34,428 KiB |
| W3 N32 | 2,483.17 ms | 1,009.88 ms | -59.3% | 2,470 ms | 1,000 ms | 22,140 KiB | 22,156 KiB |
| W4 D18 | 26,854.97 ms | 7,290.55 ms | -72.9% | 26,840 ms | 7,280 ms | 10,284 KiB | 10,568 KiB |
| W5 W100 | 11,404.22 ms | 126.78 ms | -98.9% | 11,390 ms | 120 ms | 13,408 KiB | 16,356 KiB |

This run establishes the Phase 26.1 certified performance reference. The complete 77-fixture report, candidate medians, work counters, and run metadata are committed under [`benchmarks/phase25_4/cpp_baseline/results/`](../../benchmarks/phase25_4/cpp_baseline/results/). The baseline-side counters are sparse because the pre-Phase 26 revision does not emit the current structured counter set; missing baseline values are unavailable rather than zero. Candidate expected-negative fixtures are rejected before any checker counters are emitted, so their work counters are also unavailable rather than zero.
