# Phases 10.6C–D: synchronization planning and production handler-level 2PL

Phase 10.6C implements `SynchronizationPlan`; Phase 10.6D consumes that stored
object for production handler-level 2PL. All compilation/optimization modes use
the same plan-driven synchronization boundary. Legacy mailbox, coarse-lock,
atomic, batching, coalescing, and cluster implementations remain in compiler
source for Phase 10.6E deletion, but no active handler path uses them to override
the plan. The analysis is independent of backend optimization flags.

```text
closed ConcreteDomainGraph + exact DomainSpecialization records
                          + specialized handler READ / WRITE / CONSUME effects
                          ↓
                  SynchronizationPlan
                          ↓
                  Phase 10.6D physical class locking and handler entry
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
Projection through helpers retains inferred semantic effects for primitive
parameters too. Planning never weakens WRITE to READ based on a Rust Copy or
by-value representation. Phase 10.6D implements inferred ordinary parameter
WRITE with writable caller storage, including primitive values and projections.
No parameter-mutation modifier is added; incoming message payloads stay immutable.

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
The plan supplies the ordering pair `(domain_rank, class_rank)`. Production
lowering validates the stored graph, specialization linkage, ranks, partition,
footprints, and final modes before emitting physical descriptors. It neither
recomputes synchronization analysis nor repairs a corrupt plan at runtime.

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

## Account derivation

The executable fixture is [`tests/phase106c_account.moss`](../tests/phase106c_account.moss).
Its leaf sets are:

| Handler | R | W | C |
| --- | --- | --- | --- |
| `read_config` | config_value | empty | empty |
| `read_stats` | stats | empty | empty |
| `record_fill` | balance, config_value, risk_limit, stats | balance, stats | empty |
| `rename` | empty | display_name | empty |
| `set_risk_limit` | empty | risk_limit | empty |

Here `X* = {balance, display_name, risk_limit, stats}`. `config_value` is read
but never written, so it contributes no ProtectedRead, LockSet, or class.
`record_fill` has ProtectedRead `{balance, risk_limit, stats}` and the same
leaf LockSet; WRITE dominates READ on balance and stats. Its synchronization
signature is EXCLUSIVE for those two leaves and SHARED for risk_limit.

In handler order `read_config, read_stats, record_fill, rename, set_risk_limit`,
the complete class signatures are (`-` = NONE, `S` = SHARED, `X` = EXCLUSIVE):

| Class rank | Member | Signature |
| --- | --- | --- |
| 0 | balance | `-, -, X, -, -` |
| 1 | display_name | `-, -, -, X, -` |
| 2 | risk_limit | `-, -, S, -, X` |
| 3 | stats | `-, S, X, -, -` |

Thus `record_fill` needs classes 0, 2, and 3; `rename` needs only class 1.
They are disjoint. `read_stats` conflicts with `record_fill` on class 3 but
not with itself. The test asserts these signatures against compiler JSON.

Captured pipeline reads follow the same rule. In the regression
`values |> map(_ + offset)`, both `values` and `offset` are READ. A handler
writes `values`, so only that aggregate belongs to X*; the immutable `offset`
capture does not become ProtectedRead merely because a callback reads it.

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

Phase 10.6D consumes this object for physical class storage, shared/exclusive
acquisition, and handler-lifetime 2PL, including nested message calls. Runtime
locks belong to each constructed instance, never process-global singletons.
Scoped graph activations and imported outer routes remain future design.

## Production storage and handler entry (10.6D)

Each generated domain reference owns an `Arc<MossClassRuntime<DomainLeaf>>` and
immutable route references. `DomainLeaf` is a generated typed Rust enum; it does
not use runtime trait dispatch or type erasure. The runtime owns exactly one
`std::sync::RwLock` per synchronization class, containing that class's leaves.
Leaves outside X* live in separately published immutable storage with no lock.
Class order and membership come directly from the final application's plan.
Two leaves sharing a class use one lock; split classes use distinct locks even
when their leaves belong to one source object.

`Handler_shared` is the single synchronized entry wrapper. It acquires exactly
the handler's stored ClassSet in increasing class rank, using read guards for
SHARED and write guards for EXCLUSIVE. An empty ClassSet acquires nothing.
Acquisition never upgrades a guard. The wrapper invokes a private `Handler_body`
and retains every guard until the reply/result and state restoration are complete.
Ordinary helpers and functional stages acquire no additional domain locks.

Storage is entirely safe Rust: there is no `UnsafeCell`, raw pointer projection,
or unsafe `Sync` implementation. Under retained guards, the wrapper moves its
exclusive leaves into a private working state and copies its observed READ leaves.
Unobserved leaves in that private value have inert defaults and never overwrite
published state. The body operates on ordinary Rust values and references; on
normal return every evacuated exclusive leaf is moved back before any guard is
released. This also supports whole-object operations spanning several classes.
Read snapshots are a conservative physical implementation, not new Moss value
identity or ownership rules. They can allocate/copy nontrivial values; reducing
that cost is future backend work. No coarse state guard serializes disjoint
handlers. The initial baseline prioritizes storage safety over layout efficiency.

Composition constructs descendants before owners, fully initializes state and
per-instance class locks, and publishes handles only after construction. Cloned
handles reach the same class objects. Different concrete specializations receive
their own typed leaf representations and final per-instance descriptors.

All active compiled dispatch is direct synchronous entry, including invocation
under former mailbox/reference, atomic, and cluster option configurations.
The legacy physical dispatch implementations are dormant, not deleted. Exported
module bridges and nominal specialization routing also converge on synchronized
entry. Thus no compatibility worker acknowledgement or cluster-local shortcut can
bypass locking in the active path. A future reactivated transport would have to
call this wrapper and acknowledge completion only after it returns.

## Guard lifetime, failures, and correctness

A terminating `reply` evaluates and establishes its independent by-value result
inside the body. The wrapper then restores all exclusive leaves, releases its
class guards, and returns the result to the caller. Normal no-value completion
uses the same restoration and release sequence. Reply copies remain conservative;
state is never released in an evacuated or uninitialized condition.

An unexpected panic aborts the process before the frame releases any guards.
An incomplete reply, missing storage, failed restoration, or poisoned class lock
also aborts. The wrapper does not catch a failure and permit normal Moss execution
to continue against torn state. This defines no rollback, restart, or supervision
policy; those remain later work.

For failure-free execution, Moss domain-local protected state is
conflict-serializable under compiler-derived handler-level 2PL. This is not one
global transaction across the domain call tree, a total order for outbound effects,
a rollback guarantee, or a fairness/starvation-freedom guarantee. Reader/writer
admission policy is unspecified.

Parent classes remain held across nested synchronous messages. Descendants acquire
their own classes and release them when their own handlers complete; their classes
are not flattened into an ancestor's footprint. This intentionally amplifies lock
hold time and can cause head-of-line blocking. Early unlock, lock-liveness regions,
lock elision, atomics, synchronization-class merging, clustering, cache-line padding,
and quantitative layout tuning are not implemented here.

## Structural deadlock proof

For every newly acquired Moss-managed lock:

```text
LockRank(new) > LockRank(each currently held lock)
LockRank(D, C) = (domain_rank(D), class_rank_D(C))
```

Local acquisitions increase class rank. Every route edge increases domain rank,
which lowering validates against the closed concrete DAG. A wait cycle would
therefore require `L1 < L2 < ... < Ln < L1`, which is impossible. Production
correctness depends on this static structure, not runtime deadlock detection.

The rule concerns currently held locks. An ancestor at rank 0 can call a child at
rank 2, wait for that child to release its locks, and then call a sibling at rank 1.
The second call still acquires above the held ancestor. Historical thread-wide
monotonic rank tracking would reject this valid sequence and is not used.

## Modules, inspection, and regressions

Source-free providers compile reusable handler wrappers. The final application
passes its physical descriptor to the generated constructor through an internal
Rust calling convention. `.mossi` exports semantic effects, not class IDs, ranks,
ClassSets, physical lock layout, or global LockRank. Native ABI version 2 and the
codegen fingerprint reject obsolete compiled calling conventions and caches;
providers built before this migration require rebuilding.

Existing synchronization JSON reports `physical_lowering: "handler_2pl"`.
The human dump projects each handler's acquisitions as `(domain_rank, class_rank)`
plus final mode and documents full-handler retention. Test-only generated lock
hooks observe real wrapper execution without making production tracing mandatory.
Fast Debug domain execution and lock simulation remain deferred to 10.6E.

`check_handler_2pl.py` compiles generated Rust with warnings denied, drives real
wrappers from backend threads, and uses barriers rather than timing benchmarks to
prove disjoint-writer and shared-reader overlap. It also covers conflicting writers,
read/write exclusion, empty footprints, class sharing/splitting, rank order,
nested calls and descending siblings after return, state restoration, primitive
WRITE-through, source-free providers, exact specializations, deterministic generation,
legacy-option convergence, and fail-closed failure. A separate C++ regression
corrupts stored plans and requires physical validation to reject them.
