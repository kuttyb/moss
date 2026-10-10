# Phase 21 shared error-handling and provider ABI contract

**Status:** revised Stage 21.0 checkpoint, pending human approval before the
shared freeze and before Agents 21.A–D start.
**Native/provider ABI:** 8.
**Normative parent design:** `MOSS_PHASE_21_ERROR_HANDLING.md` v4.5.2.

This document fixes the representation and ownership boundaries consumed by
Agents 21.A–D. It does not add the Phase 21 source grammar. The compiling C++
schema is `src/error_handling_abi.hpp`; the observable effect integration is
`src/functional_ir.hpp`.

## 1. Observable effects

The concrete compiler representation is:

```cpp
struct ObservableEffects {
  bool local_capture_read;
  bool local_mutation;
  bool domain_read;
  bool domain_write;
  bool message;
  bool external_io;
  bool fileio;
  bool fileio_unknown;
  bool may_panic;
  RaiseSet raise_set;       // std::set<RaisedIdentity>
  bool may_diverge;
  bool unresolved;
};
```

`RaisedIdentity` is either `enum:<concrete-type>:<variant>` or
`scalar:<concrete-type>`. `RaiseSet` is a `std::set`, so insertion deduplicates
and iteration gives the stable serialization and hashing order. Enum payload
values do not participate in the identity. Their field layout belongs to the
raised-value ABI.

Effect union ORs Boolean facts and takes set union of `raise_set`. A matching
recovery removes only its handled identities. A bare re-raise inserts the exact
selected identity again and forwards its owned payload. New raises in a recovery
arm are unioned after handled variants are removed. Missing provider effect
metadata is not an empty effect: ABI-v8 loading fails and requests a rebuild.
An explicit `unresolved` or `fileio_unknown` fact remains conservative.

`fusion_safe()` requires no panic and an empty `raise_set`. Typed raises
therefore remain an ordering barrier for ordinary eager functional fusion.
`chunk_speculation_safe()` is separate. It permits a nonempty `raise_set`, but
requires no possible panic, mutation, domain access, message, external I/O,
FileIO, unknown FileIO fact, or unresolved fact. Chunk capture legality and the
stage-local bounds proofs remain additional checks owned by 21.C.

Semantic JSON exposes `may_panic` and the sorted `raise_set` separately. The
effect fingerprint and `.mossi` interface hash include `fileio`,
`fileio_unknown`, `may_panic`, every normalized raised identity, divergence,
and unresolved state.

## 2. Owned tagged outcomes

Each concrete fallible specialization has a closed, non-generic generated Rust
outcome. It does not use unwinding and does not catch Rust panics:

```rust
pub enum __MossBodyOutcome_Worker_Run {
    Normal(ReplyType),
    Raised(__MossFailureFrame_Worker_Run),
}

pub struct __MossFailureFrame_Worker_Run {
    tag: u32,
    raised: __MossRaisedCarrier_Worker_Run, // provider-private
    captures: __MossCaptures_Worker_Run,   // provider-private
}

impl __MossFailureFrame_Worker_Run {
    pub fn tag(&self) -> u32 { self.tag }
}

enum __MossRaisedCarrier_Worker_Run {
    ParseError_Invalid,
    FileError_Full,
    FromStoreLoad(provider::__MossFailureFrame_Store_Load),
}
```

Tags are assigned from the specialization's normalized `RaiseSet`, starting at
zero. They are local to that specialization. A bridge maps the callee tag to a
canonical `RaisedIdentity`, then maps that identity to the caller tag; it never
copies a numeric tag on the assumption that the two tables have the same order.
The application may inspect only `tag()` and move the opaque frame. It cannot
name, construct, inspect, borrow, clone, or partially move provider-private
carrier or capture values. The carrier is compiler-only and is not a
source-visible mixed-enum union.

### 2.1 Cross-specialization bridge

`Raised(callee_frame)` is never returned as `Raised(caller_frame)`. The caller
provider generates a by-value adapter and a private carrier variant:

