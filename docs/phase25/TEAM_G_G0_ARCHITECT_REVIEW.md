# Phase 25 Team G — Independent G0 Architect Review

**Role:** Independent L6 Architect, Team G (Generics & Inference)  
**Reviewed baseline:** `fa3a340a0a6652568b97869489da1a84038b9857`  
**Branch:** `phase25-g0-architect`  
**Language/compiler:** `moss-0.1` / `0.1.0`, agent protocol `moss-agent-1`  
**Native toolchain used for probes:** `rustc 1.98.1
(48a229ceaefd4985c50990b14116b6d856af0985)`,
`x86_64-unknown-linux-gnu`, debug/unoptimized standalone builds  
**Scope:** report and falsification only; no shared IR implementation

## 1. Verdict

**NO-GO for work beyond G0.** The Phase 25 charter is a direction and an
acceptance envelope; it is not the written, reviewed G0 contract or L7 GO that
the charter itself requires. The baseline proves that A1's provider-owned Rust
record plus consumer-owned free-function strategy is feasible, and its existing
ordinary-function specialization machinery is useful. It does not yet prove a
shared generic nominal IR, Option B open-result closure, user-defined Map keys,
finite nominal layouts, or ABI-v10.

The block is procedural and technical, not a rejection of the mission. Major
shared-IR work may begin only after all of the following exist on a pinned
interface milestone:

1. a single Team G/Team S type-IR contract containing the identities, records,
   invariants, and phase ordering listed in this review;
2. affirmative, attributable review by the Team G Independent Architect and
   the Team S Independent Architect;
3. Team S concurrence on the Map-key representation and purity/law boundary;
4. successful A1 and compiled-only Map-key prototypes, including two independent
   consumers and native execution;
5. an ABI-v10 schema/migration contract with explicit ABI-v9 rejection tests;
6. written L7 GO naming the reviewed contract commit and both Architect reviews.

This report supplies an independent set of conditions for items 1–5. It does
not substitute for Team S review or L7 approval.

## 2. Baseline facts and falsification results

The current compiler represents source types primarily as canonicalized strings.
`ObjectType` has one mutable field vector; `infer_object_fields()` resolves an
untyped field once for the entire declaration. `FunctionSpecialization` is keyed
by ordered parameter-type strings. ABI-v9 serializes generic function bodies and
constraints, but exported object records contain only one resolved
`public_representation`; there is no nominal template, bound type-variable graph,
open result, or generic instance record.

The following baseline results were reproduced at the reviewed SHA:

| Probe | Exact result | Architectural consequence |
| --- | --- | --- |
| Two `Box` constructions with `Int` and `String` fields | `TYPE_MISMATCH` at the second construction: `Box.value` conflicts | Field inference is declaration-global, not per nominal instance. |
| `fresh() -> []`, relayed and pinned at two call sites | `TYPE_INFERENCE_FAILED` inside `fresh`, line 2 | No exported/open result summary or call-site freshening exists. |
| `var v = []; v.push(1)` | `MOSS_COMPILE_ERROR`: local member calls are not implemented | Team S dispatch and Team G closure must land together for ledger #12. |
| Never-pinned `fresh()` | `TYPE_INFERENCE_FAILED`, but at the provider result and without a call-chain explanation | The negative direction exists, but not Option B provenance or caller closure. |
| Unknown untyped receiver field access | `UNTYPED_FIELD_ACCESS` | Preserve this rejection; do not turn all field access into structural `Any`. |
| Two named records with identical `Int` layout | `TYPE_MISMATCH` (`Right`, expected `Left`) | Existing nominal distinction is correct and must survive instantiation. |
| Two-hop missing `less` requirement | Error is reported in `relay`, not the outermost user call, with no `related` chain | Ledger #9 is not satisfied. Requirement provenance must compose. |
| Direct recursive `type Node: next: Node` | `moss check --json` succeeds; generated Rust fails with E0072 and recursion-limit errors | Finite layout is not established before codegen. This is a G2 safety gate. |
| `Map[Key, Int]` where `Key` defines Moss `isequal` and `hash` | `moss check --json` succeeds; Rust fails because `Key: Eq + Hash` is absent | The current `std::collections::HashMap<K,V>` lowering is not a user-key design. |

The baseline regression suites remain valuable and all passed:

