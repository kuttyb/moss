# Phase 25 ABI v10 Design

**Status:** G0 freeze candidate; paired with
[`TYPE_IR_CONTRACT.md`](TYPE_IR_CONTRACT.md).

**Baseline:** ABI v9 at `fa3a340a0a6652568b97869489da1a84038b9857`.

**Compatibility:** source-free ABI v9 artifacts are rejected and rebuilt; v9 fixtures
remain immutable migration tests.

ABI v10 carries generic nominal templates, open-result function summaries, exact
specialization/type keys, and sufficient private semantic dependencies to reproduce
source-backed checking without exposing private Moss names. It retains ABI v9's
provider-bound SHA-256 seal and its native raised-value, failure-frame, Root-dispatch,
effects, and warning contracts unless a v10 record explicitly supersedes them.

## 1. Version and container

The v10 interface begins:

```text
moss-module-interface-v3
provider_contract_digest sha256:<64 lowercase hex>
module_id module-v1:<package>@<version>::<module>
schema_version 10
native_abi 10
compiler <version>
backend_fingerprint <fingerprint>
backend_rustc <version>
binary_contract_hash sha256:<64 lowercase hex>
semantic_contract_hash sha256:<64 lowercase hex>
interface_hash sha256:<64 lowercase hex>
imports
...
binary_contract
...
semantic_contract
...
private_semantic_dependencies
...
provenance
...
```

The physical encoding may remain deterministic line records for Phase 25, but the
logical schema and ordering in this document are normative. Every count is bounded and
validated before allocation; duplicate singleton records, unknown required record
versions, dangling IDs, noncanonical order, invalid UTF-8, and trailing data in a
record fail closed.

`schema_version` describes `.mossi` records. `native_abi` describes Rust/link/runtime
compatibility. Both are exactly 10 for this release. A v9 container, a v3 container
claiming native ABI 9, or a manually changed version line receives
`SOURCE_FREE_ABI_REBUILD_REQUIRED`; it is never parsed under v10 assumptions.

## 2. Bound hashes

ABI v10 separates three hashes, all SHA-256 with domain-separated canonical inputs:

- `binary_contract_hash`: provider Rust crate identity; generic record definitions,
  generic parameter/field order, layout attributes, exported concrete symbols,
  calling conventions, raised/failure records, and backend fingerprint.
- `semantic_contract_hash`: public templates, open summaries, constraints,
  ownership/effects, private dependency hashes, visibility, and public provenance.
- `interface_hash`: canonical complete logical interface excluding the digest line.

`provider_contract_digest` is SHA-256 of the exact emitted interface bytes with only
its own line removed, matching ABI v9's no-normalization rule. The provider `.rlib`
contains exactly one native-object marker:

```text
MOSS_ABI_V10_CONTRACT::<module-tooling-name>::<interface-digest>::END
```

The loader validates byte digest, section hashes, module identity, and the exact
selected `.rlib` marker **before** importing semantic records. The `.rlib` used for
validation is the one passed to rustc. A generic instance also records its provider
crate disambiguator/binary hash, so two consumers cannot accept structurally similar
but independently incompatible record owners.

The existing 64-bit FNV `stable_hash` may remain an internal transient lookup helper;
it must not populate v10 canonical IDs or security/integrity fields.

## 3. Canonical records

The names below describe the logical fields; a line encoding must length-prefix or
quote arbitrary strings unambiguously.

### 3.1 Module/import records

```text
Module {
  module_id,
  package_name,
  package_version,
  module_name,
  provider_binary_id,
}

Import {
  module_id,
  required_binary_contract_hash,
  required_semantic_contract_hash,
}
```

Imports are sorted by `module_id`. Transitive imports retain their exact selected
hashes. Margo/module resolution rejects two providers for one `module_id` when their
bound contract hashes differ. The baseline behavior that reads `package::module` and
then keeps only `module` is invalid for v10.

### 3.2 Type templates and instances

```text
NominalTemplate {
  record_version: 1,
  template_id,
  declaration_name,
  visibility,
  field_slots: [
    { ordinal, name, visibility, declared_type?, variable_ordinal?, provenance_id }
  ],
  variable_count,
  representation_hash,
  method_template_ids,
  provenance_id,
}

TypeExpr = Primitive | Builtin(type_id, args) |
           Nominal(template_id, args) | Enum(enum_id) | Domain(domain_id) |
           BoundVar(ordinal) | Unit

ConcreteInstance {
  instance_key,
  template_id,
  substitution,
  resolved_fields,
  provider_rust_type_symbol,
  representation_hash,
}
```

