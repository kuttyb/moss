# Team C verification: Fast Debug Root copy authority

## Scope and exact revisions

- Candidate: `7e4a56b3461ffd949f89542ad50bc7ccd4557241`
- Parent: `3f79d7f0206b6c9aa1e38dbfd506838f95bdc79a`
- Fresh detached worktree: `tmp/team-c-verify-7e4a56b` (clean at setup; no tracked files edited).
- Focused verification only; no full suite or Phase 28 oracle run.

## Build and Root gate

Built candidate in isolated paths:

```sh
make --jobs="$(nproc)" COMPILER_DIR=tmp/team-c-7e4a56b-build moss moss-opt
```

Build succeeded. Artifact SHA-256:

- `moss-dev`: `7c515b9a0ddb43f78f5e61738e46512e708cebaf2f7194405b053def9ee03c55`
- `moss-opt`: `1955138eab30e77a001433ca3db27a12f2fb7f94ce1c602213bcd46d6304133b`

Ran the registered Root gate in Default and EST-shadow:

```sh
env -u MOSS_EST python3 tests/tooling/check_phase28_root_ingress_capture_authority.py "$PWD/tmp/team-c-7e4a56b-build/moss-dev"
MOSS_EST=shadow python3 tests/tooling/check_phase28_root_ingress_capture_authority.py "$PWD/tmp/team-c-7e4a56b-build/moss-dev"
```

Both report `Phase 28 Root-ingress domain capture authority: 214 scenarios PASS`. The gate exercises checker/native/interpreter fact drop, duplicate, and perturbation hooks; unsupported nonprimitive locals fail closed in both native lowering and Fast Debug.

## Fast Debug authority and copy counts

The gate independently checks trace counters for fresh, explicit-copy, and primitive Root arguments in both Default and Shadow. Direct Default traces reproduced these exact outcomes:

- `tests/fresh_message_payload.moss`: `authority=fresh`, `explicit_copy_evaluations=0`, `authorized_independent_calls=0`, `implicit_independent_calls=0`; output `fresh: 7`.
- `tests/phase26_direct_payload.moss`: `authority=copy`, `explicit_copy_evaluations=1`, `authorized_independent_calls=1`, `implicit_independent_calls=0`; output `7 8`.
- `tests/phase28/r7_010/root_ingress_primitive_computed_reply.moss`: `authority=primitive`, all copy counters zero; output `43`.

The Phase 26 fixture trace was also reproduced under `MOSS_EST=shadow` with the same exact copy counts and output. The explicit-copy gate verifies the checker `TypedCopyFactV1`, exact source/argument binding, and interpreter drop/duplicate/perturb corruption hooks. The fresh transfer and primitive cases similarly check their authority fact corruption hooks. Root-boundary preparation no longer calls generic `independent()` for fresh or primitive values; an explicit checked `copy(...)` produces exactly one top-level authorized independent value. Recursive traversal inside that single deep-copy operation is intentional.

## Native/optimized/Fast Debug parity

Independently compiled each fixture to native Rust both without optimization and with Moss `-O` plus rustc `-O`, then compared output with Fast Debug:

```text
tests/fresh_message_payload.moss: Fast Debug/native/optimized PASS: fresh: 7
tests/phase26_direct_payload.moss: Fast Debug/native/optimized PASS: 7 8
tests/phase28/r7_010/root_ingress_primitive_computed_reply.moss: Fast Debug/native/optimized PASS: 43
```

The registered optimizer-expression three-engine gate also passed all six cases (callback counts `6, 3, 9, 3, 3, 3`).

`git diff --check 3f79d7f0206b6c9aa1e38dbfd506838f95bdc79a 7e4a56b3461ffd949f89542ad50bc7ccd4557241` passed.

## Clone/provenance assessment and verdict

The candidate adds checker-authority lookup and a Root-scoped interpreter preparation path. The observable test counters are incremented at the top-level `independent()` call while Root argument preparation is active; recursive field/element cloning remains part of that one authorized deep-copy operation. The focused gate confirms zero implicit Root-boundary independent calls for fresh/primitive transfers and exactly one authorized independent call for explicit `copy`. No clone authorization or allowlist file changes appear in the parent-to-candidate diff. I found no clone-provenance concern in this slice's Root boundary behavior.

**Team C scoped verdict: PASS for the Root-local-transfer/Fast Debug authority slice.** This review establishes the requested focused gates only; it does not establish full Phase 28 acceptance or GO.
