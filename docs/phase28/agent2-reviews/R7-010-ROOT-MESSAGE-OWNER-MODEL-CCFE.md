# Agent 2 review: Root-ingress owner-root and nested Message identity

**Candidate:** `ccfe4c2c749ea0493734816740e4dccdc31e4f23` (`phase28-r7-010-root-message-occurrence`)

**Scoped verdict: GO for the explicit owner-root vs nested Message ExprId model.** The exact identities stay joined through checker requirements and both consumers, and the two/three-message chain cases plus perturbations pass. Fast Debug has inherited, checker-accepted edge forms that still fail closed; they are documented below and are not newly introduced by this candidate. This is not a whole Phase 28, suite, oracle, or clone-provenance approval.

## Exact checkout and gates

- `git rev-parse HEAD`: `ccfe4c2c749ea0493734816740e4dccdc31e4f23`.
- Candidate worktree was clean before review. No tracked candidate files were edited.
- `make --jobs="$(nproc)" moss-dev` — exit 0. Compiler SHA-256: `e27a89f940ccc5f4cef4fe2af2c4e231ea0b0488580a62a145ee51d68f65f4e6`.
- `python3 tests/tooling/check_phase21_root_expression_ingress.py build/compiler/moss-dev` — PASS.
- `MOSS_EST=shadow python3 tests/tooling/check_phase21_root_expression_ingress.py build/compiler/moss-dev` — PASS.
- `python3 tests/tooling/check_phase28_root_ingress_capture_authority.py build/compiler/moss-dev` — PASS, 312 scenarios.
- `MOSS_EST=shadow python3 tests/tooling/check_phase28_root_ingress_capture_authority.py build/compiler/moss-dev` — PASS, 312 scenarios.
- `git diff --check ccfe4c2c^..ccfe4c2c` — PASS.

The registered tests run native generation, `rustc -Dwarnings`, execution, and Fast Debug for both the two-Message `Accept(Fetch())` and three-Message `Wrap(Accept(Fetch()))` fixtures in Default and Shadow. They also run owner-root and nested-message ExprId perturbations in both modes. The hook cases fail closed as asserted by the gate.

## Identity review

Native queue entries now carry both `owner_expression` (the root ExprId selected from the statement expression handle) and each nested `message_expression` ExprId. The owner-handle verifier requires exactly one statement handle with the exact arena, root ExprId, source fingerprint, and origin ID. Capture, payload, and copy queries check this owner before looking up authority for the nested Message. The nested Message lookup independently checks its EST node, receiver ExprId, target name, and handler. The source substring comparison only checks that Native recursion consumed the queued EST node in the expected source order; it does not choose the target or grant ownership.

Fast Debug keeps the original `tree.root` as owner while receiving the current nested Message ExprId during recursive EST evaluation. It verifies that the current node is a Message with the expected target/handler and joins the checker requirement using statement occurrence, arena, nested Message ExprId, owner-handle fingerprint/origin, and `main` context. The fact lookup compares the full requirement/fact identity, including receiver and call-edge identity; the selected handler is checked against the evaluated domain instance's handler identity. Main Root-ingress has no generic specialization context to cross. Both native and interpreter paths reject missing/ambiguous owner handles and invalid nested IDs.

Repeated same-target binary messages have distinct checker ExprIds and pass Native Default/Shadow execution; consumer ExprId corruption is rejected. Parentheses around root and child messages pass Native generation and `-Dwarnings` execution. A larger adversarial Native probe, `tmp/root-message-occurrence-adversarial.moss`, contains a three-message chain with sibling messages in a call, a collection with sibling messages, and a two-message condition; Default and Shadow generate and execute output `3\n3\n3\ncondition-ok\n`. Thus EST preorder matches the current Native recursion for these forms.

The depth-two fixture covers three nested Message occurrences and passes Native and Fast Debug in both modes. A `Wrap(Accept(Fetch()))` chain is the three-Message case; the combined four-Message adversarial chain also passes Native. No target or ownership decision is derived from source text. This commit adds no copy permission or clone authorization; existing capture facts continue to require `permits_moss_value_copy=false`.

