# Agent 2 review: nested Root message occurrence order

**Candidate:** `e471c0be5fba4b164ee41bb38fd3d5991ecb665e` (`phase28-r7-010-root-message-occurrence`)

**Scoped verdict: NO-GO for the nested-message Native path.** The EST-preorder queue repair correctly pairs the tested one-level parent/child messages, but a valid deeper chain still fails Native lowering before all queued occurrences can be consumed. This is not a whole Phase 28 verdict.

## Checkout and focused validation

- `git rev-parse HEAD`: `e471c0be5fba4b164ee41bb38fd3d5991ecb665e`.
- Candidate worktree was clean before review. No tracked source/test changes were made.
- `make --jobs="$(nproc)" moss-dev` — exit 0. Compiler SHA-256: `a7a907c60106fcc2a5797956c85706951d47e92470dbba7dde8ad0a70100139f`.
- `python3 tests/tooling/check_phase21_root_expression_ingress.py build/compiler/moss-dev` — PASS.
- `MOSS_EST=shadow python3 tests/tooling/check_phase21_root_expression_ingress.py build/compiler/moss-dev` — PASS.
- `python3 tests/tooling/check_phase28_root_ingress_capture_authority.py build/compiler/moss-dev` — PASS, 278 scenarios.
- `MOSS_EST=shadow python3 tests/tooling/check_phase28_root_ingress_capture_authority.py build/compiler/moss-dev` — PASS, 278 scenarios.
- `git diff --check 80028dbf..e471c0be` — PASS.
- `MOSS_TEST_CONSUMER_PERTURB_ROOT_INGRESS_NESTED_MESSAGE_EXPR_ID=1 build/compiler/moss-dev tests/tooling/fixtures/phase28_root_nested_message_arguments.moss -O0 -o tmp/root-message-corrupt.rs` — fails closed with `MOSS_INTERNAL_INVARIANT active Root-ingress message does not match its exact EST handle`.

## Preorder and exact authority audit

The queue now retains EST preorder and rejects duplicate `(arena_index, Message ExprId)` pairs. This order matches Native's supported recursion: `expr_lower` recognizes a parent Message first; `root_ingress_message` lowers its arguments in order; a nested Message argument then enters `expr_lower` and consumes the next queue item. Binary operands, ordinary call arguments, collection elements, and condition operands are likewise lowered left-to-right. Parenthesized/group nodes do not introduce Message queue entries; the Message node's own source span excludes its wrappers, so the trimmed source consistency check still matches the consumed Message.

The text check is only a consistency guard. A queue item comes from an exact EST arena/ExprId, and capture lookup then verifies the actual EST Message target/handler, exact receiver ExprId, statement occurrence, arena, source fingerprint/origin, context, and unique selected call edge. Queue underflow and unconsumed entries fail closed; duplicate EST identities fail closed. The child-specific corruption hook changes the second queued Message ExprId, and exact-handle resolution rejects it.

Adversarial scratch probes under `tmp/` showed the Native queue passes these forms (generated Rust compiled with `rustc --edition=2021 -Dwarnings -C opt-level=0`, and outputs matched expected values):

- `Pair((message One()), (message Two()))` — output `3`.
- `[message One(), message Two()]` — output `3` after indexing and adding the elements.
- `if message Yes() and (message Yes())` — output `condition-ok`.

The registered repeated same-target binary case passes Default/Shadow and emits two distinct checker ExprIds. The source strings are equal, so output alone cannot reveal swapping identical occurrences; identity uniqueness and deliberate child-ExprId corruption provide the relevant authority checks.

## Blocking deeper-chain reproduction

The following checker-accepted source is in ignored scratch file `tmp/root-message-occurrence-depth2.moss`:

```moss
domain Worker:
  fn Fetch() -> Int:
    reply 7
  fn Accept(value: Int) -> Int:
    reply value
  fn Wrap(value: Int) -> Int:
    reply value
fn main():
  worker = Worker()
  echo message worker.Wrap(message worker.Accept(message worker.Fetch()))
```

`build/compiler/moss-dev check tmp/root-message-occurrence-depth2.moss --json` returns `ok: true`. Native generation fails with `MOSS_INTERNAL_INVARIANT Root-ingress copy lacks its exact statement source handle`.

The failure occurs after the outer Message has entered the preorder queue: when Native lowers the nested `Accept` Message, `root_ingress_message` asks `resolve_root_ingress_copy_fact` about `Fetch` under the nested `Accept` ExprId. That resolver requires the current Message ExprId to match a top-level statement expression handle. Only the outer `Wrap` Message is the statement root handle, so the nested `Accept` occurrence cannot pass that check. The queue order itself is not the failing check; the nested invocation's statement-handle assumption prevents reaching/consuming the child Message.

A deeper chain and nested siblings were also combined in `tmp/root-message-occurrence-adversarial.moss`; its Native generation hits the same exact-statement-handle invariant. The new one-level fixture passes because the child `Fetch` has no arguments and does not enter this nested copy-fact query. Thus the tests establish one-level nested payload support, not arbitrary nested Message arguments.

## Scope notes

- The source diff from `80028dbf` changes Native queue ordering and its tests/fixture/status only; interpreter/Fast Debug sources are unchanged. No Fast Debug, copy, or clone authority is added by this commit.
- This review found no source-text authority path, wrong-fact acceptance, or queue over/under-consumption bypass. The failing deeper chain is fail-closed, but it is a checker-accepted Native expression that the nested-message path does not lower, so the candidate does not close the requested nested-chain behavior.
- Whole Phase 28 suites, ownership oracle, and runtime clone-provenance gates were not run or approved.
