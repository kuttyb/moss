# moss.text

`moss.text` provides code-point-oriented inspection, search, extraction,
ASCII-scoped transformation, tokenization, joining, and segment-based text
construction for Moss v0.1.

Add the package to a consumer manifest:

```toml
[dependencies]
"moss.text" = { path = "../sdk/text" }
```

Then `import text` in Moss source.

## Index and Unicode contract

Every index and length is a zero-based Unicode code-point count, matching
`String.length()`, `String.char_at()`, and `String.chars()`. Indices are not
UTF-8 byte offsets and not grapheme-cluster positions. A combining sequence can
therefore be sliced between its scalar components.

This package does not claim Unicode normalization, locale-aware casing,
case-folding, collation, word segmentation, or grapheme segmentation. Its trim,
token, and case functions are deliberately ASCII-specific and leave non-ASCII
characters unchanged.

## API

| Function | Contract |
| --- | --- |
| `starts_with(value, prefix) -> Bool` | Code-point prefix test; the empty prefix matches. |
| `ends_with(value, suffix) -> Bool` | Code-point suffix test; the empty suffix matches. |
| `find(value, pattern) -> Int` | First code-point index or `-1`; empty pattern returns `0`. |
| `rfind(value, pattern) -> Int` | Last code-point index or `-1`; empty pattern returns `value.length()`. |
| `contains(value, pattern) -> Bool` | True exactly when `find` succeeds. |
| `valid_slice(value, start, length) -> Bool` | Exact bounds check, delegated to `moss.core`. |
| `slice(value, start, length) -> String` | Exact code-point slice; invalid bounds return `""`. Use `valid_slice` to distinguish an invalid request from a valid empty slice. |
| `trim_ascii(value) -> String` | Trim ASCII space, tab, LF, and CR from both ends. |
| `replace_all(value, pattern, replacement) -> String` | Replace non-overlapping matches left-to-right; an empty pattern leaves the value unchanged. |
| `split(value, separator) -> Vector[String]` | Built-in split for nonempty separators; an empty separator safely returns `[value]`. Trailing empty fields are preserved. |
| `tokens_ascii(value) -> Vector[String]` | Split runs of ASCII whitespace and omit empty tokens. |
| `to_ascii_lower(value) -> String` | Map `A`–`Z`; preserve every other code point. |
| `to_ascii_upper(value) -> String` | Map `a`–`z`; preserve every other code point. |
| `join(parts, separator) -> String` | Join String parts with the separator. |
| `concat(parts) -> String` | Join String parts without a separator. |
| `new_capacity_builder(capacity_bytes) -> String` | Create a String-backed builder with an initial UTF-8 byte-capacity hint; nonpositive hints behave as zero. |
| `capacity_builder_append(builder, part)` | Append without replacing the builder; the builder is mutated and the part is read. |
| `capacity_builder_append_line(builder, line)` | Append the line followed by LF. |
| `capacity_builder_build(builder) -> String` | Return an independent snapshot while leaving the builder reusable. |
| `capacity_builder_finish(builder) -> String` | Consume the builder and return its accumulated String without a final copy. |

Search and slicing use compiler-owned String primitives that preserve Moss's
Unicode code-point index contract while avoiding per-code-point String vectors.
ASCII case conversion similarly delegates to compiler-owned, Unicode-preserving
String operations: only ASCII `A`-`Z`/`a`-`z` are mapped.

## TextBuilder

`new_builder()` returns a vector-backed segment builder. Add independent segments with
`builder_append` or `builder_append_line`, inspect them with
`builder_segment_count` and `builder_codepoint_length`, then call
`builder_build` for one final join. The builder copies each appended segment so
the caller retains its String; the native lowering recognizes the required
empty-concatenation clone directly instead of routing it through formatting.

```moss
var builder = text.new_builder()
text.builder_append(builder, "hello")
text.builder_append(builder, " ")
text.builder_append(builder, "Moss")
echo text.builder_build(builder)
```

The representation is intentionally the existing `Vector[String]`; Moss v0.1
does not provide an opaque public SDK type. It offers explicit batched
composition, not a mutable String or Unicode rope. On the current native
backend, the corrected release-provider benchmark measured the builder faster
than repeated `String +` for its 128-segment workload. This remains a
workload-specific result, not an unconditional performance guarantee. See the
Team T performance report for the measured tradeoff.

### Capacity-aware builder

The additive capacity-aware builder stores text directly in a mutable `String`.
Its capacity argument is an initial **UTF-8 byte** capacity hint, not a
code-point count and not an output limit. A hint less than or equal to zero is
accepted as zero. Appending preserves the exact String contents and does not
change Moss's code-point index semantics elsewhere.

Use `capacity_builder_build` when the builder must remain reusable: it returns
an independent snapshot and therefore performs a final String copy. Prefer
`capacity_builder_finish` when construction is complete; it consumes the
builder and returns its backing String without that final copy.

```moss
var builder = text.new_capacity_builder(64)
text.capacity_builder_append(builder, "hello")
text.capacity_builder_append(builder, " ")
text.capacity_builder_append(builder, "Moss")
result = text.capacity_builder_finish(builder)
```

The original vector-backed builder API remains available with the same
segment-count and code-point-length inspection contract. The two builder forms
are intentionally separate because changing `new_builder()`'s return value
would break source and source-free ABI consumers.

## Validation

From this directory:

```sh
../../margo test
../../margo test --interp
../../margo --release build
(cd examples/basic && ../../../../margo run)
(cd examples/basic && ../../../../margo --release run)
python3 tests/check_source_free.py
```

The Fast Debug test requires provider source. The source-free test exercises the
compiled ABI-v9 provider through its `.mossi` interface and native artifact.
