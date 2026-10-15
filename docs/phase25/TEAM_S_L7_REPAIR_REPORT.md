# Phase 25 Team S — L7 Repair Report

**Branch:** `phase25-semantics-engines`
**Reviewed SHA (L7 NO-GO):** `983acb2135d2aea6f5aecc9aa35005525c2f40f4`
**Submission SHA:** recorded in the final section after the last validation run
**Baseline:** `fa3a340a0a6652568b97869489da1a84038b9857`
**Language/compiler:** `moss-0.1` / `0.1.0`, `rustc 1.98.1 (48a229cea 2026-09-01)`

This report answers the L7 review of `983acb2` item by item. It states what
is repaired and how it was proven, what is only partly delivered and why, and
which decisions belong to L7 and Team G rather than to Team S. Nothing here
is merged into `main` or an integration branch.

## Summary

| L7 item | State |
|---|---|
| 1. IEEE Float `==`/`!=` | **Repaired.** Fast Debug was wrong (native was right). NaN/signed-zero differential regressions added. |
| 2. Indexed String | **Implemented and proven, staged.** `MossString` indexed-v1 is linear on 1M-code-point Unicode JSON (0.016 s vs 67.8 s). It is not the default: switching it on is the joint ABI-v10 cut, and cross-crate use needs the shared runtime crate (item 4). |
| 3. `MossMap` / `key-protocol-v1` | **Competing adapters retracted.** `MossFloatKey`, the `MossLess`/`MossIsequal`/`MossHash` trait layer, and the `MossKey_T` design are gone. `Map[Float, V]` fails closed until Team G's `MossMap<K,V>` lands. Scalar `less`/`isequal`/`hash` are the Team S protocol targets. |
| 4. ABI-v10 | **Solo bump reverted.** Native ABI is v9 again with its original migration tests. Team S's String/Char contributions are specified for the joint transition below. |
| 5. Fast Debug | **Repaired:** range overflow, iteration cap, block scoping (with native join semantics and `on_fail` capture), plus parity bugs found on the way (escaped quotes, Char delimiter literals) and a FileIO preflight. |
| 6. Ledger reproducers | **Re-run** against baseline, reviewed, and repaired compilers (table below). #10 was *not* fixed at `983acb2`; it is now. #12's open-receiver form remains the joint Team G G2 item. |
| 7. Acceptance gates | Purity, ordering laws, source-free, Unicode conversion, and performance gates are implemented and pass. Gaps are listed under "Outstanding limitations". |

The G0 architecture boundary is respected: no runtime trait dispatch (the
static Rust trait layer for scalar ordering was removed as well), no
source-level generic type parameters, and no Phase 25.5 D1/D2 work.

## 1. IEEE Float `==` / `!=`

**Defect.** `983acb2` routed Fast Debug's operator `==` for Float through
`moss_isequal`, so `NaN == NaN` was `true` and `0.0 == -0.0` was `false`, while
native code was IEEE-correct. It also changed native `assertEqual` on Float to
total equality, contrary to `docs/TESTING.md`.

**Repair.** `src/interpreter.hpp` `equal()` compares Float with IEEE `==`.
A separate `key_equal()` (total `isequal`) is used only for Map key identity,
including the `locate()` path in `src/interpreter_domains.inc`. Native
`assertEqual` on Float is operator equality again.

**Proof.** `check_phase25_l7_repairs.py::test_float_ieee_equality` runs in
Fast Debug, native debug, and native release with identical output: `NaN`,
sign-flipped `NaN`, `±0.0`, `Vector` equality containing them, `isequal`,
`Float.isequal`, receiver `isequal`, and `hash` agreement. Native law tests
cover quiet, signaling, and negative NaN bit patterns.

## 2. Indexed String representation (G4)

**Defect.** `983acb2` defined a `MossString` struct but never used it; native
`char_at` scanned from the start and `length()` was `chars().count()`, so a
JSON cursor loop was quadratic.

**Repair.**

- Generated code now names the Moss String type `MossStr` and calls one
  runtime helper per operation (`__moss_string_length/char_at/slice/find`).
- `MossStr = String` by default. This is the unchanged ABI v9 representation
  with identical semantics.
