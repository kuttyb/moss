# Agent 2 review: Root message occurrence threading

**Candidate:** `80028dbf27101f2e4017107450f08b3fc469ba7c` (`phase28-r7-010-root-message-occurrence`)

**Scoped verdict: GO for this exact occurrence-threading checker/native slice, with an inherited Native limitation recorded below.** This is not an end-to-end Phase 28, suite, clone-provenance, or ownership-oracle approval.

## Exact checkout and validation

- `git rev-parse HEAD`: `80028dbf27101f2e4017107450f08b3fc469ba7c`.
- Worktree was clean before review. No candidate source or test files were edited.
- Rebuilt the test compiler with `make --jobs="$(nproc)" moss-dev`; exit 0.
- Compiler SHA-256: `71994cada460df793ffe77eb174ed280398fd01620bcbc032e8635c5c5ad5429` (`build/compiler/moss-dev`).
- `python3 tests/tooling/check_phase21_root_expression_ingress.py build/compiler/moss-dev` — PASS.
- `MOSS_EST=shadow python3 tests/tooling/check_phase21_root_expression_ingress.py build/compiler/moss-dev` — PASS.
- `python3 tests/tooling/check_phase28_root_ingress_capture_authority.py build/compiler/moss-dev` — PASS, 250 scenarios.
- `MOSS_EST=shadow python3 tests/tooling/check_phase28_root_ingress_capture_authority.py build/compiler/moss-dev` — PASS, 250 scenarios.
- `git diff --check 80028dbf^..80028dbf` — PASS.

## Identity and traversal review

The checker recursively walks retained EST message nodes under each statement expression handle and retains a call edge for each exact message ExprId. Capture facts bind the exact statement occurrence, arena, Message ExprId, receiver ExprId, handle fingerprint/origin, main context, receiver binding, domain instance/graph/routes, handler identity, and a uniquely selected call edge. The call-edge identity includes the exact message and receiver ExprIds; the Native consumer rechecks that exactly one edge matches all stored fields.

Native creates a queue from the exact statement handle. It checks the root handle span and fingerprint against the statement source, gathers Message nodes with their EST IDs, source spans, target ExprIds and argument ExprIds, sorts by lexical span, rejects overlapping/ambiguous message spans, and checks each emitted message source against the next queued source. Under-consumption, over-consumption, invalid IDs/spans, missing/duplicate facts, and perturbed ExprId fail closed. Receiver and handler parsed by Native are then compared to the exact queued EST Message/Identifier before fact lookup; target/source syntax does not itself create authority.

Checker and queue walkers cover Binary, Call, Take, Member, Index, Parenthesized, Message, NamedArgument, Collection/Tuple, Constructor, and Pipeline children. Native lowering renders supported operator operands, call arguments, collection entries, and condition expressions in source order. Each statement expression / Echo argument is wrapped in its own queue, so multiple statement handles do not share identity or consumption state. The repeated identical-target binary fixture has two distinct checker ExprIds and runs Default/Shadow; its consumer tests drop/duplicate requirements and facts and perturb the message ExprId, all failing closed. The Phase 21 nested fixture exercises messages under Binary, Collection, Call, Condition, and Parenthesized contexts.

One limitation follows from lexical queue ordering: the repeated identical-source fixture proves unique identities and fail-closed corruption but cannot distinguish a hypothetical swap of two semantically identical occurrences by output alone. Code inspection shows the supported expression renderers recurse in source order, while source-span mismatch and exact fact lookup reject differing messages. No divergent supported traversal was found. Any future renderer that intentionally reorders children must change this queue protocol or fail closed.

## Nested Message argument probe

I created only an ignored scratch probe at `tmp/root-message-nested-argument-probe.moss`:

```moss
domain Worker:
  fn Fetch() -> Int:
    reply 7
  fn Echo(value: Int) -> Int:
    reply value
fn main():
  worker = Worker()
  echo message worker.Echo(message worker.Fetch())
```

`build/compiler/moss-dev check tmp/root-message-nested-argument-probe.moss --json` accepts the expression. Native generation at this candidate fails closed with `MOSS_INTERNAL_INVARIANT Root-ingress message source ordering is ambiguous`: outer and inner message spans overlap. I ran the same generation probe using the available parent compiler at `tmp/phase28-r7-010-root-interpreter-authority/build/compiler/moss-dev` (parent source SHA `7e4a56b3461ffd949f89542ad50bc7ccd4557241`); it also fails closed, with `MOSS_INTERNAL_INVARIANT active Root-ingress message ExprId unavailable`. Thus this is an inherited unsupported Native form, not introduced by this slice, and no authority is granted. This review does not claim that checker acceptance plus Native rejection is ideal behavior; it is explicitly a remaining limitation. The nested payload is primitive `Int`; nonprimitive payload expressions remain restricted to existing exact fresh/copy checker authorities, with no implicit-copy fallback added here.

## Scope and risks

- `git diff --name-only 80028dbf^..80028dbf` includes `src/semantic_checker.cpp` and `src/native_generator.cpp`, but no interpreter/Fast Debug source. The interpreter remains unchanged by this commit; these edits do not add Fast Debug authority or compute semantics there.
- Capture facts retain `permits_moss_value_copy=false`; this patch only threads occurrence identity and adds no clone/copy permission.
- Focused Default and EST-shadow gates passed. Full suite, ownership oracle, and runtime clone-provenance checks were not run and are not approved by this scoped verdict.
