# Independent review: Fast Debug imported indexed READ

**Candidate:** `1d64e51fc3ef3243aea29f4125ededa8cb6905e2`  
**Base:** reviewed imported native-authority SHA `a4cb6fb4a1fda8ea27c71c5908a06ef1ae9360f0`  
**Scoped verdict:** **GO** for Fast Debug borrowed execution of the covered imported `Vector[String]` conditional indexed reads. This does not approve whole Phase 28 or projected Box-field reads.

## Independent validation

The frozen worktree was clean at review start and HEAD exactly matched the candidate SHA. I built isolated development and optimized compilers:

```sh
make --jobs="$(nproc)" COMPILER_DIR=tmp/review-fastdebug-index-build moss-dev moss-opt
```

Build completed successfully. I ran the registered imported-generic gate on dev in both modes and on opt in both modes:

```sh
python3 tests/tooling/check_phase24_imported_generic_expressions.py tmp/review-fastdebug-index-build/moss-dev
env MOSS_EST=shadow python3 tests/tooling/check_phase24_imported_generic_expressions.py tmp/review-fastdebug-index-build/moss-dev
env MOSS_SKIP_TEST_HOOKS=1 python3 tests/tooling/check_phase24_imported_generic_expressions.py tmp/review-fastdebug-index-build/moss-opt
env MOSS_EST=shadow MOSS_SKIP_TEST_HOOKS=1 python3 tests/tooling/check_phase24_imported_generic_expressions.py tmp/review-fastdebug-index-build/moss-opt
```

All four passed with `Phase 24 imported generic expression specializations passed (source-backed and source-free)`. Dev runs cover checker/native corruption plus Fast Debug authority/requirement/fact drop, duplicate, and perturb hooks. The gate requires an observed authorized nonprimitive borrow during Fast Debug and confirms the expected source-backed output. Optimized runs skip test hooks, as those binaries do not have `MOSS_TEST_BUILD`; they independently exercise the functional Default/Shadow path. Source-free `.mossi` regeneration and native execution continue to pass via the base slice.

`git diff --check a4cb6fb4..1d64e51f` passed. No implementation files were modified during review.

## Authority and copy review

The checker now classifies the exact conditional Index result from its closed checked TypeRef. Copyable scalar results get `PrimitiveValue` disposition with `permits_copy=true` and no borrow claim. Nonprimitive results get `BorrowedNonprimitive`, `permits_copy=false`, and `temporary_nonescaping_borrow=true`. The shared fact predicate recomputes the disposition from the closed result type and rejects contradictions.

Fast Debug resolves each Index against the active condition handle/root, statement occurrence, EST arena and Index ExprId, statement slot, `frame.functional_context`, and `frame.specialization_key`. It requires exactly one requirement and fact and calls the typed module’s exact unique fact validator. Missing or duplicate global authority is caught against the checker-recorded occurrence count; missing/duplicate target rows, corrupted context/specialization, wrong receiver/selector ExprIds, or inconsistent disposition fail closed. The only no-row fallback is a separately checked Map conditional-error occurrence; otherwise absence throws an invariant before normal value evaluation, so the old String value-copy path is not used as fallback.

For nonprimitive Vector elements, evaluation checks the current located place against the exact receiver vector and integer selector, then returns a `BorrowedElement` containing the shared vector owner and index. Equality dereferences this representation to compare the existing `String` in place. This avoids both `independent()` and the previous by-value `Value` return that copied the underlying `std::string`. The registered positive-use counter test passed in both modes. The primitive path continues to return scalar values under its checker-issued primitive disposition.

## Known limitation

The Box projected-field probe remains outside this slice. The recorded failure is `conditional Map read authority unavailable`; that path is not treated as a successful borrow and is not covered by this scoped GO. Aggregate projected-field Fast Debug support remains open. The frozen test provider’s direct `Vector[String]` indexed comparison is the supported minimal repro.

## Scope boundary

This review approves only the checker-classified conditional indexed-read Fast Debug behavior on the exact SHA and its covered direct `Vector[String]` source-backed case. Full Default/Shadow suites, frozen ownership oracle, end-to-end Phase 28 parity, and independent runtime clone provenance remain pending and are not approved here.