## Exact baseline comparison for remaining Fast Debug failures

I built exact base `8d2de3301e5f2dafef079439b0143a7b6675213e` in reviewer worktree `tmp/review-root-message-base-8d2de330` (base compiler SHA-256 `26f157f25dc619184ae1d3fcaf52065d5e32720e2727276904e1544c0be9243d`). For both Default and `MOSS_EST=shadow`, these checker-accepted sources reproduce the same Fast Debug failures at base and candidate:

- `tmp/root-message-occurrence-probe-1.moss`: a `Pair((message One()), (message Two()))` call with parenthesized sibling arguments. `check --json` returns `ok:true`; Native generates, compiles with `-Dwarnings`, and prints `3`. Fast Debug at both base and candidate fails closed with `MOSS_INTERNAL_INVARIANT Root-ingress argument lacks an exact closed TypeRef`.
- `tmp/root-message-occurrence-probe-3.moss`: `if message Yes() and (message Yes())`. `check --json` returns `ok:true`; Native generates, compiles with `-Dwarnings`, and prints `condition-ok`. Fast Debug at both base and candidate fails closed with `MOSS_INTERNAL_INVARIANT Root-ingress message lacks an exact checked EST occurrence`.
- `tests/tooling/fixtures/phase28_root_repeated_nested_messages.moss`: repeated same-target messages in a binary expression. Checker accepts it; Native gate prints `14`. Fast Debug at both base and candidate fails closed with the same missing exact checked EST occurrence diagnostic.
- `tmp/root-message-occurrence-wrapper.moss`: an outer parenthesized three-Message chain with additional parenthesized nested arguments. Checker accepts it; Native generates/compiles and prints `7`. Fast Debug at both base and candidate fails closed with the exact closed TypeRef diagnostic.

The base was built using `make --jobs="$(nproc)" moss-dev`; base checker and interpreter commands were run on the same source paths with both modes. These are existing interpreter type-retention/EST-routing limitations, not candidate regressions or wrong-fact acceptance. The candidate's registered owner-root chain cases work in Fast Debug, but general sibling/condition/parenthesized-child coverage remains a separate gap if required by broader acceptance.

## Scope

Full default/shadow suites, frozen ownership oracle, and runtime clone-provenance gates were not run or approved. No clone or copy exception is authorized by this review.

## Nested explicit-copy Fast Debug comparison

The Team C-reported behavior was independently reproed with `tmp/root-message-explicit-copy-nested.moss`:

```moss
type _Payload:
  value: Int
domain Reader:
  fn Read(p: _Payload) -> Int:
    reply p.value
  fn Wrap(value: Int) -> Int:
    reply value
fn main():
  reader = Reader()
  p = _Payload(value = 9)
  echo message reader.Wrap(message reader.Read(copy(p)))
```

At `ccfe4c2c`, `check --json` returns `ok:true`; `MOSS_TEST_REQUIRE_COPY_FACT=1 ... check ... --json` also returns `ok:true`, confirming the checker retained a Copy requirement/fact. Native generation plus `rustc --edition=2021 -Dwarnings -C opt-level=0` executes and prints `9` in both Default and EST-shadow. Fast Debug fails in both modes with `MOSS_INTERNAL_INVARIANT Root-ingress argument created an unapproved independent value`.

I rebuilt exact base `8d2de3301e5f2dafef079439b0143a7b6675213e` in `tmp/review-root-message-base-8d2de330`. There, the same checker and `MOSS_TEST_REQUIRE_COPY_FACT=1` checks pass, while Fast Debug fails in both modes earlier with `MOSS_INTERNAL_INVARIANT Root-ingress statement/message handle missing or ambiguous`. The nested explicit-copy path therefore did not work before `ccfe`; this candidate advances the path through the new owner-root check but still does not authorize/evaluate the independent copy in Fast Debug. It remains a fail-closed limitation, not a newly introduced acceptance regression.

`ccfe` does change `src/interpreter_domains.inc` on this path: it separates owner-root identity from the nested Message ExprId and binds origin ID. It does not change the interpreter's explicit-copy authorization/count logic. No implicit copy is accepted by either version.