- `check_generic_specialization.py`: 40/40;
- `check_builtin_generic_typing.py`: 129/129;
- Phase 15.9 cross-package specialization, including source-free `.mossi` +
  `.rlib`: passed;
- Phase 24 imported generic expressions, source-backed and source-free: passed.

These results prove the old callable-specialization subsystem, not the new
generic nominal and open-result contracts.

## 3. Required shared type IR contract

The implementation must introduce one typed representation used by checking,
semantic queries, `.mossi`, Fast Debug, and native lowering. Parallel string
encodings for Team G and Team S are forbidden.

At minimum the shared contract needs these concepts:

- `TypeTerm`: primitive, built-in application, nominal instance, bound inference
  variable, callable identity, domain capability, error/result form, and unit;
- `TypeVarId`: `{scope_id, binder_ordinal}`; never a source name and never reused
  across calls;
- `NominalTemplateId`: the defining package/module identity plus declaration
  identity, independent of field layout;
- `NominalTemplate`: ordered fields whose type terms may contain template-bound
  variables, method signatures, visibility, source provenance, and a layout
  dependency graph;
- `NominalInstance`: template ID plus the ordered, fully resolved field-type
  vector and the substitution that produced it;
- `Substitution`: an acyclic map from bound variable IDs to type terms;
- `OpenFunctionSummary`: bound variables, parameter/result type terms,
  structural/member constraints, effect/ownership expressions, transitive
  dependencies, and provenance edges;
- `ConcreteSpecializationKey`: callable declaration ID plus ordered canonical
  concrete type IDs, not display text or allocation order.

### 3.1 Canonical identities

The charter's phrase “defining-module identity” is underspecified. Current
`module_id <package>::<module>` is insufficient if two resolved package nodes
have the same declared name. G0 must state how Margo supplies a deterministic
package-instance identity for registry, Git, and path dependencies without
embedding an absolute path. It must also state whether a version-compatible
provider rebuild preserves type identity while its semantic/binary hashes
change.

The minimum identity rule is:

```text
template_key = (resolved_package_instance_id, module_name, declaration_name)
instance_key = (template_key, ordered canonical resolved field type IDs)
```

Hash a length-delimited/versioned encoding of that tuple; never hash formatted
source text. Field names and visibility belong to the template semantic hash,
while ordered resolved field types distinguish instances. Thus:

- same declaration + `[Int]` versus `[String]` are distinct instances;
- different declarations with identical field types remain distinct;
- the same provider instance imported by two consumers has one key and one
  provider-owned Rust type;
- built-in `Vector[Int]` retains its existing spelling, while user nominal
  source declarations gain no `Box[T]` syntax.

User diagnostics need a non-source display, for example
`Box{value: Int}`. Do not print `Box[Int]` in a way that suggests newly accepted
source syntax. Semantic JSON must return the stable ID separately from display.

### 3.2 Finite layout and specialization closure

An occurs/layout check must reject every by-value cycle before native emission,
including cycles through generic instances and built-in containers whose Rust
representation is recursively sized. The diagnostic must provide a bounded
cycle such as `Node.next -> Node`, at Moss source lines, not Rust E0072.

Specialization uses a worklist keyed by `ConcreteSpecializationKey`, with
`unseen`, `in_progress`, and `closed` states. Existing ordinary recursion remains
illegal. A recursive dependency that grows its key (for example continually
nesting a collection) must produce a bounded Moss specialization witness rather
than run until memory exhaustion. Completion requires concrete type, call target,
ownership mode, and observable effects for every reachable node.

## 4. Option B open-result contract

`fresh(): []` must export a summary equivalent to `() -> Vector<α0>`, with `α0`
bound by that summary. It is not an unresolved compiler string and it is not a
global variable attached to `fresh`.

Every call performs capture-avoiding freshening of all summary binders. `relay`
imports/freshens `fresh`, then exports its own bound result variable. Consequently
`ints = fresh()` and `names = relay()` are independent, while two conflicting
uses of the same binding constrain one variable and fail.

The solver contract must include:

1. union/equality and constructor decomposition over `TypeTerm`;
2. occurs checks for substitutions;
3. contextual constraints from assignment, return/reply, constructors, nested
   fields/collections, indexing, member calls, and all control-flow joins;
4. transitive member requirements carrying a provenance edge at each call;
5. body-exit closure: every executable binding and reachable call is concrete;
6. an ambiguity error for an unpinned result, with the result origin and complete
   call chain; never a default element type and never implicit `Any`.

