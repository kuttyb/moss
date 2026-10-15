# Phase 25 Team S — Independent G0 Boundary Review

**Role:** Independent L6 Architect reviewing the Team S boundary of Team G G0

**Exact reviewed commit:** `6f109124a580c6a76a4050b4c222484a658ee4e1`

**Pinned baseline:** `fa3a340a0a6652568b97869489da1a84038b9857`

**Language/compiler:** `moss-0.1` / `0.1.0`, agent protocol `moss-agent-1`

**Scope:** member dispatch, Map/Set key semantics, shared type IR, and ABI-v10
records; no compiler implementation review

## 1. Attributable verdict

**NO-GO for L7 G0 authorization of shared-IR/compiler surgery at the reviewed
commit.**

The nominal/open-result portions provide a credible contract-review basis, and I
concur with the non-negotiable direction that Team S consume the same structured
`TypeRef`/`TypeVar` graph as Team G. I do not concur that the Team S boundary is
frozen. The durable Rust proof demonstrates a consumer-local newtype around a
provider key and `std::collections::HashMap`; the feasibility report proposes
`KeySlot<K, Ops>`; the Type IR and ABI documents instead propose a policy-free,
provider-owned `MossMap<K,V>` manipulated by consumer-specialized operations.
Those are distinct concrete representations and coherence/linkage schemes. A
successful probe of the first cannot authorize either production design.

This is a bounded G0 NO-GO, not a rejection of Phase 25. The preferred repair is
the **compiler-owned/policy-free `MossMap<K,V>` design**, with bucket operations
that statically call exact Moss equality and hash specializations. It keeps the
Map value's Rust identity independent of a consumer policy type and avoids all
foreign `Eq`/`Hash` implementations. `KeySlot<K,Ops>` remains an evaluated
alternative, not the frozen design: for a source-free generic provider key, an
`Ops` implementation owned by the provider has not been shown capable of calling
a consumer-materialized concrete method specialization without a callback,
consumer-local policy identity, or another ABI mechanism.

## 2. Member dispatch and ledger #12

The shared boundary must say more than “Team S fixes dispatch.” For a receiver
whose outer constructor is known, Team S dispatch must run before all contained
types are concrete. Thus `var v = []; v.push(1)` must resolve `push` from
`Vector[alpha]`, emit one structured `Method` requirement and its `WRITE`
receiver/`READ` argument contract into the shared IR, and let Team G unify
`alpha = Int`. It must not reject merely because `alpha` is open, create a second
Team-S-only variable representation, or defer ownership until Rust emission.

The same rule applies to a known `NominalInstance(template, open substitution)`:
field and method selection uses the known template and constrains its slots.
An unconstrained untyped receiver remains `UNTYPED_FIELD_ACCESS`/unresolved
member dispatch; Team S must not invent a structural record or `Any`. After
substitution, Team S must select one exact callable specialization and preserve
its receiver/argument `READ`/`WRITE`/`CONSUME` and observable effects. Separate
calls may freshen; one local binding may not.

Required correction: add an exact shared dispatch record/API and phase order to
`TYPE_IR_CONTRACT.md`, then make the joint #12 test ownership explicit. Team S
owns admission and static target selection for the member call; Team G owns
constraint closure and the inferred `Vector[Int]`; both own the positive test.

## 3. Map/Set key proof and laws

The production design must use one canonical proof for both Map and Set and must
freeze the following before implementation:

1. Exact source protocol: whether `isequal` and `hash` are methods or ordinary
   callables, their exact signatures, receiver/parameter access, visibility, and
   specialization identities.
2. Static effects: both operations are `READ`-only, deterministic, non-mutating,
   non-consuming, non-raising, non-panicking, non-diverging, and free of message,
   domain-state, I/O, executor, and other observable effects.
3. Laws: equality is reflexive, symmetric, and transitive; equal keys have equal
   Moss hash results; and equality/hash are stable for the key snapshot stored in
   the collection. These are acceptance obligations backed by reusable law tests,
   not a claim that arbitrary bodies can be theorem-proved by the compiler.
4. Float: **no `MapKeyProof` or `SetKeyProof` for `Float` in Phase 25**. Rust
   representation or traits are never evidence. A later explicit Team S contract
   for NaN, signed zero, equality, and hashing requires separate approval.
5. Failure mode: missing or invalid proof is a Moss checker diagnostic before
   native emission, with the key type, rejected operation/effect, and provider
   provenance.

