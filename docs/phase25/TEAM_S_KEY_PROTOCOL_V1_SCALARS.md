# Phase 25 Team S — key-protocol-v1: built-in scalar targets (S3.1)

**Branch:** `phase25-semantics-engines` (S3 leaf work, starting SHA `ee8dceaa4851fb61a7b726cee05354da6917e38d`)
**Language/compiler:** `moss-0.1` / `0.1.0`
**Status:** Team S contribution to `key-protocol-v1`, frozen for the built-in
scalars Int, Float, Char, String, and Bool. Nominal (user) keys, `KeyProof`
records, `MossMap<K,V>`, and Set belong to Team G's G2/G3 work and are not
specified or implemented here.

This document fixes what `less`, `isequal`, and `hash` mean for the five
built-in scalar key types, where each engine implements them, and which
properties the tests prove. It is the scalar half of the protocol that the
Team G contracts reference: `TYPE_IR_CONTRACT.md` §8 and
`ABI_V10_DESIGN.md` §3.5 on `origin/phase25-generic-types`, and
`TEAM_S_G0_IMPLEMENTATION_OWNER_CONCURRENCE.md`.

## 1. Signatures and effects

| operation | protocol signature | source spellings |
|---|---|---|
| total order | `less : (READ K, READ K) -> Bool` | `less(a, b)`, `a.less(b)`, `K.less(a, b)` |
| key equality | `isequal : (READ K, READ K) -> Bool` | `isequal(a, b)`, `a.isequal(b)`, `K.isequal(a, b)` |
| key hash | `hash : (READ K) -> Int` | `hash(a)`, `a.hash()`, `K.hash(a)` |

- `K` is one of Int, Float, Char, String, Bool. Both operands of `less` and
  `isequal` must have the same scalar type. Any other operand, including a
  record, a Vector, or a mixed Int/Float, Char/String pair, is rejected with
  `KEY_PROTOCOL_TYPE` before lowering. There is no implicit conversion; this
  differs on purpose from operator `==`, which converts Int to Float and
  compares Char with a one-scalar String.
- All three operations are pure. They read their operands, never write,
  consume, raise, panic, diverge, message, touch domain state, or perform
  I/O. A sort key that calls them is accepted as pure (checked by
  `check_phase25_s3_key_protocol.py`, "protocol-calling keys").
- A user function or method named `less`, `isequal`, or `hash` takes
  precedence over the builtin at its call sites in every engine. It does
  **not** change `sort`, which always uses the builtin total order of the key
  type (§4).

## 2. Lowering targets

There is no Rust trait, adapter type, or dynamic dispatch. Each call lowers to
the exact per-type operation for the statically known type.

| | Native (`src/moss.cpp` `scalar_protocol_rust`, line 17002; runtime in `src/char_string_ordering_runtime.hpp`) | Fast Debug (`src/interpreter.hpp`) |
|---|---|---|
| `less` Float | `moss_float_less(a, b)` (runtime line 32) | `moss_less` (line 1757), Float branch |
| `less` Int, Char, String, Bool | `(&(a) < &(b))` (Rust `Ord`) | `moss_less`: `<` on `int64_t`, code point, `std::string` bytes, `!a && b` |
| `isequal` Float | `moss_float_isequal(a, b)` (runtime line 42) | `moss_isequal` (line 1784), Float branch |
| `isequal` Int, Char, String, Bool | `(&(a) == &(b))` | `moss_isequal`: `==` on the value |
| `hash` Float | `moss_float_hash(a)` (runtime line 51) | `moss_hash` (line 1816) |
| `hash` Int | `(a)` | `val.integer` |
| `hash` Bool | `(if a { 1i64 } else { 0i64 })` | `val.boolean ? 1 : 0` |
| `hash` Char | `((a) as u32 as i64)` | `val.codepoint` |
| `hash` String | `moss_string_hash(&(a))` (runtime line 74) | inline FNV-1a loop in `moss_hash` |
| `sort` comparator | `total_order_compare_rust` (line 17023): `moss_float_total_cmp` for Float, Rust `cmp` otherwise; `sort_call_rust` (line 17033) emits a stable `sort_by` | `std::stable_sort` with `moss_less` (line 1216) |
| Map key identity (scalar keys) | Rust `HashMap` `Eq`/`Hash` for Int/Char/String/Bool; Float keys rejected (`FLOAT_MAP_KEY_UNSUPPORTED`) | `key_equal` (line 1751): `moss_isequal` for Float, otherwise value equality |