`NominalTemplate` is emitted once. `ConcreteInstance` is emitted for provider-owned
instances materialized or referenced by the artifact; consumers may deterministically
derive the same key from the template and types. A consumer rejects an instance whose
recomputed key or representation hash differs.

Field visibility controls source access. A separate `backend_field_symbol` in the
binary contract permits generated consumer free functions to access the provider-owned
generic Rust record; it never appears in the importable Moss symbol table.

### 3.3 Open callable summaries

```text
OpenCallableSummary {
  record_version: 1,
  summary_id,
  callable_id,
  visibility,
  bound_variables: [{ ordinal, origin_provenance_id }],
  receiver_type?,
  parameter_types,
  result_type,
  requirements,
  receiver_access?,
  parameter_access,
  parameter_leaf_effects,
  observable_effects,
  semantic_dependencies,
  body_semantic_hash,
  provenance_id,
}
```

The summary ID is SHA-256 over canonical semantic fields, excluding display strings
and physical paths. Summary variables are dense ordinals starting at zero. A loader
does not allocate compiler inference IDs until a call instantiates the summary; every
call allocates a new mapping. This is what makes `fresh()` independent across calls
and across source-free/multi-hop providers.

Requirements carry structured type expressions, access mode, source operation, and a
provenance/reason parent. The serialized DAG is topologically ordered; cycles or
dangling parents are invalid. Semantic dependencies name exact callable/summary IDs
and expected semantic hashes, including private helpers.

An open summary may contain no executable generic body when its constraints and
lowering recipe suffice. When consumer specialization needs body IR, it uses the
versioned checked semantic IR in the private dependency section. Source text is not
required or exported.

### 3.4 Methods and specialization records

A method template records containing `template_id`, method name, Moss visibility,
parameter/result `TypeExpr`, receiver/parameter access, observable effects,
requirements, semantic dependencies, and provenance. A containing nominal annotation
in a parameter is encoded as `Nominal(template_id, receiver-bound-variables)`, making
the exact-receiver rule explicit without `Self` syntax.

```text
Specialization {
  specialization_key,
  callable_id,
  receiver_type?,
  argument_types,
  result_type,
  callable_argument_ids,
  open_result_bindings,
  receiver_access?,
  parameter_access,
  parameter_leaf_effects,
  observable_effects,
  native_symbol?,
  materialization_owner,
  semantic_hash,
}
```

Provider records own generic Rust type declarations. Consumer records own specialized
free-function symbols created for calls in that consumer. The same key must reproduce
the same type, effects, and call targets; disagreement is
`SOURCE_FREE_SPECIALIZATION_MISMATCH`, not last-writer-wins.

### 3.5 Map key proof

The Team G/Team S boundary is serialized as:

```text
MapKeyProof {
  proof_id,                       # key-proof-v1 canonical SHA-256 identity
  protocol_version,               # key-protocol-v1
  law_contract_version,
  key_type,
  equality_callable_id,
  equality_specialization_key,
  equality_signature,
  equality_effects,
  hash_callable_id,
  hash_specialization_key,
  hash_signature,
  hash_effects,
  equality_semantic_hash,
  hash_semantic_hash,
  provenance_id,
}
```

The record authorizes static calls through the provider-owned `MossMap<K,V>` operation
specialization described in the Type IR contract. It never authorizes or requests a
Rust `Eq`/`Hash` impl. `proof_id` is an input to Map/Set operation specialization,
semantic dependencies, interface hashes, and cache keys. `SetKeyProof` uses the
same record shape and protocol (or Set is explicitly deferred); it cannot invent a
second key contract. Missing proof fails at Moss checking. Float has no valid proof
in Phase 25, regardless of Rust representation.

The loader checks the two signatures and separate effect records against
`key-protocol-v1`: equality is `(READ K, READ K) -> Bool`, hash is
`(READ K) -> Int`, and neither may mutate, consume, raise, panic, diverge,
message, touch domain state, perform I/O/executor work, or have another observable
effect. The law version names the reusable reflexive/symmetric/transitive,
equal-hash, and snapshot-stability acceptance suite; it is not a runtime policy.

### 3.6 Provenance

