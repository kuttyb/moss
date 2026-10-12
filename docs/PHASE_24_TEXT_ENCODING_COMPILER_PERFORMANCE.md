# Phase 24 Team T compiler/runtime performance analysis

This note records the P4 generated-code investigation on the fixed sprint base
`4a9b9be12fd230dcbd0b176d539937c29248d2f4`. The baseline command was
`../../margo bench --release --json` in `sdk/text-encoding-acceptance`; all path
providers were release artifacts. The machine had no `perf`, Valgrind,
Heaptrack, or Hyperfine executable, so attribution uses emitted release Rust,
matched algorithm decomposition, and benchmark deltas rather than invented
allocation counts.

## Generated-code findings

| Operation | Baseline emitted work | Root cause |
| --- | --- | --- |
| `text.find` | Rust `str::split`, `to_string` for every field, collect a `Vec<String>`, count it, then count prefix scalars | SDK algorithm selected an all-match, all-field operation for a first-match query. |
| `text.slice` | Materialize every scalar as a separately owned `String`, collect a vector, clone every selected scalar into another vector, then join | The original String surface exposed scalar materialization but no boundary-preserving owned substring operation. |
| ASCII case | Materialize all scalars, repeatedly call SDK search, allocate mapped one-scalar Strings, then join | The SDK had no representation-level ASCII transform. |
| UTF-8 validation | Bounds-checked `Vec<i64>` indexing and one external `bytes__is_byte` call per visited byte | `Vector[Int]` intentionally uses 64-bit integers. The avoidable part is the tiny cross-package call; stable ABI-v9 exports inhibit Rust cross-crate inlining. |
| Builder append | `format!("{}{}", part, "".to_string())` for `part + ""` | A semantic READ-to-owned copy was expressed through general concatenation and paid formatter setup rather than a direct clone. Final `join` already performs one reserved result construction in Rust's standard library. |

The fixed-base medians were 12,316 ns for the complete 256-code-point find,
15,819 ns for the complete 256-code-point slice, 27,707 ns for complete ASCII
lowercasing, 1,679 ns for complete validation, 14,400 ns for prebuilt find x8,
145,538 ns for prebuilt slice x16, 100,405 ns for prebuilt ASCII lower x4, and
50,977 ns for prebuilt validation x64. These are retained as historical inputs,
not claimed as post-change results.

## Targeted compiler surface

Four read-only String built-ins remove the demonstrated representation detours:

- `find(pattern) -> Int` uses Rust `str::find` once and converts only the matched
  byte prefix to a Unicode code-point count. Empty pattern is zero; absence is
  `-1`.
- `slice(start, length) -> String` validates signed code-point bounds, walks
  ASCII input by direct byte offsets (where bytes and code points are identical),
  otherwise walks `char_indices` once to two UTF-8 boundaries, and makes one
  owned substring copy. Invalid bounds return the empty String, matching
  `moss.text`.
- `ascii_lower()` and `ascii_upper()` use Rust's ASCII-only String transforms;
  non-ASCII Unicode scalars are unchanged.
- `String + ""` and `"" + String` lower to a direct clone. General String
  concatenation retains its established semantics and lowering.

The compiler-owned descriptor table is the common type/effect/completion
surface. Native lowering and Fast Debug have matching implementations. Bootstrap,
the practical/formal language documentation, focused Unicode/bounds/type tests,
and generated-Rust shape assertions are updated together. No byte-offset API,
unsafe view, source annotation, or ownership/effect exception was introduced.

## Cross-package optimization boundary

An experiment added release `#[inline]` to generated ordinary functions. Rust
1.98.1 rejected every case under `-D warnings`: `#[inline] is ignored on
externally exported functions`, because ABI-v9 functions use `#[export_name]`.
Removing that export would weaken source-free compatibility. Supporting both
stable native symbols and Rust inlining would require a two-symbol design: an
inline Rust function plus an exported ABI wrapper, with careful signature and
error-ABI forwarding. That is a broader codegen/ABI migration and is not hidden
inside this focused sprint.

Consequently, the SDK's local byte-range check is the narrow UTF-8 validation
optimization. It retains meaningful `moss.bytes` interoperability elsewhere and
avoids pretending the compiler can inline through the current stable export.

P1's first integrated bound-pattern matrix found long ASCII slice x128 at
270,433 ns versus 127,786 ns for idiomatic Rust (2.12x), while the multibyte
case was already 141,069 ns versus 142,102 ns (0.99x). This isolated the two
code-point-index branches per ASCII byte in the general `char_indices` path.
The guarded `is_ascii()` path above removes that avoidable traversal without
changing the multibyte algorithm or exposing byte indexing to Moss source.

## Coordination risk

`origin/phase24-integration` advances the same `src/moss.cpp` file after the
sprint base. Its changes are confined to mixed Int/Float comparison and
arithmetic casts around lines 18,074–18,130. This work changes the built-in
descriptor table, String concatenation/member lowering, and bootstrap surface;
the conceptual changes do not overlap, but Team I must merge `src/moss.cpp`
carefully rather than replace either side.
