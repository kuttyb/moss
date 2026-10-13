# Team T focused capacity-builder follow-up

This follow-up adds a capacity-aware `moss.text` builder without changing the
existing `Vector[String]` builder or any of its observable segment APIs. It is
based on the compiler-owned `String.reserve(additional_bytes)` and
`String.append(part)` primitives from the focused concatenation workstream.

## API and ownership contract

- `new_capacity_builder(capacity_bytes)` starts an empty String with an initial
  UTF-8 byte-capacity hint. Nonpositive hints are safe no-ops.
- `capacity_builder_append` mutates the builder (`WRITE`) and reads the part.
- `capacity_builder_append_line` appends the line and one ASCII LF.
- `capacity_builder_build` reads the builder and returns an independent clone,
  retaining reusable-builder behavior.
- `capacity_builder_finish` consumes the builder and returns its backing String
  without a final copy.

`moss effects` confirms `WRITE + READ`, `READ`, and `CONSUME` respectively for
append, build, and finish. A compiled-only ABI-v9 consumer exercises all paths;
the imported ownership summary also rejects passing the same String as both the
writable builder and readable part.

## Measurement method

Moss uses `margo bench --release --json` with five warmups, 31 samples, and
1,000 iterations per sample. The repository release validator verifies `-O`
and SHA-256 equality between every benchmark-linked provider and its release
artifact. The strict Rust reference uses rustc 1.98.1,
`--edition=2021 -D warnings -O -C debuginfo=1`, the same contents, capacities,
segment counts, and result assertions. Times are p25 / median / p75 ns.

The finish workloads include construction, one preconstructed owned 8-byte
part, exact reservation, all appends, and consuming finalization. Append-only
separates the finalization path. The corrected reusable-build row is explicitly
end-to-end: both timed bodies construct the owned part, reserve and fill the
1,024-byte builder, and then clone it 16 times.

## Allocation evidence

The untimed Rust global-allocator instrument reports:

| Rust workload | Allocations | Requested bytes |
| --- | ---: | ---: |
| Capacity finish, 8 segments | 2 | 72 |
| Capacity finish, 128 segments | 2 | 1,032 |
| Capacity finish, 1,024 segments | 2 | 8,200 |
| Append preconstructed input, 128 segments | 2 | 1,032 |
| Reusable build, 128 segments, 16 snapshots (setup excluded) | 16 | 16,384 |

The finish counts comprise one owned 8-byte part and one exactly reserved
builder buffer. For the allocation-only reusable snapshot evidence, the builder
and input are intentionally created before allocation tracking, so its 16
allocations are exactly the 16 independent snapshots. That untimed allocator
scope is separate from the end-to-end timing row and is not timing evidence.

Moss allocation counts are not claimed: this repository has no supported Moss
native allocation counter. Generated release Rust provides narrower structural
evidence: construction reserves once, append calls `push_str`, reusable build
clones, and finish returns the owned String directly. This supports the
mechanism but is not mislabeled as a measured Moss allocation count.

## Results

The final sequential release run produced:

| Workload | Moss | Rust | Median ratio |
| --- | ---: | ---: | ---: |
| Finish, 8 segments | 101 / 102 / 105 | 69 / 69 / 69 | **1.48x** |
| Finish, 128 segments | 381 / 392 / 409 | 276 / 278 / 283 | **1.41x** |
| Finish, 1,024 segments | 2,340 / 2,401 / 2,675 | 1,758 / 1,857 / 2,121 | **1.29x** |
| Append preconstructed part, 128 segments | 252 / 273 / 386 | 195 / 200 / 205 | **1.36x** |
| Reusable 1,024-byte build, 16 snapshots | **invalid historical row** | **invalid historical row** | setup mismatch |

The original reusable-build row is invalid because only Rust constructed its
builder outside timing. It is preserved here rather than silently rewritten.
The corrected, relabeled end-to-end row produced:

| Run | Moss p25 / median / p75 | Rust p25 / median / p75 | Median ratio |
| ---: | ---: | ---: | ---: |
| 1 | 846 / 1,292 / 1,391 | 885 / 894 / 906 | **1.45x** |
| 2 | 890 / 1,022 / 1,361 | 876 / 887 / 930 | **1.15x** |
| 3 | 881 / 904 / 949 | 912 / 1,140 / 1,283 | **0.79x** |

The ratio of the median Moss median to the median Rust median is 1.14x. All
three runs used `margo bench --release --json`, passed the six-provider hash
validator, and compiled the strict Rust source tracked in the same candidate.
The broad distributions are reported candidly; none exceeds the 1.5x target.

The result checks deliberately perform the same semantic work. In particular,
both finish workloads count Unicode scalars and traverse to the first and last
scalar; using Rust byte indexing would have made its timed assertion cheaper
than Moss's code-point API and produced a false builder gap.

## Validation

Focused validation covers native debug, release, Fast Debug, generated lowering,
source-free ABI-v9 behavior and ownership conflicts, strict Rust references,
release-provider provenance, and feedback-schema checks. During validation the
benchmark harness was repaired to avoid borrowing locals after a checked
`CONSUME`; the permanent consuming-benchmark regression prevents recurrence.
Full repository gates remain the Coordinator's integration responsibility.
