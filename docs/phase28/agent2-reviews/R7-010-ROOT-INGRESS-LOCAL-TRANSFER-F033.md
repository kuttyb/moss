# Agent 2 review: CFG-issued Root-ingress direct-local Consume

**Candidate:** `f033056c0eb6621ba12f4cac5180e29bde03ee66`
**Parent:** `e3c52c8ed7ec4a1088dd1cd61918b63ab0740c8e`
**Fresh review worktree:** `tmp/agent2-review-root-consume-f033`

**Scoped verdict: GO for exact direct-local Root-ingress transfer disposition.** The checker issues authority only for a direct nonprimitive Identifier argument whose exact reaching definition is owned, Consume-demanded, type-equal and Available in the CFG. Native and Fast Debug move that binding once; ambiguous/nested cases fail closed. This is not a full Phase 28 GO or clone authorization.

## Exact build and gates

- Candidate `git rev-parse HEAD`: `f033056c0eb6621ba12f4cac5180e29bde03ee66`.
- Fresh worktree was clean before review; no candidate source/test files were edited.
- `make --jobs="$(nproc)" moss-dev` — exit 0.
- Compiler SHA-256: `f7693e80c97396a482a5f38ca705ceb1bc41e3470a6bedb5ad5b8e07cdd1a959`.
- `python3 tests/tooling/check_phase28_root_ingress_capture_authority.py build/compiler/moss-dev` — PASS, 414 scenarios.
- `MOSS_EST=shadow python3 tests/tooling/check_phase28_root_ingress_capture_authority.py build/compiler/moss-dev` — PASS, 414 scenarios.
- `git diff --check e3c52c8e..f033056c` — PASS.

The registered gate covers Default/Shadow fact dump, checker fact drop/duplicate/field perturb, consumer requirement/fact drop/duplicate/field perturb, O0 and optimized native, Fast Debug parity, explicit-copy reuse, post-use rejection, branch/loop ambiguity, nested non-owner rejection, and owned-local transfer.

## Checker authority and CFG identity

For a direct nonprimitive Identifier argument at the root-owned Message, the producer records a fact with exact statement occurrence, arena, owner-root ExprId, Message ExprId, argument index/ExprId, source fingerprint, binding identity, reaching-definition identity, ownership specialization identity, EST context, closed TypeRef, `Consume`, owned and available flags. The producer requires exactly one reaching definition for the local name, definition-owned with Consume demand, structurally equal closed type, and `Available` in the saved `availability_at_statement` CFG state. It marks the statement use as Consume so ordinary post-use/CFG checks see the move.

For `tests/phase4_materialized_message.moss`, independently ran `MOSS_TEST_DUMP_ROOT_INGRESS_ARGUMENT_DISPOSITION_FACT=1 build/compiler/moss-dev check ... --json`; the exact dump was:

`stmt=3 arena=0 owner=16 message=16 arg=0 arg_expr=15 binding=main@12:statement definition=main@12:statement:materialized type=vector[int] access=2 context=main`.

Consumer requirement/fact joins bind statement, arena, owner ExprId, Message ExprId, argument index/ExprId, fingerprint and context. The semantic API then compares the complete requirement and fact, including binding/definition/type/specialization identity and availability fields. Capture authority separately confirms the selected exact receiver/handler/call-edge occurrence. `main` is the only Root-ingress consumer context; no generic body specialization can be crossed.

## Native/Fast Debug transfer and fail-closed probes

Native resolves the disposition for the exact Message argument and passes its local expression to the move thunk without appending `.clone()`. Fast Debug resolves the same exact fact, rejects aliases and consumed/missing locals, moves the value out of `caller.locals`, erases the old local, and records it consumed. Both execute the gate fixture with output `12` in O0, optimized Native and Fast Debug in Default and Shadow.

Additional adversarial scratch checks in `artifacts/`:

- `duplicate-local-arguments.moss`: passing the same local twice to two nonprimitive parameters is rejected by checker in Default/Shadow as `OWNERSHIP_USE_AFTER_CONSUME`; the second argument cannot reuse the first transfer.
- `two-root-local-arguments.moss`: two distinct locals passed through two root Messages in separate Echo arguments yield two distinct facts sharing statement/arena/context but with distinct owner/Message ExprIds and binding/definition IDs. Native and Fast Debug both print `9 10` in Default/Shadow; the joins do not cross the sibling occurrences.
- The registered post-use mutation adds `echo materialized` after transfer and checker rejects with `OWNERSHIP_USE_AFTER_CONSUME`.
- Branch-join and loop-carried generated probes reject with `OWNERSHIP_ROOT_INGRESS_TRANSFER_UNPROVEN`; they do not get a disposition from a guessed/ambiguous definition.
- `phase28_root_nested_local_argument_unproven.moss` checker-rejects a nonprimitive local passed to a nested, non-owner Message with `OWNERSHIP_ROOT_INGRESS_TRANSFER_UNPROVEN`. No nested consumer falls back to implicit copy.
- Explicit-copy reuse replaces `message sink.Total(materialized)` with `message sink.Total(copy(materialized))`, then reads `materialized`; the original remains available, and Native/Fast Debug parity is preserved.

Drop/duplicate/perturb tests cover owner, Message, argument, binding, definition, type and context in checker, Native and interpreter paths. Missing/duplicate entries fail with internal-invariant diagnostics; perturbations do not authorize transfer.

## Scope and limits

This fact authorizes a move of one checked owned binding and grants no Moss copy action. Native Rust move and Fast Debug `std::move`-equivalent value transfer are tested for the scoped fixture. The review does not approve generated runtime clone sites or exceptions. Full Phase 28 suites, frozen ownership oracle and clone-provenance gates were not run.
