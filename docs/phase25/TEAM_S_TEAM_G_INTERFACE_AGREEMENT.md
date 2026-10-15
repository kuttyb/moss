# Phase 25 — Team S / Team G Joint Interface Agreement (G2/G3/G4/G5)

**Author:** Team S L6 Coordinator
**Team S branch:** `phase25-semantics-engines`
**Team G branch read:** `origin/phase25-generic-types` at `97b7d422b3d5075831bb6f471d4d3017c4dbb4b8`
**Builds on (frozen G0, L7 GO):** `TYPE_IR_CONTRACT.md` §4.2, §8; `ABI_V10_DESIGN.md` §3.5;
`TEAM_S_G0_IMPLEMENTATION_OWNER_CONCURRENCE.md` (all on the Team G branch)
**Status:** Team S proposal. Each numbered item becomes binding when Team G's
Coordinator records concurrence in the table at the end. Nothing here changes
the frozen G0 contracts; where G0 left a detail open, this document proposes
the detail.

This agreement settles interfaces now so that neither team waits for G2 to be
complete. It does not start a second generic type system, Map representation,
or ABI-v10 transition on the Team S branch.

## 1. Runtime crate ownership of `MossString` and `MossMap<K,V>`

**Problem.** Every generated crate embeds its own runtime prelude, so a
provider's `MossString` and a consumer's are different Rust types. A two-package
staged indexed-v1 build fails in rustc for that reason (Team S L7 repair report
§2). `MossMap<K,V>` has the same requirement.

**Agreement.**

1. There is one compiler-owned Rust runtime crate, proposed name `moss_runtime`.
   Every provider and consumer crate links the same build of it. Generated code
   names `moss_runtime::MossString` and `moss_runtime::MossMap<K, V>`, never a
   private copy.
2. **Team G owns** the crate's existence, build, and link plumbing (rlib/extern
   placement, Margo artifact roots, cache keys, and the `binary_contract`
   identity field) and the `moss_runtime::map` module (`MossMap<K,V>`, and Set
   storage reusing the same kernel).
3. **Team S owns** two modules inside that crate, moved verbatim from the current
   prelude in `src/char_string_ordering_runtime.hpp`:
   - `moss_runtime::string`: `MossString` (indexed-v1) and the
     `__moss_string_length / char_at / slice / find` helpers;
   - `moss_runtime::key_protocol`: `moss_float_less`, `moss_float_isequal`,
     `moss_float_hash`, `moss_float_total_cmp`, `moss_string_hash`,
     `__moss_char_from_codepoint`.
   Team S does not add another crate or a second embedding mechanism.
4. The crate's identity (name, version, and content hash) is part of the
   ABI-v10 `binary_contract`. A provider and a consumer that record different
   runtime identities fail closed before rustc runs.

## 2. Built-in `KeyProof` targets are the Team S scalar lowering

**Agreement:** yes. For the built-in key types, the `equality_callable_id` and
`hash_callable_id` in a `MapKeyProof`/`SetKeyProof` (ABI_V10 §3.5) name exactly
the functions Team S lowers today for `isequal` and `hash`
(`scalar_protocol_rust` in `src/moss.cpp`). No other built-in equality or hash
may back a Map or Set.

| Key type | `isequal(a, b)` lowering | `hash(a)` lowering | `less(a, b)` lowering |
|---|---|---|---|
| `Int` | `a == b` | `a` (identity) | `a < b` |
| `Float` | `moss_float_isequal(a, b)` | `moss_float_hash(a)` | `moss_float_less(a, b)` |
| `Char` | `a == b` (scalar value) | `a as u32 as i64` | `a < b` |
| `String` | byte equality | `moss_string_hash` (FNV-1a 64 over UTF-8 bytes) | byte order (= code-point order) |
| `Bool` | `a == b` | `0` / `1` | `false < true` |

- The proof identifiers are `builtin:key-protocol-v1:isequal:<Type>` and
  `builtin:key-protocol-v1:hash:<Type>`. Their semantic hashes are taken over
  the lowering text above, so changing a lowering changes the `proof_id`.
- Float semantics are settled: operator `==` is IEEE; `isequal(NaN, NaN)` is
  true for every NaN bit pattern; `isequal(-0.0, +0.0)` is false; `less(-0.0, +0.0)`
  is true; NaNs sort last; `hash` maps every NaN to one value and the two zeros
  to different values. Equal keys have equal hashes for every built-in type.
