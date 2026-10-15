# Phase 25 Team S — G0 Implementation-Owner Concurrence

- **Role:** Team S Implementation Owner
- **Status:** CONCURRENCE for G0 Candidate Boundary Freeze; NO-GO for G2 implementation
- **Pinned baseline:** `fa3a340a0a6652568b97869489da1a84038b9857`
- **Reviewed candidate SHA:** `aa83fb66b7dbcbb33144aca023734f43a6ae4e0c`
- **Target branch:** `phase25-generic-types`

---

## 1. Concurrence summary

Team S concurs with the G0 boundary definition in `TYPE_IR_CONTRACT.md` and `ABI_V10_DESIGN.md` as amended by the L7 review decisions:
- One unified `TypeRef`/`TypeVar` structured graph across inference, member dispatch, ownership/effects, semantic queries, Fast Debug, native lowering, and ABI v10.
- A compiler-owned, policy-free `MossMap<K,V>` runtime representation whose Rust value type contains no `Ops` parameter, stored callbacks, function pointers, closures, trait objects, or vtables.
- The shared `MemberDispatchRequirement` record for open-receiver member calls (ledger #12).
- Removal of the blanket Phase 25 Float-key prohibition; Float `isequal`/`hash` semantics are committed as an approved Phase 25 G3 deliverable owned by Team S.
- Canonical key-proof coherence across consumers as an explicit compile/link invariant.

---

## 2. Key-protocol specification and source declarations

Moss v0.1 has no source-level generic parameter syntax (`[T]`), so source code does not declare generic type variables. Instead, nominal key types satisfy the structural key protocol via concrete structural methods declared on the type:

```moss
type UserKey:
  code: Int

  fn isequal(other: UserKey) -> Bool:
    return self.code % 10 == other.code % 10

  fn hash() -> Int:
    return self.code % 10
```

For any nominal key type `Key`, the concrete structural methods are:
- **Structural equality method:** `fn isequal(other: Key) -> Bool` (with inferred receiver/argument access `(READ Key, READ Key) -> Bool`).
- **Structural hash method:** `fn hash() -> Int` (with inferred receiver access `(READ Key) -> Int`).

In ordinary functions or helper callables, non-method free functions on concrete types (`fn isequal(left: Key, right: Key) -> Bool`, `fn hash(value: Key) -> Int`) are also supported by duck-typed candidate selection.

Inside the compiler and ABI-v10, the shared `key-protocol-v1` retains internal generic type variables (`TypeVar K`):
- `equality_signature`: `(READ K, READ K) -> Bool`
- `hash_signature`: `(READ K) -> Int`

Both operations must be pure, deterministic, non-mutating, non-consuming, non-raising, non-panicking, non-diverging, and free of messages, domain-state access, I/O, executor, or other observable side effects.

---

## 3. Protocol, comparator-law validation, and coherence invariants

### 3.1 Comparator-law validation suite
Team S owns the **`key-protocol-v1` comparator-law validation suite**, verifying:
1. **Reflexivity:** `x.isequal(x) == true` for all valid `x`.
2. **Symmetry:** `x.isequal(y) == y.isequal(x)` for all `x, y`.
3. **Transitivity:** `x.isequal(y) && y.isequal(z) ==> x.isequal(z)`.
4. **Equal-keys/Equal-hash:** `x.isequal(y) ==> x.hash() == y.hash()`.
5. **Snapshot stability:** `isequal` and `hash` evaluate consistently on the key snapshot stored inside the map across subsequent map operations.

### 3.2 Canonical key-proof coherence invariant
Canonical key-proof coherence across consumers is an explicit **compile/link invariant**:
- For any concrete nominal type `Key`, there is at most one canonical `KeyProof` identity in the program.
- When multiple independent consumer packages import, specialize, or pass values of type `Map[Key, V]`, all consumers must bind to the exact same canonical `KeyProofId`.
- Conflicting or duplicate key proofs for the same nominal type fail closed at compile or link time with `KEY_PROOF_COHERENCE_MISMATCH`.

---

## 4. Float-key delivery commitment (Phase 25 G3)

Team S formally accepts ownership of **Float `isequal`/`hash` semantics** as a Phase 25 G3 deliverable:
- At G0, Float keys remain unimplemented.
- In G3, Team S will define and implement approved **Julia-like Float semantics**:
  - `isequal(-0.0, +0.0)` is **`false`**; `-0.0` and `+0.0` are distinct keys with distinct hashes (`hash(-0.0) != hash(+0.0)`).
  - Canonical NaN equivalence: `isequal(NaN, NaN)` is **`true`** for all NaN representations, mapping to a single deterministic hash value.
  - Signed-zero ordering and hashing are consistent with total Float ordering: `-Inf < negative finite < -0.0 < +0.0 < positive finite < +Inf < NaN`.
  - Well-distributed 64-bit integer hash projection for all classes of floating-point values.
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
