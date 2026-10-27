# Phase 26.1 C1 Results — Function-Scoped Check & Package Rules

- Commit SHA: `5cdf6528d15bbb1e3aa1fc72c4b76a3bb1463dea`
- Branch: `phase26-1-m1`

## Scope and Invariants
- **R3**: `moss check <file>` checks everything transitively reachable from that file, function by function within its package. Sibling method calls, implicit iterators, module-qualified functions, and nominal field/domain type dependencies are fully retained. Unreachable helper and sibling declaration errors are omitted from file check scope.
- **R4**: Package-wide semantic rules (duplicate declarations, coherence, route conflicts, cross-handler analyses) run only under whole package modes (`margo build --check`, `margo build`).
- **Diagnostic Oracle**: File-mode diagnostics match whole-package diagnostics restricted to the reachable set.
- **Partial-reach fixture**: Verifies file check visits exactly reachable functions by identity (`m1_partial.moss:main`, `reached_top`, `reached_leaf`), whereas package mode checks all 6 functions.

## Validation Gates
1. `make --jobs="$(nproc)" check`: 144/144 passed.
2. Fast Debug parity shard: passed (`shard_fast_debug`).
3. `MOSS_M1_ENFORCE_TARGETS=1 make package-perf`: 70/70 scenario rows met.
4. Full Corpus Differential (2,064 sources):
   - 0 false rejections.
   - 0 hangs / timeouts.
   - 12 differences are all verified intended R3/R4 omissions of errors in unreachable sibling declarations (`calc_interpreter` and `text_toolkit`).
