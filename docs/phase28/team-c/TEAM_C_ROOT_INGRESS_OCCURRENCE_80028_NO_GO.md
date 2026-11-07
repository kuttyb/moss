# Team C scoped verification: Root message occurrence repair

Date: 2026-11-06
Candidate source: `80028dbf27101f2e4017107450f08b3fc469ba7c` (`phase28-r7-010-root-message-occurrence`)
Fresh verification checkout: `tmp/phase28-r7-010-root-message-occurrence/tmp/teamc-root-occurrence-80028` (detached exact candidate; source tree clean at setup).

## Build identity

- Bootstrap: `./moss agent bootstrap --json` -> `ok=true`, `language_version=moss-0.1`.
- Built independently with `make --jobs="$(nproc)" COMPILER_DIR=tmp/teamc-occurrence-build/compiler moss-dev moss-opt`.
- `moss-dev` SHA-256: `71994cada460df793ffe77eb174ed280398fd01620bcbc032e8635c5c5ad5429`.
- `moss-opt` SHA-256: `e7fd0adc222c3c371c24ca1968d56faf6179b7dab6f14f59d8c08ddb2cca33c8`.

## Focused gates

Passed both commands (each ran against the independent development binary):

- `env -u MOSS_EST python3 tests/tooling/check_phase21_root_expression_ingress.py tmp/teamc-occurrence-build/compiler/moss-dev` -> `Phase 21 nested Root ingress and source-free trailer checks passed`.
- `env -u MOSS_EST python3 tests/tooling/check_phase28_root_ingress_capture_authority.py tmp/teamc-occurrence-build/compiler/moss-dev` -> 250 scenarios PASS.
- `MOSS_EST=shadow` versions of both commands -> same pass results, including 250 capture-authority scenarios.

The Phase 28 gate exercises repeated identical-target messages, same-line messages, nested Binary/Collection/Call/Condition message forms, distinct ExprId/call-edge identities, native `-Dwarnings` build and execution, and missing/duplicate/perturbed checker and Native consumer fact hooks in Default and Shadow. It also runs Fast Debug, traces the Root-ingress copy counts (zero implicit/authorized independent calls for fresh payload cases), and verifies unsupported nonprimitive payloads fail closed in Native generation and Fast Debug. The Phase 21 gate covers nested expression ingress, native O0/O3 execution, Fast Debug, and source-backed ABI-v9 provider behavior.

## Adversarial nested-message-in-message probe

Created only in ignored `tmp/`: `tmp/nested-message-argument-probe.moss`:

```moss
domain Worker:
  fn Fetch() -> Int:
    reply 7
  fn Accept(value: Int) -> Int:
    reply value
fn main():
  worker = Worker()
  echo message worker.Accept(message worker.Fetch())
```

`moss-dev check ... --json` accepts it (`ok=true`, no diagnostics), and `moss-dev run --interp ...` prints `7`. Native generation (`moss-dev ... -O0 -o ...`) fails with `MOSS_INTERNAL_INVARIANT Root-ingress message source ordering is ambiguous`. Inspection confirms the queue collects outer and nested Message EST nodes and rejects overlapping source spans. This is an accepted Root-ingress syntax that reaches the consumer invariant, so the checkpoint is **NO-GO** until the checker rejects this form during checking or Native occurrence pairing is made unambiguous by identity rather than overlapping spans. This finding is in scope and is not covered by passing repeated-sibling tests.

## Verdict and scope

Scoped verdict: **NO-GO** for candidate `80028dbf27101f2e4017107450f08b3fc469ba7c`: requested Phase 21 and Phase 28 gates pass, but the accepted nested Message argument reproduces a Native consumer invariant. No source, tests, oracle inputs, or clone allowlist were edited. No clone exception is authorized. This is not a full Phase 28 verdict.