- The String hash reads only the UTF-8 bytes. It is therefore identical for the
  plain (`String`) and indexed-v1 (`MossString`) representations, and the
  representation flip at the ABI-v10 cut does not change any `proof_id`.
- Hashing is deterministic and unseeded. `MossMap` must not mix in
  per-process randomness (for example `RandomState`), so a hash is the same in
  every crate and every run.
- Team S keeps the reusable law suite (`check_phase25_l7_repairs.py`
  ordering laws, `check_phase25_s3_key_protocol.py`) as the acceptance gate for
  these targets. `docs/phase25/TEAM_S_KEY_PROTOCOL_V1_SCALARS.md` is the
  detailed scalar specification.
- Nominal keys use the structural methods `fn isequal(other: Key) -> Bool` and
  `fn hash() -> Int` (G0). Team S resolves those targets and checks their
  purity with the same observable-effect summary that backs `SORT_KEY_IMPURE`.
  Team G records them in the proof and lowers the Map operations.

## 3. ABI-v10 String and Char requirements

**Char.**
- Char is the Rust `char` (a 32-bit Unicode scalar value). Its layout does not
  change at v10, and it already crosses source-free boundaries in v9.
- Every constructor of a Char validates. `Char(Int)` lowers to
  `__moss_char_from_codepoint`, which rejects surrogates, negative values, and
  values above U+10FFFF before any Char exists. No ABI path may build a `char`
  by transmuting an unchecked integer.
- Char is distinct from String, by L7 decision in this cycle. There is no
  transparent `Char == String` and no mixed `isequal` or `assertEqual`, so no
  ABI record has to describe a Char/String coercion.

**String (indexed-v1).**
- Layout: `MossString { bytes: String, codepoints: usize, offsets: Vec<u32> }`,
  56 bytes on 64-bit targets against 24 for `String`, so +32 bytes per String
  value. Measured metadata is 1.6–7.1 % of the payload
  (`TEAM_S_UNICODE_STRING_BENCHMARK.md`).
- Invariants: ASCII exactly when `codepoints == bytes.len()`, and then `offsets`
  is empty; otherwise `offsets[k]` is the byte offset of code point `64·(k+1)`.
  Stride 64, `u32` offsets.
- The v10 `binary_contract` carries a versioned String record:
  `string_record = moss-string-indexed-v1 { stride: 64, offset_width: 32 }`. It
  replaces the staged `.mossi` header line `string_representation indexed-v1`.
  A different record is `STRING_REPRESENTATION_MISMATCH`.
- At the cut, the default `MossStr` becomes `moss_runtime::MossString`
  (a one-line change in `char_string_ordering_runtime_rust`), the
  `MOSS_STAGED_STRING_REPR` switch and the `STRING_REPRESENTATION_STAGED_SINGLE_CRATE`
  guard are removed, and the SDK is rebuilt.
- `u32` offsets limit one String to 4 GiB of UTF-8. Above that limit the
  runtime already fails closed: `MossString::offset` panics with "String
  exceeds the 4 GiB indexed offset limit" and never wraps. L7 decides at the cut
  whether this stays a panic or becomes a typed failure.

**Provider identity.** The `binary_contract` records the `moss_runtime`
identity (§1.4) and the String record. A consumer accepts a provider only when
both match.

**Cross-crate acceptance tests (owned jointly, to run at the cut):**
1. A provider exports `fn label() -> String` holding non-ASCII text longer than
   64 code points. A consumer calls `length`, `char_at`, `slice`, `find`, and
   `append` on the result, in Native Debug and Native Release, plain and indexed.
2. A String stored in an exported record field, in a `Vector[String]`, and as a
   `Map[String, V]` key and value, all crossing the boundary.
3. Char fields, parameters, returns, and `Vector[Char]` across the boundary.
4. Two consumer crates of one provider exchange Strings through a provider
   function, which proves a single `MossString` type.
5. A v9 provider with a v10 consumer, and a mismatched runtime identity, both
   fail closed before rustc, with stable diagnostic codes.
6. The `check_phase25_s2_char_string.py` randomized Unicode properties, rerun
   with the provider/consumer split.

## 4. Team S admission interface for `MemberDispatchRequirement` (ledger #12)

