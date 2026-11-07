# Team C exact-SHA review: nested Root-ingress explicit copy

Date: 2026-11-06
Candidate: `a9d8cf256ff217a06599d29fe2ecb494b43920be`; parent `ccfe4c2c749ea0493734816740e4dccdc31e4f23`.
Fresh detached review worktree: `tmp/teamc-a9d8-copy-fastdebug` (tracked source clean). Build cache was verified absent before use: `tmp/teamc-a9d8-copyfast-build/compiler`.

## Fresh compiler and focused gates

Candidate compiler bootstrap: `./tmp/teamc-a9d8-copyfast-build/compiler/moss-dev agent bootstrap --json` -> `ok=true`, `language_version=moss-0.1`.

Fresh isolated build command:

`make --jobs="$(nproc)" -C tmp/teamc-a9d8-copy-fastdebug COMPILER_DIR=tmp/teamc-a9d8-copyfast-build/compiler moss-dev moss-opt`

- `moss-dev` SHA-256: `5ec7ddf4fa43cc0c9da16ee6b7e7180eb817acd602ef0fba4e610b53bf1e646f`.
- `moss-opt` SHA-256: `d9aa18712ce0302ea959aec612b4e2b2e2e5e9f4c3fd9de5bca27530224547e5`.

Exact scoped commands/results:

- `env -u MOSS_EST python3 tests/tooling/check_phase28_root_ingress_capture_authority.py tmp/teamc-a9d8-copyfast-build/compiler/moss-dev` -> **368 scenarios PASS**.
- `MOSS_EST=shadow python3 tests/tooling/check_phase28_root_ingress_capture_authority.py tmp/teamc-a9d8-copyfast-build/compiler/moss-dev` -> **368 scenarios PASS**.
- `env -u MOSS_EST python3 tests/tooling/check_phase21_root_expression_ingress.py tmp/teamc-a9d8-copyfast-build/compiler/moss-dev` -> PASS.
- `MOSS_EST=shadow python3 tests/tooling/check_phase21_root_expression_ingress.py tmp/teamc-a9d8-copyfast-build/compiler/moss-dev` -> PASS.

No full Phase 28 suite or oracle was run.

## Exact nested explicit-copy probe

The parent reproduction is `tmp/teamc-root-occurrence-ccfe/tmp/ccfe-nested-explicit-copy.moss`:

```moss
type Payload:
  value: Int
domain Worker:
  fn Accept(value: Payload) -> Int:
    reply value.value
  fn Wrap(value: Int) -> Int:
    reply value
fn main():
  worker = Worker()
  payload = Payload(value = 9)
  echo message worker.Wrap(message worker.Accept(copy(payload)))
```

The candidate's registered fixture `tests/tooling/fixtures/phase28_root_nested_copy_payload.moss` exercises the same case. For both Default and Shadow:

- `MOSS_TEST_REQUIRE_COPY_FACT=1 <moss-dev> check ... --json` succeeds.
- Fast Debug prints `9`; with `MOSS_TEST_TRACE_ROOT_INGRESS_COPY_COUNTS=1`, trace reports the inner argument as `authority=copy`, `explicit_copy_evaluations=1`, `authorized_independent_calls=1`, `implicit_independent_calls=0`, and outer argument as `authority=primitive` with all counts zero.
- Native `-O0` generation, `rustc --edition=2021 -Dwarnings -C opt-level=0`, and execution print `9`.
- Generated Rust contains exact copy authority `typed-copy-fact-v1:6bff7bfc08438ee1d1a4c34b8ba09b17de1ae6c9d916d6ca295cca235aed69ab` for `copy(payload)`, alongside separate outer Wrap and inner Accept capture facts.

At parent `ccfe4c2c`, the same semantic source checked and Native generation succeeded, but Fast Debug failed with `MOSS_INTERNAL_INVARIANT Root-ingress argument created an unapproved independent value`. At `a9d8cf2`, the same source succeeds in both engines. This candidate fixes the observed nested-copy Fast Debug parity gap.

## Adversarial authority checks

The registered Phase 28 gate invokes the owner-root and copy-ExprId perturbations for the nested-copy fixture in Default and Shadow. I independently reran them against the fresh exact candidate binary; all failed closed:

- Native `MOSS_TEST_CONSUMER_PERTURB_ROOT_INGRESS_COPY_OWNER`: `Root-ingress copy lacks its exact statement source handle`.
- Native `MOSS_TEST_CONSUMER_PERTURB_ROOT_INGRESS_COPY_EXPR_ID`: `Root-ingress copy does not match exact message argument`.
- Fast Debug `MOSS_TEST_INTERPRETER_PERTURB_ROOT_INGRESS_COPY_OWNER`: `checked Root-ingress copy was not evaluated exactly once`.
- Fast Debug `MOSS_TEST_INTERPRETER_PERTURB_ROOT_INGRESS_COPY_EXPR_ID`: same exact-once invariant.

I also independently reran Native and Fast Debug fact drop, duplication, and perturbation hooks. Each exited nonzero with an invariant (`required checked copy fact unavailable`, `duplicate or contradictory checked copy facts`, or `required checked copy fact contradicts requirement`).

An adversarial helper case puts `copy(value)` inside `duplicate(value)` and passes `duplicate(payload)` into nested Root Accept. The checker accepts the source, but Native generation and Fast Debug both fail closed for missing exact Root copy/fresh-transfer authority; the inner helper's unrelated copy fact does not authorize the outer Root payload.

## Depth and other scoped regression probes

- Candidate fixture `Wrap(Accept(Fetch()))`: Native and Fast Debug both print `7`; capture facts retain three distinct Message ExprIds and selected handler identities.
- Prior same-handler chain `Accept(Accept(Fetch()))`: both engines print `7`.
- Four-Message primitive chain `Wrap(Wrap(Accept(Fetch())))`: both engines print `7`.
- Stateful sibling order probe `Pair(Next(), Next())`: both engines print `12`.
- Existing Phase 28 repeated-target and same-line scenarios pass as part of 368-scenario gates.
- Unsupported nested nonprimitive payloads without exact authority remain fail-closed in the prior ccfe review; this candidate gate also retains the registered unsupported-payload checks in both modes.

## Scoped verdict

**PASS for the nested explicit-copy Fast Debug repair at exact SHA `a9d8cf256ff217a06599d29fe2ecb494b43920be`.** The exact nested copy fact is recognized by checker and Native, Fast Debug binds copy evaluation to that exact owner/message/argument/copy occurrence, both engines agree on output, and adversarial owner/ExprId/fact corruption fails closed. Focused Phase 21 and Phase 28 gates pass in Default/Shadow. This is not a full Phase 28 GO, no full suite/oracle certification, and no clone authorization.
