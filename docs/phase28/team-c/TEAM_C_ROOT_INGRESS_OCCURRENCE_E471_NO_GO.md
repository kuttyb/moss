# Team C exact-SHA verification: nested Root Message occurrence repair

Date: 2026-11-06
Candidate: `e471c0be5fba4b164ee41bb38fd3d5991ecb665e` (`phase28-r7-010-root-message-occurrence`), parent NO-GO `80028dbf27101f2e4017107450f08b3fc469ba7c`.
Fresh detached checkout: `tmp/teamc-root-occurrence-e471`; worktree was clean at start and remains clean (generated probes/build outputs are ignored under `tmp/`).

## Build and focused gates

- Exact candidate compiler bootstrap: `tmp/teamc-e471-build/compiler/moss-dev agent bootstrap --json` -> `ok=true`, `moss-0.1`.
- Fresh build: `make --jobs="$(nproc)" COMPILER_DIR=tmp/teamc-e471-build/compiler moss-dev moss-opt`.
- Dev binary SHA-256 `a7a907c60106fcc2a5797956c85706951d47e92470dbba7dde8ad0a70100139f`.
- Opt binary SHA-256 `c318adec8206a5b3eb3c53cdf78c222b40e522592c48dd0b207e74c0904bf267`.
- `env -u MOSS_EST python3 tests/tooling/check_phase21_root_expression_ingress.py tmp/teamc-e471-build/compiler/moss-dev` -> pass.
- `env -u MOSS_EST python3 tests/tooling/check_phase28_root_ingress_capture_authority.py tmp/teamc-e471-build/compiler/moss-dev` -> 278 scenarios PASS.
- The same two commands with `MOSS_EST=shadow` -> Phase 21 pass and Phase 28 278 scenarios PASS.

The Phase 28 gate covers repeated/same-line and nested-message argument forms, Native Rust `-Dwarnings` build/execution, corruption hooks for dropped/duplicated/perturbed requirements/facts, the new child-ExprId perturb hook, Fast Debug parity for the nested argument fixture, and unsupported payload fail-closed behavior. The Phase 21 gate covers nested Root ingress, native profiles, Fast Debug, and source-backed ABI-v9 provider checks.

## Parent/child occurrence evidence

For `tests/tooling/fixtures/phase28_root_nested_message_arguments.moss` (`message worker.Accept(message worker.Fetch())`), checker dump gives two distinct capture facts in EST preorder:

- Parent `Worker.Accept`: arena 0, Message ExprId 6, receiver ExprId 3, call edge `main|handler:Worker.Accept|...:10:0|receiver=worker|est=0:6:3`.
- Child `Worker.Fetch`: arena 0, Message ExprId 5, receiver ExprId 4, call edge `main|handler:Worker.Fetch|...:10:0|receiver=worker|est=0:5:4`.

The call-edge selected handler identities and exact EST occurrence IDs are distinct. Generated Rust lowers/evaluates the nested Fetch `runtime_invoke` inside the outer Accept argument, then invokes the parent Accept; this matches EST preorder consumption (parent entry first, recursive argument second) and language argument evaluation. Fast Debug prints `7`; Native generation followed by `rustc --edition=2021 -Dwarnings -C opt-level=0` and execution also prints `7`.

`MOSS_TEST_CONSUMER_PERTURB_ROOT_INGRESS_NESTED_MESSAGE_EXPR_ID=1` fails generation with `MOSS_INTERNAL_INVARIANT active Root-ingress message does not match its exact EST handle` (exit 1), so corruption fails closed.

An additional sibling-order probe with stateful `Next()` handlers and `message worker.Pair(message worker.Next(), message worker.Next())` passes check, prints `12` in Fast Debug, and prints `12` in Native O0 `-Dwarnings` execution. This distinguishes left-to-right child evaluation and supports sibling EST preorder pairing.

## Remaining accepted-depth defect

A three-level nested chain `message worker.Accept(message worker.Accept(message worker.Fetch()))` was checked with `moss-dev check --json`: `ok=true`, no diagnostics. Yet `moss-dev run --interp` fails with `MOSS_INTERNAL_INVARIANT Root-ingress statement/message handle missing or ambiguous`; Native generation fails with `MOSS_INTERNAL_INVARIANT Root-ingress copy lacks its exact statement source handle`. Checker capture dump independently shows three nested message ExprIds (8, 7, 6) with three selected handler facts, so the source is recognized as nested Root ingress rather than rejected.

## Verdict

Scoped verdict: **NO-GO** for `e471c0be5fba4b164ee41bb38fd3d5991ecb665e`. The previous two-level overlap case is repaired in both engines, exact parent/child identities are distinct, evaluation order agrees with EST preorder, corruption fails closed, and requested Phase 21/Phase 28 gates pass. However, the checker accepts a valid three-level parent/child Root Message chain that reaches Fast Debug and Native internal invariants. Full nested Root occurrence support therefore remains incomplete. This is not a whole Phase 28 verdict; no clone authority is granted.
