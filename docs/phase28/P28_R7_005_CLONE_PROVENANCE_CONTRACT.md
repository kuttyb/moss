# P28-R7-005 — Clone Provenance Contract (Draft v3)

**Status:** Proposed harness contract. Version 3 adds checked specialization
fact lists and an independently derived source-to-Rust mapping. It does not
change the certified v1/v2 oracle fixtures or their digests. The language
semantics are approved; the oracle is not frozen. No complete compiler-side
implementation is claimed.

## Artifact naming and coverage

For a generated Rust artifact named `generated.rs`, the sidecar is exactly
`generated.rs.mossclone.json` (append the suffix; do not replace `.rs`). Each
debug and optimized artifact has its own sidecar. Audit the complete generated
file, including helper definitions before `main`.

The manifest uses format `moss-clone-provenance`, version `3`, mode `debug` or
`optimized`, and SHA-256 digests of the exact generated Rust and Moss source
bytes. Coordinates are one-based, and span ends are exclusive. No count
subtraction or aggregate clone budget is permitted.

## Separate source operations from emitted Rust operations

The manifest carries three correlated lists:

- `source_copy_occurrences`: one entry for every lexically discovered Moss
  `copy(expression)` call, identified by a unique `copy_id`, exact `source_span`,
  `semantic_status`, and `rust_clone_spans`. The latter is the union of clone
  spans derived from all per-fact materializer helpers for this lexical
  occurrence.
- `checked_copy_facts`: one group per lexical `copy_id`, containing every exact
  live checker fact for that occurrence, including one row per specialization.
  Group rows must match the independent checker query byte-for-byte after
  canonical JSON encoding. Every source-copy row must have exactly one fact
  group; a missing or unavailable fact cannot be marked `implemented`.
- `occurrences`: one entry for every generated Rust `.clone()` call. Every Rust
  occurrence appears exactly once. A row classified `explicit_copy` names the
  responsible lexical `copy_id`; `evidence.semantic_identity` is the full
  canonical `typed-copy-fact-v1:<sha256>` identity of the exact specialization
  fact that authorizes the call. The harness compares its Rust span with the
  source row's `rust_clone_spans` and independently derives which fact-specific
  helper body contains that clone.

This permits a valid source copy to lower to zero, one or multiple Rust
duplications, without treating source-copy count as Rust clone count. It also
requires every source copy's semantics to be explicitly represented, even when
lowering emits no `.clone()` call.

An explicit-copy helper is unique to one complete checked fact, including its
specialization identity and context. Its Rust symbol is derived from that fact's
canonical hash; shortened symbol prefixes must be collision-checked against all
facts in the artifact. The checked lowering path emits one direct helper call
with this marker immediately before the helper identifier:

```rust
(/* Moss copy fact call: typed-copy-fact-v1:<full sha256> */
 __moss_copy_value_<hash-prefix>(...))
```

The harness parses the raw generated Rust to bind this marker to the direct
call, finds the unique helper definition, rejects aliases and extra callsites,
and traverses its nested helper closure to derive every clone span. The marker
and symbol are locators, not authority: the matching full checker fact must be
present in the live query, and independent review must verify that native
lowering resolves that exact fact before emitting the call and helper. Shared
materializers that can be called outside this fact-gated path remain
unclassified and block acceptance.

Each Rust row has `rust_span`, `origin_kind`, and an `evidence` object containing
nonempty `semantic_type`, `ownership_fact`, and `semantic_identity`, plus the
exact enclosing `rust_context_span` and SHA-256 of those Rust context bytes.
Runtime classifications (`compiler_runtime_handle` and `runtime_support`) must
name a `runtime_callsite_id` in the separate oracle-owned
`tests/phase28/oracle/runtime_callsite_allowlist.json`. Each allowlist row must
be independently reviewed and bind the origin kind, repository-relative path,
whole-file digest, exact source span, semantic type, ownership fact and semantic
identity, independent reviewer identity, and a digest-pinned repository review
record that names the callsite. The registry validator rejects self-review.
The sidecar's evidence must exactly match that reviewed row. Arbitrary
repository text, comments, matching keywords or a runtime label do not authorize
a clone. The registry is currently empty, so no runtime classification is
authorized by this draft. Missing, stale, malformed, duplicate, extra, unlinked
or out-of-bounds evidence fails closed.

The harness independently checks Rust/source digests, exact clone occurrence
spans, exact source-copy spans and identities, source-to-Rust links, context
hashes, and exact equality with an independently reviewed runtime callsite row.
A v3 sidecar is mandatory for every generated Rust artifact, including artifacts
whose Rust has no `.clone()` occurrence and whose Moss source has no `copy(...)`
call. An empty textual scan cannot stand in for provenance evidence.
A sidecar assertion is not itself semantic proof; independent harness review
must assess that the allowlisted callsite actually accounts for the generated
clone and that its ownership/type evidence is sufficient.

## Scanner limits and alternate duplication mechanisms

The current Rust scanner lexes comments, nested block comments, ordinary/raw
strings and character literals before recognizing syntactic `.clone()` calls.
This is an occurrence scanner, not a proof that all semantic duplication in
Rust uses `.clone()`. It does not establish behavior of macros before expansion,
`to_owned`, `to_string`, `copied`, `cloned`, `clone_from`, `mem::replace`,
`mem::take`, pointer operations, `Copy` loads, collection constructors, or
other APIs that may duplicate data. A clean `.clone()` scan must never be
reported as “zero hidden clones.”

Controls against alternate duplication mechanisms are therefore explicit:

1. Keep source-level `copy(...)` occurrences independently enumerated and
   require semantic implementation evidence for each one.
2. Review the complete generated artifact, not only user `main`, and preserve
   raw debug/optimized Rust plus sidecars by exact digest.
3. Add an independent generated-Rust audit for known duplication operators and
   suspicious APIs, including macro-expanded output where available. Each
   occurrence must be linked to source semantics or to checked runtime
   implementation evidence; an unclassified candidate blocks approval.
4. Treat the limits of static text scanning, compiler intrinsics, and Rust
   `Copy` as open review items until an independent audit demonstrates adequate
   coverage. Do not infer absence from an empty textual search.

The harness implements occurrence-level `.clone()` checks and strict sidecar
evidence shape checks. It does not yet implement a complete Rust AST/MIR
duplication audit and the compiler does not yet emit v3 provenance. Consequently,
the clone gate remains blocked, and existing observed clone occurrences remain
**unverified**, not proven hidden Moss-value clones.