```rust
fn __moss_bridge_consumer__Worker_Get__Store_Load(
    callee: provider::__MossFailureFrame_Store_Load,
    captures: __MossCaptures_Worker_Get,
) -> __MossFailureFrame_Worker_Get {
    let caller_tag = match callee.tag() {
        provider::STORE_ERROR_DOWN_TAG => WORKER_STORE_ERROR_DOWN_TAG,
        _ => __moss_abort_invalid_provider_abi(),
    };
    __MossFailureFrame_Worker_Get {
        tag: caller_tag,
        raised: __MossRaisedCarrier_Worker_Get::FromStoreLoad(callee),
        captures,
    }
}
```

`ImportedRaiseBridgeAbi` freezes the callee specialization, concrete callee
outcome/frame types, caller carrier variant, bridge symbol, and every
`callee_tag -> RaisedIdentity -> caller_tag` translation. Frames are nested by
value. There is no cast, serialization buffer, trait object, heap allocation,
or host exception.

Validation has two explicit levels. Loading the caller's `.mossi` invokes the
local bridge verifier, which proves unique tags/symbols and agreement with the
caller outcome. Application linkage invokes
`imported_raise_bridge_matches_callee` with the actually resolved callee ABI;
it additionally requires the named specialization, outcome type, frame type,
callee tag, semantic identity, and ordered owned payload schema to agree
(projection/residual Rust types remain specialization-private). A bridge naming
a missing callee or mapping a real callee tag to a different identity or payload
cannot reach code generation.

### 2.2 Typed recovery and bare re-raise

Each raised identity has a reversible provider projection:

```rust
pub enum __MossProjection_Store_Load_Down {
    Matched {
        payload: __MossPayload_StoreError_Down,
        residual: __MossResidual_Store_Load_Down,
    },
    Other(__MossFailureFrame_Store_Load),
}

pub fn __moss_project_Store_Load_Down(
    frame: __MossFailureFrame_Store_Load,
) -> __MossProjection_Store_Load_Down;

pub fn __moss_rebuild_Store_Load_Down(
    payload: __MossPayload_StoreError_Down,
    residual: __MossResidual_Store_Load_Down,
) -> __MossFailureFrame_Store_Load;
```

Projection consumes the frame. `Matched` moves out the exact typed owned
payload and an opaque owned residual containing private captures and origin
state. An ordinary recovery consumes/drops the residual. A bare re-raise
consumes the payload and residual through `rebuild`, then passes that exact
frame through the caller bridge. The selected payload/residual slot is lexical
to the recovery arm and is never inherited by helpers or sibling arms.

Each payload-field record also carries `Kind::Scalar` or `Kind::ErrorEnum`, an
owned bit, and a bounded bit. The ABI verifier requires consecutive tags in
normalized identity order, unique field names, and owned bounded fields; it
rejects String, capabilities, guards, protected views, domain handles, and
references.

Raised payload fields are owned, bounded, heap-free scalar/enum fields. No
String, domain handle, FileIO, Range, RangeBatch, guard, protected view, mutable
alias, or borrowed reference is legal. Panic remains a separate aborting channel
and never appears as a raised tag.

An ordinary helper returns its own tagged outcome. A same-specialization nested
wrapper may forward it directly; a cross-specialization caller must use the
bridge above. A nested wrapper never dispatches a trailer. The Root wrapper
alone may consume its own specialization's frame after abandonment.

## 3. Provider and application roles

The provider materializes the body and each failure arm as separate callables:

```rust
pub fn __moss_body_provider__Worker_Run(
    state: &mut __MossAttemptState_Worker_Run<'_>,
    job: Job,
) -> __MossBodyOutcome_Worker_Run;

pub fn __moss_on_fail_provider__Worker_Run_0(
    state: &mut __MossFailureState_Worker_Run<'_>,
    frame: __MossFailureFrame_Worker_Run,
) -> ReplyType;
```

The body return above is the concrete `__MossBodyOutcome_Worker_Run` (there is
no Rust type parameter). The failure callable consumes the whole opaque caller
frame and projects the selected typed payload and captures inside the provider.
The application selects the callable using the caller-local stable tag and
`.mossi` pattern table. This avoids exposing private residual/capture types in a
cross-crate signature.

The final application owns both synchronized wrappers because only it has the
concrete domain graph, synchronization classes, ranks, and ingress role. Its
nested wrapper calls the provider body and forwards the outcome. Its Root
wrapper performs this exact sequence on `Raised(frame)`:

1. join or safely quiesce every Branch and descendant;
2. close every abandoned Root-local FileIO without implicit sync and without
   replacing the selected error;
3. release all old attempt guards and end their protected views;
4. acquire the fresh ranked `h_fail` lock plan;
5. call the provider's selected failure-arm body with the opaque owned frame;
6. fulfill the original handler reply contract and release the fresh guards.

No wrapper uses host exception unwinding or `catch_unwind`. A one-way Root has
no reply value. Every normally completing failure arm of a value-returning Root
returns the original handler's reply type.

### 3.1 Root totality boundary

Provider validity and application Root validity are deliberately distinct. A
materialized provider with a nonempty raised outcome and partial or absent
`on_fail` arms is valid for nested use: the nested wrapper returns its typed
frame to its caller. `provider_handler_abi_valid` therefore does not demand
exhaustive arm coverage.

Before emitting a concrete application Root wrapper,
`verify_root_totality(provider, escaping)` requires every actually escaping
identity to select exactly one arm after source-order/catch-all expansion. It
also requires empty failure-arm escape sets and total replies of the exact
handler reply type. A still-defined but unreachable named variant is legal in
an arm (the semantic phase may warn); it does not contribute to Root coverage.
A nonempty escaping set with no exhaustive trailer is rejected even though the
same provider interface remains valid as a nested callee.

## 4. Captures and D7

An `on_fail` capture record contains its source name, concrete Moss type,
provider-private Rust field, ownership fact, and definite-initialization fact.
The attempt must materialize the value into the opaque frame before its frame
and guards end. Every path selecting that arm must initialize it under arm-
refined D7. A capture is rejected unless it is owned and definitely initialized.

The boundary categorically forbids FileIO, Range, RangeBatch, any guard,
protected read/write view, reference, or other borrow. The restriction applies
even if a backend lifetime could otherwise be expressed. A provider-private
owned type is allowed only when the artifact's counted
`handler_owned_type_shapes` table carries its complete structural declaration
and every dependency. The application still only moves its opaque value back
to the provider.

Legality is structural. `ResolvedTypeAbi` is a recursive tree with kinds
`Scalar`, `ErrorEnum`, `Aggregate`, `OwnedUnbounded`, `Capability`, `Borrowed`,
and `Unknown`. Payloads admit only recursively bounded `Scalar`/`ErrorEnum`
trees. Captures admit any recursively owned non-capability tree, including an
owned String or aggregate, but reject a borrow or capability at any depth.
Unknown and cyclic inline representations fail closed.

Every payload/capture `.mossi` record carries a length-delimited recursive type
fingerprint. The same handler carries a canonical, counted structural table;
each payload/capture fingerprint must equal its table entry, and every child
must have an equal table entry. The loader independently resolves intrinsic and
exported enum/record fields and requires those declarations to match. An
otherwise unknown name is legal only as a table-declared aggregate or error
enum; an unknown scalar/unbounded/capability/borrow claim fails closed. Raised
enum payload names, types, order, and arity are also checked against the
defining variant whenever that definition is local. Thus
`owned=1 bounded=1` cannot disguise `String`, `&T`, or an enum/aggregate that
contains FileIO, Range, a domain handle, guard, or protected view. Structural
declarations used only for ABI verification do not make a private name
source-visible; the recursive legality checks still apply.

## 5. `h_fail` synchronization contract

The trailer group is planned as a distinct pseudo-handler `h_fail`, with its
own effect summary, mode-signature column, ClassSet, and ranked acquisition.
Its leaf accesses are never unioned into the normal handler's leaf footprint.

Adding the `h_fail` signature column can still repartition global classes. If
normal `h` writes both `x` and `y`, those leaves may initially share one class.
If `h_fail` writes only `x`, their complete signature vectors differ and split
into two classes. Normal `h` can consequently acquire two exclusive locks while
retaining exactly the same per-leaf WRITE requirements. Trailer writes can also
make readers acquire shared locks. These effects are ordinary consequences of
global signature partitioning and do not merge `h_fail` into `h`.

## 6. Exceptional CFG

Every syntactic or primitive raising node has one or more explicit edges:

- `RaiseToRecover` to matching arms of the associated original `try`;
- `RaiseToHandlerExit` for an unhandled tagged return;
- `ReRaiseToOuter` for a bare raise from the selected arm.

