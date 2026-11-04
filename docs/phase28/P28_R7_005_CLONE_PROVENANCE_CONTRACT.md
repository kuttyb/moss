# P28-R7-005 — Clone Provenance Contract (Draft v2)

**Status:** Proposed harness contract for L7 review. The language semantics are
approved; the oracle is not frozen. No compiler-side implementation is claimed.

## Artifact naming and coverage

For a generated Rust artifact named `generated.rs`, the sidecar is exactly
`generated.rs.mossclone.json` (append the suffix; do not replace `.rs`). Each
debug and optimized artifact has its own sidecar. Audit the complete generated
file, including helper definitions before `main`.

The manifest uses format `moss-clone-provenance`, version `2`, mode `debug` or
`optimized`, and SHA-256 digests of the exact generated Rust and Moss source
bytes. Coordinates are one-based, and span ends are exclusive. No count
subtraction or aggregate clone budget is permitted.

## Separate source operations from emitted Rust operations

The manifest carries two independent lists:

- `source_copy_occurrences`: one entry for every lexically discovered Moss
  `copy(expression)` call, identified by a unique `copy_id`, exact `source_span`,
  `semantic_status: "implemented"`, and `rust_clone_spans`. The latter list may
  be empty, contain one span, or contain multiple emitted Rust clone spans.
- `occurrences`: one entry for every generated Rust `.clone()` call. Every Rust
  occurrence appears exactly once. A row classified `explicit_copy` names the
  responsible `copy_id`; the harness compares its Rust span with that source
  entry's `rust_clone_spans`.

This permits a valid source copy to lower to zero, one or multiple Rust
duplications, without treating source-copy count as Rust clone count. It also
requires every source copy's semantics to be explicitly represented, even when
lowering emits no `.clone()` call.

Each Rust row has `rust_span`, `origin_kind`, and an `evidence` object containing
nonempty `semantic_type`, `ownership_fact`, and `semantic_identity`, plus the
exact enclosing `rust_context_span` and SHA-256 of those Rust context bytes.
Runtime classifications (`compiler_runtime_handle` and `runtime_support`) also
require a repository-relative evidence path, file digest and source span. A
runtime label by itself never authorizes a clone. Missing, stale, malformed,
duplicate, extra, unlinked or out-of-bounds evidence fails closed.

The harness independently checks Rust/source digests, exact clone occurrence
spans, exact source-copy spans, source-to-Rust links, context hashes, repository
evidence file hashes and coordinates, requires the source slice to substantiate
a duplication/handle operation and name the claimed semantic type, and checks
mode and uniqueness. The contract expects
evidence to support a reviewer checking the stated type, ownership role and
runtime implementation against the referenced source. A sidecar assertion is
not itself semantic proof; independent harness review must assess that evidence
is sufficient and that the implementation does not accept labels as authority.

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

The current harness implements occurrence-level `.clone()` checks and strict
sidecar evidence shape checks. It does not yet implement a complete Rust AST/MIR
duplication audit and the compiler does not emit v2 provenance. Consequently,
the clone gate remains blocked, and existing observed clone occurrences remain
**unverified**, not proven hidden Moss-value clones.
