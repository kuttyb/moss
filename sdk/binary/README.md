# moss.binary

`moss.binary` provides owned, bounds-checked byte readers and writers over the
Phase 24 public byte representation, `Vector[Int]` with every element in
`0..255`. The source module is `binary`; import it as `import binary`.

The package depends only on `moss.bytes`. It uses arithmetic division and
remainder for binary encoding and does not depend on bitwise syntax, native
layout, generated Rust, or lossy numeric conversion.

## Limits and ownership

`new_reader` validates and copies its input. `reader_input` and
`writer_output` also return independent copies. Public records can be forged in
Moss v0.1, so every exported operation validates all record invariants before
indexing or mutation. `validate_reader` and `validate_writer` provide
non-raising checks for untrusted records.

The default total input/output ceiling is **64 MiB** (`67,108,864` bytes). The
default ceiling for one length-delimited payload is **16 MiB** (`16,777,216`
bytes). Custom constructors accept positive limits no larger than those
ceilings, and a payload limit cannot exceed its enclosing total limit.

All failing public mutators are atomic. A failed reader operation leaves its
cursor unchanged, and a failed writer operation appends no bytes.

## API

Construction and state:

- `new_reader(values)` and `new_reader_with_limits(values, input_limit, payload_limit)`
- `new_writer()` and `new_writer_with_limits(output_limit, payload_limit)`
- `reader_cursor`, `reader_remaining`, `reader_input`
- `writer_length`, `writer_output`
- `require_finished` rejects trailing input
- `validate_reader`, `validate_writer`

Byte and integer operations:

- `read_byte`, `read_bytes`, `write_byte`, `write_bytes`
- `read_u16_be/le`, `read_u32_be/le`, `read_u64_be/le`
- `write_u16_be/le`, `write_u32_be/le`, `write_u64_be/le`
- `read_i16_be/le`, `read_i32_be/le`, `read_i64_be/le`
- `write_i16_be/le`, `write_i32_be/le`, `write_i64_be/le`

Unsigned values are nonnegative Moss `Int` values. Unsigned 64-bit wire values
above `INT64_MAX` raise `BinaryError.Overflow`. Signed operations use exact
two's-complement interpretation and cover `INT64_MIN` without negating it.

`read_uvarint` and `write_uvarint` use canonical unsigned base-128 varints for
`0..INT64_MAX`. Decoding consumes at most ten bytes and distinguishes truncated,
overlong, nonterminating, and overflowing inputs. `read_length_delimited` and
`write_length_delimited` prefix an owned byte payload with that canonical
varint encoding and enforce the configured payload and total limits.

`BinaryError` distinguishes invalid bytes and records, invalid limits, invalid
lengths/values, unexpected EOF, malformed and overlong varints, integer
overflow, trailing input, and resource-limit excess. Offset-bearing variants
report the cursor or output position at the failing public boundary.

## Complexity

Constructors and output/input copies are O(n). Length-delimited operations are
O(n) in the payload size. Varints inspect at most ten bytes. Fixed-width codec
work is O(1) for the encoded width.

Because public Moss records are forgeable and the frozen Team A contract
requires validation before every public access, operations also perform O(n)
record validation over the current input or output storage. Therefore the full
public-call cost is O(n + k), where `n` is current storage and `k` is copied or
appended payload size. No zero-copy, borrowed-view, packed-byte-layout, or
amortized multi-write claim is made.

From the repository root, run the package gates with:

```sh
(cd sdk/binary && ../../margo test --json)
(cd sdk/binary && ../../margo test --release --json)
(cd sdk/binary && ../../margo test --interp)
```
