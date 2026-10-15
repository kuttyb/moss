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

Member admission and target selection use the same IR; Team S does not create a
second receiver-variable system:

```text
MemberDispatchRequirement {
  requirement_id,
  receiver: TypeExpr,
  member_name,
  argument_types: [TypeExpr],
  result: Optional<TypeExpr>,
  receiver_access: AccessMode,
  argument_access: [AccessMode],
  admitted_template_or_builtin_family,
  selected_callable_id: Optional<CallableId>,
  origin: RequirementOrigin,
}
```

Dispatch may admit a member when the receiver's outer constructor is known while
contained slots remain open. Thus `Vector[alpha].push(Int)` records the `WRITE`
receiver/argument contract and contributes `alpha = Int`; a known nominal
template with open fields behaves the same way. Exact target selection follows
substitution and precedes ownership/effect closure. An entirely unknown receiver
remains rejected. Team S owns admission and target selection; Team G owns
unification and closure; the positive ledger #12 test is jointly owned.

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

The bounded A1 Rust prototype (`tests/phase25/g0/check_rust_feasibility.py`) exercises
this selected strategy directly with mechanical public backend fields (`pub field_0: T`)
and consumer-generated free functions. The accessor/private-field experiment is preserved
as an evaluated alternative in `tests/phase25/g0/rust/accessor_alternative/`. A forward
negative test for Moss-level private field visibility is established in
`tests/phase25/g0/conformance/private_field_visibility_negative.moss` for the earliest
compiler milestone supporting the relevant source restriction (G1/G2); Phase 25.5 D2
is not implemented now.

## 8. Map key specialization boundary (joint with Team S)

Directly requiring a consumer to implement Rust `Eq`/`Hash` for a provider-owned
nominal type violates Rust's orphan rules. Deriving those traits would also bypass
custom Moss `isequal`/`hash` semantics and incorrectly assume Float `Eq`/`Hash`.

The proposed G0 boundary, confirmed by Team S implementation-owner concurrence,
is a **policy-free compiler-owned map runtime, not a foreign trait
implementation**:

- Team G provides a representation-stable `MossMap<K,V>` whose Rust type does not
  require `K: Eq + Hash` and contains no policy type, callback, function pointer,
  closure, trait object, or vtable. Set, if delivered in Phase 25, reuses the same
  versioned key proof and kernel; otherwise it is explicitly deferred.
- Team S defines the Moss key proof and resolves exact `isequal(K,K)->Bool` and
  `hash(K)->Int` call targets, purity, effects, and laws for each concrete `K`.
- Team G lowers each Map operation to a consumer-side specialized free function keyed
  by `(operation, K, V, isequal-target, hash-target)`. The function calls those targets
  statically and manipulates the provider-owned container through its stable public
  backend representation/accessors. No function pointer, closure, trait object, or
  vtable is stored in the Map value.
- Built-in keys may use an internal fast path only when Team S proves it observationally
  equivalent. User keys never fall back to Rust derives. Float receives no key proof
  at G0 while Team S specifies and implements its Moss equivalence/hash contract for G3.
- `.mossi` exports the key-proof target identities and their semantic/effect hashes;
  source-free consumers instantiate the same adapter specialization without provider
  source.

The G0 reference implementation stores `(Moss hash, key, value)` entries and
exposes neutral backend accessors. Consumer-owned specialized operations call the
two exact proof targets directly. It is intentionally linear; the current linear
Rust prototype remains acceptable G0 feasibility evidence.
The production `MossMap<K,V>` targeted for G3 is a bucketed open-addressing /
robin-hood hash table storing `(Moss hash, key, value)` entries with expected $O(1)$
lookup, insert, and delete, and $O(N)$ capacity-proportional traversal.
`keys()` returns an eager owned snapshot `Vector[K]` where Copy keys are copied and
non-Copy keys are cloned/copied, guaranteeing snapshot stability detached from subsequent
map mutations in $O(N)$ time and allocation. If performance forces a policy type into
the Rust value type, that is an A1 identity change and must return to L7.

The shared semantic protocol is `key-protocol-v1`. Moss v0.1 has no source-level
generic parameter syntax (`[T]`), so source code does not declare type-variable
parameters. Instead, nominal types declare concrete structural methods:
`fn isequal(other: Key) -> Bool` and `fn hash() -> Int` (or duck-typed free callables).
Inside the compiler Type IR and ABI-v10, the protocol retains internal generic type
variables (`TypeVar K`): equality is `(READ K, READ K) -> Bool` and hash is
`(READ K) -> Int`. Both are deterministic, non-mutating, non-consuming, non-raising,
non-panicking, non-diverging, and free of message, domain-state, I/O, executor, and
every other observable effect. Reusable acceptance law tests owned by Team S require
reflexivity, symmetry, transitivity, equal-keys/equal-hash, and stability of the
stored key snapshot; this is not a claim of general theorem proving.

