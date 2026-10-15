# Phase 25 Team G G0 coordinator decision

**Status:** corrected review candidate; **NO-GO for shared-IR implementation**  
**Pinned baseline:** `fa3a340a0a6652568b97869489da1a84038b9857`  
**Target branch:** `phase25-generic-types`

## Integrated decision

The G0 candidate adopts one structured Type IR for inference, member dispatch,
ownership/effects, semantic queries, Fast Debug, native lowering, and ABI v10.
Nominal identity is declaration-based and concrete instances add ordered resolved
field types. Open callable results use summary-local binders freshened independently
at every call, with transitive requirement/provenance chains and closure before
execution or code generation.

The native A1 choice is the Type IR contract's **provider-owned generic Rust
record with mechanical public backend fields plus Moss visibility checking**.
Consumer methods are specialized free functions. The private-field/accessor
prototype remains evidence for an evaluated alternative, not the selected Phase 25
contract; stronger physical opacity remains Phase 25.5 D1/D2.

Map uses a compiler-owned, policy-free `MossMap<K,V>`. Its Rust value identity
contains no `Ops` parameter, stored callback, function pointer, closure, trait object,
or vtable. Consumer-owned static operations invoke exact equality/hash proof targets.
The durable G0 Rust prototype passes one provider-created map through two independent
consumer rlibs and exercises custom equivalence, insert, lookup, overwrite, delete,
and eager owned keys. The earlier consumer-local adapter and `MossMap<K,V,Ops>`
variants are retained in history as rejected alternatives, not production decisions.

The corrected contract also freezes the shared open-receiver dispatch record for
ledger #12, separate equality/hash signatures and effect records, versioned key proof
and law identities, shared Map/Set proof shape, and the Phase 25 Float-key prohibition.

## Review history

- Team G contract draft: `6ebe562f24cc7a091c331b76fb06b2727ec067c7`.
- Initial feasibility: `2bc47bdc7acb2d442a5108df68622f8585e3788c`.
- Independent Team G Architect NO-GO on the baseline/draft:
  `40fd3b6ea1aa89ae5a05e2afe092c8699a56f1f2`.
- Independent Team S boundary Architect NO-GO on integrated draft `6f109124`:
  `8a417edd06e8194fe6c29023c2e21070d710cfa0`.
- Two-consumer policy-bearing Map alternative:
  `040ed4b1951ca9c8371becb2387db332addfedfd` (rejected/superseded).
- Policy-free two-consumer Map proof:
  `d0423e0f728fff0a7f40a8b41953fd822195d3a4`.

Historical NO-GO reports remain unchanged. Both Architects must review the exact
corrected candidate and issue attributable addenda; an addendum does not erase the
earlier findings.

## Evidence at G0

The pinned baseline passes its existing 40 generic-specialization probes and 129
builtin/generic typing probes. The Phase 25 forward fixtures correctly expose the
baseline gaps: declaration-global nominal field inference, provider-local rejection
of open results, incomplete transitive requirement provenance, local member dispatch,
recursive nominal acceptance until rustc, and custom Map-key acceptance until rustc.

The bounded Rust proof runs with:

```text
python3 tests/phase25/g0/check_rust_feasibility.py
PASS: Phase 25 G0 provider identity, canonical cross-crate MossMap, and visibility prototype passed
```

This is feasibility evidence, not Moss implementation acceptance. ABI-v9 remains the
production format at this checkpoint; ABI-v10 is a reviewed design only.

## Remaining approval items

Shared compiler/IR surgery remains blocked until all items are recorded:

1. fresh exact-SHA GO addenda from the independent Team G and Team S Architects;
2. an attributable Team S implementation-owner concurrence naming the source spelling
   that resolves to `key-protocol-v1`, Map/Set law-test ownership, Set delivery or
   deferment, and joint ledger #12 ownership;
3. an L7 decision on package identity: approve
   `module-v1:<package-name>@<package-version>::<module>` with graph-level collision
   rejection, or require a durable package instance identity;
4. written L7 GO naming the corrected contract commit and both Architect addenda; and
5. implementation guard budgets after corpus measurement (budgets are diagnostics,
   never semantics).

The Phase 25 charter authorizes the mission and G0 work but, per its own gate, is not
the later written GO naming reviewed contracts. No source-level `Box[T]`, `where T`,
`Self`, local annotation, dynamic dispatch, or implicit `Any` is authorized.
