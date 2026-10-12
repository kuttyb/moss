# moss.encoding

`moss.encoding` provides strict, pure UTF-8 conversion between owned
`Vector[Int]` byte buffers and Unicode scalar integers. It depends on
`moss.bytes` and validates every untrusted byte before interpreting it.

## Public API

| API | Contract |
| --- | --- |
| `is_unicode_scalar(value: Int) -> Bool` | True exactly for `0..U+10FFFF`, excluding the surrogate range `U+D800..U+DFFF`. |
| `validate_utf8(values: Vector[Int]) -> Bool` | True for exactly the well-formed UTF-8 byte vectors. Values outside `0..255`, overlong forms, surrogates, truncation, invalid continuations, and values above `U+10FFFF` return false. |
| `decode_utf8(values: Vector[Int]) -> Vector[Int]` | Decode strict UTF-8 to Unicode scalar integers. Raises a typed `Utf8Error` at the first invalid byte or sequence. |
| `encode_utf8(scalars: Vector[Int]) -> Vector[Int]` | Encode Unicode scalar integers to canonical UTF-8. Raises `Utf8Error.InvalidScalar` or `Utf8Error.SurrogateScalar` for invalid input. |

`Utf8Error` has these public cases:

- `InvalidByte(index, value)`
- `UnexpectedContinuation(index, value)`
- `InvalidLeadingByte(index, value)`
- `TruncatedSequence(index, expected_bytes, available_bytes)`
- `InvalidContinuation(index, value)`
- `OverlongEncoding(index, scalar)`
- `SurrogateScalar(index, scalar)`
- `InvalidScalar(index, scalar)`

Every index is a zero-based index into the input vector: a byte index while
decoding and a scalar index while encoding. The codec never replaces malformed
input and never accepts byte integers outside `0..255`.

## String interoperability boundary

Moss v0.1 exposes code-point-based String traversal through `length`,
`char_at`, and `chars`, but it does not expose String UTF-8 bytes, construction
of a one-code-point String from an integer scalar, or construction of a String
from a byte vector. Therefore this package does not claim String conversion.
Its complete supported contract is the pure codec layer above. Adding String
interop requires the language/runtime to expose those conversions explicitly.

## Commands

From this directory:

```sh
../../margo test
../../margo test --interp
../../margo --release build
(cd examples/basic && ../../../../margo run)
(cd examples/basic && ../../../../margo --release run)
python3 tests/check_source_free.py
```

The runnable cross-package example is under `examples/basic` and prints `9`
followed by `128578`.