**Canonical Key-Proof Coherence Invariant:**
Canonical key-proof coherence across consumers is an explicit compile/link invariant.
For any concrete nominal key type, there is at most one canonical `KeyProof` identity
in the program (`KeyProofId`). When multiple independent consumer packages import,
specialize, or pass values of type `Map[Key, V]`, all consumers must resolve and bind
to the exact same canonical `KeyProofId`. Conflicting key proofs for the same nominal
type fail closed at compile or link time with `KEY_PROOF_COHERENCE_MISMATCH`.

Float `isequal`/`hash` semantics remain an approved Phase 25 G3 deliverable owned
by Team S. G0 leaves them unimplemented, without a blanket Phase 25 prohibition.
In G3, Float keys follow approved Julia-like semantics: `isequal(-0.0, +0.0)` is `false`,
with distinct hashes (`hash(-0.0) != hash(+0.0)`), canonical NaN equivalence
(`isequal(NaN, NaN) == true`), and signed-zero total ordering. Missing, premature,
or invalid proof fails at Moss checking before native emission. Negative tests
distinguish valid future Float proofs from malformed or forged proofs.
Built-in fast paths must be proven observationally equivalent and use the same proof
identity. A versioned `MapKeyProofId`/`SetKeyProofId` participates in operation
specialization, semantic dependencies, ABI hashes, and cache keys.

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

### 11.2 Decisions resolved for G0 baseline

The open questions for G0 have been resolved as follows:

1. **Team S Concurrence:** Confirmed in `docs/phase25/TEAM_S_G0_IMPLEMENTATION_OWNER_CONCURRENCE.md`.
   Nominal key types declare concrete structural methods `fn isequal(other: Key) -> Bool`
   and `fn hash() -> Int` under `key-protocol-v1`. Team S owns comparator-law validation
   tests, Float-key delivery in G3, Set delivery/deferment, and member admission for
   open receivers (ledger #12).
2. **Map Complexity & Keys Contract:** Confirmed policy-free `MossMap<K,V>` representation.
   Production target in G3 is bucketed hash table with expected $O(1)$ lookup/insert/delete.
   `keys()` is an eager owned snapshot `Vector[K]` with snapshot stability in $O(N)$ time.
3. **Package Identity:** Frozen per L7 verdict as `module-v1:<name>@<version>::<module>`
   with deterministic provider-conflict rejection, subject to confirming Margo's
   resolution and artifact binding semantics.
4. **Native A1 Strategy:** Provider-owned generic Rust record with mechanical public
   backend fields (`pub field_0: T`) plus Moss-enforced visibility checking, with
   consumer-generated free functions. Preserved accessor alternative in test suite.
5. **Float Keys:** Blanket prohibition removed; Float keys are an approved Phase 25 G3
   deliverable owned by Team S with Julia-like semantics (`isequal(-0.0, +0.0) == false`).
6. **Canonical Key-Proof Coherence:** Cross-consumer key-proof coherence is an explicit
   compile/link invariant failing closed with `KEY_PROOF_COHERENCE_MISMATCH`.

### 11.3 Decisions reserved for G2 / implementation phase

The following remain reserved before beginning shared compiler implementation:
- Fresh exact-SHA review addenda from Team G and Team S independent L6 Architects;
- Written L7 authorization for G2 shared compiler surgery; and
- Compiler guard budgets after measured corpus evidence.

## 12. G0 sign-off

| Reviewer | Verdict | Exact reviewed commit | Notes |
| --- | --- | --- | --- |
| Team G independent L6 Architect | NO-GO on draft | `40fd3b6ea1aa89ae5a05e2afe092c8699a56f1f2` | Historical review; addendum pending on corrected candidate. |
| Team S independent L6 Architect | NO-GO on integrated draft | `8a417edd06e8194fe6c29023c2e21070d710cfa0` | Historical review; addendum pending on corrected candidate. |
| Team S Implementation Owner | CONCURRENCE for G0 | `docs/phase25/TEAM_S_G0_IMPLEMENTATION_OWNER_CONCURRENCE.md` | Frozen boundary, key signatures, laws, Float G3 delivery, #12. |
| L7 | CONDITIONAL GO for G0; NO-GO for G2 | `27110249abeca3102cfd6cbf71ac4db436f1a7bb` | G0 accepted subject to review corrections; G2 implementation held. |

**Author verdict:** corrected G0 candidate complete and validated; major shared-IR
compiler surgery remains held at NO-GO pending final Architect review addenda and
explicit written L7 G2 authorization.
keys, package-stripped imported module identity, and ABI-v9 nominal schema must be
replaced together rather than patched independently.
