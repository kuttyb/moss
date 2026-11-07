# Team C exact-SHA review: Root-ingress owned-local transfer

Date: 2026-11-06
Candidate: `f033056c0eb6621ba12f4cac5180e29bde03ee66`; parent `e3c52c8ed7ec4a1088dd1cd61918b63ab0740c8e`.
Fresh detached review worktree: `tmp/teamc-f033-root-consume`, clean tracked source at start and finish. The compiler cache was verified absent before build: `tmp/teamc-f033-fresh-build/compiler`.

## Build and gate results

Fresh independent build:

`make --jobs="$(nproc)" -C tmp/teamc-f033-root-consume COMPILER_DIR=tmp/teamc-f033-fresh-build/compiler moss-dev moss-opt`

- Dev compiler SHA-256: `f7693e80c97396a482a5f38ca705ceb1bc41e3470a6bedb5ad5b8e07cdd1a959`.
- Opt compiler SHA-256: `9acad14d9dcde5b3d9f3b2f7180bbc782490df48e06ab1502d16f650580d2baf`.
- Candidate compiler bootstrap: `moss-0.1`, `ok=true`.

Focused Default and Shadow gates:

- `env -u MOSS_EST python3 tests/tooling/check_phase28_root_ingress_capture_authority.py tmp/teamc-f033-fresh-build/compiler/moss-dev` -> **414 scenarios PASS**.
- `MOSS_EST=shadow python3 tests/tooling/check_phase28_root_ingress_capture_authority.py tmp/teamc-f033-fresh-build/compiler/moss-dev` -> **414 scenarios PASS**.

## Checker authority identity and move parity

The focused fixture is `tests/phase4_materialized_message.moss`, whose `main` materializes a `Vector[Int]` and passes that local directly to `message sink.Total(materialized)`. Command:

`MOSS_TEST_DUMP_ROOT_INGRESS_ARGUMENT_DISPOSITION_FACT=1 moss-dev check tests/phase4_materialized_message.moss --json`

produces one CFG-derived fact:

`stmt=3 arena=0 owner=16 message=16 arg=0 arg_expr=15 binding=main@12:statement definition=main@12:statement:materialized type=vector[int] access=2 context=main`

The fact contains exact statement, arena, owner/Message occurrence, argument and identifier ExprIds, reaching binding and definition identities, closed type, Consume access, and main context. The checker derives it from the single reaching definition and availability state, requiring owned Consume demand and exact type. Missing/duplicate/contradictory requirement and fact sets and perturbations of owner, message, argument, binding, definition, type, and context are included in the focused gate.

Observed executions:

- Fast Debug: `./tmp/teamc-f033-fresh-build/compiler/moss-dev run --interp tests/phase4_materialized_message.moss` -> `12`.
- Native O0: generated with `-O0`, built using `rustc --edition=2021 -Dwarnings -C opt-level=0`, then executed -> `12`.
- Native optimized: generated with `-O`, built using `rustc --edition=2021 -Dwarnings -C opt-level=3`, then executed -> `12`.
- Generated Native argument is `let __moss_root_arg_1 = materialized;`; the argument expression has no `.clone()` and transfers the existing owned binding.

## Negative, ambiguous, nested, and copy controls

Independently reproduced checks:

- Post-use of `materialized` after Root Consume: rejected with `OWNERSHIP_USE_AFTER_CONSUME`.
- Branch-joined materialized local: rejected with `OWNERSHIP_ROOT_INGRESS_TRANSFER_UNPROVEN`.
- Loop-carried materialized local: rejected with `OWNERSHIP_ROOT_INGRESS_TRANSFER_UNPROVEN`.
- Nested non-owner local argument fixture `tests/tooling/fixtures/phase28_root_nested_local_argument_unproven.moss`: rejected with `OWNERSHIP_ROOT_INGRESS_TRANSFER_UNPROVEN`.
- Explicit-copy reuse (`message sink.Total(copy(materialized))`, then later echo the original): Fast Debug and Native both print `12` then `[2, 4, 6]`; the original binding remains available because the source copy is explicit and the transfer-disposition fact is not applied to it.

The 414-scenario gates additionally inject consumer drop/duplicate requirement and fact cases and perturb each owner/message/argument/binding/definition/type/context identity in Native and Fast Debug; those cases fail closed with internal invariants. Checker-side fact drop, duplicate, and identity perturbation checks are also exercised in both modes.

## Scoped verdict

**PASS for the exact direct-local Root-ingress Consume disposition slice at `f033056c0eb6621ba12f4cac5180e29bde03ee66`.** Checker-issued owner/statement/Message/argument/binding/definition/type/Consume facts match the Native and Fast Debug transfer behavior; the Native path moves the existing value without a Moss-value clone. Post-use, ambiguous branch/loop, and nested non-owner cases fail closed; explicit copies remain separately authorized and reusable. Focused Phase 28 gates pass 414/414 in Default and Shadow. This is not a full Phase 28 GO; no clone authorization is granted.
