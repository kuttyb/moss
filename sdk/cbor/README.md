# moss.cbor

`moss.cbor` provides an owned, iterative CBOR document arena and reciprocal
RFC 8949 definite-length encoder and decoder. The source module is `cbor`;
import it with `import cbor`.

## Supported data model

The arena has closed node kinds for null, Bool, signed Moss Int, Float64, UTF-8
text bytes, byte strings, arrays, and maps. Encoding and decoding support all
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
other simple values, and Float16/Float32 are outside this subset. The decoder
returns typed errors for each unsupported class. Float16 and Float32 raise
`UnsupportedFloat`; Float64 raises `FloatCapabilityUnavailable`. No float is
converted through a lossy numeric or textual representation. This is a
standards-compliant RFC 8949 subset, not a private Moss format and not a claim
of deterministic CBOR map ordering.

Float64 remains blocked because Moss does not yet expose a checked,
bit-preserving Float-to-binary64 conversion with Native and Fast Debug parity.
`add_float`, `float_value`, encoding a Float node, and decoding binary64 raise
`CborError.FloatCapabilityUnavailable`. Decimal formatting is never used as a
wire substitute.

The decoder accepts definite, structurally valid non-preferred integer and
length widths and preserves their semantic value. Re-encoding produces the
encoder's shortest-width representation. Exactly one top-level item is
required; empty, truncated, malformed, reserved-additional-information, and
trailing input are rejected. Text payloads are validated as strict UTF-8.

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

Decoding:

- `decode(bytes)` applies the default resource limits;
- `decode_with_limits(bytes, max_depth, item_limit, input_limit,
  allocation_limit, payload_limit)` only accepts limits at or below the public
  defaults;
- one iterative preorder loop uses explicit container stacks, attaches every
  completed array child or map key/value pair to the linked-edge arena, and
  validates the complete document before returning it.

Decoder errors are atomic from the caller's perspective: the arena under
construction is local and no `CborDoc` is returned unless parsing, ownership,
duplicate-key checks, trailing-data checks, and final validation all succeed.

## Limits and ownership

Defaults are maximum depth 64, one million combined nodes plus edges, 64 MiB
encoded output, and 16 MiB for one text or byte-string payload. Custom limits
may only tighten those ceilings. Every arithmetic growth check occurs before
mutation or allocation.

Decoder defaults additionally cap input at 64 MiB and decoded arena allocation
at 67,108,864 logical units. Allocation accounting is deterministic: each node
uses 10 units (the ten per-node arena columns), each linked edge uses 4 units,
and each retained payload byte uses 1 unit. Container lengths are preflighted
against the minimum item and allocation cost, and actual cumulative node,
edge, and payload growth is checked again before mutation. The explicit parser
stacks are independently bounded by `max_depth`.

Input vectors are copied into the arena. Payload accessors and `encode` return
owned vectors. The package makes no borrowing, view, packed-byte-layout,
zero-copy, or native String claim.

Run the package gates from the repository root:

```sh
(cd sdk/cbor && ../../margo test --json)
(cd sdk/cbor && ../../margo test --release --json)
(cd sdk/cbor && ../../margo test --interp)
```
