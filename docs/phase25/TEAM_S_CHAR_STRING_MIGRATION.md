# Phase 25 — Char/String contract: migration notes

**L7 decision (this cycle):** `Char` is a distinct scalar type. Transparent
`Char == String` equality and mixed-type `isequal`/`assertEqual` are removed.
A String is obtained from a Char only with an explicit `c.to_string()`.

## What changed

| Form | Before (`983acb2`…`ee8dcea`) | Now |
|---|---|---|
| `c == "x"`, `"x" == c`, `!=` | true when the String held exactly that one code point | `TYPE_MISMATCH` at `moss check` |
| `assertEqual(c, "x")` | accepted | `TEST_ASSERTION_CONFIGURATION_ERROR` (the existing mixed-type assertEqual code) |
| `isequal(c, "x")`, `less(c, "x")` | `KEY_PROTOCOL_TYPE` | unchanged (`KEY_PROTOCOL_TYPE`) |
| `chars() == ["a", "b"]` (`Vector[Char]` vs `Vector[String]`) | accepted by the checker, failed in rustc | `TYPE_MISMATCH` |
| `chars() \|> filter(_ == "a")` | accepted | `TYPE_MISMATCH` |
| record `a == b` / `a != b` | accepted by the checker; ran in Fast Debug; rustc E0369 | `RECORD_EQUALITY_UNSUPPORTED` |
| `assertEqual(record, record)`, including records inside a `Vector` | `TEST_ASSERTION_CONFIGURATION_ERROR` or rustc failure | `RECORD_EQUALITY_UNSUPPORTED` |
| `1 == "a"`, `true == 1`, `[1] == [1.5]` | accepted by the checker, failed in rustc | `TYPE_MISMATCH` |
| `x = if c: 1 else: 2`, `x = foo bar` | accepted by the checker, failed in both engines | `INVALID_EXPRESSION` |
| `c + "x"`, `'a' + 'b'`, `true + 1`, `"a" - "b"`, `[1] + [2]`, `-c` | accepted; Fast Debug printed an invented value, rustc failed | `TYPE_MISMATCH` (P25-S-ARCH-013); build Strings with `c.to_string() + "x"` |
| a Map key that is a Map, enum, or collection of enums/records/Floats | accepted; rustc failed | `MAP_KEY_TYPE_UNSUPPORTED`, `ENUM_MAP_KEY_UNSUPPORTED`, `RECORD_MAP_KEY_UNSUPPORTED`, `FLOAT_MAP_KEY_UNSUPPORTED` |
| `Char(codepoint: 65)` and other labelled builtin arguments | accepted; failed in both engines | `CALL_NAMED_ARGUMENT_UNSUPPORTED` (P25-S-ARCH-016) |
| `r = range(0, 3)`, `range(0, 3) == range(0, 3)` | accepted; failed in both engines | `RANGE_OUTSIDE_FOR` (P25-S-ARCH-017) |

Unchanged:
- `Char == Char` and Char ordering.
- `Int`/`Float` mixed comparison.
- Equality on `Vector`, `Queue`, and `Map` of equality-comparable element types.
- `Range == Range` between FileIO `Range` values (byte-exact; native only, since
  Fast Debug does not execute FileIO). A `range(...)`
  call is not a value: it is valid only as a `for` source (`RANGE_OUTSIDE_FOR`,
  P25-S-ARCH-017).
- `Char(Int)` validation: surrogates, negative values, and values above
  U+10FFFF stop every engine before a Char exists.
- `char_at() -> Char` and `chars() -> Vector[Char]`.

## How to migrate

```moss
# before
if path.char_at(0) == "/":
assertEqual(text.char_at(1), "é")

# after: compare with a Char literal …
if path.char_at(0) == '/':
assertEqual(text.char_at(1), 'é')

# … or convert explicitly when a String is really needed
if text.char_at(1).to_string() == prefix:
```

Escapes in Char literals follow the String escape set (`'\\'`, `'\''`,
`'\n'`, `'\u{1F600}'`, `'\x41'`).

Records: compare their fields explicitly (`a.x == b.x and a.y == b.y`).
No equality is synthesized for records.

## In-repo callers migrated

- `sdk/path/src/path.moss`: four `char_at`/`chars()[i]` comparisons now use Char
  literals.
- `sdk/tables/src/csv_adapter.moss`: two `'\\'` comparisons.
- `sdk/text-encoding-acceptance/benches/{text_encoding,case_builder}.moss`:
  14 `assertEqual(… .char_at(i), …)` checks.
- `tests/phase15_15_string_map.moss`, `tests/tooling/check_phase25_g4_unicode.py`,
  and `tests/tooling/check_phase25_l7_repairs.py`.

All 1,831 `.moss` files tracked at the time of the S2 change (`f272af9`) were checked with the starting compiler
(`ee8dcea`) and with the new one. The only files the new compiler rejects are
the Char/String callers above, and those are now migrated.

## Diagnostics that differ by engine (for L7)

An invalid dynamic `Char(Int)` stops all three engines before the Char is used,
and nothing after it runs. The report still differs by engine:

- Fast Debug: `interpreter error: ValueError: invalid Unicode scalar value N`,
  exit 1.
- Native: a Rust panic, `Char codepoint out of Unicode scalar range: N`,
  exit 101.

A typed, recoverable conversion error was not introduced in this cycle, per
L7 instruction.

## Evidence

`tests/tooling/check_phase25_s2_char_string.py`, registered in `tests/run.sh`.
It runs:
- the rejection cases above, which stop every engine before execution;
- Char(Int) boundaries and fail-before-use;
- escaped-literal parity across 14 escapes in four forms;
- a seeded randomized Unicode property test (`MOSS_S2_SEED`) with roughly 900
  checks against a Python oracle, in Fast Debug and in plain and indexed-v1
  native debug/release.
