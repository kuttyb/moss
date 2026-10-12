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

Search is a straightforward code-point comparison with worst-case
`O(value.length * pattern.length)` work. Transformations accumulate parts and
perform one final `join`.

## TextBuilder

`new_builder()` returns a vector-backed segment builder. Add independent segments with
`builder_append` or `builder_append_line`, inspect them with
`builder_segment_count` and `builder_codepoint_length`, then call
`builder_build` for one final join. The builder copies each appended segment so
the caller retains its String.

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
backend, measured repeated `String +` is faster for the 128-segment benchmark,
so the builder is not advertised as an unconditional performance win. See the
Team T performance report for the measured tradeoff.

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
