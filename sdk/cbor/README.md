# moss.cbor

`moss.cbor` provides an owned, iterative CBOR document arena and an RFC 8949
definite-length encoder. The source module is `cbor`; import it with
`import cbor`.

This A10 package intentionally implements the encoder only. A reciprocal
decoder belongs to A11 and is not included here.

## Supported data model

The arena has closed node kinds for null, Bool, signed Moss Int, Float64, UTF-8
text bytes, byte strings, arrays, and maps. The encoder currently supports all
of those kinds except Float64:

- null and Bool use the standard CBOR simple values;
- nonnegative and negative Moss Int values use major types 0 and 1 and the
  shortest legal additional-information width;
- text is exact, strictly validated UTF-8 stored as `Vector[Int]` bytes;
- byte strings preserve exact validated bytes;
- arrays and maps use definite lengths and retain insertion order;
- map keys are limited to Int and text, and duplicate semantic keys are
  rejected.

Tags, indefinite-length values, bignums, non-Int/text map keys, undefined or
other simple values, and Float16/Float32 are outside this subset. This is a
standards-compliant RFC 8949 subset, not a private Moss format and not a claim
of deterministic CBOR map ordering.

Float64 remains blocked because Moss does not yet expose a checked,
bit-preserving Float-to-binary64 conversion with Native and Fast Debug parity.
`add_float`, `float_value`, and encoding a Float node raise
`CborError.FloatCapabilityUnavailable`. Decimal formatting is never used as a
wire substitute.

No native `String` adapter is claimed. Use `add_text_bytes` with strict UTF-8;
`text_bytes` returns an owned copy and `text_scalars` exposes Unicode scalar
values through `moss.encoding`.

## Arena ownership

`CborDoc` stores node attributes in parallel vectors. Every array or map owns a
linked list of global edges through its per-node `first_edges`, `last_edges`,
and `container_counts` fields. An array edge owns one child; a map edge owns one
key/value pair. Every child has exactly one parent, and cycles are rejected.
This linked design keeps a parent's traversal correct when descendants and
later siblings are created in an interleaved order.

Builders create initially detached nodes. `array_push` and `map_set` attach
them exactly once, and `set_root` selects the one unparented root. `validate_doc`
checks a structurally valid construction forest. `validate_complete` additionally
requires every node to be reachable from the selected root; traversal and
`encode` enforce the appropriate validation before indexing.

Public records are forgeable in Moss v0.1, so validation checks all parallel
lengths, kinds, canonical payload ranges, UTF-8, linked edges, parent counts,
cycles, limits, and reachability. Malformed records raise typed `CborError`
variants rather than indexing unchecked storage.

## Public API

Construction:

- `new_doc`, `new_doc_with_limits`
- `add_null`, `add_bool`, `add_int`, `add_float`
- `add_text_bytes`, `add_byte_string`, `add_array`, `add_map`
- `array_push`, `map_set`, `set_root`

Inspection and traversal:

- `validate_doc`, `validate_complete`, `node_count`, `edge_count`, `root_id`
- `node_kind`, `parent_of`, `bool_value`, `int_value`, `float_value`
- `text_bytes`, `text_scalars`, `byte_string`
- `array_length`, `array_get`, `map_length`, `map_key`, `map_value`

Encoding:

- `encoded_size` checks the complete document, depth, Float capability, and
  exact output size without producing partial public output;
- `encode` uses an explicit iterative work stack and `moss.binary.ByteWriter`.

## Limits and ownership

Defaults are maximum depth 64, one million combined nodes plus edges, 64 MiB
encoded output, and 16 MiB for one text or byte-string payload. Custom limits
may only tighten those ceilings. Every arithmetic growth check occurs before
mutation or allocation.

Input vectors are copied into the arena. Payload accessors and `encode` return
owned vectors. The package makes no borrowing, view, packed-byte-layout,
zero-copy, or native String claim.

Run the package gates from the repository root:

```sh
(cd sdk/cbor && ../../margo test --json)
(cd sdk/cbor && ../../margo test --release --json)
(cd sdk/cbor && ../../margo test --interp)
```
