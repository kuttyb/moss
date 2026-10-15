# Phase 25 Team G Type IR Contract

**Status:** G0 freeze candidate; implementation is blocked until Team G Architect,
Team S Architect, and L7 record GO in the sign-off table below.

**Baseline:** `fa3a340a0a6652568b97869489da1a84038b9857` (`moss-0.1`)

**Owners:** Team G owns the representation and inference implementation. Team S
consumes this contract for dispatch and Map/Set key semantics.

**Scope:** inferred generic nominal records, Option B contextual result inference,
static specialization, ownership/effects, and the semantic identities exported by
ABI v10.

This contract introduces no source spelling for type parameters. In particular it
does not add `Box[T]`, `where T`, generic trait declarations, local annotations,
`Self`, dynamic dispatch, implicit `Any`, or a generalized user-written form of the
built-in `Vector[Int]()` constructor.

The six G0 obligations are covered as follows:

| G0 obligation | Contract location |
| --- | --- |
| Canonical nominal identity | Sections 2–3 |
| Open-result summaries | Section 4.2 and ABI v10 Section 3.3 |
| Method annotation/open field rule | Section 4.4 |
| A1 lowering and visibility | Section 7 and ABI v10 Sections 4 and 6 |
| User-defined Map keys | Section 8 and ABI v10 Section 3.5 |
| Type IR/ABI IDs, hashes, provenance, mismatch | Sections 2–3 and ABI v10 Sections 1–5, 8 |

## 1. Baseline facts and reason for one new IR

The baseline has several representations that are adequate only while user nominal
types have one concrete layout:

- `canonical_type_name` normalizes type strings and module rewriting turns a
  declaration into `module__Name`.
- an inferred object field is stored by mutating `Field.type`, with `Field.inferred`
  remembering that the source omitted the type;
- a function specialization is keyed by a vector of canonical parameter strings;
- constraints refer to parameter/local names as strings; and
- ABI v9 serializes open function parameters and statement IR, but every exported
  nominal record is a single concrete `export type ... nominal` plus a
  `public_representation` field list.

Phase 25 must not add another family of placeholder strings beside these. The
checker, module loader, semantic queries, Fast Debug, and native lowering must share
the structured `TypeRef`/`TypeVar` model below. Canonical text is a projection for
diagnostics and serialization, never an equality or lookup implementation.

## 2. Normative type algebra

The compiler has exactly one semantic type algebra:

```text
TypeRef = Primitive(primitive_id)
        | Builtin(builtin_id, [TypeRef])
        | NominalInstance(template_id, [TypeRef])
        | Enum(enum_id)
        | Domain(domain_id)
        | TraitConstraint(trait_id)          # constraint only; never a value layout
        | InferenceVar(TypeVarId)            # checking only; forbidden in closed IR
        | Unit

TypeVarId = (inference_session_id, owner_id, ordinal)
```

`Primitive`, `Builtin`, `NominalInstance`, `Enum`, and `Domain` IDs are interned.
Equality and hashing operate on tags and interned IDs recursively. `InferenceVar`
identity is allocation identity, not its display name. A type variable carries a
union-find representative, an optional binding, an origin, and accumulated
requirements; it is never represented by `""`, `_value`, `_element`, or a guessed
concrete type.

Required companion records are:

```text
NominalTemplate {
  template_id: NominalTemplateId,
  defining_module: ModuleIdentity,
  declaration_name: String,
  declaration_provenance: SourceProvenance,
  field_slots: [FieldSlot],               # source order
  methods: [MethodTemplate],
  visibility: MossVisibility,
}

FieldSlot {
  name: String,
  declared_type: Optional<TypeRef>,       # closed annotation, if present
  variable_ordinal: Optional<u32>,        # one slot for an omitted field type
  provenance: SourceProvenance,
}

ResolvedNominalInstance {
  template_id: NominalTemplateId,
  substitution: [TypeRef],                # variable ordinal order
  resolved_fields: [TypeRef],             # field source order
  instance_key: NominalInstanceKey,
}

Substitution = ordered map<TypeVarId, TypeRef>
```

An omitted field gets one template slot even when two fields happen to infer the same
type. Explicit field types contain no template slot. Substitution must be complete
before a value crosses a closed-body, ownership, Fast Debug, or code-generation
boundary.

