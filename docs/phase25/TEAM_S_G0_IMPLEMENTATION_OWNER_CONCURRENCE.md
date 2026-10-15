# Phase 25 Team S — G0 Implementation-Owner Concurrence

- **Role:** Team S Implementation Owner
- **Status:** CONCURRENCE for G0 Candidate Boundary Freeze; NO-GO for G2 implementation
- **Pinned baseline:** `fa3a340a0a6652568b97869489da1a84038b9857`
- **Target branch:** `phase25-generic-types`

---

## 1. Concurrence summary

Team S concurs with the G0 boundary definition in `TYPE_IR_CONTRACT.md` and `ABI_V10_DESIGN.md` as amended by the L7 review decisions:
- One unified `TypeRef`/`TypeVar` structured graph across inference, member dispatch, ownership/effects, semantic queries, Fast Debug, native lowering, and ABI v10.
- A compiler-owned, policy-free `MossMap<K,V>` runtime representation whose Rust value type contains no `Ops` parameter, stored callbacks, function pointers, closures, trait objects, or vtables.
- The shared `MemberDispatchRequirement` record for open-receiver member calls (ledger #12).
- The removal of the blanket Phase 25 Float-key prohibition; Float `isequal`/`hash` semantics are committed as an approved Phase 25 G3 deliverable owned by Team S.

---

## 2. Exact `isequal` and `hash` source signatures

Under `key-protocol-v1`, key operations are declared and resolved as:

- **Equality callable:**
  ```moss
  fn isequal(left: K, right: K) -> Bool
  ```
  Inferred parameter access: `(READ K, READ K) -> Bool`.
- **Hash callable:**
  ```moss
  fn hash(value: K) -> Int
  ```
  Inferred parameter access: `(READ K) -> Int`.

Both functions must resolve statically for each concrete key type `K`. They are pure, deterministic, non-mutating, non-consuming, non-raising, non-panicking, non-diverging, and free of messages, domain-state access, I/O, executor, or other observable side effects.

---

## 3. Protocol and comparator-law validation ownership

Team S owns the **`key-protocol-v1` comparator-law validation suite**.

This acceptance test harness verifies the algebraic laws for key implementations:
1. **Reflexivity:** `isequal(x, x) == true` for all valid `x`.
2. **Symmetry:** `isequal(x, y) == isequal(y, x)` for all `x, y`.
3. **Transitivity:** `isequal(x, y) && isequal(y, z) ==> isequal(x, z)`.
4. **Equal-keys/Equal-hash:** `isequal(x, y) ==> hash(x) == hash(y)`.
5. **Snapshot stability:** `isequal` and `hash` evaluate consistently on the key snapshot stored inside the map across subsequent map operations.

These laws are backed by reusable acceptance tests executed against key providers, not a claim of general theorem proving.

---

## 4. Float-key delivery commitment (Phase 25 G3)

Team S formally accepts ownership of **Float `isequal`/`hash` semantics** as a Phase 25 G3 deliverable:
- At G0, Float keys remain unimplemented.
- In G3, Team S will define and implement IEEE-754 key equivalence:
  - Canonical NaN equivalence (`NaN == NaN` for map lookup) and deterministic NaN hashing.
  - Signed zero equivalence (`+0.0 == -0.0`) with identical hash values.
  - Well-distributed 64-bit integer hash projection for normal and subnormal floating-point values.
- ABI-v10 negative verification tests will distinguish valid future Float proofs from malformed, forged, or premature proofs.

---

## 5. Set delivery or deferment

Set will reuse the exact same `key-protocol-v1`, `KeyProof` ABI record shape, and compiler-owned runtime storage representation as Map in G3. If Set implementation cannot be completed concurrently with Map in G3 without schedule risk, Set will be explicitly deferred to avoid fragmenting or duplicating the key protocol.

---

## 6. Joint ledger #12 responsibilities

For member dispatch on open receivers (ledger #12):
- **Team S owns:** member admission and static candidate target selection when the outer constructor of a receiver is known (e.g. `Vector[alpha].push(Int)`), emitting the structured `MemberDispatchRequirement` record with required receiver/argument access modes into the shared Type IR.
- **Team G owns:** constraint unification and closure over the shared Type IR, determining `alpha = Int` and propagating types to call sites.
- **Joint ownership:** Team S and Team G jointly own the positive end-to-end ledger #12 regression test suite (`var v = []; v.push(1)`).