For ledger #9, the final diagnostic belongs at the outermost concrete user call.
Its `related` entries must walk `outer call -> relay requirement -> inner
requirement -> missing Token.less`, with real source lines and semantic IDs.
The current error in `relay` without `related` entries is insufficient.

Known generic nominal receivers permit field projection: if `b` has
`Box{value: α}`, then `b.value` constrains/returns `α`. An unconstrained untyped
parameter remains rejected with `UNTYPED_FIELD_ACCESS`. Inside `Reverse`, an
annotation `other: Reverse` is rebound to the receiver's exact instance; it
must not mean an existential/independent `Reverse` and no `Self` syntax is added.

Constraint solving must finish before ownership/effect validation is considered
closed. Specialized methods may refine required READ/WRITE/CONSUME and observable
raise/panic facts, but may never weaken provider summaries. The existing rule
that only READ + READ aliases may overlap remains unchanged.

## 5. A1 native lowering and visibility

A standalone Rust prototype succeeded with one provider-owned
`pub struct BoxValue<T>`, two independently compiled consumer executables, and
consumer-local specialized free functions over `BoxValue<i64>` and
`BoxValue<String>`. Outputs were `42` and `moss-g0`. This establishes the basic
coherence strategy:

- the provider emits the only generic Rust record declaration;
- consumers name that provider crate type with concrete field parameters;
- generic Moss methods lower to consumer-side free functions, never foreign
  inherent impls or consumer trait impls;
- provider-owned concrete methods remain provider symbols only when their Moss
  semantics are already closed;
- inferred effects determine `&`, `&mut`, or owned parameters exactly as for
  existing specializations.

For Phase 25, choose **Rust `pub` representation plus Moss visibility checking**.
Current Moss has no field-level private syntax, and this choice is the smallest
A1-compatible extension. `.mossi` may expose only the representation required
by an exported template and private semantic dependencies transitively required
to specialize exported behavior. Those dependencies remain non-importable and
must not be added to completion/symbol exports. D1/D2 opacity and accessor
lowering remain Phase 25.5.

This choice requires a negative source test showing that Rust `pub` does not
make a non-exported Moss declaration or private helper name accessible. Generated
Rust visibility is not Moss visibility.

## 6. Map-key proof is currently blocking

The baseline lowers Map to Rust `HashMap<K,V>` and performs no Moss-level proof
that a user key has usable equality/hash semantics. A `Key` with Moss `isequal`
and `hash` passes `moss check`, then fails rustc for missing `Eq` and `Hash`.
Deriving Rust traits is semantically wrong when Moss methods define custom
equivalence/hash, and a consumer cannot implement foreign Rust traits for a
provider-owned foreign key type because of orphan rules.

G0 must select and prototype one coherent strategy. Two viable alternatives are:

1. **Compiler-owned Moss map runtime (recommended):** store keys without a Rust
   `K: Eq + Hash` bound and generate statically specialized insert/lookup/delete
   operations that directly call the resolved Moss `isequal`/`hash` free
   functions. The map representation stays identical across consumers; no
   callback object or trait implementation is stored.
2. **Provider-owned key adapter:** the defining provider emits the adapter and
   legal Rust `Eq`/`Hash` impl. This is viable only if the provider can implement
   the exact custom Moss methods for every generic instance without consumer
   specialization or hidden dynamic dispatch. That has not been demonstrated
   and conflicts with the general consumer-free-function method rule.

A consumer-local newtype adapter is not sufficient: a Map embedded in a public
nominal instance would then have consumer-specific Rust representation, defeating
cross-crate identity. Native Rust `Float: Eq + Hash` assumptions are also
forbidden. Team S must own purity and the `isequal`/`hash` law contract; Team G
owns the static representation and specialization hook. Until a compiled-only
provider key executes insert, lookup, overwrite, delete, and `keys()` in two
consumers with the same representation, this obligation remains NO-GO.

## 7. ABI-v10 contract requirements

ABI-v10 must use a new interface schema marker (recommended
`moss-module-interface-v3`) and `native_abi 10`. A v9 artifact must fail with a
specific rebuild diagnostic before semantic records are parsed. Keep separate
v9 fixtures to prove this migration path.

Required deterministic records are:

- resolved package/module instance ID and declaration IDs;
- nominal template binders, ordered fields, method annotations/effects,
  visibility, and source provenance;
- open function summaries with bound variables, result graph, requirements,
  transitive dependency edges, effect expressions, and provenance;
- concrete nominal/specialization keys and the provider-owned Rust type/symbol
  identity needed for linkage;
- private semantic dependencies needed for exported specialization, marked
  non-exportable;
- compiler/language/schema/native ABI versions and backend fingerprint;
- distinct SHA-256 public semantic, generic semantic, and native binary-contract
  hashes, covered by the existing provider-artifact binding seal.

Do not use the current 64-bit display hash as a globally stable type ID. Cache
keys must include the ABI/schema version, provider contract digest, imported
template/summary hashes, concrete instantiation keys, backend identity, and
selected `.rlib` bytes/path identity. Any changed input invalidates the consumer.

Source-backed Fast Debug and native codegen must consume the same checked type
graph and summary substitutions. A source-free provider must preflight with
`FAST_DEBUG_NATIVE_DEPENDENCY`; it must not be reported as a generic semantic
mismatch.

## 8. Mandatory acceptance and adversarial cases

Before an Architect GO, permanent tests must cover:

- independent `Box{Int}`/`Box{String}` and `Entry{String,Int}` instances;
- `fresh -> relay -> use` over at least two hops, both source-backed and
  source-free;
- zero-iteration loops proving type closure is compile-time, not first-insert
  runtime behavior;
