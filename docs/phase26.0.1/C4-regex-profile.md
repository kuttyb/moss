# Phase 26.0.1 C4 — Regex parser lookup attribution

## Counts before mechanism selection

Main baseline is `274f848502df08f62b69c54c614ad9f05a90a3ec`. Its regex work-counter record reports 4,356,244 parse-cache hits and 1,081,378 misses, or 5,437,622 total cache lookups across the check. This reproduces the plan's roughly 4.4 million hit count.

The C3 candidate (`fdeb72b16faa82a8dfefd04a1db90e9d87fe4cf9`) reports two Checker profiles over the same 49 analyzed functions: one with 5 inference rounds, one with 10. Each profile has 2,704,056 cache hits and 14,755 misses. Combined candidate lookups remain 5,437,622: C3 turns repeated failed parses into negative-cache hits but does not reduce the number of parse-helper lookups.

Derived helper-hit counts across both candidate profiles are:

| Helper | Cache hits |
|---|---:|
| Binary expression split | 3,267,924 |
| Outer parentheses | 785,642 |
| Simple call | 763,198 |
| Member call | 568,932 |
| Top-level split | 22,416 |

These are not separate compiler counters: they are the Callgrind source-annotated execution counts of the five `++parse_cache_hits_` statements in `src/semantic_checker.cpp`, summed across the two Checker profiles. The total (5,408,112) matches twice the per-Checker aggregate counter (2,704,056).

The largest repeated shape is therefore cached binary-expression splitting during repeated function-body walks, rather than repeated execution of the textual parse itself. `walk_type_environment_block` recursively processes structured `if`/`else`, loop, and match branches while visitors derive expression constraints and update `TypeEnv`/join facts. The same source functions are revisited in multiple inference/checker passes, which explains the cache lookups; the aggregate profile does not prove that any one such traversal can be removed safely.

## Callgrind attribution and decision

Regex was checked alone with the optimized C3 compiler built with `-O2 -g` and Valgrind 3.22.0 Callgrind. It recorded 19,265,895,988 instruction references. `walk_type_environment_block` had 18,926,820,068 inclusive Ir (98.24%). This is inclusive cost over visitor callbacks and recursive control-flow branches, not the function's standalone cost. The parser/cache methods together account for less than 0.1% of Ir, so changing map lookup or cache structure is not justified by this profile.

**Decision: no C4 code change.** C3 already removes the expensive failed-parser executions. The remaining cache lookups are a small part of total cost, and the control-flow walker has stateful semantic effects (environment merges, inferred types, joins, and diagnostics). The profile alone does not identify a redundant walk. The separate C2 snapshot-reuse workstream is aimed at the known duplicate checker instance; after it integrates, profile the combined C2+C3 compiler and attribute walker cost by caller before considering any further pass elimination.

## Gates

No compiler source, tests, benchmark tooling, ABI, or language semantics change in this C4 workstream. The exact C3 candidate underlying this report passed the full 2,064-source differential, Fast Debug parity, `make check` (141/141), and `MOSS_PERF_BASELINE=274f848 make compiler-perf` with no noise-floor regression. C3's targeted D18 timing improved 5.0%; complete performance evidence is recorded in `C3-parse-cache.md`.