Line numbers are for this branch after the S3 change; the function names are
the stable anchors.

String order is lexicographic over UTF-8 bytes, which equals code-point order.
Fast Debug's `std::string::operator<` compares bytes as `unsigned char`, so the
two engines agree; the law tests include `é`, `😀`, and an embedded `\u{0}`.

## 3. Float semantics

| property | value |
|---|---|
| operator `==`, `!=` | IEEE: `NaN == NaN` is false, `-0.0 == +0.0` is true |
| `isequal(NaN, NaN)` | `true` for every NaN, whatever its sign or payload |
| `isequal(-0.0, +0.0)` | `false` |
| `less(-0.0, +0.0)` | `true` |
| total order | `-inf < negative finite < -0.0 < +0.0 < positive finite < +inf < NaN` |
| NaN in `less` | no NaN is less than another NaN; every non-NaN is less than every NaN |
| `sort` | NaNs sort last and keep their input order (stable) |
| `hash(+0.0)` / `hash(-0.0)` | `0` / `-9223372036854775808` (sign bit only); distinct |
| `hash(NaN)` | `9221120237041090560` (`0x7ff8000000000000`) for every NaN |
| `hash(x)`, other x | the IEEE-754 bit pattern of `x` as a signed 64-bit Int |

**Equal keys have equal hashes.** For Float, `isequal` identifies all NaNs
and nothing else beyond bit equality of non-NaN values, and `hash`
canonicalizes NaN; so `isequal(a, b)` implies `hash(a) == hash(b)`. The
other scalars hash an injective image of the value (identity, code point,
0/1) or FNV-1a of the bytes, so the implication holds trivially.

## 4. `sort`

- `sort(v)`, `sort(v, by: key)`, `sort(v, by: Reverse(key))` sort in place and
  are stable in all three engines (Rust `sort_by` and `std::stable_sort`).
- `sort(v)` needs scalar elements; a record with a structural `less` method is
  still rejected with `SORT_KEY_UNORDERED`.
- A key is a named function, imported or local, typed or untyped. Its return
  type must be a scalar (`SORT_KEY_UNORDERED` otherwise). An untyped key is
  specialized at the element type, and its purity is judged on that
  specialization.
- The key must be pure: `SORT_KEY_IMPURE` for I/O (direct or transitive),
  raise, possible panic, possible divergence, writing its argument, or an
  unresolved effect. A key that consumes a non-Copy argument receives a clone.
  Purity makes key-evaluation count and order unobservable, so the three
  engines may evaluate keys differently and still agree.
- `Reverse` must wrap a named function directly; `Reverse(Reverse(k))` is
  `SORT_KEY_CALLABLE`.

## 5. Hash stability

