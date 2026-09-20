# Phases 10.6C–E: synchronization planning and production handler-level 2PL

Phase 10.6C implements `SynchronizationPlan`; Phase 10.6D consumes that stored
object for production handler-level 2PL. All compilation/optimization modes use
the same plan-driven synchronization boundary. Phase 10.6E removes legacy mailbox, coarse-lock,
atomic-domain, batching, coalescing, and clustering implementations.
The analysis is independent of backend optimization flags.

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
footprints, and final modes before emitting typed layouts and direct acquisitions. It neither
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

## Production storage and handler entry (10.6F.1)

SynchronizationPlan is compile-time only. Code generation projects its exact
classes, member leaves and modes into typed Rust. Every runtime instance owns an
`Arc<DomainRuntime>` containing direct `RwLock<DomainClassN>` fields. Each class
struct owns exactly its protected leaves; immutable-after-publication leaves live
in a separate immutable struct. Identical concrete layouts can share a Rust type
while their instances own independent state and locks. Different specializations
receive their own checked layouts.

`Handler_shared` is the single synchronized entry wrapper. It emits straight-line
read/write acquisitions against known class fields, in increasing class rank,
then invokes a typed borrowed handler body. Guards remain live through the entire
body and nested messages. Reply values are established before the guards drop.
An empty ClassSet emits no lock acquisition. There are no runtime plans, handler
searches, class/leaf maps, guard maps, evacuated sets or restoration vectors.
Ordinary helpers and functional stages acquire no additional domain locks.

Storage and projections are safe Rust. SHARED reads borrow directly through a
read guard; EXCLUSIVE operations use mutable field borrows through a write guard.
Immutable reads borrow directly from immutable storage. Ordinary WRITE mutates
in place, with no evacuation/restoration framework. Rust lifetimes bind these
references to retained guards; concurrent handlers never receive mutable
references to a complete shared domain state. Unused top-level state fields are
omitted from each handler's view. The runtime has no Clone bound.

For split aggregates the compiler constructs a statically typed view whose field
capabilities are generic types, not runtime mode tags. `MossRead` and `MossWrite` are backend-only types storing `&T` and `&mut T`
respectively. Partial object projections use zero-sized absent
capabilities only where the object access trait requires an unused member;
no unused value is constructed. Invalid access through such a capability aborts,
but checked effects make that branch unreachable in valid programs.

The earlier D/D.1 generic `MossClassRuntime`/`MossHandlerFrame`/`MossSlot`
representation and generic take/restore path are retired by F.1.

Nested objects have recursive typed views. Statically dispatched Rust accessor
traits let ordinary helper/method bodies operate on an owned object or its view,
including whole-object reads spanning classes. Accessors return references to the
original leaves; they do not reconstruct an owned object. There is no `dyn` trait,
virtual dispatch, Arc/COW value substitution, or new Moss borrowing syntax. Source
traits remain compile-time structural constraints. Ordinary helper/callable and
functional pipeline reads execute under the already-held handler guards.

Explicit `message` and `reply` boundaries establish independent values. Owned
payloads enter target handlers; no Rust reference into caller domain storage
crosses a message. For a borrowed aggregate, `__moss_value` constructs the owned
boundary value by copying its leaves. It is called for value boundaries, never
handler entry or an ordinary READ call. Trivial primitive copies remain
observationally irrelevant. Conservative boundary copies may later be optimized
with an equivalence proof; ordinary READ has no hidden snapshot cost.

Composition constructs descendants before owners, fully initializes state and
per-instance class locks, and publishes handles only after construction. Cloned
handles reach the same class objects. Different concrete specializations receive
their own typed leaf representations and final per-instance descriptors.

All compiled dispatch is direct synchronous entry. Exported provider bridges and
nominal specialization routing converge on the same synchronized wrapper. There
is no alternate transport, completion acknowledgement, or backend selector.

## Guard lifetime, failures, and correctness

A terminating `reply` evaluates and establishes its independent by-value result
inside the body. The wrapper then finishes all typed state accesses, releases its
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

Source-free providers compile reusable handler bodies over semantic typed state
references and static route contracts. The final application generates class
layouts and synchronized wrappers from its own stored plan, then calls those
provider bodies. This also works when no domain instance existed in the original
provider build. `.mossi` exports semantic types/effects, not class IDs, ranks,
ClassSets, physical lock layout, or global LockRank. Native ABI version 5 and the
codegen fingerprint reject obsolete compiled calling conventions and caches;
providers built before F.1 require rebuilding.