- nested nominal constructors in collection literals (ledger #11);
- `var v = []; v.push(1)` positive behavior plus never-pinned ambiguity
  (joint ledger #12);
- same binding conflicting uses versus separate fresh calls;
- exact-self method annotations and open-field projection;
- unknown receiver field rejection;
- complete outer-call requirement provenance for missing `less` (ledger #9);
- reassignment to a different instance and equal-layout nominal mismatch;
- READ/WRITE/CONSUME alias conflicts through generic methods before Rust;
- direct, mutual, container-mediated recursive layouts and specialization-key
  growth rejection;
- provider plus two consumer crates, source-backed and compiled-only, with one
  provider Rust record identity;
- ABI-v9 rebuild rejection and semantic/binary tamper/cache invalidation;
- user-defined compiled-only Map key proof under Team S's laws;
- specialization counts, generated-code size, and repeated-build timing for
  repeated equivalent instantiations.

Every accepted result must record command, exact output/status, profile,
toolchain, and exact SHA. Passing only Fast Debug or only native is a failure.

## 9. Conclusion

The architecture can proceed once the above contracts are made concrete and the
Map-key proof closes. The existing specialization and ABI-v9 machinery offers a
credible foundation, but extending its string types and declaration-global field
inference directly would create unstable identities, leaked variables, weak
diagnostics, and backend-only failures. The charter therefore does **not** itself
constitute G0 GO, and this independent review remains **NO-GO beyond G0** pending
the named reviews, prototypes, and written L7 authorization.

## 10. Addendum — Attributable G0 Architectural Review of Corrected Candidate

- **Role:** Independent L6 Architect, Team G (Generics & Inference)
- **Status:** Attributable G0 Review Addendum
- **Reviewed candidate SHA:** `aa83fb66b7dbcbb33144aca023734f43a6ae4e0c` (with narrow corrective contract updates)
- **Pinned baseline:** `fa3a340a0a6652568b97869489da1a84038b9857`
- **Attributable Verdict:** **GO for G0 Boundary Freeze; NO-GO for G2 Shared Compiler/IR Implementation**

### 10.1 Evaluation of corrected contracts and prototypes

The earlier NO-GO verdict on baseline draft `40fd3b6ea1aa89ae5a05e2afe092c8699a56f1f2`
remains valid historical review evidence. This addendum independently evaluates the
corrected candidate on `phase25-generic-types`.

1. **Option B and Shared Type IR:**
   `TYPE_IR_CONTRACT.md` adopts a unified `TypeRef`/`TypeVar` structured representation
   across inference, member dispatch, ownership/effects, semantic queries, Fast Debug,
   native lowering, and ABI v10. Per-call freshening and open-result summaries are
   consistently specified with transitive provenance and compile-time closure rules.
2. **Native A1 Strategy:**
   The selected strategy—provider-owned generic Rust records with mechanical public
   backend fields (`pub field_0: T`) and consumer-generated free functions—is proven
   in `tests/phase25/g0/check_rust_feasibility.py`. The private-field/accessor experiment
   is preserved as an evaluated alternative under `tests/phase25/g0/rust/accessor_alternative/`
   verifying `E0616`. Source-level visibility enforcement belongs to the Moss compiler,
   with forward negative test `tests/phase25/g0/conformance/private_field_visibility_negative.moss`
   using valid Moss identifier import syntax (`import provider_module`), scheduled for
   the earliest compiler visibility milestone (G1/G2). Phase 25.5 D2 encapsulation
   is not implemented now.
3. **Policy-Free Cross-Crate `MossMap<K,V>`:**
   The updated prototype demonstrates one runtime-owned `MossMap<K,V>` value crossing
   two independent consumer rlibs (`consumer_a` and `consumer_b`), calling provider
   `isequal` and `hash` functions directly without an `Ops` type parameter, stored
   callbacks, trait objects, or dynamic dispatch. The negative test proves that foreign
   Rust `Hash` implementation is rejected by rustc (`E0117`).
4. **Canonical Key-Proof Coherence:**
   Cross-consumer key-proof coherence is frozen as an explicit compile/link invariant:
   all consumers must bind to the exact same canonical `KeyProofId` for a given nominal
   type, failing closed with `KEY_PROOF_COHERENCE_MISMATCH` on conflict.
5. **Package Identity:**
   Package identity is frozen per L7 verdict as `module-v1:<name>@<version>::<module>`
   with deterministic provider-conflict rejection, subject to confirming Margo's
   resolution and artifact binding semantics.
6. **Map Complexity and Eager `keys()` Contract:**
   The production target for G3 is specified as a bucketed open-addressing / robin-hood
   hash table with expected $O(1)$ lookup, insert, and delete. `keys()` returns an eager
   owned snapshot `Vector[K]` with snapshot stability in $O(N)$ time and allocation.
   The linear Rust prototype remains acceptable G0 feasibility evidence.
7. **Float-Key Semantics:**
   The blanket Phase 25 Float-key prohibition has been removed. Float `isequal`/`hash`
   semantics are an approved Phase 25 G3 deliverable owned by Team S, following approved
   Julia-like semantics: `isequal(-0.0, +0.0) == false`, `hash(-0.0) != hash(+0.0)`,
   `isequal(NaN, NaN) == true`, and signed-zero total ordering. Negative tests
   distinguish valid future Float proofs from malformed or forged proofs.
8. **Team S Concurrence:**
   Formal implementation-owner concurrence is recorded in
   `docs/phase25/TEAM_S_G0_IMPLEMENTATION_OWNER_CONCURRENCE.md`, specifying valid Moss
   structural method declarations (`fn isequal(other: Key) -> Bool`, `fn hash() -> Int`)
   while retaining internal generic type variables in the IR, `key-protocol-v1`,
   comparator-law validation ownership, Float-key delivery in G3, and joint ledger #12
   responsibilities.

### 10.2 Final verdict

The G0 boundary is complete, consistent, and validated. I issue an attributable
**GO for G0 Candidate Boundary Freeze**. Shared compiler/IR implementation remains
strictly **NO-GO** until explicit written L7 G2 authorization.

---

## 11. Implementation Kickoff Sign-off

- **Role:** Independent Team G L6 Architect
- **Verdict:** **ACCEPTANCE OF FINAL G0 CONTRACT SHA (`fb2d273616a14e255ff29f6c5a9824f4826062a6`) — PROCEED TO G2 IMPLEMENTATION**
- **Date:** 2026-10-14
- **Evaluated Commit:** `fb2d273616a14e255ff29f6c5a9824f4826062a6`

Following L7's disposition (**GO for G0 architecture; Proceed to G2 implementation**):
1. I record formal acceptance of the final corrected G0 contract SHA `fb2d273616a14e255ff29f6c5a9824f4826062a6`.
2. The G0 architectural freeze is complete. No further broad G0 architecture review is required.
3. Team G kicks off G2 implementation covering: generic type inference, open-result summaries, nominal specialization, source-free ABI contracts, and related compiler defects.
4. Scope boundaries remain strictly enforced: Phase 25.5 retains D1 path-granular ownership and D2 opaque types; they are not absorbed into G2. Neither team may merge directly into `main`. All milestones will be committed and pushed to `origin/phase25-generic-types`.
