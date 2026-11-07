# Agent 2 review: nested explicit-copy Fast Debug authority

**Candidate:** `a9d8cf256ff217a06599d29fe2ecb494b43920be`
**Parent:** `ccfe4c2c749ea0493734816740e4dccdc31e4f23`
**Review worktree:** `tmp/agent2-review-root-copy-a9d8`

**Scoped verdict: GO for the nested explicit-copy Fast Debug repair.** Exact checker Copy authority now flows through the nested Root message into exactly one Fast Debug independent value, and no implicit independent copy was accepted. This does not approve broader runtime clone provenance or whole Phase 28.

## Build and focused gates

- Candidate checkout is exact `a9d8cf256ff217a06599d29fe2ecb494b43920be`; no candidate source/test file was edited.
- `make --jobs="$(nproc)" moss-dev` — exit 0.
- Candidate compiler SHA-256: `5ec7ddf4fa43cc0c9da16ee6b7e7180eb817acd602ef0fba4e610b53bf1e646f`.
- `python3 tests/tooling/check_phase28_root_ingress_capture_authority.py build/compiler/moss-dev` — PASS, 368 scenarios.
- `MOSS_EST=shadow python3 tests/tooling/check_phase28_root_ingress_capture_authority.py build/compiler/moss-dev` — PASS, 368 scenarios.
- `git diff --check ccfe4c2c..a9d8cf25` — PASS.

## Explicit-copy fixture evidence

The registered fixture `tests/tooling/fixtures/phase28_root_nested_copy_payload.moss` nests an exact `copy(payload)` argument in `message worker.Accept(...)`, itself nested under `message worker.Wrap(...)`.

For both Default and EST-shadow, independently ran:

1. `MOSS_TEST_REQUIRE_COPY_FACT=1 build/compiler/moss-dev check tests/tooling/fixtures/phase28_root_nested_copy_payload.moss --json` — `ok: true`. The checker retains a Copy requirement/fact.
2. Native generation, `rustc --edition=2021 -O -Dwarnings`, and execution — output `9`.
3. `build/compiler/moss-dev run --interp ...` — output `9`.
4. `MOSS_TEST_TRACE_ROOT_INGRESS_COPY_COUNTS=1 ... run --interp ...` — exactly two records: nested `Accept` reports `authority=copy`, one explicit copy evaluation, one authorized independent call, zero implicit calls; outer `Wrap` reports primitive authority and zero copy/independent calls.

The generated Rust includes a Moss copy-fact marker at the exact `copy(payload)` expression and carries its copied value through the inner handler thunk before returning the primitive reply. This confirms explicit checker-authorized copy lowering for this fixture; it is not a general approval of runtime `.clone()` sites.

An additional adversarial scratch fixture in `artifacts/nested-two-copies.moss` passes in both modes: a nested handler receives `copy(payload)` twice, Native and Fast Debug both print `8`, and the trace has two separate `authority=copy` records each with exactly one authorized independent call and zero implicit calls, followed by one primitive outer record. This checks sibling counter isolation and no counter leakage.

## Exact identity and fail-closed behavior

The Fast Debug checker path resolves the copy fact before evaluation using the exact statement occurrence, arena, nested Message ExprId, argument index and copy ExprId; it verifies the Copy requirement's source child and TypeRef. It creates a scoped `RootIngressCopyAuthorityOccurrence` containing statement ID, arena, owner-root ExprId, nested Message ExprId, argument index and copy ExprId. At `copy(...)` execution, the interpreter confirms the current statement, tree/arena, owner root, current copy ExprId, and that the same copy ExprId is the selected child of the selected Message argument. Only then does the runtime copy counter advance.

The authority scope is restored around each message argument, and the message-level counter scope restores all three counters (implicit independents, authorized independents, explicit Copy evaluations) on exit, including nested calls. The two-copy adversarial probe confirms each sibling copy is counted independently and child counts do not leak into its parent.

Independent corruptions all failed closed:

- Native owner perturb: `MOSS_TEST_CONSUMER_PERTURB_ROOT_INGRESS_COPY_OWNER=1` → `Root-ingress copy lacks its exact statement source handle`.
- Native copy ExprId perturb: `MOSS_TEST_CONSUMER_PERTURB_ROOT_INGRESS_COPY_EXPR_ID=1` → `Root-ingress copy does not match exact message argument`.
- Fast Debug owner perturb: `MOSS_TEST_INTERPRETER_PERTURB_ROOT_INGRESS_COPY_OWNER=1` → checked Copy was not evaluated exactly once.
- Fast Debug copy ExprId perturb: `MOSS_TEST_INTERPRETER_PERTURB_ROOT_INGRESS_COPY_EXPR_ID=1` → checked Copy was not evaluated exactly once.
- Fast Debug Copy fact drop, duplicate and perturb hooks each produced `MOSS_INTERNAL_INVARIANT` before accepting the value.

No path uses source text to select an ownership action. The candidate leaves the checker producer and TypedCopyFact identity schema unchanged; it threads an already resolved, exact checker fact into the interpreter's runtime copy evaluation.

## Parent comparison

Built exact parent `ccfe4c2c749ea0493734816740e4dccdc31e4f23` in `tmp/teamc-root-occurrence-ccfe`. On the same nested-copy fixture, checker accepted it in Default and Shadow, but Fast Debug failed in both modes with `MOSS_INTERNAL_INVARIANT Root-ingress argument created an unapproved independent value`. Candidate `a9d8cf25` changes this to successful output `9` with exactly one checked Copy and no implicit independent calls, so this is a repair rather than a regression.

## Scope

No full suites, ownership oracle, or runtime clone-provenance gate was run. This scoped GO approves only the exact nested explicit Copy authority behavior described here. It does not authorize unreviewed clone sites or exceptions.