- `MOSS_STAGED_STRING_REPR=indexed-v1` selects `MossStr = MossString`:
  - UTF-8 bytes, an eager code-point count, and a `u32` byte offset for each
    64th code point;
  - `length()` is O(1) and `char_at()` decodes at most 63 scalars;
  - `slice`/`find` use the offsets;
  - `append` scans only the appended part (amortized linear construction).
- The staged choice enters the backend fingerprint (cache keys) and every
  `.mossi` header (`string_representation indexed-v1`). Mixing
  representations fails with `STRING_REPRESENTATION_MISMATCH`.

**Proof.**

- `check_phase25_indexed_string.py` runs:
  - 12,800 randomized indexed-vs-plain comparisons of `length/char_at/slice/find`
    after random Unicode appends, with the offset invariants checked;
  - a String-heavy program whose output is identical in Fast Debug and in
    plain and staged native debug/release;
  - fail-closed mixing of plain and staged artifacts.
- `tests/benchmarks/phase25_unicode_traversal.py` gives the release-mode
  results in [`TEAM_S_UNICODE_STRING_BENCHMARK.md`](TEAM_S_UNICODE_STRING_BENCHMARK.md):
  - plain scales ≈100× per 10× (quadratic; 67.8 s at 1M code points);
  - indexed scales ≈8–11× per 10× (linear; 0.016 s at 1M code points).
- Metadata costs 1.6–7.1% of payload, plus 32 bytes per String value
  (56 vs 24).

**Why it is staged, not the default.**

1. Changing the native String layout changes the source-free boundary. L7
   requires that to land in the single coordinated ABI-v10 transition.
2. Each generated crate embeds its own runtime prelude. A provider's
   `MossString` and a consumer's are therefore different Rust types; a
   two-package staged build failed in rustc for this reason. Cross-crate
   indexed Strings need one compiler-owned runtime crate linked by every
   provider and consumer. That is the same placement Team G's G0 prototype
   uses for `MossMap<K,V>`. Building a second such mechanism on the Team S
   branch would repeat the competing-architecture problem from item 3.
   Staged builds are limited to single-crate programs and fail closed with
   `STRING_REPRESENTATION_STAGED_SINGLE_CRATE` before writing any artifact.

## 3. `MossMap<K,V>` and `key-protocol-v1`

Retracted from `983acb2`:

- `MossFloatKey` (a type-owned Rust `Eq`/`Hash` key adapter).
- `HashMap<MossFloatKey, V>` lowering.
- The Rust `MossLess`/`MossIsequal`/`MossHash` trait layer behind the
  `less`/`isequal`/`hash` builtins.
- The `MossKey_T` design in G0 Proof 2, marked superseded in
  `G0_JOINT_ARCHITECTURE_PROOFS.md`.

What Team S delivers toward the canonical design:

- **Key-protocol targets for the built-in scalars.** `less`, `isequal`, and
  `hash` for Int, Float, Char, String, and Bool lower to exact per-type
  functions. Float has the approved Julia-like semantics: `-0.0 < +0.0 < NaN`,
  `isequal(NaN, NaN)`, `isequal(-0.0, +0.0) == false`, distinct zero hashes,
  and one canonical NaN hash. Operands must share one scalar type
  (`KEY_PROTOCOL_TYPE`), and a user function named `hash`/`less`/`isequal`
  is never hijacked.
- **Laws, verified in all three engines over boundary values.** Irreflexive,
  asymmetric, total, and transitive `less`; reflexive, symmetric, and
  transitive `isequal`; equal keys have equal hashes. Native Float laws are
  also checked over NaN sign and payload bit patterns.
- **`Map[Float, V]` fails closed.** It is rejected with
  `FLOAT_MAP_KEY_UNSUPPORTED`, in index, assignment, and declared-type
  positions, until `MossMap<K,V>` exists. Nothing reaches rustc.
- **`sort(v)`, `sort(v, by: key)`, `sort(v, by: Reverse(key))`.** These are
  stable sorts under the key type's total `less`. The key must be a named
  function whose specialization has no observable effect (`SORT_KEY_IMPURE`),
  so key evaluation counts are unobservable in every engine. `sort` writes its
  place (WRITE effect), works through parameters and in tail position, and
  `Reverse` is only valid as a sort key.

