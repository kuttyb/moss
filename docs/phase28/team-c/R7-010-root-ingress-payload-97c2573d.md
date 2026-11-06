# Team C verification: Root-ingress payload authority

## Scope and exact sources

- Candidate: `97c2573d8e11e1e5ac4d7a58cd28242bccf66c8d`
- Parent: `9b440e9376873d8480a52d87b2bc43b1fdd5d7c7`
- Fresh isolated detached worktree: `tmp/team-c-verify-97c2573d` (clean at setup; no tracked files edited).
- Read-only verification. No suite/oracle inputs or clone allowlists were changed. This is a focused checkpoint, not Phase 28 acceptance.

## Build and focused gates

Built the exact candidate with:

```sh
make --jobs="$(nproc)" COMPILER_DIR=tmp/team-c-97c2573d-build moss moss-opt
```

Build succeeded. Compiler artifact SHA-256:

- `moss-dev`: `0f65d8086f1b7bd4ab68c8baca6de9ce83e9fe8eb06341663c1c8b740ab0e936`
- `moss-opt`: `686b5c57bd9a9b0b5d073285f9a5207e7ef60f9140b28cf800558d71235d70c4`

Ran the candidate's registered focused gate in both modes:

```sh
env -u MOSS_EST python3 tests/tooling/check_phase28_root_ingress_capture_authority.py "$PWD/tmp/team-c-97c2573d-build/moss-dev"
MOSS_EST=shadow python3 tests/tooling/check_phase28_root_ingress_capture_authority.py "$PWD/tmp/team-c-97c2573d-build/moss-dev"
```

Both report `Phase 28 Root-ingress domain capture authority: 138 scenarios PASS`.
This includes fresh-constructor and string-literal payload facts, producer and consumer drop/duplicate/perturb fail-closed checks, Fast Debug output, and unsupported identifier/materialized-local cases.

## Independent native `-D warnings` checks

Generated and compiled the two positive payload fixtures independently in Default and EST-shadow, using isolated outputs under `tmp/team-c-97c2573d-build/rustc-checks`:

```sh
env -u MOSS_EST tmp/team-c-97c2573d-build/moss-dev tests/fresh_message_payload.moss -o tmp/team-c-97c2573d-build/rustc-checks/fresh-default.rs
rustc --edition=2021 -Dwarnings -C opt-level=0 tmp/team-c-97c2573d-build/rustc-checks/fresh-default.rs -o tmp/team-c-97c2573d-build/rustc-checks/fresh-default
tmp/team-c-97c2573d-build/rustc-checks/fresh-default
MOSS_EST=shadow tmp/team-c-97c2573d-build/moss-dev tests/fresh_message_payload.moss -o tmp/team-c-97c2573d-build/rustc-checks/fresh-shadow.rs
rustc --edition=2021 -Dwarnings -C opt-level=0 tmp/team-c-97c2573d-build/rustc-checks/fresh-shadow.rs -o tmp/team-c-97c2573d-build/rustc-checks/fresh-shadow
tmp/team-c-97c2573d-build/rustc-checks/fresh-shadow
env -u MOSS_EST tmp/team-c-97c2573d-build/moss-dev tests/string_payload_snapshot.moss -o tmp/team-c-97c2573d-build/rustc-checks/string-default.rs
rustc --edition=2021 -Dwarnings -C opt-level=0 tmp/team-c-97c2573d-build/rustc-checks/string-default.rs -o tmp/team-c-97c2573d-build/rustc-checks/string-default
tmp/team-c-97c2573d-build/rustc-checks/string-default
MOSS_EST=shadow tmp/team-c-97c2573d-build/moss-dev tests/string_payload_snapshot.moss -o tmp/team-c-97c2573d-build/rustc-checks/string-shadow.rs
rustc --edition=2021 -Dwarnings -C opt-level=0 tmp/team-c-97c2573d-build/rustc-checks/string-shadow.rs -o tmp/team-c-97c2573d-build/rustc-checks/string-shadow
tmp/team-c-97c2573d-build/rustc-checks/string-shadow
```

All four rustc invocations succeeded with warnings denied. Constructor fixture output was `fresh: 7` in both modes; string fixture output was `fresh` in both. Default and shadow generated sources match byte-for-byte per fixture:

- fresh source SHA-256: `559832493b58d6e8158d00512d14a3cbde9c2d9fc6f150c9eb6d774dab12993e`
- string source SHA-256: `843e2d2a39ddf528623aa98d3a086c29e6349c5842886cf78c6951bc7caea663`

## Fail-closed behavior and copy/clone boundary

The focused gate confirms `tests/phase26_direct_payload.moss` (identifier) and `tests/phase27/conformance/message_argument_binding_use.moss` (materialized local) remain checker-accepted without a payload authority fact, while native generation rejects both with `MOSS_INTERNAL_INVARIANT root message payload needs...`. This is the expected fail-closed boundary for the currently supported syntax.

Review of the parent-to-candidate source diff found the new checker fact explicitly sets `permits_moss_value_copy = false`; the native consumer requires an authoritative `TransferOwnedFreshRvalue` fact with that permission false. The generated payload expressions contain no `.clone()`. No clone allowlist or clone authorization file was added or changed. The change introduces checker authority for this fresh transfer case, not authorization for a Moss value copy.

`git diff --check 9b440e9376873d8480a52d87b2bc43b1fdd5d7c7 97c2573d8e11e1e5ac4d7a58cd28242bccf66c8d` passed.

## Team C scoped verdict

**PASS for the reviewed Root-ingress fresh-payload slice at this checkpoint.** The exact candidate builds; the registered 138-scenario gate passes in Default and EST-shadow; direct native compilation with `rustc -Dwarnings` and execution pass for both supported payload fixtures in both modes; unsupported identifier/materialized-local forms fail closed; no Moss copy/clone permission was introduced. This does not establish full Phase 28 GO or validate unrelated suite/oracle/provenance gates.
