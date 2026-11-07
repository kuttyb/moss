# Team C exact-SHA review: nested Root Message ownership

Date: 2026-11-06
Candidate: `ccfe4c2c749ea0493734816740e4dccdc31e4f23` (`phase28-r7-010-root-message-occurrence`), based on `e471c0be5fba4b164ee41bb38fd3d5991ecb665e`.
Fresh detached candidate checkout: `tmp/teamc-root-occurrence-ccfe`; clean at start and remains tracked-source clean. Independent baseline checkout: exact detached `8d2de3301e5f2dafef079439b0143a7b6675213e` at `tmp/teamc-root-occurrence-base-8d2de330`.

## Fresh build and gates

Candidate compiler bootstrap returned `ok=true`, `moss-0.1`. Fresh build used an initially absent cache directory:

`make --jobs="$(nproc)" COMPILER_DIR=tmp/teamc-ccfe-independent-a73/compiler moss-dev moss-opt`

- Dev compiler SHA-256: `e27a89f940ccc5f4cef4fe2af2c4e231ea0b0488580a62a145ee51d68f65f4e6`.
- Opt compiler SHA-256: `8527a2b46456874e1e95e5ef27df07cc2f22adba9955dd4dddca4f65c7c031b4`.

Commands and results:

- `env -u MOSS_EST python3 tests/tooling/check_phase21_root_expression_ingress.py tmp/teamc-ccfe-independent-a73/compiler/moss-dev` -> PASS.
- `env -u MOSS_EST python3 tests/tooling/check_phase28_root_ingress_capture_authority.py tmp/teamc-ccfe-independent-a73/compiler/moss-dev` -> 312 scenarios PASS.
- `MOSS_EST=shadow python3 tests/tooling/check_phase21_root_expression_ingress.py tmp/teamc-ccfe-independent-a73/compiler/moss-dev` -> PASS.
- `MOSS_EST=shadow python3 tests/tooling/check_phase28_root_ingress_capture_authority.py tmp/teamc-ccfe-independent-a73/compiler/moss-dev` -> 312 scenarios PASS.

The Phase 28 gate includes same-line/repeated-target messages, three-message `Wrap(Accept(Fetch()))` capture facts, Native `-Dwarnings` builds and execution, Fast Debug, unsupported payload fail-closed cases, and wrong owner-root / child-ExprId corruption checks in both engines.

## Owner and nested Message identity

For `tests/tooling/fixtures/phase28_root_depth2_message_arguments.moss` (`message worker.Wrap(message worker.Accept(message worker.Fetch()))`), checker capture facts are in preorder and identify:

- statement owner expression/handle ExprId 8; outer `Wrap` Message ExprId 8, receiver ExprId 3;
- nested `Accept` Message ExprId 7, receiver ExprId 4;
- nested `Fetch` Message ExprId 6, receiver ExprId 5.

The statement owner handle is bound by arena, source fingerprint, and origin ID. The outer expression is itself the `Wrap` Message, so its ExprId naturally equals the owner root ExprId; the nested child Message ExprIds are distinct from that root. Selected handlers and exact occurrence tuples remain distinct: `Worker.Wrap` (0:8:3), `Worker.Accept` (0:7:4), `Worker.Fetch` (0:6:5). Fast Debug and Native O0 `-Dwarnings` both print `7`.

The prior form `message worker.Accept(message worker.Accept(message worker.Fetch()))` also prints `7` in both engines. A four-Message chain `Wrap(Wrap(Accept(Fetch())))` likewise prints `7` in both. A stateful sibling-order probe `message worker.Pair(message worker.Next(), message worker.Next())` prints `12` in Fast Debug and Native, confirming left-to-right nested argument evaluation. Existing gate repetitions and same-line cases pass.

All four exact corruption hooks fail closed on the depth-two fixture:

- Native owner perturb: `Root-ingress copy lacks its exact statement source handle`.
- Native nested Message ExprId perturb: `active Root-ingress message does not match its exact EST handle`.
- Fast Debug owner perturb: `Root-ingress statement/message handle missing or ambiguous`.
- Fast Debug nested Message ExprId perturb: `Root-ingress argument does not match exact EST message`.

## Nested explicit-copy parity gap

Exact probe `tmp/ccfe-nested-explicit-copy.moss`:

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

`MOSS_TEST_REQUIRE_COPY_FACT=1` checker check succeeds. Capture facts distinguish outer `Wrap` Message ExprId 14 (receiver 8) from nested `Accept` ExprId 13 (receiver 9). Native generated Rust carries the exact copy-fact identity `typed-copy-fact-v1:c9576fc99dd23ecf852dda4e31843af41195986f0708c2b4710229dc33eef4df`, and Native O0 `-Dwarnings` execution prints `9` in Default and Shadow. Perturbing the copy owner fails closed with `Root-ingress copy lacks its exact statement source handle`.

Fast Debug instead fails in both modes with `MOSS_INTERNAL_INVARIANT Root-ingress argument created an unapproved independent value`. The nested `copy(payload)` is authorized and counted by the inner `Accept` argument boundary, but the outer `Wrap` argument preparation also observes that independent-call counter; this indicates nested-boundary accounting does not isolate the inner authorized copy. This behavior is inherited at least from the requested `8d2de330` comparison, although the failing boundary differs:

- Baseline rebuilt from inside exact detached 8d2de330 using `make -C tmp/teamc-root-occurrence-base-8d2de330 COMPILER_DIR=tmp/teamc-base-isolated/compiler moss-dev`; compiler SHA-256 `26f157f25dc619184ae1d3fcaf52065d5e32720e2727276904e1544c0be9243d`.
- Same probe checks successfully on base; Fast Debug fails `Root-ingress statement/message handle missing or ambiguous`; Native generation fails `active Root-ingress message ExprId unavailable`.
- At ccfe Native is repaired and succeeds, while Fast Debug still fails, now at nested copy accounting. Thus this is not newly introduced by ccfe, but parity remains incomplete for the nested explicit-copy case.

## Unsupported nonprimitive nested payloads

Two nested nonprimitive argument forms were checked in Default and Shadow. Checker accepts both; no authorized payload fact is available. Both engines fail closed:

1. `message worker.Wrap(message worker.Accept(identity(payload)))`, where `payload` is a local `Payload` and there is no `copy`: Fast Debug reports `nonprimitive Root-ingress argument lacks checked copy or fresh-transfer authority`; Native reports `root message payload needs a checked copy(x) fact or exact consume authority`.
2. `message worker.Wrap(message worker.Accept(message worker.Fetch()))`, where `Fetch` returns a fresh `Payload` constructor: same fail-closed results in both engines when no exact transfer authority is present.

## Verdict

Scoped verdict: **NO-GO** for exact candidate `ccfe4c2c749ea0493734816740e4dccdc31e4f23`. The previous accepted primitive two/three-level chain failures are fixed in both engines; owner/capture identities are exact, wrong owner/child perturbations fail closed, sibling ordering is preserved, and requested Phase 21 / Phase 28 gates pass in both modes. However, a nested explicit nonprimitive copy has valid checker and Native authority but Fast Debug rejects the inner authorized independent call while preparing the outer message argument. This parity limitation is inherited relative to `8d2de330`, but remains unresolved at this checkpoint. Unsupported nested nonprimitive cases without authority fail closed. This is not a full Phase 28 verdict and authorizes no clones.