Each edge carries its normalized alternatives and certifies all consumers:

1. D7 definite initialization;
2. ownership and move checking;
3. untouched-guard cancellation;
4. early acquisition and ranked 2PL planning;
5. lexical/FileIO/Branch cleanup;
6. code generation.

`RaisingNodeAbi` carries the complete possible-alternative set, its ordered
lexical recovery-scope chain, and (for bare re-raise only) the selected-error
scope and designated outer scope. `ExceptionalDestinationAbi` declares every
valid target, its role (`RecoveryArm`, `HandlerExit`, or
`OuterRecoveryOrExit`), lexical owner, source `match_order`, and accepted
alternatives.

`verify_exceptional_cfg` requires the outgoing alternatives of each raising
node to be a disjoint exact partition of that node's possible set. It rejects
missing or impossible alternatives, duplicate/contradictory routes, undeclared
destinations, kind/destination mismatches, lexical-scope violations, a
destination that does not accept an alternative, skipping the first matching
arm in the innermost accepting scope, duplicate `(scope, match_order)` recovery
destinations, repeated scopes in a node's lexical chain, and any missing
consumer bit.
Only `RaiseToHandlerExit` alternatives determine the handler's escaping outcome;
locally recovered alternatives need not appear there. A bare raise is valid
only at an active selected-error slot and targets its designated outer scope.
Helpers do not inherit a caller's slot, and sibling arms cannot receive a new
or re-raised error from another arm.

## 7. `.mossi` ABI-v8 records

Every concrete function and handler effect string serializes:

```text
...;fileio=0;fileio_unknown=0;may_panic=0;
raise_set=enum:FileError:Full,enum:FileError:IO;
may_diverge=0;unresolved=0
```

Every exported handler has these counted records, including explicit zero
counts:

```text
handler_abi
handler_outcome_contract
handler_wrapper_contract
handler_reply_contract
handler_owned_type_shapes / handler_owned_type_shape
handler_raise_tags / handler_raise_tag
handler_raise_payload
handler_raise_projections / handler_raise_projection
handler_raise_bridges / handler_raise_bridge / handler_raise_bridge_tag
handler_failure_arms / handler_failure_arm
handler_failure_capture
handler_raising_nodes / handler_raising_node
handler_exceptional_destinations / handler_exceptional_destination
handler_exceptional_edges / handler_exceptional_edge
handler_exceptional_consumers
```

Records cover body and arm symbols, the opaque frame type, application wrapper
ownership, specialization-local stable tags, reversible projection symbols,
cross-specialization tag/identity translations, typed/grouped/catch-all
patterns, per-arm capture and state footprints, empty escaping raise sets,
reply totality, complete raising-node alternative sets, declared destinations,
exceptional edges, and the full consumer mask. Counted lists, wrapper order,
recursive type tables/fingerprints, local bridge mappings, capture legality,
effect/tag agreement, exact CFG partitions, and reply consistency are validated
while loading. Caller-to-callee bridge agreement is validated again during
application linkage against the resolved callee ABI. Missing or inconsistent
records fail closed.

Serialization order is source arm order for arms/captures and normalized
identity order for raise tags and edge alternatives. These records participate
in the concrete interface hash. The canonical specialization identity remains
the project-qualified module identity, entity identity, generic semantic hash,
concrete type tuple, and now the complete ABI-v8 effect/handler metadata hash.

ABI v8 is intentionally incompatible with the tentative v7 checkpoint and all
earlier artifacts. V8 adds concrete projection/rebuild signatures, imported
frame bridges with linkage verification, counted structural type tables and
fingerprints, complete raising-node sets, and declared exceptional destinations.
A v7 `.mossi` is rejected with a full-rebuild diagnostic; there is no mixed
v7/v8 adapter.

## 8. Stage ownership

21.A populates raised alternatives, arm/capture/reply records, exceptional CFG,
and generated body/arm callables. 21.B uses the tagged origin interface and
cleanup boundary for FileIO. 21.C uses `chunk_speculation_safe`, indexed raised
outcomes, and join-before-observe. 21.D supplies numeric raised identities and
native/Fast Debug parity. None may independently change the tag identity,
opaque-frame calling convention, wrapper role split, consumer mask, or ABI
version.