`MapKeyProof.effects` is too coarse as currently written. ABI v10 needs separate
equality/hash signatures and effect records, a versioned key-protocol/proof ID,
law-contract version, exact semantic hashes, and proof provenance. The proof ID
must participate explicitly in Map/Set operation specialization and cache keys.
No built-in fast path may bypass the same observable semantics.

## 4. Canonical key proof and representation

The current prototype proves only these useful negative/feasibility facts:

- a consumer cannot implement foreign Rust `Hash` for a provider-owned key
  (`E0117`);
- a local adapter can statically invoke chosen equality/hash functions; and
- no trait object or vtable is intrinsically required for that local experiment.

It does **not** prove a source-free user-defined key under the proposed ABI,
shared Map identity across two consumers, overwrite/delete/`keys()` semantics,
generic-key method specialization, law rejection, or Float prohibition.

For G0, freeze the policy-free compiler/runtime-owned `MossMap<K,V>` shape and a
reference bucket algorithm whose consumer-specialized operations call the two
exact proof targets directly. The Map representation may store keys, values, and
Moss hash results, but no function pointer, closure, policy object, trait object,
or vtable. Its Rust type identity contains `K,V` only. Prove it with a
source-free provider and two independent consumers performing insert, lookup,
overwrite, delete, and `keys()` on the same representation. If that cannot be
done, invoke the charter's escalation with `KeySlot<K,Ops>` and provider-owned
Map specialization as the two documented alternatives; do not silently switch
representations.

Set must either reuse the same versioned key proof and collection kernel or be
explicitly deferred. A parenthetical “Set equivalent if Team S lands one” is
not a frozen shared contract.

## 5. Shared IR and ABI-v10 findings

I concur conditionally with one structured semantic type algebra, per-call
freshening, nominal identity distinct from layout, reason/provenance DAGs, and
ABI-v9 fail-closed rebuild. Team S must not introduce parallel strings or key
types. The following corrections are required:

- Resolve the package identity collision question. `package-name@version` plus
  “reject different bound hashes” is a graph conflict rule, not necessarily a
  durable resolved-package identity for two legitimate same-name/version nodes.
- Add the exact shared member-dispatch record and closure phase order described
  above.
- Replace the ambiguous `MapKeyProof.effects` field and include the versioned
  proof ID in specialization, semantic dependency, and cache-key records.
- Name Set's record/reuse rule and Float's Phase 25 prohibition explicitly.
- Make private key-callable dependencies non-nameable yet transitively available
  to source-free specialization, with their effect and provenance chains intact.
- Add malformed/tampered key-proof negatives to the ABI-v10 matrix, including
  weakened effects, mismatched callable hashes, Float forgery, and proof/cache
  substitution between nominally distinct same-layout keys.

Every reachable Map/Set operation must close to concrete `K`, `V`, equality and
hash call targets, ownership modes, and observable effects before native or Fast
Debug execution. No runtime lookup or dynamic fallback is permitted.

## 6. Visibility fork as it affects Team S

Team S does not require either public Rust fields or provider accessors as a
source-language feature. Either backend strategy is acceptable to this boundary
only if `.mossi` keeps mechanical field/accessor symbols out of Moss name
resolution and completion, hashes the chosen binary surface, and permits the
statically specialized Map/method operations without leaking private Moss names.
The reviewed documents currently disagree—feasibility recommends private fields
plus accessors, while the Type IR contract chooses public fields plus the Moss
checker. L7/Team G must select one for A1, but Team S concurrence cannot be used
as approval of that unresolved backend fork.

## 7. Independent evidence at the reviewed commit

Environment: `rustc 1.98.1 (48a229cea 2026-09-01)`, host
`x86_64-unknown-linux-gnu`, debug standalone Rust prototype.

```text
./moss agent bootstrap --json
PASS: ok=true, language_version=moss-0.1, compiler_version=0.1.0

python3 tests/phase25/g0/check_rust_feasibility.py
PASS: Phase 25 G0 provider identity, accessor visibility, and Map adapter prototype passed

./moss check tests/phase25/g0/conformance/local_pin_positive.moss --json
EXPECTED BASELINE FAIL (exit 1): MOSS_COMPILE_ERROR at line 3,
"local member calls are not implemented; use a top-level function"
```

These results reproduce the useful bounded proof and the still-open Team S
dispatch gate. They do not constitute production Map/Set or ABI-v10 evidence.