## 3. Stable identities and canonical formatting

### 3.1 Defining module and declaration identity

`ModuleIdentity` is the exact ABI module ID, not the source-only `module__Name`
spelling:

```text
module-v1:<package-name>@<package-version>::<module-name>
```

Margo must reject two resolved packages that claim the same `ModuleIdentity` but do
not have the same bound ABI contract. Git/path location and absolute paths are
provenance, not nominal identity. This gives independent consumers the same identity
for the same package version while preventing a second, incompatible provider from
silently impersonating it in one resolved graph.

```text
NominalTemplateId =
  nominal-template-v1:<ModuleIdentity>:type:<declaration-name>
```

The ID is intentionally independent of source path, line, private method bodies, and
field inference results. Renaming the module or declaration changes nominal identity.
Changing a template's ordered field declaration contract changes its ABI semantic
hash and requires dependent rebuild/recheck; it does not masquerade as a second
declaration.

Two declarations with identical field layouts have different template IDs. Two
instances of one declaration with different resolved field types have different
instance keys.

### 3.2 Concrete instance and type keys

Canonical keys use a length-prefixed binary encoding (`type-key-v1`) rather than
delimiter concatenation. The descriptions below are readable equivalents:

```text
NominalInstanceKey = H256(
  "nominal-instance-v1" || template_id ||
  count(resolved_fields) || each(type_key(resolved_field)))

BuiltinInstanceKey = H256(
  "builtin-instance-v1" || builtin_id ||
  count(arguments) || each(type_key(argument)))
```

The ordered **resolved field types**, not merely distinct type variables, are hashed
for a nominal instance. The record also preserves the ordered substitution so a
field-to-variable relationship can be explained. All referenced nominal arguments
use their instance keys. Hashes are SHA-256 lowercase hex with an algorithm/version
prefix in serialized form; the baseline 64-bit FNV `stable_hash` is not collision
resistant enough to be a cross-package type identity.

Canonical diagnostic text is deterministic and source-like:

```text
Box[value=Int]
Entry[key=String,value=Int]
Vector[Box[value=Int]]
```

Qualified semantic/API text includes the defining module when ambiguity is possible:
`pkg@1.0::inventory.Box[value=Int]`. Formatting must not be parsed back to recover a
type key.

### 3.3 Specialization keys

Every static callable instance uses:

```text
SpecializationKey = H256(
  "specialization-v2" || callable_semantic_id ||
  receiver_type_key? || ordered_argument_type_keys ||
  ordered_callable_argument_ids || ordered_open_result_bindings)
```

Ownership and observable effects are checked outputs keyed by this identity, not
inputs that can accidentally create two specializations of the same call. If
rechecking one key produces different effects or return type, compilation fails as a
non-deterministic/inconsistent specialization.

## 4. Constraint and inference contract

### 4.1 Fresh construction

Each nominal constructor expression creates fresh variables for every omitted field
slot, unifies named arguments with the corresponding slots, and produces a
`NominalInstance(template_id, substitution)`. Constructor argument order has no
effect; the template's field order determines the key. Every field must be resolved
by body closure. Separate constructor calls never share variables.

Nested constructors and collection literals recursively contribute constraints. For
example `[Box(value = 1), Box(value = 2)]` closes as
`Vector[Box[value=Int]]`; a mixed incompatible element use reports the literal and
both constructor origins. A zero-iteration loop cannot be the only event that chooses
a type: constraints are solved statically from its body without relying on runtime
execution.

One local binding has one monotype in a body. Reassignment or conflicting uses cannot
freshen that binding. The checker rejects an incompatible second concrete instance.

### 4.2 Option B open-result summaries

A callable may close its body while exporting variables in its result. `fresh() -> []`
therefore exports a generic result even though no element is inserted inside `fresh`.
The summary is:

```text
OpenFunctionSummary {
  callable_id,
  variables: [BoundVar],                  # canonical ordinal order
  parameter_types: [OpenTypeExpr],
  result_type: OpenTypeExpr,
  requirements: [Requirement],
  parameter_effects: [AccessMode],
  observable_effects: ObservableEffects,
  semantic_dependencies: [CallableId],
  provenance: SourceProvenance,
}
```

