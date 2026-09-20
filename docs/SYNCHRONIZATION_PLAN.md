# Phase 10.6C: compiler-owned synchronization planning

Phase 10.6C implements `SynchronizationPlan`. Production handler-level 2PL
lowering is not yet implemented. The existing Rust lock objects, mailbox
compatibility, batching, coalescing, atomics, and acquisition order are unchanged.
The new plan is required analysis, independent of backend optimization flags.

```text
closed ConcreteDomainGraph + exact DomainSpecialization records
                          + specialized handler READ / WRITE / CONSUME effects
                          ↓
                  SynchronizationPlan
                          ↓
                  Phase 10.6D physical locking (future)
```

`Program::synchronization_plan` is the authoritative object. The builder takes a
supplied closed graph and follows each instance's specialization index, verifying
its identity against the source declaration. It analyzes with concrete state and
handler parameter types, including statically resolved helper and method calls.
An optional observation sink on the existing semantic effect traversal retains
storage paths; it does not replace or mutate ordinary ownership summaries.
Checked object fields expand into leaves. Runtime-indexed collections remain one
conservative aggregate region, using the existing `StorageLocation` machinery.
There is no separate source-field parser, dynamic key locking, or lock elision.

The semantic observations retain separate sets `R(h)`, `W(h)`, and `C(h)`.
Repeated observations may put a leaf in several of these sets. Normalization is
separate: `CONSUME > WRITE > READ > NONE`. Ownership still distinguishes WRITE
from CONSUME. For synchronization, READ maps to SHARED and both WRITE and CONSUME
map to EXCLUSIVE; absence maps to NONE.

For each concrete domain:

- `X(h) = W(h) ∪ C(h)` is a handler's exclusive state set.
- `X* = union_h X(h)` is the protected mutable universe.
- `ProtectedRead(h) = R(h) ∩ X*` contains only reads of potentially mutable state.
- `LockSet(h) = X(h) ∪ ProtectedRead(h)` is a **set of state leaves**.
- Each leaf in `X*` gets its full vector of synchronization modes in stable
  handler-identity order. Equal vectors define one synchronization class.
- `ClassSet(h)` maps the leaf LockSet through `leaf_to_class` and deduplicates.
  Its acquisition modes are unambiguous because every member agrees.
- `class_rank` is a contiguous local ordinal. Classes are created in the order
  of their first lexicographically sorted member path. No hash iteration defines
  identity, handler order, member order, or rank.

Leaves outside `X*` have no class. Reading a construction-only configuration
field therefore creates no protected read or lock footprint. Two invocations of
one handler conflict exactly when its `X` is nonempty. Two different handlers
conflict when they share a class and at least one needs EXCLUSIVE access. Conflict
witnesses name the class and its member leaves.

Class identity is compiler policy for an unchanged checked program, not source
syntax or permanent ABI. A new reader may split a previously shared class.
Every domain rank is checked for uniqueness, and every class rank is checked for
contiguity and uniqueness. Partition, subset, semantic-state membership, and
class-mode invariants fail closed in release builds as well as debug builds.
The plan supplies the future ordering pair `(domain_rank, class_rank)` but emits
no acquisitions or runtime rank checks.

A message remains an observable sequencing point. A caller's plan contains only
its own state; routed callee state is never flattened into the caller's LockSet.
No code motion, early unlock, atomic promotion, domain clustering, scope syntax,
or concurrent ingress semantics are introduced.

## Introspection

Existing `inspect`, `effects`, and `why` JSON queries include
`synchronization_plan` and a deterministic human-readable `synchronization_dump`.
They are views of the same stored object, not independent analyses. For example:

```sh
./moss inspect main --source tests/phase106c_account.moss --json
./moss inspect main --source tests/phase106c_account.moss --json \
  | python3 -c 'import json,sys; print(json.load(sys.stdin)["result"]["synchronization_dump"], end="")'
```

The structured view includes graph and specialization identities, provenance,
domain ranks, state leaves, X*, class members/signatures/ranks, inverse mapping,
all handler sets, normalized semantic effects, class acquisition modes,
self-conflict, pairwise conflict witnesses, and basic class/handler/pair counts.
Class-size distributions and conflict matrices can be derived from these fields.
`moss agent schema --json` advertises derived synchronization data.

## Modules and validation

A `.mossi` provider carries `handler_state_effects` records: handler name, then
counted READ, WRITE, and CONSUME leaf lists. These are semantic effects included
in interface hashes; no class identity, rank, lock object, or layout is exported.
An explicit empty record distinguishes an empty effect from missing metadata.
Consumers fail closed and request a provider rebuild if records are missing.
Source providers are analyzed using the same checked effect traversal before
export. Existing compiled-provider specialization restrictions remain in force.

The Account golden has four classes for balance, display_name, risk_limit, and
stats. Its config_value is read but has no class. Tests also cover identical
signature sharing, deterministic splitting, read-modify-write normalization,
exact object-layout specializations, helper/functional effects, conservative
indexing, local-only topology, and source-free compiled providers.

Existing ownership rules reject consuming domain-owned state. The C++ plan-layer
regression therefore supplies semantic WRITE and CONSUME observations directly
to prove they remain distinct while producing identical EXCLUSIVE signatures;
no source ownership exception is introduced for this test.

## Future graph contexts and traits

Synchronization plans are derived from supplied static domain graph/specialization
contexts and do not assume process-lifetime singleton domains. Future lexical
domain scopes or reusable graph templates may reuse the same analysis. Nested
scope semantics, imported outer routes, and scope activation remain intentionally
unresolved.

Current Moss has no lexical domain scopes, graph template activations, or nested
scope rank composition. Graph identities name analysis contexts, never runtime
activation lifetimes. A graph owns its plan through its containing checked
`Program`; there is no global process plan.

Moss traits are structural compile-time predicates rather than nominal
memberships. A concrete type is checked when specialization/use requires it;
there is no `implements` declaration or runtime trait dispatch. Planning consumes
statically closed callables and fails closed on unresolved method targets.

Phase 10.6D must consume this object for physical class storage, shared/exclusive
acquisition, and handler-lifetime 2PL, including nested message calls. This phase
does not prove or implement that backend migration.