## 8. Meaning of Team S concurrence

There is no Team S implementation branch or attributable Team S owner decision
in the reviewed material. Consequently this independent review can concur with
invariants and recommend a boundary, but it **cannot**:

- promise Team S delivery or reserve Team S implementation ownership;
- certify current or future member-dispatch code;
- choose source-visible `isequal`/`hash` signatures on Team S's behalf;
- attest Map/Set purity or law suites that do not exist;
- approve a production representation not exercised by the prototype; or
- substitute for written Team S implementation-lead concurrence and L7 GO.

A future Team S concurrence must name the corrected contract commit, exact
protocol and law version, chosen policy-free Map/Set representation, joint #12
test ownership, and the implementation branch/milestone. Until then the Team S
sign-off row must remain pending and the G0 gate remains **NO-GO**.

## 9. Addendum — Attributable Team S Independent Boundary Review of Corrected Candidate

- **Role:** Independent L6 Architect, reviewing the Team S boundary
- **Status:** Attributable Boundary Review Addendum
- **Reviewed candidate:** `phase25-generic-types` corrected review candidate
- **Attributable Verdict:** **GO for G0 Boundary Freeze; NO-GO for G2 Shared Compiler/IR Implementation**

### 9.1 Boundary re-evaluation

The earlier NO-GO verdict on integrated draft `6f109124a580c6a76a4050b4c222484a658ee4e1`
remains valid historical review evidence. This addendum independently evaluates the
corrected candidate on `phase25-generic-types` against the Team S boundary criteria.

1. **Policy-Free Map Runtime Proven Across Independent Consumers:**
   The updated Rust prototype in `tests/phase25/g0/check_rust_feasibility.py` validates
   the compiler-owned, policy-free `MossMap<K,V>` design. A single provider-created
   map instance crosses two independently compiled consumer rlibs (`consumer_a` and
   `consumer_b`), calling provider `isequal` and `hash` functions directly without an
   `Ops` parameter, stored callbacks, closures, trait objects, or vtables. Rust's
   orphan rule failure on direct foreign `Hash` implementation is confirmed (`E0117`).
2. **Member Dispatch and Ledger #12 Joint Ownership:**
   `TYPE_IR_CONTRACT.md` §3 defines the shared `MemberDispatchRequirement` record.
   Phase ordering and ownership are unambiguous: Team S admits member calls on
   partially open receivers (e.g. `Vector[alpha].push(Int)`) and selects candidate
   targets; Team G unifies constraints and performs closure (`alpha = Int`). Both
   teams jointly own the positive end-to-end ledger #12 regression test.
3. **Team S Implementation-Owner Concurrence:**
   Formal implementation-owner concurrence is executed in
   `docs/phase25/TEAM_S_G0_IMPLEMENTATION_OWNER_CONCURRENCE.md`. It explicitly establishes:
   - Exact signatures: `fn isequal(left: K, right: K) -> Bool` and `fn hash(value: K) -> Int`
     with pure `READ`-only access.
   - Protocol: `key-protocol-v1` purity, non-mutation, and effect guarantees.
   - Law suite ownership: Team S owns the reusable comparator-law validation suite
     (reflexivity, symmetry, transitivity, equal-keys/equal-hash, snapshot stability).
   - Float-key delivery: Team S accepts delivery of Float `isequal`/`hash` semantics in
     Phase 25 G3; blanket prohibition is removed.
   - Set delivery/deferment: Set will share the exact same key protocol and runtime
     storage kernel in G3 or be explicitly deferred.
4. **ABI-v10 Key Proof Records and Negative Controls:**
   `ABI_V10_DESIGN.md` §3.5 specifies separate equality/hash signatures, separate effect
   records, and versioned `proof_id` binding. Negative verification rules distinguish
   valid future G3 Float proofs from malformed or forged proofs.
5. **Backend Representation and Opacity:**
   The native A1 choice (mechanical public backend fields with consumer-generated free
   functions) is proven, with the accessor alternative preserved for evaluation. Forward
   Moss-level visibility checking is scheduled for compiler milestones G1/G2; Phase 25.5
   D2 opacity is not prematurely implemented.

### 9.2 Final verdict

The boundary requirements for member dispatch, key proof, and Map runtime representation
are satisfied and frozen. I issue an attributable **GO for G0 Boundary Freeze**. Shared
compiler and IR surgery remains strictly **NO-GO** pending explicit written L7 G2
authorization.