```text
SourceProvenance {
  provenance_id,
  module_id,
  logical_source_id,
  source_file_display?,
  start_line, start_column, end_line, end_column,
  declaration_id?,
}
```

`logical_source_id` is a stable provider-emitted content/source identity; absolute
build paths are optional display data and are not hashed into nominal or
specialization identity. Public declaration provenance and all requirement origins are
preserved. Private dependency provenance may name a redacted logical file plus span,
but must still support a linked diagnostic chain. The consumer reports its outer call
as primary and provider origins as related notes; no imported failure should degrade
to line 0 alone.

## 4. Public API versus private semantic dependencies

The interface has three distinct namespaces:

1. `public_exports`: names Moss source may import or complete;
2. `binary_contract`: Rust layout/symbol facts generated code may use; and
3. `private_semantic_dependencies`: checked templates/bodies/requirements needed to
   specialize a public export but not nameable by Moss source.

A private field or method can appear in (2) or (3) without appearing in (1). Loader
construction must not append private dependencies to the public `Program` declaration
set used by name resolution, symbols, references, or completion. Each private record
is reachable from at least one exported summary; unrelated private declarations are
omitted. Reachability is transitive and deterministic.

This supports the G0 pub-Rust/Moss-checker decision: mechanical provider fields can be
used by generated consumer code without becoming Moss API. Phase 25.5 may strengthen
physical opacity, but no v10 consumer may equate Rust visibility with Moss visibility.

## 5. Validation and closure

The loader validates, in order:

1. magic, exact schema/native version, bounded record framing, and canonical order;
2. provider digest and selected native artifact marker;
3. binary, semantic, and full interface hashes;
4. module/import identity and dependency hashes;
5. unique IDs, references, type-expression arity, type-variable scope, and occurs-free
   template shapes;
6. recomputed nominal instance and specialization keys;
7. visibility reachability and private dependency closure;
8. ownership/effect record completeness and non-weakening consistency;
9. requirement/provenance DAG validity; and
10. native symbol/layout availability for all selected provider-owned records.

Only after these checks may records enter inference. Before native or Fast Debug
execution, the reachable program closure additionally requires every value type, call
target, ownership access, and observable effect to be concrete. Open summaries are
legal artifacts; open reachable execution values are not.

Imported ownership/effects remain authoritative. A consumer specialization may add a
more precise field path while preserving the provider's aggregate access, but it may
not change `WRITE`/`CONSUME` to `READ`, remove a raise/panic/divergence fact, or erase a
warning witness.

## 6. A1 cross-crate linkage

For each exported generic nominal template, the provider rlib exports one Rust generic
record under a deterministic symbol/module path derived from `template_id`. The binary
contract contains:

- provider crate identity/disambiguator;
- generic parameter order;
- field order, backend field symbols, and Rust type recipes;
- representation/layout attributes and representation hash; and
- constructor/destructor/value-boundary requirements.

Consumers use that definition and generate only free functions. Two independent
consumers importing the same bound provider and `NominalInstanceKey` therefore compile
against the same provider record definition. An integration proof must link values
through a third crate or provider API, not merely compare generated spelling.

Consumer-generated code never creates an inherent impl or foreign-trait impl for the
provider type. Ordinary method calls lower to direct specialized functions whose
receiver form (`&`, `&mut`, owned) comes from Moss effects. The provider's mechanical
public fields or future provider accessors are selected by the binary contract, not
guessed from generated Rust.

## 7. Cache invalidation

The build cache key includes:

- compiler and schema/native ABI versions;
- backend fingerprint and profile;
- exact selected `.mossi` and `.rlib` paths and byte hashes;
- every direct/transitive module ID plus bound binary/semantic hashes;
- all used template, summary, instance, key-proof/protocol/law, and specialization
  IDs/hashes; and
- consumer source/manifest/lock identities already required by Margo.

A method/body-only change invalidates consumers that depend on its semantic hash but
does not change an unrelated nominal type ID. A layout/template change invalidates all
instances. An open-summary change invalidates transitive relay summaries and their
callers. Changing only optional display provenance does not change type identity but
does change the sealed interface bytes.

No explicit-module cache shortcut may bypass provider revalidation. Dependency hashes
are checked even when consumer source is unchanged.

## 8. Diagnostics and semantic queries

ABI mismatch diagnostics use stable codes:

- `SOURCE_FREE_ABI_REBUILD_REQUIRED` — v9/unknown schema or native ABI;
- `SOURCE_FREE_CONTRACT_INVALID` — malformed/noncanonical v10;
- `SOURCE_FREE_ARTIFACT_CONTRACT_MISMATCH` — `.mossi`/rlib seal disagreement;
- `SOURCE_FREE_MODULE_ID_CONFLICT` — one module identity, different providers;
- `SOURCE_FREE_TYPE_IDENTITY_MISMATCH` — bad template/instance/layout record;
- `SOURCE_FREE_SPECIALIZATION_MISMATCH` — key/type/effect/call-target disagreement;
- `SOURCE_FREE_SUMMARY_INVALID` — variable scope, requirement, or dependency failure;
  and
- `SOURCE_FREE_PRIVATE_DEPENDENCY_MISSING` — exported summary cannot be specialized.

Diagnostics include consumer call provenance and provider record provenance where
safe. Semantic APIs expose structured type/summary/specialization IDs and source-free
status. They do not expose private dependency names through symbol enumeration.

## 9. Fast Debug contract

Source-backed packages feed the same structured Type IR and summaries to Fast Debug
and native lowering. Type closure, requirements, ownership, and effects must match.

ABI v10 does not add mixed interpreted/native Moss execution. A reachable source-free
provider is rejected during Fast Debug preflight with
`FAST_DEBUG_NATIVE_DEPENDENCY`, before interpreting any consumer body. This capability
limit is not reported as type ambiguity, missing method, ABI mismatch, or semantic
divergence.

## 10. Migration and retained fixtures

Production emits and accepts ABI v10 only after the version switch. The repository
retains v9 `.mossi`/rlib fixtures in a clearly historical fixture directory. Tests
assert that:

- an untouched v9 pair receives `SOURCE_FREE_ABI_REBUILD_REQUIRED`;
- changing its version text cannot forge a v10 seal or schema;
- rebuilding source produces a bound v10 pair;
- source-free consumers are invalidated and rebuilt; and
- v9 tests are not rewritten to claim v10.

There is no in-place v9-to-v10 metadata converter: v9 never recorded generic nominal
templates, open result variables, or v10 binary identities, so only a source rebuild
can create trustworthy records.

## 11. Required proof matrix

The ABI implementation is not accepted until evidence records exact SHA, compiler,
rustc/backend fingerprint, profile, command, and output/status for:

1. one source-free provider exporting `Box`, `fresh`, and `relay`;
2. two independent consumer crates using `Box[Int]` and `Box[String]` and sharing the
   provider Rust type identity;
3. a source-removed multi-hop open-result consumer with separate fresh calls;
4. an ambiguity negative and conflicting-use negative with full provider provenance;
5. method exact-instance annotations and known-open field constraints;
6. transitive missing-method diagnostic chains;
7. ownership/effect negatives before rustc;
8. compiled-only custom nominal Map key proof with custom Moss equality/hash, the same
   policy-free `MossMap<K,V>` value crossing two consumers, and no orphan impl;
9. corrupt type key, summary, provenance edge, semantic hash, binary hash, provider
   marker, weakened key effects, mismatched key-callable hash, Float-proof forgery,
   and same-layout/different-nominal proof-substitution negatives;
10. v9 rebuild diagnostics and cache invalidation; and
11. source-backed native/Fast Debug agreement plus source-free Fast Debug preflight.

Repeated use evidence reports unique template/instance/specialization counts, emitted
code size, and clean/incremental build time so avoidable monomorphization is visible.

## 12. Freeze boundary and required concurrence

Team G can freeze the logical record shapes, per-call summary freshening, canonical
validation order, provenance DAG, migration failure behavior, and the rule that one
provider owns a generic Rust record after its independent Architect review.

Team G cannot unilaterally freeze the following cross-team choices. Before
implementation, Team S and L7 must record concurrence with:

- the exact `module_id`/package collision rule;
- SHA-256 `type-key-v1` and `specialization-v2` identities;
- provider-owned record plus pub mechanical fields guarded by Moss visibility;
- `MapKeyProof` and provider-owned `MossMap<K,V>` with no policy type in value identity;
- private semantic dependencies as a non-nameable namespace; and
- unconditional v9 rebuild rather than compatibility parsing.

Until those reviews and the Type IR sign-off table contain GO, this document is a
precise implementation proposal, not authorization for shared-IR changes.
