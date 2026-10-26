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

Each workstream lands as its own change with its own before/after numbers, so a regression or a non-win is attributable.

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

## Exit criteria and the decision point after Phase 26

Phase 26 is done when every merged workstream shows a measured reduction on its target workload with all correctness gates green.

- [ ] W1–W5 each merged with before/after benchmark numbers, or reverted for no gain
- [ ] Expression-text parsing share re-measured on the 8 baseline workloads
- [ ] B1 hotspots re-timed: W5 w100 inference, W4 d18 effects, regex, datastructures, time
- [ ] Final attribution published next to the baseline

**Decision point.** Re-run the attribution after W5. If string-type handling and string-keyed maps are still a large share, that measured number is the case for further representation work in a later phase. If they are small, we stop and live with the current architecture under the discipline rules.

Open question: what share counts as "large"? Proposed threshold: 15% of compiler instructions on any baseline workload.
