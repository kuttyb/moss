# Phase 25 Team G G0 A1/ABI feasibility

Status: **bounded feasibility GO; shared-contract approval still required**  
Pinned baseline: `fa3a340a0a6652568b97869489da1a84038b9857`  
Branch: `phase25-g0-feasibility`  
Scope: architecture prototype and conformance design only; no compiler or
shared-IR changes.

## 1. Result

The G0 A1 and Map-key paths have viable static Rust representations. One
provider crate can own a generic nominal Rust record while independently
compiled consumer crates own specialized free functions over distinct concrete
instances. Provider-owned generated accessors allow those functions to
manipulate the record without making its fields public. A compiler-owned shared
`MossMap<K,V,Ops>` representation can cross both consumer boundaries while a
provider-owned operations marker invokes custom Moss `isequal` and `hash`
semantics by static dispatch. Directly implementing `Hash` for a provider-owned
key correctly fails Rust's orphan rule.

This is not L7 G0 authorization for shared-IR work. Team S and both Architects
still need to approve the exact type identity, adapter placement, visibility,
and ABI-v10 records below. Until that signed GO exists, these are proposed
contracts and implementation work must stay out of shared compiler IR.

## 2. A1 cross-crate prototype

The durable prototype is in `tests/phase25/g0/rust/` and is driven by
`tests/phase25/g0/check_rust_feasibility.py`.

It compiles four crates/artifacts:

1. `phase25_provider` owns `GenericRecord<T>` and keeps `value` private.
2. `phase25_consumer_a` owns `increment(&mut GenericRecord<i64>)`.
3. `phase25_consumer_b` owns `double(&mut GenericRecord<i64>)` and
   `append(&mut GenericRecord<String>, &str)`.
4. The application passes one exact `GenericRecord<i64>` value through both
   independently compiled consumers, and also constructs the String instance.

The program prints exactly
`84 moss-g0 map=10,10,11,15,80,1,true`. The same run checks two expected
compiler failures:

- direct consumer access to `GenericRecord.value` fails with Rust `E0616`;
- direct consumer `impl Hash for provider::UserKey` fails with Rust `E0117`.

This demonstrates the required ownership shape: one Rust nominal declaration,
not a structurally duplicated record in every consumer. Concrete Rust type
identity is the provider crate path plus its type declaration; consumer free
functions name that type directly. Generic Moss methods should use the same
shape as the two prototype free functions and retain their inferred
READ/WRITE/CONSUME contracts in specialization metadata.

### Visibility decision and alternatives

**Recommended G0 decision: provider-owned opaque fields plus generated native
accessors.** Emit the Rust record itself as `pub` because consumers must name
it, keep fields private, and emit deterministic provider-owned accessor symbols
needed by checked specializations. Accessors are native implementation ABI and
must not become Moss exports merely because Rust requires `pub`. The `.mossi`
public export table remains the only Moss visibility authority.

Two alternatives were evaluated:

- Public Rust fields plus enforcement only in the Moss checker is mechanically
  simpler, but unnecessarily exposes representation to every linked Rust crate
  and makes accidental backend bypass easier. It is not recommended.
- Provider-owned concrete method wrappers preserve privacy, but cannot cover
  arbitrary consumer-side static specialization without either an accessor
  vocabulary or moving all specializations back to the provider. The latter
  conflicts with source-free consumer specialization and is not recommended.

Generated accessors need collision-resistant symbols derived from the template
ID and field ordinal, not source field spelling. `.mossi` may carry a private
specialization dependency record for such a symbol, but must not list it in the
Moss export namespace or expose unrelated private declarations.

## 3. Map key proof and placement

The completed positive prototype adds a separate compiler-owned runtime crate.
That crate defines the single canonical `MossMap<K,V,Ops>` representation and a
static `MossKeyOps<K>` contract. The compiled provider owns `UserKey`, the
zero-sized `UserKeyOps` identity, its implementation of the runtime contract,
and the canonical `UserMap<V>` alias. This implementation is coherent because
the provider-owned marker is the implementing self type; it does not implement
a Rust foreign trait for `UserKey`.

