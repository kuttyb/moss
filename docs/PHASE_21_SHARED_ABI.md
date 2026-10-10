# Phase 21 shared error-handling and provider ABI contract

**Status:** frozen Stage 21.0 contract for Phase 21 implementation workstreams.
**Native/provider ABI:** 7.
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
metadata is not an empty effect: ABI-v7 loading fails and requests a rebuild.
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

Each concrete fallible specialization has a closed generated Rust outcome. It
does not use unwinding and does not catch Rust panics:

```rust
pub enum __MossBodyOutcome_Worker_Run<T> {
    Normal(T),
    Raised(__MossFailureFrame_Worker_Run),
}

pub struct __MossFailureFrame_Worker_Run {
    tag: u32,
    raised: __MossRaisedPayload_Worker_Run, // provider-private
    captures: __MossCaptures_Worker_Run,   // provider-private
}

impl __MossFailureFrame_Worker_Run {
    pub fn tag(&self) -> u32 { self.tag }
}

enum __MossRaisedPayload_Worker_Run {
    ParseError_Invalid,
    FileError_Full,
    ProviderError_Bad { code: i64 },
}
```

Tags are assigned from the normalized `RaiseSet`, starting at zero. The
application may inspect only `tag()` and move the opaque frame. It cannot name,
construct, inspect, borrow, clone, or partially move provider-private payloads
or captures. This lets one specialization carry alternatives from several Moss
error enums without creating a source-visible cross-enum type. It also preserves
private provider types: the public frame type is opaque because all fields and
the payload/capture types remain private.

Each payload-field record also carries `Kind::Scalar` or `Kind::ErrorEnum`, an
owned bit, and a bounded bit. The ABI verifier requires consecutive tags in
normalized identity order, unique field names, and owned bounded fields; it
rejects String, capabilities, guards, protected views, domain handles, and
references.

Raised payload fields are owned, bounded, heap-free scalar/enum fields. No
String, domain handle, FileIO, Range, RangeBatch, guard, protected view, mutable
alias, or borrowed reference is legal. Panic remains a separate aborting channel
and never appears as a raised tag.

Ordinary helpers and nested handlers return the tagged outcome directly. A
nested wrapper propagates `Raised(frame)` and never dispatches a trailer. The
Root wrapper alone may consume the frame after the abandonment transition.

## 3. Provider and application roles

The provider materializes the body and each failure arm as separate callables:

```rust
pub fn __moss_body_provider__Worker_Run(
    state: &mut __MossAttemptState_Worker_Run<'_>,
    job: Job,
) -> __MossBodyOutcome_Worker_Run<ReplyType>;

pub fn __moss_on_fail_provider__Worker_Run_0(
    state: &mut __MossFailureState_Worker_Run<'_>,
    frame: __MossFailureFrame_Worker_Run,
) -> ReplyType;
```

The failure callable consumes the whole opaque frame and unpacks the selected
typed payload and captures inside the provider. The application selects the
callable using the stable tag and `.mossi` pattern table. This avoids exposing a
private Rust payload type in a cross-crate signature.

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

## 4. Captures and D7

An `on_fail` capture record contains its source name, concrete Moss type,
provider-private Rust field, ownership fact, and definite-initialization fact.
The attempt must materialize the value into the opaque frame before its frame
and guards end. Every path selecting that arm must initialize it under arm-
refined D7. A capture is rejected unless it is owned and definitely initialized.

The boundary categorically forbids FileIO, Range, RangeBatch, any guard,
protected read/write view, reference, or other borrow. The restriction applies
even if a backend lifetime could otherwise be expressed. Provider-private owned
types are allowed because the application only moves the opaque frame back to
the provider.

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

`verify_exceptional_cfg` independently rejects a raising node without an edge,
an edge with no alternative, or an edge missing any consumer bit. Selected-error
slots are lexical to a recovery arm. A bare raise uses the innermost active slot;
helpers do not inherit a caller's slot, and sibling arms cannot receive a new or
re-raised error from another arm.

## 7. `.mossi` ABI-v7 records

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
handler_raise_tags / handler_raise_tag
handler_raise_payload
handler_failure_arms / handler_failure_arm
handler_failure_capture
handler_raising_nodes
handler_exceptional_edges / handler_exceptional_edge
handler_exceptional_consumers
```

Records cover body and arm symbols, the opaque frame type, application wrapper
ownership, stable tags, typed/grouped/catch-all patterns, per-arm capture and
state footprints, empty escaping raise sets, reply totality, exceptional edges,
and the full consumer mask. Counted lists, wrapper order, capture legality,
effect/tag agreement, CFG coverage, and reply consistency are validated while
loading. Missing or inconsistent records fail closed.

Serialization order is source arm order for arms/captures and normalized
identity order for raise tags and edge alternatives. These records participate
in the concrete interface hash. The canonical specialization identity remains
the project-qualified module identity, entity identity, generic semantic hash,
concrete type tuple, and now the complete ABI-v7 effect/handler metadata hash.

ABI v6 is intentionally incompatible. Fallible materialized signatures and the
provider/application wrapper contract changed, so a v6 `.mossi` is rejected
with a full-rebuild diagnostic. There is no mixed v6/v7 adapter.

## 8. Stage ownership

21.A populates raised alternatives, arm/capture/reply records, exceptional CFG,
and generated body/arm callables. 21.B uses the tagged origin interface and
cleanup boundary for FileIO. 21.C uses `chunk_speculation_safe`, indexed raised
outcomes, and join-before-observe. 21.D supplies numeric raised identities and
native/Fast Debug parity. None may independently change the tag identity,
opaque-frame calling convention, wrapper role split, consumer mask, or ABI
version.
