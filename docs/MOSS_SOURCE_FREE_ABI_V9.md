# Moss source-free provider ABI v9

ABI v9 succeeds the frozen Phase 21 ABI v8 interface. Its native raised-value,
failure-frame, Root-dispatch and specialization layouts retain the v8 shape.
The version number advances because a v9 consumer requires a provider-bound
semantic contract that a v8 `.mossi` and `.rlib` do not supply. Rebuild both
artifacts with a v9 compiler; changing an old interface's version field is
invalid.

## Trust and scope

The checked provider build is trusted to emit its Rust crate and `.mossi`
together. A consumer may receive an edited, stale, misplaced or otherwise
untrusted `.mossi`. ABI v9 verifies that its exact contract bytes are bound to
the **selected compiled provider artifact** before importing any type, effect,
failure-arm or warning facts. It does not authenticate the publisher or defend
against an attacker who can replace both the executable object code and its
embedded binding. A signature or reproducible source rebuild would be needed
for publisher identity.

## Interface and digest

The provider emits UTF-8 records in deterministic order with LF terminators.
The header begins:

```text
moss-module-interface-v2
provider_contract_digest sha256:<64 lowercase hex digits>
module_id <package>::<module>
compiler <version>
native_abi 9
backend_fingerprint <fingerprint>
backend_rustc <version>
concrete_interface_hash <16 lowercase hex digits>
generic_semantic_hash <16 lowercase hex digits>
interface_hash <16 lowercase hex digits>
imports
...
contract
...
semantic_exports
...
```

The canonical digest input is the complete emitted `.mossi` byte sequence
with the single `provider_contract_digest ...\n` record removed. It includes
the version, module identity, backend fingerprint, import list, complete
contract, generic semantic IR, method effects and warning witnesses. No
whitespace normalization is performed on consumer input: a changed byte
changes the digest. The three v8-style section hashes remain useful internal
structure checks but cannot authenticate an independently edited interface.

Each provider module Rust crate contains one `#[used]` static with the framed
ASCII value:

```text
MOSS_ABI_V9_CONTRACT::<module>::<sha256 digest>::END
```

The value is in the compiled native object member of the `.rlib`. It has no
runtime dispatch or per-call cost. The consumer reads the exact `.rlib` later
passed to rustc, parses its archive in-process, ignores `.rmeta` copies, and
requires exactly one native-object binding for the selected module. It rejects
missing, duplicate, malformed and disagreeing bindings. This check precedes
semantic record parsing and native build. No external `ar`, `nm` or `objcopy`
tool is needed.

An adversary can swap failure-arm identities, recompute all editable section
hashes and recompute the header SHA-256. Because the unchanged provider object
still carries the original digest, `moss check` rejects the import with
`SOURCE_FREE_ARTIFACT_CONTRACT_MISMATCH`. A metadata-only hash is insufficient;
the independent object binding is the decisive comparison.

## Checked callable contract

ABI v9 retains exported ordinary-function effects, parameter ownership,
parameter leaf effects, generic constraints, static-dispatch flags and the
semantic IR needed for consumer specialization. Imported function summaries
remain authoritative during fixed-point analysis; synthetic bodies cannot
reset them. A concrete specialization can refine a summary, but cannot erase
provider-declared panic or typed-raise facts.

Every exported object method records its return type, receiver READ/WRITE/
CONSUME effect, ordered parameter names/types/effects and the complete
`ObservableEffects` record. This includes a normalized, variant-precise
`raise_set` and a separate `may_panic` bit. The checker uses those facts for
recovery exhaustiveness, Root totality, ownership, pipelines and Branch
eligibility. Missing or invalid required fields, unresolved effects and a v8
method record fail closed at Moss checking. Object methods remain statically
resolved; ABI v9 adds no dynamic dispatch or general method specialization
mechanism.

Imported methods have no Moss implementation body in the consumer. Their
receiver and parameter effects stay fixed through ownership-effect inference.
For synchronization planning, the consumer conservatively projects a READ,
WRITE or CONSUME effect over the full actual receiver or argument when it
cannot replay provider field-level body effects. This may acquire more locks
than a source-backed build but cannot weaken a required WRITE or CONSUME.
Imported handler reply totality is verified from the provider's bound ABI
reply contract; the consumer does not type-check a guessed placeholder body.

## Recovery-warning provenance

`function_warning`, `object_method_warning` and `handler_warning` records
carry a checked witness for each relevant no-op catch-all. A witness contains
its source line, `recover`/`on_fail` kind, the selected typed-error identities
and the formal WRITE leaf paths that can precede the selected raise. The
producer derives these with the same exceptional-flow analysis used for
source-backed warnings. For concrete helper chains, it forwards nested
witnesses through static call arguments into the exported callable's formals;
the consumer needs no private implementation body.

At a consumer call site, the checker maps formal paths through the actual
receiver and arguments and warns only when the resulting path is protected
domain state. A local receiver or argument does not create a warning. Imported
handler witnesses preserve `RECOVER_EMPTY_AFTER_STATE_WRITE` and
`ON_FAIL_EMPTY_AFTER_STATE_WRITE` when the provider source is absent. The
records are diagnostic provenance, not executable recovery or an effect
substitute. Their bytes are part of the bound contract.

## Validation, linkage and cache

Source-free import resolution selects exactly one provider `.mossi` for each
requested module. The v9 loader rejects old version markers, missing paired
rlibs, malformed digests, wrong module bindings and effect/ABI inconsistencies.
The paired artifact selected during checking is the artifact passed through
`--extern` at native build; filename rescanning may not pick a different
package's same-named provider. Native and optimized provider builds each emit
their own checked interface and artifact pair.

Explicit-module builds re-resolve imports and verify artifact bindings before
returning a cached result. Their cache key includes the bytes and paths of
each selected external `.mossi` and `.rlib`, compiler version, native ABI
version, mode and backend fingerprint. A changed or missing provider therefore
invalidates the consumer even when its Moss source is unchanged. The early
cache shortcut remains available only to unmodular builds with no import.
Determinism depends on deterministic Moss interface ordering and backend
identity; the binding digest itself has no clock, path nonce or random input.

Source-backed imports continue to use the checked source graph. Fast Debug
requires reachable Moss source and does not silently execute source-free
native providers. Concrete cross-crate `Range` remains the separately tracked
Phase 20 limitation and fails at Moss checking. ABI v9 does not change that
native type boundary.

## Alternatives considered

- A SHA-256 stored only in `.mossi` lets an editor change the contract and
  recompute the digest. It cannot solve the observed forgery.
- Hashing the whole `.rlib` into `.mossi` is self-referential if the provider
  artifact also embeds the interface digest, and metadata-only storage still
  permits coordinated edits to the interface's editable fields. Embedding the
  canonical interface digest directly in provider object code is simpler.
- Provider-owned failure-arm dispatch could reduce consumer interpretation of
  arm identity, but would alter the established static specialization and
  recovery ABI more broadly. The object-bound contract closes the observed
  mismatch without adding runtime dispatch. Provider-owned dispatch remains a
  possible future design if a separate native contract issue warrants it.

## Migration

An ABI-v8 `.mossi` receives `SOURCE_FREE_ABI_REBUILD_REQUIRED` when imported
source-free by a v9 compiler. Rebuild the provider with its Moss source to
produce a v9 `.mossi` and `.rlib`, then rebuild consumers and invalidate old
cached artifacts. A file edit that merely claims `native_abi 9` cannot create
the required native-object binding. The historical v8 specification and its
known limitations remain unchanged.
