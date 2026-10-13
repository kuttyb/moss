# Phase 24 Team A — bounded FlatBuffers/Arrow feasibility (A9–A11)

**Role:** bounded feasibility leaf agent

**Status:** assessment only; no FlatBuffers, Arrow, schema generator, or compiler work

**Inspection baseline:** `0d5784241a43caba87e8f024d4b10e20b4a996ea`

**Language/tool contract:** `moss-0.1`, compiler `0.1.0`, `moss-agent-1`

**Date:** 2026-10-12

## 1. Decision

The current platform can construct and inspect **owned serialized byte buffers**
for a deliberately limited, schema-specific subset of FlatBuffers. It must do
so bytewise, with validation and copying. This is enough to justify a future
interoperability proof of concept; it is not an efficient general FlatBuffers
binding.

General schema-generated FlatBuffers access and production Arrow IPC are
**blocked** at the current source/runtime boundary. The decisive gaps are not
integer endian arithmetic: `moss.binary` already supplies that. They are exact
Float bit conversion, a packed/aligned byte owner, source-visible borrowed
views, automatic schema generation, native String/UTF-8 conversion, and a
typed representation of the upper half of `UInt64`.

Arrow integer/Bool body buffers remain a plausible future copy-based proof of
concept after FlatBuffers metadata is available. Arrow's zero-copy in-memory
model and a general IPC implementation are not supportable with the current
public contracts.

## 2. Evidence baseline

This assessment uses the integrated APIs and contracts rather than generated
Rust layout:

- `sdk/binary/src/binary.moss` and `sdk/binary/README.md`;
- `sdk/foundations/bytes/src/bytes.moss`;
- `sdk/encoding/src/encoding.moss` and
  `docs/PHASE_24_TEXT_ENCODING_ARCHITECTURE.md`;
- `sdk/tables/src/tables.moss` and `sdk/tables/README.md`;
- `docs/phase24/TEAM_A_ADVANCED_DATA_ARCHITECTURE.md` and
  `docs/phase24/TEAM_A_CAPABILITY_REQUESTS.md`;
- bootstrap's live `source_surface`, plus the canonical language and runtime
  contracts.