The provider constructs one `UserMap<Int>` value. Its Moss-like custom key
semantics compare and hash by the final decimal digit, deliberately differing
from Rust field equality. Consumer A inserts keys 7 and 8 and later overwrites
the first entry with equivalent key 27. Consumer B reads it through equivalent
keys 17 and 37 before and after the overwrite, obtains an eager owned keys
snapshot, deletes key 8 through equivalent key 18, checks the remaining length,
and confirms equivalent key 28 is absent. The one value crosses both
independently compiled consumer rlibs, and both function signatures resolve to
the exact runtime-owned `MossMap<UserKey,i64,UserKeyOps>` Rust type. Observed
results are initial lookup 10, overwritten value 10, replacement lookup 11,
key sum 15, deleted value 80, remaining length 1, and missing lookup `true`.

`MossMap` stores each checked Moss hash with its key and uses the provider's
statically dispatched Moss equality only among equal-hash candidates. This
prototype uses a vector-backed collision search to isolate representation and
coherence correctness; it is not a production complexity claim. `keys()`
returns an eager owned snapshot. There is no trait object, `dyn`, vtable,
Float-derived `Eq`/`Hash`, consumer-local adapter type, or foreign-trait
implementation on the provider key.

The negative control implements Rust `Hash` directly for
`provider::UserKey`; rustc rejects it with `E0117`. Therefore Team G must never
emit `impl std::hash::Hash for <foreign provider key>` in a consumer.

**Recommended production placement:** retain this ownership split. The shared
native collections runtime owns generic `MossMap<K,V,Ops>`; the module defining
`K` owns a zero-sized `Ops` marker and implements the runtime's
`MossKeyOps<K>` contract by static calls to the resolved Moss `isequal` and
`hash` specializations. A concrete `Map[K,V]` selects `Ops` deterministically
from canonical `K`. The production runtime may replace the prototype's linear
bucket storage without changing this nominal representation or static contract.

This closes the consumer-local-adapter identity gap: no consumer defines the
Map or key adapter representation. Team S owns Map/Set laws, purity, and exact
collection behavior; Team G owns the canonical `K -> Ops` identity and ABI
hook.

### Required escalation triggers

Stop the affected path and notify L7 with a reduced program and at least two
alternatives if any of these occurs:

1. A consumer specialization cannot mutate/read a provider-owned record through
   generated accessors without exposing a Moss-private name or body in the
   public `.mossi` export table.
2. Two consumers compile the same canonical instance to incompatible provider
   Rust paths, field order, layout, or accessor signatures.
3. A source-free consumer needs to redeclare the provider record or implement a
   Rust foreign trait for the provider key.
4. `MossMap<K,V,Ops>` cannot preserve exact custom Moss equality/hash semantics,
   ownership effects, or one canonical cross-crate Map representation without
   dynamic dispatch.
5. Map specialization would require assuming Rust `Eq`/`Hash` for Float or
   bypassing Team S's purity/law checks.

If (1) or (2) fires, evaluate at minimum (a) public Rust fields guarded only by
the Moss checker and (b) provider-owned generated accessor functions. If (3),
(4), or (5) fires, evaluate at minimum (a) shared-runtime `MossMap<K,V,Ops>` with
provider-owned operations and (b) a provider-owned Map specialization/wrapper.
Do not add runtime dispatch or source-level generic syntax as a workaround.

## 4. Proposed canonical type contract

There must be one type-variable representation shared by inference, methods,
collections, `.mossi`, Fast Debug, and native lowering. A source declaration
does not acquire explicit type parameters. Omitted fields create compiler-owned
template slots.

The canonical inputs are:

```text
TemplateKey = (defining_module_id, nominal_declaration_id)
InstanceKey = (TemplateKey, ordered_resolved_field_type_ids)
```

`nominal_declaration_id` is a compiler semantic declaration identity, not a
field-layout hash. Consequently two declarations with identical fields remain
distinct. The ordered field types are part of the instance key, so the same
declaration instantiated with `Int` and `String` is distinct. Field names and
visibility belong to the template semantic hash; they are not a substitute for
the declaration identity.

Canonical serialization should be length-prefixed bytes, never delimiter-only
text:

```text
"moss-nominal-instance-v1\0"
lp(defining_module_id)
lp(nominal_declaration_id)
u32(field_count)
repeat field_count: lp(canonical_resolved_field_type_id)
```

The stable display/ABI ID is
`nominal-instance-v1:sha256:<64 lowercase hex digits>` over those bytes. Loaders
must compare the canonical components when materialized, not silently treat a
hash collision as identity. Built-in scalar and collection type IDs use their
own versioned canonical encodings. Recursive expansion is bounded by declaration
references; an actual recursive value layout remains rejected before codegen.

