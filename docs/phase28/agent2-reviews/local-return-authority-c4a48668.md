# Independent review: local specialized ReturnValue authority

**Implementation SHA:** `c4a48668b7a1adee0528efa8e8ff1a7770231e54`  
**Status-only descendant:** `ea8cddeba7f344c27509aadcde11cfbef50ece87`  
**Scoped verdict:** **GO** for the local specialized ReturnValue checker authority change. This does not approve whole Phase 28.

## Independent validation

The implementation tree was reviewed at the exact SHA above; the checked-out HEAD is the requested status-only descendant and its parent is the implementation SHA. The candidate implementation files were unchanged during review. I built isolated Debug and optimized compilers:

```sh
make --jobs="$(nproc)" COMPILER_DIR=tmp/review-local-return-build moss-dev moss-opt
```

The build completed successfully. Focused checks:

```sh
python3 tests/tooling/check_phase28_local_return_authority.py tmp/review-local-return-build/moss-dev
```

Result: `Phase 28 local ReturnValue EST authority: 44 scenarios PASS`. The two modes are Default and EST-shadow. The gate checks checker authority shape, native debug and optimized generation plus Rust `-D warnings` compile/run parity, Fast Debug parity, checker drop/contradict/perturb cases, and Native/Fast Debug consumer drop/duplicate/perturb/context/specialization corruption.

```sh
python3 tests/tooling/check_phase26_1_1_x1_v10.py tmp/review-local-return-build/moss-dev
```

Result: ABI-v10 Section 2 binding, private helper execution, generic Section 1 and provider-only E1 recheck passed. In particular, the imported generic ReturnValue EST source-fact drop, source-context perturb, duplicate fact, and wrong consumer specialization probes fail closed.

```sh
python3 tests/tooling/check_phase27_est_mossi.py tmp/review-local-return-build/moss-dev
```

Result: Phase 27 `.mossi` EST passed in raw Default/Shadow parity, including one generic and two Section 2 imported dumps. Its provider source-context drop hook failed closed as required.

`git diff --check c043ea07..c4a48668` passed. The focused gate is registered for both `check-parallel` and `check-shadow` in the implementation commit.

## Authority and fail-closed review

The producer adds the local specialized path only when there is an active checked specialization and current function owner; the owner is not an imported projected body, is defined in the current main module, is the current return owner, and is being checked in its unspecialized function context. It verifies unique function-table/declaration identity, a unique body statement occurrence, a unique specialization object and stable type key, a closed specialization return TypeRef structurally equal to the ReturnValue expression type, a valid exact EST occurrence, and matching specialization identity/context.

The producer records the closed specialization-selected return type only at the exact ReturnValue EST occurrence and specialization context. A pre-existing contradictory closed TypeRef is rejected; an injected contradictory existing fact, dropped local fact, or perturbed local fact each fails closed in the registered gate. The resulting ReturnValue fact retains that precise EST context for Native and Fast Debug consumers to verify. No authority is inferred in either consumer.

The local producer predicate explicitly excludes imported projected bodies and requires `defining_module == p_.main_module`. I independently ran both imported-path probes: X1 v10 checks the imported generic Section 1 fact/source-context hooks, and the Phase 27 `.mossi` gate checks the provider source-context drop path. Both passed; the imported source fact is not rescued by local fact materialization.

The fixture’s local specialization returns a primitive integer. The registered gate verifies generated specialized bodies contain no `.clone()` or `.to_owned()`, then compiles both Native modes with warnings denied and checks output `3\n`; Fast Debug also produces `3\n`. This change adds semantic ReturnValue type authority only and introduces no copy/clone authorization.

## Scope boundary

This is a scoped GO for the exact local ReturnValue authority implementation only. Full Default/Shadow suites, the frozen ownership oracle, end-to-end Phase 28 parity, and the independent runtime clone provenance gate remain pending and are not approved by this review.