Team S owns admission and target selection, and Team G owns unification and
closure (TYPE_IR_CONTRACT §4.2). The interface between the two:

```text
admit_member(receiver: TypeExpr, member_name, argument_types: [TypeExpr],
             origin: RequirementOrigin) -> MemberAdmission

MemberAdmission =
    Admitted { admitted_template_or_builtin_family,
               selected_callable_id?,          # nominal method target, when known
               receiver_access: AccessMode,    # READ / WRITE / CONSUME
               argument_access: [AccessMode],
               result: TypeExpr?,
               contributes: [(TypeVar, TypeExpr)] }   # e.g. alpha = Int for Vector[alpha].push(Int)
  | Deferred  { waiting_on: [TypeVar] }        # outer constructor not yet known
  | Rejected  { diagnostic }                   # existing stable code and message
```

- The data source for built-in families is the existing receiver table in
  `src/moss.cpp` (`BuiltinReceiverFamily` entries such as
  `{String, "char_at", ..., Effect::Read, {Effect::Read}, {"int"}}`). It
  already gives each member's receiver access, argument access, argument types,
  and result. Team S turns that table into `admit_member` and does not build a
  second receiver-variable system.
- Admission needs only the receiver's outer constructor. `Vector[alpha].push(Int)`
  is `Admitted` with a `WRITE` receiver, a `READ` argument, and
  `contributes = [(alpha, Int)]`.
- An entirely unknown receiver gets `Deferred` while the solver can still pin
  it. At closure it gets `Rejected` with today's diagnostic ("local member calls
  are not implemented…" is replaced by a precise code agreed at G2).
- `Deferred` is never final. After closure every requirement is `Admitted` or
  `Rejected`, before effects or ownership run.

**Joint ledger #12 acceptance suite** (positive results must agree in three
engines):
- `var v = []` then `v.push(1)` then `echo v |> count` prints `1`;
- the same with `Queue()` and `push`/`pop`, and with `Map()` and `m["k"] = 1`;
- the first push inside a loop body, and inside both branches of an `if`
  (both paths must agree, under the existing control-flow rule);
- conflicting pins (`v.push(1)` then `v.push("a")`) fail at checking;
- a receiver that is never pinned is rejected;
- a never-inserted `Map().get(k, d)`, which is the S1.5 handoff (§5), either
  pins through Option B or is rejected before rustc.

## 5. Trigger for Team S final Map/Set integration

Team S starts final Map/Set integration on a throwaway integration worktree
that merges both branches, never on `main` or `phase25-integration`, once all
of the following hold:

1. Team G publishes an exact SHA on `phase25-generic-types` and states that its
   G2 foundations pass:
   - Type IR records;
   - Option B open-result summaries, with the `tests/phase25/g0/conformance`
     cases passing;
   - the `MemberDispatchRequirement` hook point;
   - `moss_runtime` with the policy-free `MossMap<K,V>` API (§1).
2. That SHA passes `make check` on its own.
3. Team G's `MapKeyProof` record exists, even with only built-in keys.

Team S then:
1. moves its two runtime modules into `moss_runtime` (§1.3);
2. wires the built-in proof targets (§2) and the nominal key-method resolution;
3. runs `python3 tests/phase25/key_protocol/run_fixtures.py --activate` and
   turns every pending fixture into a passing three-engine regression: Float
   keys, imported nominal keys, two consumers of one provider Map,
   `KEY_PROOF_COHERENCE_MISMATCH`, eager `keys()` snapshots, and Map and Set on
   one protocol;
4. lands `admit_member` against the joint #12 suite.

Until then, `Map[Float, V]` and user-keyed Maps keep failing closed
(`FLOAT_MAP_KEY_UNSUPPORTED`) and the fixtures run in pending mode.

**Handoff to Team G (Option B):** the unpinned empty Map fixture under
`tests/phase25/key_protocol/` is the failing reproducer. Team S adds no local
inference workaround for it.

## Concurrence

| Party | Item(s) | Verdict | SHA | Notes |
|---|---|---|---|---|
| Team S L6 Coordinator | 1–5 | Proposed | this document's commit | — |
| Team G L6 Coordinator | 1–5 | **pending** | — | — |
| L7 | §3 4 GiB failure form; Set scope | **pending** | — | Set remains a Phase 25 deliverable unless L7 defers it |