Existing synchronization JSON reports `physical_lowering: "handler_2pl"`.
The human dump projects each handler's acquisitions as `(domain_rank, class_rank)`
plus final mode and documents full-handler retention. Test-only generated lock
hooks observe real wrapper execution without making production tracing mandatory.
Fast Debug directly interprets synchronous domains without physical locks or
scheduling. Its semantic execution does not depend on SynchronizationPlan.

`check_handler_2pl.py` compiles generated Rust with warnings denied, drives real
wrappers from backend threads, and uses barriers rather than timing benchmarks to
prove disjoint-writer and shared-reader overlap. It also covers conflicting writers,
read/write exclusion, empty footprints, class sharing/splitting, rank order,
nested calls and descending siblings after return, state restoration, primitive
WRITE-through, source-free providers, exact specializations, deterministic generation,
optimization-level consistency, and fail-closed failure. A separate C++ regression
corrupts stored plans and requires physical validation to reject them.


## Borrowed-read validation and provider compatibility (10.6D.1)

`check_borrowed_reads.py` compiles a runtime with non-Clone values, checks a clone
counter stays zero across entry/repeated reads/helpers/methods, and verifies that
an explicit independent copy increments it. A compile-fail test proves a returned
READ borrow cannot outlive its frame. Generated-code tests use large Strings and
Vectors and compare their backing addresses in real handler/helper/method bodies
with the protected original storage. Coverage includes nested and whole objects,
mixed READ/WRITE, immutable state, generic helpers, method arguments, indexed
reads/writes, captured map/filter/reduce, independent replies and payloads, and
deterministic output. The full 10.6D concurrency and failure suite remains active.

Source-free providers export reusable native static accessors and generic helper
implementations; the consumer needs only existing semantic type/effect metadata.
Generated decomposition methods move private provider fields into class storage
without granting new Moss source-level field access. A loader off-by-one error in
existing `public_representation field` records is corrected; synchronization
analysis is unchanged. `.mossi` contains no view/borrow layout, Rust lifetimes, or
synchronization policy. Native ABI version 5 requires rebuilding older providers
so these backend-private entry points exist.

Remaining representation costs include static accessor/body code size,
fail-closed unwind checks, and conservative explicit-boundary copies. F.1 removes
runtime descriptor/slot selection and generic storage navigation. Further
layout/profitability work requires measurement. Fast Debug domain execution and legacy retirement are implemented in Phase 10.6E.


## Shared frontend, two execution engines

```text
Moss source → checker / specialization / effects → ConcreteDomainGraph
                                                   ├─ SynchronizationPlan
                                                   │    → production: class RwLocks + borrowed views
                                                   └─ Fast Debug: logical state + synchronous frames
```

Production entry may be invoked concurrently. Fast Debug executes one
deterministic schedule, without lock simulation or concurrency exploration.
Both preserve the checked value boundaries, exact routes, and terminating reply.
Phase 10.6F adds diagnostics and quantitative validation without changing
synchronization semantics; see [the measured report](PERFORMANCE_10_6F.md).

## v0.1 diagnostic projections (10.6F)

Existing `inspect`, `effects`, and `why` output adds deterministic summary metrics
and explanations derived solely from the stored plan. Domain metrics count state
leaves, protected leaves, classes, handlers, read-only handlers, self-conflicting
handlers, unordered distinct handler pairs, conflicting/non-conflicting pairs, and
the percentage non-conflicting. `class_compression_ratio = classes / |X*|` is a
diagnostic only. Empty-denominator ratios are `null` (`n/a` in text). Read-only
means no domain-state WRITE/CONSUME; it does not imply absence of I/O or messages.

Per-handler fields count exact ClassSet size and SHARED/EXCLUSIVE acquisitions.
`handler_order` indexes a symmetric boolean `conflict_matrix`, including diagonal
self-conflicts. Existing `conflicts` records retain specific class/member-leaf
witnesses. Pair summary counts exclude the diagonal.

`class_opportunities` lists member count, reader/writer identities, and shared/shared
pairs per class. Such pairs can still conflict on another class; the domain-level
`shared_reader_pairs` count excludes those conflicts. `class_splits` gives the first
deterministic handler whose modes distinguish each class pair. Members collapse
into one class precisely because their complete stored mode signatures match.
Neither sharing nor splitting is automatically a performance problem. The text
dump includes summary counts, signatures, acquisitions, and split witnesses.

Optional `moss_perf` Rust instrumentation and `MOSS_PROFILE_COMPILER` developer
timing are documented in the [benchmark guide](../benchmarks/synchronization/README.md).
They are off by default, do not enter `.mossi`, and do not define language behavior.
Private handler working-state route fields now borrow their runtime handles; this
removes measured redundant Arc clones without changing value boundaries or locks.