The Rust owner path is derived only from `TemplateKey`. Concrete field types are
Rust generic arguments in declaration order. Specialization symbol keys include
the full `InstanceKey` plus callable identity, effect signature, and ordered
call argument types. This separates one provider record declaration from any
number of consumer-owned specialized free functions.

The built-in Map instance follows the same rule:

```text
MapInstanceKey = (builtin-map-v1, key-instance-id, value-instance-id)
MapOpsKey = (moss-key-ops-v1, key-instance-id, isequal-id, hash-id)
```

`MapOpsKey` is derived once from canonical `K`; it is not consumer identity.
Every artifact lowers that pair to the shared runtime
`MossMap<K,V,ProviderOwnedOps>` type, as the prototype's two consumer signatures
demonstrate.

## 5. Proposed ABI-v10 additions

ABI v10 should retain v9's artifact-bound `provider_contract_digest`, backend
fingerprint, concrete/effect records, and native-object binding. It should add
these exact logical records (final byte grammar requires joint approval):

```text
native_abi 10
type_ir_schema nominal-generics-v1
open_summary_schema option-b-open-results-v1

type_template_begin <template-id>
  defining_module <module-id>
  declaration <semantic-declaration-id>
  visibility exported|private-dependency
  field <ordinal> <source-name> <fixed-type-id-or-$field-slot>
  method_receiver exact-instance
  semantic_hash sha256:<digest>
type_template_end

open_result_begin <callable-id>
  fresh_var <ordinal> kind=type
  result_type <canonical-type-expression>
  requirement <id> <operation> <receiver-type-expression> <argument-types>
  provenance <id> <module-id> <callable-id> <logical-source> <line> <column>
  effects <existing-ABI-v9-effect-encoding>
  semantic_hash sha256:<digest>
open_result_end

instantiation <instance-id> template=<template-id>
  fields=<ordered-canonical-type-ids> semantic_hash=sha256:<digest>
```

`private-dependency` records are available only to the specialization loader;
they do not enter import name lookup. Logical source provenance must be stable
and repository/package relative, not an absolute build-machine path.

The existing whole-interface `provider_contract_digest` binds these bytes to
the selected rlib. `type_template.semantic_hash` and
`open_result.semantic_hash` detect semantic invalidation; the backend
fingerprint and `native_abi 10` protect binary compatibility. Margo cache keys
must include the selected `.mossi` and rlib bytes, all template/summary semantic
hashes, the canonical instantiation set, compiler version, profile, and backend
fingerprint.

An ABI-v9 provider selected source-free by an ABI-v10 consumer must fail before
semantic import with `SOURCE_FREE_ABI_REBUILD_REQUIRED` and an explicit
"rebuild provider for ABI v10" message. Preserve ABI-v9 fixtures as migration
tests; never rewrite them to claim v10. A source-backed provider may be rebuilt
to v10 normally.

## 6. Option B summary and closure contract

For `fresh() -> []`, the provider exports an open result `Vector[$0]`; it does
not guess `$0` or reject the provider solely because its body leaves `$0`
unbound. Each call creates a new inference variable. `relay() -> fresh()`
composes and alpha-normalizes that summary, rather than capturing the provider's
or another call's variable.

Function/body closure rules:

1. Freshen all imported/local open-summary variables per call expression.
2. Accumulate equality, field, method, trait, ownership, and observable-effect
   requirements with predecessor edges to their source requirement.
3. Permit unresolved variables to leave a function only through an exported
   open result/requirement summary. All other reachable locals, call targets,
   effects, and ownership modes close before codegen.
4. Apply constraints from statically checked code even when a loop or branch
   executes zero times. Runtime insertion is never type initialization.
5. At the enclosing closed body/root, reject every unresolved variable with an
   ambiguity diagnostic. Reject conflicting constraints at the outermost user
   call and include the complete predecessor chain as related locations.
6. Preserve fresh variables and requirement provenance through source-free
   import. Fast Debug with source packages consumes the same checked summary;
   compiled-only Fast Debug still reports its existing capability limitation.

The diagnostic for a missing transitive `less` method should identify the
outer call, each relay/callee requirement edge, and the original comparison.
It must not be line 0 or an internal compiler error.

## 7. Conformance cases

Forward fixtures live under `tests/phase25/g0/conformance/`:

| Fixture | Phase 25 expectation |
| --- | --- |
| `open_result_multihop.moss` | PASS; independent `Vector[Int]` and `Vector[String]` calls through direct and relay paths; output `1` then `one`. |
| `open_result_ambiguous.moss` | Reject at closure with a precise unresolved open-result diagnostic and chain to `fresh`; no default type. |
| `open_result_conflict.moss` | Reject the same call instance's `Int`/`String` conflict; separate calls remain independent. |
| `open_result_zero_iteration.moss` | PASS and infer `Vector[Int]` from the checked branch even when called with `false`; output `0`. |
| `local_pin_positive.moss` | PASS after Team S's dispatch portion and Team G inference closure; infer `Vector[Int]`; output `1`. |

G-B/G-D must extend this seed into a three-layer provider/relay/application
package graph, then repeat with only provider `.mossi` + rlib available.
Assertions must cover fresh variable IDs per call, deterministic summary bytes,
full diagnostic provenance, source-backed/source-free equality, zero-iteration
native behavior, and Fast Debug parity where source is available.

At the pinned baseline, all four open-result fixtures fail immediately with
`TYPE_INFERENCE_FAILED: cannot infer return type for function 'fresh'` at line
1. `local_pin_positive.moss` reaches line 3 but fails with
`MOSS_COMPILE_ERROR: local member calls are not implemented; use a top-level
function`. These are baseline observations, not accepted Phase 25 outcomes.

## 8. Exact evidence

Environment:

- Moss compiler `0.1.0`, bootstrap protocol `moss-agent-1`, language
  `moss-0.1`.
- `rustc 1.98.1 (48a229cea 2026-09-01)`, host
  `x86_64-unknown-linux-gnu`, LLVM `22.1.8`.
- `g++ 13.3.0`, Python `3.12.3`.

Commands run at exact baseline
`fa3a340a0a6652568b97869489da1a84038b9857`:

```text
./moss agent bootstrap --json
PASS: ok=true, result.language_version=moss-0.1

python3 tests/phase25/g0/check_rust_feasibility.py
PASS: Phase 25 G0 provider identity, canonical cross-crate MossMap, and visibility prototype passed

/usr/bin/time -f 'elapsed=%e maxrss_kb=%M' \
  python3 tests/phase25/g0/check_rust_feasibility.py
PASS: elapsed=0.22, maxrss_kb=111048

python3 tests/tooling/check_phase21_abi_v9_migration.py ./moss
PASS: ABI-v9 source-free migration and damaged-v9 diagnostics passed

./moss check tests/phase25/g0/conformance/open_result_multihop.moss --json
EXPECTED BASELINE FAIL (exit 1): TYPE_INFERENCE_FAILED at fresh line 1

./moss check tests/phase25/g0/conformance/open_result_ambiguous.moss --json
EXPECTED BASELINE FAIL (exit 1): TYPE_INFERENCE_FAILED at fresh line 1

./moss check tests/phase25/g0/conformance/open_result_conflict.moss --json
EXPECTED BASELINE FAIL (exit 1): TYPE_INFERENCE_FAILED at fresh line 1

./moss check tests/phase25/g0/conformance/open_result_zero_iteration.moss --json
EXPECTED BASELINE FAIL (exit 1): TYPE_INFERENCE_FAILED at fresh line 1

./moss check tests/phase25/g0/conformance/local_pin_positive.moss --json
EXPECTED BASELINE FAIL (exit 1): MOSS_COMPILE_ERROR at member call line 3
```

The unoptimized prototype artifacts were 17,064 bytes for the shared Map
runtime rlib, 18,502 bytes for the provider rlib, 31,434 bytes for consumer A,
128,586 bytes for consumer B, and 4,558,832 bytes for the executable. These are
feasibility measurements, not production Moss code-size targets.

## 9. G0 handoff and verdict

**GO for contract review and bounded follow-on prototypes.** The A1 record,
visibility, and user-key paths do not presently trigger escalation. The design
has static implementations that obey Rust coherence and preserve provider type
identity.

**NO-GO for major shared-IR/compiler surgery until signed L7 G0.** Outstanding
approval items are the canonical defining-module/declaration identity, exact
ABI-v10 byte grammar, shared-runtime `MossMap<K,V,Ops>` ownership with Team S,
accessor visibility/private dependency policy, and both Architects' review of
specialization termination, ownership/effect preservation, and source-free
provenance.

Phase 25.5 D1/D2 opacity and source-level visibility design remain deferred.
This G0 result does not add `Box[T]`, `where T`, `Self`, local annotations,
dynamic dispatch, implicit `Any`, or user-written generic parameter syntax.