The external wire requirements were checked against the official
[FlatBuffers internals](https://flatbuffers.dev/internals/) and
[Arrow columnar/IPC specification](https://arrow.apache.org/docs/format/Columnar.html).
FlatBuffers stores self-aligned little-endian scalars and normally relies on
schema-generated accessors. Arrow IPC uses FlatBuffers metadata and a padded
sequence of physical data buffers.

Current validation at this baseline:

| Evidence | Result |
| --- | --- |
| `./moss agent bootstrap --json` | `language_version = moss-0.1`; no packed-byte, alignment, raw-Float-bit, String/UTF-8 conversion, schema-generation, or general borrowed-view source surface is advertised. |
| `(cd sdk/binary && ../../margo test --json)` | 9/9 pass, Native Debug. |
| `(cd sdk/binary && ../../margo test --release --json)` | 9/9 pass, Native Release. |
| `(cd sdk/binary && ../../margo test --interp)` | 9/9 pass, Fast Debug. |
| `moss ownership` on `binary.new_reader` and `binary.reader_input` | Input parameters are `READ`; the implementation explicitly constructs independent vectors. |
| `moss ownership` on `binary.write_bytes` | Writer is `WRITE`; byte input is `READ`. |
| Minimal `1.0.to_bits()` probe | `moss check` accepts an unresolved primitive-member leak, but Fast Debug rejects it with `method receiver is not a Moss object`; this is not a supported Float-bit API. |
| Minimal `String.bytes()` probe | `moss check --json` rejects it with `TYPE_MISMATCH: invalid builtin operation 'string.bytes'`. |

The last two results reconfirm the already-documented Team A capability
request. They do not create a new finding or an alternative backend-dependent
workaround.

## 3. Classification summary

| Area | Feasible now | Feasible with copying/limits | Blocked |
| --- | --- | --- | --- |
| Owned byte construction | Arbitrary validated bytes; explicit LE/BE signed integers; LE/BE unsigned values through `INT64_MAX`; bytewise padding; bounded output. | FlatBuffers-compatible buffers for fixed schemas that avoid unsupported scalar/text cases; offsets and padding computed in Moss. | A packed `u8` owner, guaranteed base-address alignment, or zero-copy export. |
| Schema-based access | Handwritten concrete readers can validate offsets and decode supported integer fields. | Pre-generated concrete Moss accessors could return owned records/vectors and copy strings/children. | Integrated schema compiler/build hook, runtime reflection, recursive schema values, generic typed views, or zero-copy accessor objects. |
| FlatBuffers scalars | Bool/byte and signed 16/32/64 plus unsigned 16/32 are mechanically representable; 32-bit offsets fit Moss `Int`. | Unsupported widths can sometimes be assembled as raw bytes when no typed Moss result is needed. | Float32/Float64 exact bits and typed `UInt64` values above `INT64_MAX`. |
| Text | UTF-8 bytes can be validated and converted to/from `Vector[Int]` Unicode scalars. | A FlatBuffers or Arrow text field can remain owned validated bytes/scalars. | Native Moss `String` to UTF-8 bytes and UTF-8 bytes to native `String`. |
| Arrow body buffers | Bytewise integer/Bool buffer construction is mechanically possible. | A narrow signed-integer/Bool proof of concept could pack validity and values into copied IPC body bytes with explicit limits. | General Arrow IPC, Float columns, native String columns, zero-copy arrays, aligned native buffers, and C Data Interface-style sharing. |

“Feasible with copying/limits” means the current language has enough semantic
operations for a future implementation; it does not mean that implementation
exists in this branch.

## 4. A9 — owned-buffer construction

### 4.1 What `moss.binary` supplies

`ByteReader` and `ByteWriter` own `Vector[Int]` storage whose elements are
validated to `0..255`. Reader construction, `reader_input`, `writer_output`,
`read_bytes`, and length-delimited reads produce independent vectors. Writers
append validated bytes and make a failing public mutation atomic.

The package supports:

- little- and big-endian signed 16-, 32-, and 64-bit values;
- little- and big-endian unsigned 16- and 32-bit values;
- unsigned 64-bit wire values only through `INT64_MAX`;
- canonical unsigned varints through `INT64_MAX`;
- byte arrays and bounded length-delimited payloads;
- explicit total and payload ceilings (64 MiB and 16 MiB by default).

These operations are enough to emit FlatBuffers' little-endian integer fields,
32-bit relative offsets, vtable entries, lengths, and zero padding for a fixed
schema. Lack of bitwise syntax is not a correctness blocker: arithmetic
division/remainder already passes all three `moss.binary` execution modes.

### 4.2 The important alignment distinction

Serialized alignment and native storage alignment are different capabilities.

- **Serialized alignment is feasible:** a builder can compute its current byte
  offset and append the required padding before a field or body buffer.
- **Native aligned storage is blocked:** `Vector[Int]` does not promise packed
  one-byte elements, a contiguous public byte address, or an aligned base
  address. Moss exposes no `sizeof`, `alignof`, pointer, or safe cast/view API.

Consequently bytewise construction can produce a conforming file/IPC byte
sequence, but consumers cannot treat its backing `Vector[Int]` memory as an
aligned FlatBuffers object or Arrow primitive buffer.

### 4.3 FlatBuffers construction verdict

A fixed-schema builder is **feasible with copying/limits** if all of the
following hold:

1. it computes relative offsets and padding explicitly;
2. it writes little-endian values bytewise;
3. it validates every offset, length, and arithmetic operation before mutation;
4. its schema excludes Float fields, native String adapters, and typed full
   `UInt64`, or represents those fields as an explicitly nonstandard/raw-byte
   application contract;
5. its total output stays within the configured `moss.binary` limit; and
6. it makes no zero-copy, direct-memory, or ABI-layout claim.

The current `ByteWriter` is append-oriented and provides no FlatBuffers
backward-builder or patch API. A future library could stage child buffers and
offsets in ordinary owned vectors, or define a separately reviewed builder.
That is implementation work outside A9–A11; direct mutation of the exported
writer record is not a stable substitute for such an API.

## 5. A10 — schema-based typed access

### 5.1 Concrete generated accessors are plausible

FlatBuffers access is schema-dependent. Moss can express concrete types,
closed enums, checked index arithmetic, and specialized ordinary functions. An
external generator could therefore emit one concrete accessor set per schema.
Such accessors could:

- validate the root offset, table/vtable bounds, field offsets, vector lengths,
  and nested ranges before indexing;
- decode supported scalars through `moss.binary` arithmetic;
- return owned Moss records and copied vectors; and
- preserve missing/default field behavior in schema-specific code.

This path is **feasible with copying/limits**, not zero-copy. The accessor must
hold or read the owned byte vector and materialize independent values. It cannot
return a general view whose lifetime is tied to the input buffer.

### 5.2 Why a general current binding is blocked

- Moss v0.1 has no user-defined generic nominal types, recursive source values,
  runtime trait objects, dynamic dispatch, pointers, or source-visible lifetime
  parameters. A generic `TableView[T]`/`VectorView[T]` model is unavailable.
- Ordinary `READ` effects permit temporary compiler-managed borrows, but do not
  create a storable/returnable Moss view. Backend borrow optimization is not a
  public serialization API.
- FileIO `Range` is the one public borrowed byte surface. It is scoped,
  read-only, runtime-owned, non-storable, and cannot cross message/executor
  boundaries. `moss.bytes.from_range` copies it while live. It cannot back a
  durable FlatBuffers object graph.
- There is no integrated `.fbs` parser, `flatc`-style Moss generator, macro or
  reflection facility, or Margo schema-generation hook. Checked-in source from
  an external generator is conceivable, but no current workflow defines or
  validates it.
- Public records are forgeable. Every accessor accepting a public reader/view
  record must revalidate its invariants or clearly retain a validated private
  owner that Moss can actually enforce; the present SDK cannot expose an opaque
  invariant-bearing record.

### 5.3 Scalar and text blockers

| Capability | Current fact | Consequence |
| --- | --- | --- |
| Scalar physical layout | Moss defines semantic `Int`, `Float`, `Bool`, and `String`; it does not expose their native byte layout. `Float` is IEEE binary64 semantically. | Accessors must decode bytes explicitly, never cast a Moss record/vector or infer compiler layout. Bool needs an explicit wire rule. |
| Float raw bits | No supported Float-to-bits or bits-to-Float pair exists with checker/native/Fast Debug/ABI parity. | Exact FlatBuffers Float32/Float64 and Arrow floating buffers are blocked. Decimal conversion is invalid for binary serialization. |
| Byte owner | Public bytes are `Vector[Int]` values in `0..255`, copied and revalidated. | Correct bytewise access is possible; packed memory, direct slices, and interoperability by address are not. |
| Native String conversion | String methods expose code-point operations, not bytes. `moss.encoding` converts UTF-8 bytes to/from scalar integers only. | Schema text can remain validated bytes/scalars, but native `String` fields/adapters are blocked. |
| 64-bit signed values | `moss.binary` covers the complete signed two's-complement 64-bit range, including `INT64_MIN`. | FlatBuffers `long` and Arrow signed Int64/length metadata fit, subject to resource limits. |
| 64-bit unsigned values | Moss `Int` cannot carry `2^63..2^64-1` as a nonnegative value; `moss.binary.read_u64_*` raises `Overflow`. | General FlatBuffers/Arrow `UInt64` typed access is blocked. Raw eight-byte preservation is possible only as bytes or a future split carrier. |

## 6. A11 — future Arrow IPC buffers

Arrow IPC combines two distinct layers:

1. FlatBuffers metadata describing schema, field nodes, and body-buffer
   offsets/lengths; and
2. a padded sequence of physical body buffers such as validity bitmaps,
   fixed-width values, offsets, and variable-width data.

### 6.1 Narrow future proof of concept

A copy-based proof of concept is mechanically plausible for a fixed schema
limited to signed integer and Bool columns:

- translate the table's `Vector[Bool]` validity into a packed bitmap using
  arithmetic byte assembly;
- encode signed fixed-width values little-endian;
- insert IPC padding explicitly and record copied body offsets/lengths;
- hand-author or externally pre-generate the required FlatBuffers metadata;
- cap array lengths and bytes below both Moss arithmetic and configured resource
  limits; and
- compare complete output against an independent Arrow implementation.

This would validate serialization compatibility only. It would not make
`moss.tables` an Arrow physical representation: its validity is
`Vector[Bool]`, its values use ordinary Moss vectors, and all resulting buffers
would be newly packed copies.

### 6.2 General/production Arrow remains blocked

- **Metadata:** no FlatBuffers schema generator or verified metadata binding is
  integrated.
- **Floats:** exact Float32/Float64 bit encoding is absent.
- **Text/binary:** table Strings cannot be converted to exact UTF-8 bytes;
  byte/scalar-only text would require a different public input contract.
- **Physical buffers:** no packed byte owner, aligned allocation guarantee,
  foreign-buffer owner, or source-level read-only slice/view exists.
- **Validity:** the conceptual `moss.tables` bitmap is one `Bool` per row, not
  Arrow's packed validity buffer. Packing is possible only by copying.
- **Zero-copy:** Arrow's principal in-memory sharing properties cannot be
  represented by `Vector[Int]` copies or by backend-only borrows.
- **Large and unsigned ranges:** signed 64-bit Arrow lengths are representable,
  but current binary defaults are far smaller; full `UInt64` data is not a Moss
  scalar value.
- **Schema breadth:** nested, dictionary, union, extension, compressed, and
  variable-view layouts require contracts and validation not present here.

Accordingly, production Arrow IPC is **blocked**, while a narrowly scoped,
copying interoperability experiment is **feasible with limits** after its
FlatBuffers metadata path is separately approved.

## 7. Actionable next steps (future scope only)

1. Resolve and independently accept the two existing Team I requests:
   exact Float/raw-binary64 conversion and strict owned String/UTF-8 conversion,
   with checker, Native Debug/Release, Fast Debug, and ABI-v9 parity.
2. Decide explicitly between a copy-only serialization goal and a zero-copy
   memory goal. A copy-only FlatBuffers/IPC writer needs logical padding but not
   native aligned storage; zero-copy requires a new packed/aligned buffer owner
   and non-escaping view contract.
3. Choose a schema strategy. The lowest-risk path is an external, versioned
   generator that emits checked-in concrete Moss source and records the
   generator/schema versions. Do not introduce runtime reflection to solve a
   static schema problem.
4. Define the `UInt64` policy before claiming general FlatBuffers support:
   reject upper-half values, expose raw bytes/two 32-bit halves, or add an
   accepted unsigned carrier. Silent signed reinterpretation is not acceptable.
5. If a later proof of concept is approved, begin with one fixed schema and
   signed Int32/Int64/Bool fields, adversarial offset/bounds cases, and
   bidirectional interoperability against official FlatBuffers tooling.
6. Only after that metadata proof should Arrow work try one uncompressed
   record batch with copied Int32/Int64/Bool buffers and a packed validity
   bitmap. Treat Float, String, nested/dictionary/compressed layouts, and
   zero-copy as separate gates.
7. Add performance claims only after measuring equivalent release workloads.
   The current `moss.binary` contract performs O(n) public-record validation
   and explicit output copies, so it is a correctness foundation rather than a
   zero-copy performance foundation.

## 8. Scope closeout

This assessment does not add a dependency, public API, schema, generated file,
runtime representation, compiler intrinsic, FlatBuffers builder/accessor, or
Arrow buffer. It preserves the Phase 24 architecture's bounded conclusion:
bytewise owned interoperability is plausible; general efficient typed access
and production Arrow IPC require capabilities that are not currently present.