Measured, not assumed (`check_phase25_s3_key_protocol.py`, "pinned hash
values", expected values computed independently in Python):

- **Within one compiler build, `hash` returns the same Int in Fast Debug,
  Native Debug, and Native Release** for every scalar, including signed zeros,
  NaNs, infinities, `Int` extremes, non-ASCII Chars, and Strings.
- The values in §3 and the formulas in §2 (Int identity, Float bit pattern
  with canonical NaN, Char code point, Bool 0/1, String FNV-1a 64 over UTF-8
  with offset basis `0xcbf29ce484222325` and prime `0x100000001b3`) are the
  `key-protocol-v1` definition.
- They are **not** promised stable across protocol versions; a change
  requires a new `law_contract_version`/protocol version, which `MapKeyProof`
  records. Programs must not persist hash values.
- They are not seeded or randomized, and they are not well distributed for
  Int (identity) or Float (raw bits). The G0 concurrence asked for a
  well-distributed projection; Team S recommends that `MossMap<K,V>` apply its
  own finalizer to the protocol hash rather than change the protocol values.

## 6. Test evidence

`tests/tooling/check_phase25_s3_key_protocol.py` (new, 26 cases) adds to the
L7 suites (`check_phase25_l7_repairs.py`, `check_phase25_g3_ordering.py`):

- **Imported named keys**: typed and untyped keys from another module, from a
  path-dependency package (margo, three engines), and from a compiled-only
  provider (source removed after the first build).
- **Concrete specializations**: one untyped key at Int, Float, Char, String,
  Bool, and a record element.
- **Non-Copy elements and keys**: record and String elements with String,
  Float, and Int keys, including ties that check stability.
- **NaN values**: six NaNs from different computations are pairwise `isequal`,
  hash-equal, unordered, and sort last in input order; a native-only check
  covers eight sign/payload bit patterns Moss source cannot construct, in debug
  and `-O`.
- **Adversarial keys**: user `less`/`isequal`/`hash` free functions and
  methods that break every law; a law checker that reports 0 failures for a
  lawful nominal key and the same nonzero count in every engine for a lawless
  one; constant, Bool, NaN-producing, and signed-zero keys; eleven rejection
  cases (`SORT_KEY_IMPURE`, `SORT_KEY_UNORDERED`, `SORT_KEY_CALLABLE`,
  `KEY_PROTOCOL_TYPE`), each also refused by Fast Debug and native lowering;
  an impure imported key rejected by `moss check`, `margo build`, and
  `margo debug`.

## 7. Defects and limitations found by S3

| ID | finding | state |
|---|---|---|
| P25-S3-D1 | `sort(v, by: lib.key)` with an untyped key imported from another module, used only as a sort key, passed `moss check` and failed in rustc (`cannot find function __moss_specialize_keys__loose_0`). Module projection collected call edges for pipeline stages but not for sort keys, so the consumer crate never emitted the specialization. | **Fixed**: `collect_local_call_sites` in `src/moss.cpp` (line 9076) records the key edge at the element type. Regression: "same-package module key functions". |
| P25-S3-D2 | A `Map` keyed by a record or nominal type passes `moss check`. Native rustc rejects the `HashMap` (no `Eq`/`Hash`), and Fast Debug compares keys structurally, ignoring custom `isequal`/`hash` methods (wrong results, or `missing map key`). This holds for valid, malformed, impure, and missing proofs alike. | **Handed off**, not fixed (Map key admission belongs to `MossMap<K,V>`/KeyProof work). Pinned by fixtures `03`–`07` as `known-failing`. |
| P25-S3-D3 | Empty `Map()` whose K/V are not fixed where it is created: never-inserted `m.get(k, d)`, `Map().get(k, d)`, an open-result helper `fn fresh(): return Map()`, and a never-used `Map()` all pass `moss check` and fail in rustc. Annotated or unannotated domain state `Map()` and a cross-package open result are rejected instead. | **Handed off** to Team G Option B (`TYPE_IR_CONTRACT.md` §4.2). Fixtures `09b`–`09h`. |
| P25-S3-L1 | An untyped key whose body makes a member call (`x.length()`) is accepted from a same-package module but rejected from a path dependency with `SORT_KEY_IMPURE` "unresolved effect": the `.mossi` template of an untyped function carries an unresolved effect summary. Fail-closed. | Pinned by `test_untyped_member_key_across_packages`; belongs to open summaries with effects (Team G G2). |
| P25-S3-L2 | Purity is conservative: `x % 10`, `x / 10`, `s.char_at(i)`, and `xs[i]` count as possible panics, and `c.codepoint()` on a `for` element of a bound `Vector[Char]` is unresolved. Such keys are rejected. The G0 concurrence's own `UserKey` example (`code % 10`) has `may_panic=1` in its `.mossi` record and would therefore fail the protocol's purity rule as now computed. | Fail-closed; Team G/L7 should decide whether integer `%`/`/` by a nonzero literal is panic-free before freezing proof purity. |
| P25-S3-L3 | `parts.push(item.field)` (or `let l = item.field`) for a non-Copy field of a `for` loop element passes `moss check` and fails in rustc (`cannot move out of ... behind a shared reference`). Unrelated to the key protocol; the S3 tests index instead. | Recorded, not fixed (outside S3 scope). Reproducer in the S3 report. |
| P25-S3-L4 | `moss check <project-directory> --json` reports `ok` with no diagnostics without checking the project; `moss check src/main.moss` does check it. | Recorded (tooling). |

## 8. Map/Set acceptance fixtures

`tests/phase25/key_protocol/` holds the Map/Set fixtures for the full
protocol (Float keys, imported nominal keys, two consumers of one
provider-owned Map, coherence and malformed/forged proofs, `keys()`
snapshots, Map and Set sharing one protocol, and the S1.5 unpinned-empty-Map
cases). Its README states the activation trigger. Set uses a minimal
spelling (`Set()`, `insert`, `contains`, `values()`) marked
**PROVISIONAL — pending Team G G2 Set API**, because no frozen Team G document
specifies one.
