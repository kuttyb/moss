# Phase 28 B4 — Consumer Fail-Closed Verification

**Base:** R7-008 frozen implementation `e9d3f844bae715809f6b78f4d0d0f8ba908db6bc`

**Scope:** Match authority fallback, missing Match requirements/facts, and substring-based ownership specialization selection. No merge to `phase28-work` or `main` is authorized.

## Findings and classification

1. **Option match fallback — inherited fail-open path.** R7-008 inherited the Match lowering introduced in R7-003 (`480fa258`). Both native and Fast Debug asked for `TypedMatchOwnershipFactV1`, but when lookup returned null they classified Option using a type-string/runtime-value string. The checker creates a requirement for every Match, so a missing typed record is not a documented unsupported case. R7-008's prior `MOSS_TEST_DROP_MATCH_FACT` coverage ran at `moss check` with test-only contract validation; it did not exercise consumers. The current change makes the requirement and fact mandatory, uses the exact checked scrutinee TypeRef/kind, and reads payload types from the selected checked arm. No raw Option type-string inference remains in Match lowering.

2. **Other missing ownership records — inherited bounded debt, left scoped.** Local ownership consumers intentionally retain a bounded legacy path for `is_intentionally_unsupported()` records, as recorded in the compiler contract. This slice does not convert all local ownership to required facts or broaden the migration. It adds consumer-level tests for missing Match requirements/facts, including a test-only consumer requirement-drop hook, so Match cannot silently enter the legacy path.

3. **Substring-based specialization selection — inherited matcher, tightened.** Match, loop, and exceptional ownership resolvers accepted a requirement when its context merely contained the active specialization marker. The exact `functional_context` is already available; interpreter frames now also carry the stable specialization key. All three resolvers require exact context and key, and fact-to-requirement audits compare those same identities. Test-only target labels that use substring selection remain confined to mutation-hook setup; they do not select production semantic facts.

## Architecture boundary

The R7-008 branch does not contain `docs/COMPILER_ARCHITECTURE.md`. For its current stage map, this audit read that guide from `origin/main`; the version difference is explicitly acknowledged. The base compiler contract remains the authoritative debt ledger. No new semantic decision class, checker edit, shared API edit, `src/moss.cpp` change, broad EST/string migration, or oracle expectation change is included.

## Validation

- `make --jobs="$(nproc)"`: PASS (compiler rebuilt in this isolated worktree).
- `python3 tests/tooling/check_phase28_b4_consumer_fails_closed.py ./moss`: PASS. It verifies Option and generic user-enum matches across Fast Debug, native Debug, and native `-O`; then confirms 21 consumer failures for dropped/perturbed/duplicated Match facts, a missing Match requirement, and a specialization identity deliberately changed to a substring of the active function context.
- `python3 tests/tooling/check_phase28_r7_003_consumers.py ./moss`: PASS.
- `python3 tests/tooling/check_phase28_specialized_native_consumer.py ./moss`: PASS.
- `python3 tests/tooling/check_phase28_team_b_consumer.py ./moss`: PASS.
- `python3 tests/tooling/check_phase28_take_consumer.py ./moss`: PASS (22/22 targeted fail-closed cases).
- Full `make check` and `make check-shadow` were not run; this assignment requested targeted coverage and coordinated avoidance of concurrent full suites.

## Review and inherited defects

Independent adversarial review: pending on the frozen implementation commit. No review verdict is claimed yet.

The R7-008 report's other known items are unchanged: native clone provenance sidecar is missing; native `copy(x)` and one-argument `Map.get(key)` retain clone debt; nested `take(...)` native lowering is limited to supported root slots; and CollectionError registration/raise inference is absent. This slice does not claim those defects are repaired.
