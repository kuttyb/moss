# Phase 24 Team A CBOR L7 corrective acceptance

**Role:** Fresh independent L6 Architect reviewer

**Verdict:** **GO**

**Exact reviewed implementation SHA:** `142b75009c229a318f46b52670f3d7b8aaca848b`

**Corrective implementation SHA:** `debf99f068735630ca28a7d5ee4fc343832ac094`

**Previously accepted candidate SHA:** `c80cf8ac51a9fa264b271b5a56c89ea2068ff4b7`

**Reviewed branch:** `phase24-advanced-data-cbor-l7-architect`

**Reviewed worktree:** `tmp/worktrees/team-a-cbor-l7-architect`

**Language contract:** `moss-0.1`

**Date:** 2026-10-13

## Scope and independence

This review is limited to the two L7-requested CBOR corrections and their
regression evidence:

1. every array child and every map key/value `parents[]` entry must equal the
   exact owner of the linked edge; and
2. a node already designated as the document root must be rejected as an
   array child, map key, or map value with typed `CborError.InvalidRoot` before
   mutation.

I had no implementation ownership. I independently inspected the correction,
the two primary regressions, all eight added forged-record tests, the repaired
ownership-consistent parent cycle, and the compiled-only ABI-v9 consumer. I did
not modify source, tests, prior Architect reports, delivery/metrics/status
documents, or developer-experience records. The only review-authored change is
this report. I did not merge or push.

Mandatory startup was completed in the designated worktree. The branch and
candidate SHA matched the request before source inspection. I read `AGENTS.md`,
both repository-local Moss skills, `agent-feedback/README.md`, and the current
Phase 24 Team A status entry. Because `./moss` was absent, I ran `make`, then
`./moss agent bootstrap --json`; bootstrap returned `ok: true`, compiler
`0.1.0`, schema `moss-agent-1`, and `result.language_version = moss-0.1`.

## Provenance and change classification

`git rev-parse HEAD` returned the full exact reviewed implementation SHA
`142b75009c229a318f46b52670f3d7b8aaca848b`, and `git branch --show-current`
returned `phase24-advanced-data-cbor-l7-architect`.

The production diff from the previously accepted candidate changes only six
lines in `sdk/cbor/src/cbor.moss`: four lines add exact edge-owner checks in
`validate_doc`, and two lines reject a designated root in
`require_attachable`. Encoder and decoder wire logic is unchanged. The
remaining corrective evidence consists of the two primary regression tests,
eight independent forged-record tests, and direct `InvalidRoot` recovery in
the existing source-free consumer.

Relevant commits carried by the exact candidate are:

| Commit | Classification |
| --- | --- |
| `debf99f068735630ca28a7d5ee4fc343832ac094` | Both production corrections and the two primary regressions. |
| `a6ecc79f6d0fd7be9fd8a7bc0beb119608a46783` | Eight independent forged-record/adversarial tests. |
| `76baf8739276a5e451baa18ae13cde9970b0ac08` | Repairs the parent-cycle fixture so reciprocal edges, ownership, incoming counts, and container bookkeeping are consistent before cycle detection. |
| `58340e636738331b84c5be1e42556ee612a73449` | Adds typed corrective `InvalidRoot` recovery and atomicity checks to the compiled-only ABI-v9 consumer. |
| `142b75009c229a318f46b52670f3d7b8aaca848b` | Records the frozen corrective candidate; no further production change. |

## Fresh commands and exact results

All commands below were run from the exact reviewed candidate. Package commands
used `MARGO_HOME="$PWD/tmp/margo-cbor-l7-architect"` and
`TMPDIR="$PWD/tmp"`; Python commands kept temporary/cache state under `tmp/`.

| Command | Fresh result |
| --- | --- |
| `./moss agent bootstrap --json` | PASS: `moss-0.1`, compiler `0.1.0`, schema `moss-agent-1`. |
| `(cd sdk/cbor && ../../margo test)` | PASS: 34/34 Native Debug tests, 0 failed. |
| `(cd sdk/cbor && ../../margo --release test)` | PASS: 34/34 Native Release tests, 0 failed. |
| `(cd sdk/cbor && ../../margo --interp test)` | PASS: 34/34 Fast Debug tests. |
| `(cd sdk/cbor && ../../margo test "edge owners must match child parent metadata")` | PASS: 1/1 exact swapped nested-array/map-owner primary reproducer, 0 failed. |
| `(cd sdk/cbor && ../../margo test "designated roots cannot be attached and failed mutations are atomic")` | PASS: 1/1 exact array-child/map-key/map-value root-attachment primary reproducer, 0 failed. |
| `python3 sdk/advanced-data-acceptance/validate_cbor_fixtures.py` | PASS: 12/12 unique fixed RFC 8949 vectors. |
| `python3 sdk/advanced-data-acceptance/check_source_free.py` | PASS: five Team A modules, 14 exact ABI-v9 provider pairs, no provider Moss source. |
| `uv run --no-project --with cbor2==5.7.1 python sdk/advanced-data-acceptance/cbor2_oracle.py` | PASS: pinned `cbor2 5.7.1`; 12 `cbor2` to Moss plus 12 Moss to `cbor2` comparisons. |

The expanded CBOR suite therefore passed 34 unique tests in each of three
engines, for 102 successful suite executions. The two primary filtered runs add
two focused successful executions. The fixed/oracle wire evidence comprises 12
fixed vectors and 24 two-way cross-implementation comparisons.

One initial attempt to run the two filtered reproducers concurrently was
discarded: one process encountered the already documented shared package-build
artifact race while opening `libbytes.rlib`. This was not candidate behavior.
Both filters were rerun serially and passed 1/1 as reported above.

