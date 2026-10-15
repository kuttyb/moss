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
and law identities, shared Map/Set proof shape, frozen package identity
`module-v1:<name>@<version>::<module>`, and Team S G3 ownership of Float keys.

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
- Integrated review candidate:
  `27110249abeca3102cfd6cbf71ac4db436f1a7bb`.
- L7 verdict on `2711024`: **CONDITIONAL GO for G0; NO-GO for G2 shared compiler/IR implementation**.

Historical NO-GO reports remain unchanged. Both Architects review the exact
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
PASS: Phase 25 G0 provider identity, public backend fields, canonical cross-crate MossMap, and preserved accessor alternative passed
```

This is feasibility evidence, not Moss implementation acceptance. ABI-v9 remains the
production format at this checkpoint; ABI-v10 is a reviewed design only.

## Status of G0 requirements & G2 prerequisites

Following the L7 G0 review corrections:

1. **Float-key prohibition removed:** Float `isequal`/`hash` semantics remain an
   approved Phase 25 G3 deliverable owned by Team S. G0 leaves them unimplemented;
   negative tests distinguish valid future Float proofs from malformed/forged proofs.
2. **A1 prototype:** Provider-owned generic Rust record with mechanical public backend
   fields (`pub field_0: T`) and consumer-generated free functions verified. Accessor
   alternative preserved; forward Moss visibility negative test recorded in
   `tests/phase25/g0/conformance/private_field_visibility_negative.moss`.
3. **Package identity:** Frozen per L7 verdict as `module-v1:<name>@<version>::<module>`
   with deterministic provider-conflict rejection, subject to confirming Margo's
   resolution and artifact binding semantics.
4. **Team S Concurrence:** Formal implementation-owner concurrence recorded in
   `docs/phase25/TEAM_S_G0_IMPLEMENTATION_OWNER_CONCURRENCE.md`.
5. **Map Complexity & Keys Contract:** Production target specified as bucketed open-addressing
   hash table with expected $O(1)$ lookup/insert/delete; `keys()` is an eager owned snapshot
   `Vector[K]` with snapshot stability in $O(N)$ time.
6. **Fresh Architect Reviews:** Fresh exact-SHA review addenda requested and recorded
   from independent Team G and Team S Architects.

Shared compiler/IR surgery remains strictly **NO-GO** until written L7 G2 authorization.
No source-level `Box[T]`, `where T`, `Self`, local annotation, dynamic dispatch, or
implicit `Any` is authorized.
