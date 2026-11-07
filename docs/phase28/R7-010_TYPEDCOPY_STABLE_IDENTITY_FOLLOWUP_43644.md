# R7-010 TypedCopyFact identity follow-up

## Scope

This isolated follow-up starts at `43644e9d84947eaeb556422b9da1a917983c2bc2`.
It stabilizes the identity used for a checked explicit-copy fact and generated
copy helper across checkout roots. It does not change Moss copy semantics,
runtime clone markers, sidecar schema, or authorization rows. The exact source
path, source bytes, source hash, EST spans, callable identity, and occurrence
fields remain present in the checked fact and artifact record.

Worktree: `tmp/phase28-r7-010-typedcopy-stable-identity-43644`  
Branch: `phase28-r7-010-typedcopy-stable-identity-43644`

## Root cause and correction

`native_checked_copy_identity()` previously hashed the complete serialized
`TypedCopyFactV1` evidence row. That row included the absolute path in both
`source_file` and `provenance.source_file`. Checking identical source bytes
from a second checkout therefore changed the fact ID, the first-16-hex helper
name, and the Rust marker. The prior `43644e9` gate normalized those IDs only
for its structural digest; that did not stabilize the identity itself.

The identity serializer now substitutes the exact source SHA-256 into those
two path fields only while hashing. It still hashes the source digest, exact
copy/source spans and EST IDs, statement occurrence, specialization and
callable/module context, and checked type/access disposition. Normal sidecar
serialization continues to emit the physical source path. The independent
Python oracle uses the same identity projection, while validating the original
path and source bytes before accepting a fact.

## Two-checkout evidence

The same fixture bytes were compiled from two distinct checkout-like paths.
Both source facts retain their own exact absolute path, but their content hash,
semantic fact identity, generated helper identity, and provenance audit match:

| Checkout | Exact fact source path | Source SHA-256 | TypedCopyFact identity | Generated fact identity | Audit |
|---|---|---|---|---|---|
| A | `tmp/typedcopy-identity-a/tests/phase28/oracle_r7_005_1/fixtures/oracle_10a_explicit_copy_only.moss` | `sha256:90e2f18939e3db4be156b9725599c24286a1d617f539aa43c139ef5b56765f22` | `typed-copy-fact-v1:fc1d82a2b1c25828500236edc9afe6042c2720271a0b5bd4ed3b064ce99571a9` | suffix `fc1d82a2b1c25828` | `UNAUTHORIZED`, 1 authorized / 3 unauthorized / 3 unclassified |
| B | `tmp/typedcopy-identity-b/tests/phase28/oracle_r7_005_1/fixtures/oracle_10a_explicit_copy_only.moss` | same | same | suffix `fc1d82a2b1c25828` | same |

The focused gate also changes source bytes and confirms the identity changes.
A separate forged-path test confirms that two facts can share the stable
semantic identity while the production oracle still rejects a fact whose
`source_file` does not match the artifact being audited.

## Validation

- `make --jobs="$(nproc)" moss-dev` — passed.
- `python3 tests/phase28/r7_010/team_b/check_clone_provenance_v3_builder.py` —
  passed; C++ builder checks exact sidecar paths and stable fact identities.
- `MOSS_EST=shadow python3 tests/phase28/r7_010/team_b/check_clone_provenance_v3_builder.py`
  — passed.
- `python3 -m unittest -v tests.phase28.oracle_r7_005_1.test_oracle_harness tests.tooling.test_phase28_r7_010_clone_inventory`
  — 48 tests passed, 1 skipped.
- The same unittest command with `MOSS_EST=shadow` — 48 tests passed, 1
  skipped.
- `python3 tests/tooling/check_phase28_r7_010_runtime_clone_evidence.py build/compiler/moss-dev tests/phase28/oracle_r7_005_1/fixtures/oracle_10a_explicit_copy_only.moss`
  — passed; repeated with `MOSS_EST=shadow` — passed.
- Python compile checks, feedback validation, and `git diff --check` — passed.

No full suite or frozen oracle run is claimed. No runtime clone was authorized.
Fresh exact-SHA Team C and Agent 2 reviews remain required before integration.