## Invariant 1: exact edge ownership

**Accepted.** During linked-edge traversal, `validate_doc` already proves that
the current edge is unique, belongs to the container being traversed, and has
in-range endpoints. The correction now additionally requires:

- `doc.parents[value] == node` for an array edge; and
- both `doc.parents[key] == node` and `doc.parents[value] == node` for a map
  edge.

These checks occur before incoming-reference counters are incremented. Existing
post-traversal checks still require every attached node to have exactly one
incoming reference, require its named parent to be a container, reject unseen
edges, and reject parent ancestry cycles. Thus equal incoming counts can no
longer hide a child whose public parent metadata names a sibling container.

The exact primary reproducer swaps the `parents[]` entries of two nested-array
children while preserving incoming counts; validation and completeness fail.
The same regression separately forges map-key and map-value ownership. Public
`array_get`, `map_key`, and `map_value` calls recover typed
`CborError.InvalidDocument(code: 1, index: -1)`, while encoding each malformed
complete document recovers typed
`CborError.InvalidDocument(code: 2, index: <designated root>)`.

The eight-test adversarial file additionally covers wrong parent metadata,
multiple incoming edges, a parent cycle, an edge cycle, an orphan/disconnected
designated root, inconsistent map-key ownership, inconsistent map-value
ownership, and failed root-attachment atomicity. The repaired true-cycle case
forges reciprocal array edges and matching edge owners, parent entries,
incoming counts, first/last edges, and container counts. It therefore passes
the newly added ownership conditions and is rejected by the ancestry-cycle
check itself, as intended.

## Invariant 2: designated roots are not attachable

**Accepted.** `require_attachable` now calls `require_node` and immediately
raises `CborError.InvalidRoot(root: child)` when `child == doc.root`. Both
public attachment operations route through this helper:

- `array_push` checks the child before edge-capacity checking or `append_edge`;
- `map_set` checks the key and then the value before duplicate-key checking,
  edge-capacity checking, `append_edge`, or either parent assignment.

No operation before the new rejection mutates the document. This ordering
therefore gives typed rejection before mutation for all three attachment roles.
The exact primary reproducer recovers the exact root payload for array child,
map key, and map value attempts. It verifies unchanged node count, root identity,
root/peer parent entries, edge count, and container length after each failure.
It then performs a valid replacement attachment and confirms the resulting
parent entries, edge count, container length, and `validate_doc` result.

The independent atomicity test snapshots node count, edge count, array length,
root parent, and root identity, verifies every value is unchanged after typed
rejection, and confirms the document still validates. The compiled-only
consumer independently recovers `CborError.InvalidRoot` through ABI-v9 and
checks unchanged root identity, root parent, edge count, array length, and
document validity.

## Mutation validity and typed public failures

Every successful construction mutation in the new eight-test adversarial file
is followed by `assert(cbor.validate_doc(doc))`; this includes node additions,
array/map attachments, and root selection. The primary correction regressions
also validate the successful follow-up attachments after each failed role.
Failed attachment evidence checks both observable metadata and subsequent
validity, rather than relying only on the recovered error tag.

Malformed forged documents are rejected at the public API boundary through
typed `InvalidDocument` recovery before access or encoding indexes malformed
topology. The exercised accessors include `array_get`, `map_key`, `map_value`,
`parent_of`, `node_kind`, and `array_length`; the primary owner-mismatch cases
also exercise `encode`. Designated-root attachment is distinguished from those
forged-document failures by typed `InvalidRoot` with the exact root ID.

## Wire-semantics preservation

Previously accepted CBOR wire semantics are unchanged.

- The production diff does not alter encoding, decoding, key ordering,
  integer-width selection, payload handling, or supported-value dispatch.
- Native Debug, Native Release, and Fast Debug each pass the full 34-test suite,
  including primitive/shortest encodings, definite payloads and containers,
  nested ownership, decoder width/boundary cases, strict UTF-8, and semantic
  encode/decode round trips.
- The fixed fixture gate passes all 12 frozen RFC 8949 Appendix A vectors.
- Pinned `cbor2 5.7.1` agrees in all 12 `cbor2`-encode/Moss-decode-reencode
  comparisons and all 12 Moss-encode/`cbor2`-decode comparisons.

No supported wire value changed, and no correction introduces a new encoding
or decoding path.

## Finding accounting

- **L7 corrections independently verified:** 2 — L7-A-001 and L7-A-002.
- **Additional forged-record tests inspected and executed:** 8/8, including
  the repaired true parent cycle.
- **Net-new confirmed defects:** 0.
- **Duplicate candidate defects:** 0.
- **False-positive candidate concerns:** 0.
- **Open corrective findings:** 0.

The discarded parallel-build artifact race matches an already documented
repository tooling limitation and is not a CBOR defect, duplicate CBOR finding,
or false-positive candidate concern.

## Verdict

**GO** for exact reviewed implementation SHA
`142b75009c229a318f46b52670f3d7b8aaca848b`.

Both L7 invariants are correctly enforced, all three root-attachment roles fail
atomically with typed recovery, malformed ownership is rejected through typed
accessor/encoding errors, the repaired cycle test reaches the intended cycle
check, compiled-only ABI-v9 recovery passes, and accepted RFC/`cbor2` wire
behavior remains unchanged. There are no net-new defects and no open corrective
findings.

The commit containing only this report may carry this verdict forward. Any
later source, test, ABI, fixture, oracle, or acceptance-gate change requires a
new exact-SHA review.
