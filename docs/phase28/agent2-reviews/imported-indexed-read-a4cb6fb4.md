# Independent review: imported generic indexed-expression READ

**Implementation SHA:** `a4cb6fb4a1fda8ea27c71c5908a06ef1ae9360f0`  
**Base:** `7a17015187ecd2652229157f13ca39cdfbe6bbe8`  
**Scoped verdict:** **GO** for this imported generic indexed-expression READ authority slice. This does not approve whole Phase 28.

## Independent validation

The checked-out branch is a status-only descendant `2c5fc831451e7d0547cc25eb0f28edf21bb8243c` whose parent is the exact implementation SHA. The implementation was reviewed and built at that tree; candidate source files were not edited. I built isolated Debug and optimized compilers:

```sh
make --jobs="$(nproc)" COMPILER_DIR=tmp/review-imported-index-build moss-dev moss-opt
```

The build completed successfully. I ran the imported generic gate in all four compiler/mode combinations:

```sh
python3 tests/tooling/check_phase24_imported_generic_expressions.py tmp/review-imported-index-build/moss-dev
env MOSS_EST=shadow python3 tests/tooling/check_phase24_imported_generic_expressions.py tmp/review-imported-index-build/moss-dev
env MOSS_SKIP_TEST_HOOKS=1 python3 tests/tooling/check_phase24_imported_generic_expressions.py tmp/review-imported-index-build/moss-opt
env MOSS_EST=shadow MOSS_SKIP_TEST_HOOKS=1 python3 tests/tooling/check_phase24_imported_generic_expressions.py tmp/review-imported-index-build/moss-opt
```

All four passed: `Phase 24 imported generic expression specializations passed (source-backed and source-free)`. The gate rebuilds the provider and consumer, runs Debug/Release and Margo Debug paths with expected output `-1\n7\n3\n1\n1\n`, then rebuilds the consumer using only the provider’s compiled `.mossi`/rlib. Source-free check/build and executable output all pass. The development compiler exercises all 12 checker/consumer fact/requirement drop, duplicate, and perturb hooks; optimized functional runs set `MOSS_SKIP_TEST_HOOKS=1` because optimized binaries do not define `MOSS_TEST_BUILD`.

`tests/run_parallel.py` routes this hook-dependent job to `moss-dev` via its `lightweight` set. It likewise routes X1 v9/v10 and Phase 27 `.mossi` hook jobs to dev, while the functional/optimized test coverage remains separate. `git diff --check 7a170151..a4cb6fb4` passed. The test was already registered in the parallel test manifest; this change corrects which compiler executes test-only hooks.

## Authority and adversarial review

The checker emits `TypedIndexedExpressionReadFactV1` only for a checked `if`/`while` condition with an exact condition EST handle and statement occurrence. It anchors the fact to the exact Index, receiver and selector ExprIds, source span, receiver binding and definition identities, closed receiver/selector/result TypeRefs, Read access, specialization identity/context, EST fact context, and source provenance. Its authority predicate requires a closed `vector[T]` receiver, closed primitive `int` selector, result TypeRef structurally equal to `T`, Read access, temporary nonescaping borrow, and `permits_copy=false`. Incomplete producer facts throw an invariant error.

The Native consumer requires one exact active condition handle, walks only that condition’s reachable EST nodes, matches the exact indexed source occurrence and specialization/context, then calls the typed-module unique fact/requirement validator. It rechecks the exact EST Index base/index IDs, Read access, `permits_copy=false`, temporary nonescaping status, specialization identity/context, and closed lowering receiver/result TypeRefs before returning the indexed place expression. Missing, duplicate, contradictory, or incomplete authority raises `MOSS_INTERNAL_INVARIANT`; no clone or copy fallback is authorized.

As an additional malformed-use probe, I modified only a temporary provider fixture under `tmp/review-index-escape` so an imported generic `if` condition passed `items[i]` to a helper returning the indexed `String`. Native package build exited nonzero in checker processing with `moss: error: checker indexed expression-read authority is incomplete`; it did not reach lowering and did not use the new READ fact to authorize a consuming/escaping argument. The registered positive case remains the nonescaping comparison `items[i] == target`.

Fast Debug’s indexed-expression execution locates a `Place` and reads its referenced element; it does not call `independent()` or authorize a Moss `copy`/transfer. The source-backed Margo Debug parity in the gate succeeds. The focused scope grants no new Fast Debug authority and makes no claim that all host-language `Value` representations avoid implementation-level value materialization.

## Scope boundary

This is a scoped GO for the exact checker-to-Native imported indexed READ repair and its dev-only hook routing. Full Default/Shadow suites, frozen ownership oracle, whole-engine parity, and independent runtime clone provenance remain pending and are not approved by this review.
