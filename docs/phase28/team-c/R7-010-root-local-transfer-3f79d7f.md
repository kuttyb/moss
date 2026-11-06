# Team C verification: Root local transfer

## Scope and exact revisions

- Candidate: `3f79d7f0206b6c9aa1e38dbfd506838f95bdc79a`
- Parent: `ce1bf102d4288dca47aa1aba28e8d96cc7efbd7a`
- Fresh detached verification worktree: `tmp/team-c-verify-3f79d7f`; source tree was clean at setup and remains unmodified.
- This is a focused candidate review, not full Phase 28 acceptance.

## Build and registered Root gate

Built isolated candidate artifacts:

```sh
make --jobs="$(nproc)" COMPILER_DIR=tmp/team-c-3f79d7f-build moss moss-opt
```

Build succeeded. Artifact SHA-256:

- `moss-dev`: `4ca53228a30d52a77952b6387678e7f69508928913472ee7e2305d299eb62f44`
- `moss-opt`: `40a07d46f0aa76a444f6d120d0d9a067f61d399579dd5a800aeb0a3a88d98094`

Ran the registered Root gate:

```sh
env -u MOSS_EST python3 tests/tooling/check_phase28_root_ingress_capture_authority.py "$PWD/tmp/team-c-3f79d7f-build/moss-dev"
MOSS_EST=shadow python3 tests/tooling/check_phase28_root_ingress_capture_authority.py "$PWD/tmp/team-c-3f79d7f-build/moss-dev"
```

Both modes report `190 scenarios PASS`. The gate covers fresh payload/no-copy fail-closed behavior, explicit-copy checker and consumer corruption hooks, native compilation/execution, Fast Debug, and the Phase 26 direct-payload fixture among the explicit-copy cases.

The Phase 26 fixture was also run directly in Fast Debug:

```sh
env -u MOSS_EST tmp/team-c-3f79d7f-build/moss-dev run --interp tests/phase26_direct_payload.moss
MOSS_EST=shadow tmp/team-c-3f79d7f-build/moss-dev run --interp tests/phase26_direct_payload.moss
```

Both produce `7 8`. In each Root-gate mode, the fixture's checked explicit copy is required (`MOSS_TEST_REQUIRE_COPY_FACT=1`), emitted with `typed-copy-fact-v1` provenance and lowered through the Root thunk, compiled by `rustc -O -D warnings`, and executed with expected output `7 8`. Producer drop/duplicate/perturb and consumer drop/duplicate/perturb copy-fact corruptions are rejected.

The negative fixture `tests/phase28/r7_010/root_ingress_local_without_copy.moss` is checker-accepted without payload authority, while native generation fails closed with the expected Root-ingress payload invariant. This confirms that the direct local payload does not acquire implicit native copy authority.

## Optimizer parity

Ran:

```sh
env -u MOSS_EST python3 tests/tooling/check_phase27_optimizer_expression_uses.py "$PWD/tmp/team-c-3f79d7f-build/moss-dev"
```

All six registered cases passed three-engine Fast Debug/native/optimized parity, with callback counts `6, 3, 9, 3, 3, 3` respectively. The missing-dependency-handle case also passed its injected fail-closed check.

## Authority boundary and runtime limitation

The candidate's source diff introduces exact `TypedCopyFactV1` resolution and threads that authority into native Root lowering. The explicit-copy hook tests demonstrate native producer and consumer fail-closed behavior. The changed checker code does not introduce a message `CONSUME` fact, and the candidate diff changes no clone allowlist/authorization file.

However, Fast Debug does not consume this typed copy authority. `src/interpreter_domains.inc` is unchanged from the parent. In `prepare_handler_call()` it still applies `independent(...)` to every evaluated message argument in all three overloads (source invocation, EST message, and executor Root submission). `independent()` recursively allocates copies for objects and collections. Consequently, for the Phase 26 `message worker.Process(copy(data))`, the explicit checked `copy(data)` is evaluated and then the generic argument preparation performs another independent deep copy. The observed output parity (`7 8`) is not provenance evidence and does not show that Fast Debug follows the exact checker copy decision.

This candidate therefore fixes the native explicit-copy Root path and prevents implicit local payload transfer from reaching native generation, but leaves a runtime semantic-authority gap open: the checker copy fact is not consumed by Fast Debug and generic `independent()` cloning remains unconditional. This is a scoped blocker for treating copy authority as end-to-end complete.

`git diff --check ce1bf102d4288dca47aa1aba28e8d96cc7efbd7a 3f79d7f0206b6c9aa1e38dbfd506838f95bdc79a` passed. No full suites or oracle were run.

## Team C scoped verdict

**Native focused gates pass; end-to-end copy-authority review remains NO-GO pending the Fast Debug authority gap.** The candidate does not earn an overall Phase 28 GO from this review.