`BoundVar` IDs are summary-local ordinals. On **every call expression**, the caller
allocates fresh `TypeVarId`s, substitutes all summary types and requirements, unifies
argument evidence, and attaches the result variables to that expression. Assignment
does not itself freshen; two uses of one assigned binding constrain the same variables.
Two calls receive independent variables.

Summaries compose transitively. `relay(): fresh()` exports a new summary whose result
and requirements are the projection of `fresh`, with fresh summary-local ordinals.
No intermediate function may replace an open result with `Vector[_]`, a default type,
or a shared provider variable. The same rule applies when any hop is loaded only from
`.mossi`.

Open summaries are generalized only over variables reachable from the result or an
open parameter relationship. Locals not escaping the callable must resolve before
summary publication. Mutation/consume requirements and all structural operations on
an escaping variable are retained. Generalization is deterministic and follows first
occurrence in `(parameters, result, requirements)` traversal.

At the closure of `main`, a test, handler, concrete function specialization, or other
reachable execution body, every value type and call target must be concrete. An
unbound reachable variable produces `TYPE_AMBIGUOUS_OPEN_RESULT` at its binding/use,
with the creating call and provider summary as related provenance. There is no
default element/key/value type and no implicit `Any`.

### 4.3 Requirements and complete reason chains

Requirements use type terms and stable callable/field/method IDs, not expression
strings:

```text
Requirement {
  kind: SameType | Field | Method | Operator | Index | Iterate | Callable | MapKey,
  subject: TypeExpr,
  arguments: [TypeExpr],
  result: Optional<TypeExpr>,
  access: AccessMode,
  origin: RequirementOrigin,
  parent_requirement: Optional<RequirementId>,
}
```

`RequirementOrigin` contains module ID, source file provenance, span, enclosing
callable, and call-expression identity. Imported summaries preserve provider origins.
Instantiation adds a caller edge rather than replacing the provider origin. Failure
reports the outermost user call as primary and the complete deterministic caller-to-
requirement chain as related notes. A missing transitive `less` method through two or
more calls is a Moss diagnostic, never an internal error or line-0-only report.

### 4.4 Field access and method annotation rule

Inside `type Reverse`, an annotation `other: Reverse` denotes the receiver's exact
`NominalInstance`, including all field substitutions. It is equivalent to applying
the current receiver substitution to the template, but adds no source `Self` syntax.
The rule applies recursively in built-in containers in a method annotation.

A receiver known to be `NominalInstance(template, open substitution)` permits
`receiver.field`: that access resolves the declared field slot and adds constraints to
the corresponding variable. An unconstrained untyped parameter has no known nominal
template, so `value.field` remains illegal. It must not be accepted by inventing a
structural record type.

Ordinary generic methods lower as statically selected free functions keyed by exact
receiver instance and argument types. Moss method visibility is checked before
selection and is independent of the Rust representation strategy in Section 7.

## 5. Solving, closure, and termination

The solver is a deterministic worklist over interned terms:

1. allocate/fresh variables;
2. union equality classes and bind variables with an occurs check;
3. decompose built-in and nominal applications only when their constructors/template
   IDs match;
4. resolve field/method/operator/collection requirements when their receiver becomes
   known;
5. enqueue newly discovered callable specializations and composed requirements; and
6. require closure before ownership/codegen.

Nominal unification is invariant. `Box[Int]` and `Box[String]` do not unify, and
instances from distinct declarations never unify even with identical fields.

Recursive value layouts are rejected when a nominal instance contains itself through
owned fields without an approved indirection. The diagnostic includes the minimal
template/field cycle. Phase 25 adds no approved general indirection.

Specialization uses a tri-color table keyed by `SpecializationKey`. Re-entering an
`IN_PROGRESS` key reports the existing unsupported recursion cycle. A recursive edge
whose key changes is also rejected as uncontrolled recursive specialization; the
diagnostic prints the bounded first cycle plus the first differing type argument.
Type depth, requirement count, and total specialization count have implementation
budgets only as denial-of-service guards. Exhausting a budget produces
`TYPE_INFERENCE_LIMIT`/`SPECIALIZATION_LIMIT` with counts and provenance; it never
accepts a partially closed program. Memoization is by canonical key, so repeated
generic use does not remonomorphize the same instance.

## 6. Ownership and effects

Inference never weakens the existing `READ`, `WRITE`, `CONSUME` contract:

- every requirement and callable summary carries access modes independent of type
  variables;
- after substitution, ownership is checked on the concrete storage paths before Rust
  emission;
- only `READ + READ` overlap is legal; mutation/consume-after-borrow and all other
  overlaps retain existing diagnostics;
- a nominal instance is `Copy` only if the existing language rule says all of its
  resolved representation is Copy; codegen must not infer this from a convenient Rust
  derive; and
- generic method/function specializations retain their checked receiver, parameter,
  leaf, observable, raise, failure, and divergence effects. Imported summaries are
  authoritative and may be refined only without weakening them.

Open results are owned values, not implicit borrows. A type variable cannot hide a
domain handle, `FileIO`, `Range`, or another restricted capability: capability/layout
validation runs after every binding and again at closure.

## 7. A1 native lowering and Moss visibility decision

The provider crate owns exactly one generic Rust record per exported Moss nominal
template:

```rust
pub struct Box<F0> {
    #[doc(hidden)] pub __moss_field_value: F0,
}
```

Concrete consumers refer to `provider::Box<i64>`; they do not redeclare it. Therefore
two consumer crates that import the same instance use the same Rust type constructor
and representation. Layout-affecting attributes, field order, generic parameter order,
and provider crate identity are ABI-hashed.

**Decision: pub representation plus the Moss checker.** Rust fields needed by
consumer-generated specialized free functions are `pub` and mechanically named. They
are backend plumbing, not Moss visibility. Moss field/method/type access is admitted
only from the source visibility table and `.mossi` semantic dependency records.
Private records and helpers required to specialize an exported public body appear in a
non-exported semantic-dependency section; they never enter the importable symbol table
or semantic completion. Generated Rust is not a supported Moss API.

Ordinary value methods become specialized free functions in the artifact that owns the
call specialization. They accept `&provider::Box<T>`, `&mut ...`, or owned `...`
according to checked effects. No consumer writes inherent impls for the provider type,
and no runtime vtable or trait object is introduced. A specialization that needs a
private Moss method may carry its semantic body as a private dependency, but source
code still cannot name it.

This is a G2 decision only. Phase 25.5 D1/D2 ownership/opacity work may replace the
mechanical public representation with provider accessors, but Phase 25 must not
silently implement that source feature.

## 8. Map key specialization boundary (joint with Team S)

Directly requiring a consumer to implement Rust `Eq`/`Hash` for a provider-owned
nominal type violates Rust's orphan rules. Deriving those traits would also bypass
custom Moss `isequal`/`hash` semantics and incorrectly assume Float `Eq`/`Hash`.

The proposed G0 boundary, pending Team S and L7 concurrence, is an **equivalent static key adapter, not foreign trait
implementation**:

- Team G provides a provider-owned, representation-stable `MossMap<K,V>` (and Set
  equivalent if Team S lands one) whose Rust type does not require `K: Eq + Hash` and
  whose layout is independent of a consumer policy type.
- Team S defines the Moss key proof and resolves exact `isequal(K,K)->Bool` and
  `hash(K)->Int` call targets, purity, effects, and laws for each concrete `K`.
- Team G lowers each Map operation to a consumer-side specialized free function keyed
  by `(operation, K, V, isequal-target, hash-target)`. The function calls those targets
  statically and manipulates the provider-owned container through its stable public
  backend representation/accessors. No function pointer, closure, trait object, or
  vtable is stored in the Map value.
- Built-in keys may use an internal fast path only when Team S proves it observationally
  equivalent. User keys never fall back to Rust derives. Float receives no key proof
  unless Team S defines its Moss equivalence/hash contract.
- `.mossi` exports the key-proof target identities and their semantic/effect hashes;
  source-free consumers instantiate the same adapter specialization without provider
  source.

The implementation may use hash buckets plus stored Moss hash values or a linear
reference implementation, but representation and complexity must be documented before
G3. This decision keeps Rust coherence local and static while preserving custom Moss
semantics. If performance forces a policy type into `MossMap`'s Rust type, that is an
A1 identity change and must return to L7 rather than landing implicitly.

## 9. Semantic API contract

`type`, `inspect`, `calls`, `effects`, `ownership`, `why`, and `cost` expose the same
ABI-v10 facts for source and imported instances:

- `template_id`, `instance_key`, structured `field_types`, and canonical display;
- `specialization_key`, exact receiver/argument/result types, and call target;
- `open_result` with summary ID and fresh caller variable IDs before closure;
- complete requirement chains and source provenance;
- authoritative access modes and observable effects; and
- materialization owner (`provider-record` / `consumer-free-function`) and reuse/count
  facts.

Public API JSON adds fields; it does not repurpose existing `entity-v1` IDs. A concrete
instance's identity is stable across separate consumers, while inference-session
variable IDs are deliberately session-local.

## 10. Conformance gates

Before this contract is called implemented, tests must establish:

1. independent `Box[Int]` and `Box[String]`, plus nominal inequality for two declarations
   with the same layout;
2. nested nominal constructors in collection literals and zero-iteration loops;
3. one binding rejects conflicting instances while separate constructor/call results
   remain independent;
4. `fresh`/`relay`/multi-hop source-free summaries freshen per call and a never-pinned
   result reports precise ambiguity;
5. a transitive missing method reports the full outer-call-to-provider requirement
   chain;
6. known-open nominal field access succeeds and unconstrained receiver field access
   fails;
7. generic ownership overlap/use-after-consume failures occur before Rust emission;
8. recursive layouts and changing-key specialization cycles terminate diagnostically;
9. two consumers name one provider-owned Rust record identity and compatible layout;
10. compiled-only user keys call custom Moss equality/hash without an orphan impl; and
11. native and Fast Debug agree where source is present; source-free Fast Debug fails
    during preflight with its established capability diagnostic.

## 11. Freeze authority and joint-review questions

### 11.1 Team G decisions ready to freeze now

Team G can freeze the following within its ownership after its independent Architect
review. These do not claim Team S or L7 approval:

- one structured type/variable IR across all compiler consumers;
- module/declaration/ordered-field-based nominal identity;
- per-call freshening and transitive open-result summaries;
- exact-receiver interpretation of a containing nominal annotation;
- provider-owned generic Rust record plus consumer free functions;
- specialization closure/termination rules; and
- preservation of existing ownership/effect semantics through substitution.

### 11.2 Decisions blocked on external written acknowledgement

The following are deliberately **not frozen** by this document:

- Team S's exact key method names, signatures, purity/law contract, Map/Set proof
  ownership, and dispatch integration;
- the shared `MossMap` representation and whether a built-in fast path can preserve
  identical semantics without adding a policy type to value identity;
- L7's final package/module identity rule and collision handling;
- the cross-team ABI-v10 schema and version-switch milestone;
- pub backend fields plus Moss-only visibility rather than provider accessors; and
- implementation resource budgets after measured corpus evidence.

They require explicit Team S Architect concurrence where applicable and written L7 GO.
Silence, branch availability, or implementation feasibility is not acknowledgement.

### 11.3 Questions to resolve in the recorded review

The following must be answered in the recorded G0 review, not during implementation:

1. **Team S:** confirm the exact Moss method names/signatures, purity rule, and law-test
   ownership for `isequal`/`hash`, and whether Set shares the same key-proof record.
2. **Team S + Team G:** freeze the `MossMap` backend representation and complexity
   promise before G3; confirm no policy type enters its nominal Rust type.
3. **L7:** approve `module-v1:<name>@<version>::<module>` plus graph-level conflicting
   provider rejection, or require a durable package UUID/source identity before code.
4. **Both Architects:** confirm that pub mechanical Rust fields plus a closed Moss
   symbol table satisfies Phase 25 visibility, with stronger opacity deferred to 25.5.
5. **L7:** set implementation guard budgets only after corpus measurement; budgets are
   diagnostics, not semantics.

## 12. G0 sign-off

| Reviewer | Verdict | Exact reviewed commit | Notes |
| --- | --- | --- | --- |
| Team G independent L6 Architect | Pending | — | Must challenge inference closure, termination, identity, effects, and A1. |
| Team S independent L6 Architect | Pending | — | Must confirm dispatch and Map/Set key boundary. |
| L7 | Pending | — | `GO` is required before shared Type IR/compiler surgery. |

**Author verdict:** ready for joint G0 design review, not yet authorized for shared-IR
implementation. The baseline architecture can support the design, but its string type
keys, package-stripped imported module identity, and ABI-v9 nominal schema must be
replaced together rather than patched independently.