Not delivered (it depends on Team G's `MossMap<K,V>`): Map/Set with
user-defined keys, Float Map/Set keys, and the Set kernel.

## 4. ABI-v10

- `kNativeAbiVersion` is 9 again. The `ABI_VERSION_MISMATCH` diagnostics that
  claimed a String/Char representation change that had not happened are
  removed. The five ABI-v9 migration tests weakened at `983acb2` are restored
  byte for byte.
- `Char` lowers to Rust `char` (a fixed 32-bit layout) and is declarable in
  fields, parameters, returns, and collections. It crosses source-free
  boundaries today without a layout change.

**Team S contribution to the joint ABI-v10 transition (proposal for L7/Team G).**

1. Host `MossString` (indexed-v1) and `MossMap<K,V>` in the same
   compiler-owned runtime crate, whose identity the v10 `binary_contract`
   records. Generated crates then reference `moss_runtime::MossString`
   instead of embedding a copy.
2. Replace the staged `string_representation indexed-v1` header line with a
   versioned String record in the v10 `binary_contract`.
3. At the cut, flip the default `MossStr` to `MossString` (a one-line change
   in `char_string_ordering_runtime_rust`), remove the staging switch, and
   rebuild the SDK.
4. The +32 bytes per String value should be weighed at that decision.

## 5. Fast Debug

All of the following were confirmed against native output before repair and
are covered by three-engine regressions in `check_phase25_l7_repairs.py`:

| defect at `983acb2` | repair |
|---|---|
| `for i in range(top - 2, top)` near `Int` max wrapped (signed overflow) and looped until the safety cap | Overflow-safe stepping that matches Rust `start..end` / `step_by` |
| `while` and `for` aborted after 10,000,000 iterations; native code does not | Cap removed (10,000,001-iteration regression) |
| No block scoping: `let y` in any `for`/`while`/`if` body overwrote the outer `y`, and later control flow diverged | RAII block scopes restoring shadowed bindings |
| — introduced by scoping, found by `make check` — names bound in every branch of `if`/`else`, `match`, or `try`/`recover` must outlive the statement (native hoisting); `on_fail` captures of try-body bindings; reinitialising a consumed binding | Join-aware scopes; edge bindings carried on the raised signal; consumed-name tracking |
| `s.char_at(i) == '"'` failed in both engines (Char literal holding a delimiter read as syntax) | Lexer canonicalizes such literals to `'\u{HEX}'` |
| `s.find("\"c")` and `"b\"" == x` failed in Fast Debug only (scanners ignored escapes; one scanned backwards with forward logic) | Escape-aware scanners in both directions |
| `match Shape.Square(side: 4):` failed in rustc (unparenthesized struct literal) | Scrutinee always parenthesized |
| FileIO failed mid-run after earlier output | Static preflight (`FAST_DEBUG_FILEIO`) from `main` reachability before any statement runs |

## 6. Original ledger reproducers

`tests/phase25/ledger/run_ledger.py` states the correct behaviour for each
ledger entry and runs `moss check`, Fast Debug, and native debug for each
compiler. Reproducers for #10 and #14 are
package builds through each compiler's own `margo`. "pass" means `check`,
Fast Debug, and native debug all match the stated behaviour.

| reproducer | baseline-fa3a340 | reviewed-983acb2 | repaired |
|---|---|---|---|
| #1 Int ordering above 2^53 | Fast Debug false,false,true,false | pass | pass |
| #2 ordering on user types rejected | accepted (should reject) | pass | pass |
| #3 one type per binding | accepted (should reject) | pass | pass |
| #4 parameter-based indexed assignment | native rustc failed | pass | pass |
| #5 repeated discard | native rustc failed | pass | pass |
| #6 assert text with braces | check rejects: assert condition must have type 'bool' | pass | pass |
| #7 Float.to_string | Fast Debug moss:3: interpreter error: method receiver is not a Moss obj | pass | pass |
| #8 for traversal | Fast Debug moss:4: interpreter error: Fast Debug does not support for i | pass | pass |
| #12d local collection member call | pass | pass | pass |
| #12 open receiver (joint Team G G2) | check rejects: local member calls are not implemented; use a top-level function | check rejects: local member calls are not implemented; use a top-level function | check rejects: local member calls are not implemented; use a top-level function |
| #15 local field member call | check rejects: local member calls are not implemented; use a top-level function | pass | pass |
| #10 cross-package constructor label | native: unknown field 'net__row_count' in net__Table constructor; Fast Debug: moss:10: error: [UNKNOWN_SYMBOL_OR_TYPE] unknown field 'net__row_count | native: unknown field 'net__row_count' in net__Table constructor; Fast Debug: moss:10: error: [UNKNOWN_SYMBOL_OR_TYPE] unknown field 'net__row_count | pass |
| #14 raising call in bench | margo bench failed | pass | pass |

- **#10 was still broken at `983acb2`.** Its regression used a same-module,
  non-package file with `moss resolve`, which never reached the module
  namespace rewriter. The rewriter qualified `row_count` in
  `Table(row_count: n)` because a same-module function shares the name. It
  now leaves named-argument labels alone (`named_argument_label`).
- **#4 passes in its original parameter form.** `983acb2` already lifted the
  index expression; the reviewed regression only exercised a local `var`, and
  the parameter form is now in `check_phase25_l7_repairs.py`.
- **#12 open receiver** is unchanged in all three columns. It is the joint
  Team S/Team G `MemberDispatchRequirement` item and depends on the G2 Type IR.

## 7. Acceptance gates

| gate | evidence |
|---|---|
| Purity | `sort` keys: `SORT_KEY_IMPURE` from the checked observable-effect summary; I/O, messages, domain access, mutation, panics, raises, divergence, and unresolved effects are all rejected. |
| Ordering laws | Three-engine law programs for Float, Int, Char, String, Bool; native Float laws over NaN payloads. |
| Source-free | A compiled-only provider exporting `sort ... by:`, `Char`, `less`, and `hash` gives the same results; #10 runs across packages. |
| Unicode conversion | `Char(cp)` boundaries (0, U+D7FF, U+E000, U+10FFFF); surrogates and out-of-range values fail in every engine; `codepoint`, `to_string`, `char_at`, `chars`, `Char == "x"`. |
| Performance | Linear indexed traversal and measured metadata (benchmark document). |
| 64-bit | Exact `Int` ordering above 2^53 in all engines (#1). |

## Outstanding limitations

- **Indexed String is not the default** (item 2). It waits on the joint ABI-v10
  cut and the shared runtime crate.
- **Map/Set with user-defined or Float keys** wait on Team G's `MossMap<K,V>`.
  Set is not delivered.
- **#12 open receiver** (`var v = []; v.push(1)`) is still rejected. It is the
  jointly owned `MemberDispatchRequirement` work that depends on Team G's G2
  Type IR. Team S will own admission once that IR exists.
- **`==` on user structs** is accepted by `moss check` and runs in Fast Debug,
  but native lowering fails in rustc (no `PartialEq`). This pre-existing
  defect was found during this pass and not repaired, to keep scope.
- **`Char(cp)` failures** stop every engine but report differently (Fast Debug
  error vs native panic, exit 1 vs 101). A typed recoverable error would be
  a language decision for L7.
- **Transparent `Char == String` equality**, added in `983acb2`, was not in
  any approved G0 proof. It is retained because nothing depends on it in
  key positions (Map keys have one type), but L7 should confirm or remove it.
- **Interpreter maps** are vectors of pairs (linear lookup). This is
  pre-existing and not a parity issue.
- **Fast Debug FileIO preflight** uses identifier reachability, so a
  same-named but unrelated identifier can make it reject conservatively.

## Validation

VALIDATION_SECTION

## Coordination requests

1. **Team G.** Confirm placement of `MossString` in the compiler-owned runtime
   crate that will host `MossMap<K,V>`. Confirm that the scalar
   `less`/`isequal`/`hash` lowering in `scalar_protocol_rust` is the
   `key-protocol-v1` target for built-in keys. Schedule #12 admission against
   the `MemberDispatchRequirement` record.
2. **L7.** Schedule the single ABI-v10 cut including the String record. Decide
   on transparent `Char == String` equality and on a typed `Char(cp)` failure.
   Note that Set remains blocked on `MossMap<K,V>`.
