# Phase 24 Team T corrected performance report

Measurements were taken on 2026-10-12 on Linux WSL2, a 16-vCPU 13th Gen Intel
Core i9-13900K system. Moss compiler version is `0.1.0`; native release code
used `rustc 1.98.1 (48a229cea 2026-09-01)`, LLVM 22.1.8, `-O`, and debuginfo
level 1. These are single-machine observations, not portable guarantees.

## Correction to the first report

The first Team T report used `margo bench --json`. Although the benchmark root
is always optimized, Margo builds path dependencies according to its explicit
profile flag; without `--release`, `moss.text`, `moss.encoding`, and Foundation
providers came from `build/debug`. The published numbers were therefore
mixed-profile and are withdrawn.

The earlier sources also failed their stated workload contract: the Moss find
literal had 240 code points, the slice literal had 222, and the Rust find/slice
inputs were constructed outside the timed body. Those rows are not comparable
and must not be used. This follow-up uses:

```sh
cd sdk/text-encoding-acceptance
../../margo bench --release --json
python3 validate_release_benchmarks.py
```

The verifier reruns the benchmark and checks the benchmark root plus all six
path providers (`moss.text`, `moss.encoding`, `moss.core`, `moss.bytes`,
`moss.collections`, and `moss.algorithms`) for release metadata and `-O`. It
also SHA-256 matches every provider rlib copied into `build/bench` to the
corresponding release artifact, so stale neighboring metadata cannot pass.

## Workload equivalence contract

Moss and Rust now construct the same contents inside every measured body:

| Workload | Contract asserted in both sources |
| --- | --- |
| Find | 255 `a` code points plus `b`; pattern `aaaaab`; length 256; result 250 |
| Slice | `é🙂` repeated 128 times; slice start 64, length 128; first `é`, last `🙂` |
| ASCII lower | `AbCdEfGh` repeated 32 times and the corresponding 256-code-point lowercase result |
| Concatenation/builder | 128 segments of `abcdefgh`; result length 1,024 |
| UTF-8 validation | 1,024 integers `65 + i % 26`; first 65, last 74; valid UTF-8 |

Assertions and checksums execute in the timed bodies. This adds work, but it is
the same logical work in both implementations and makes workload drift fail
loudly. Repeated-operation profiling cases reuse one asserted input and execute
8 find, 16 slice, 4 case-conversion, or 64 validation operations per body.
The Rust builder and integer-vector construction also mirror the explicit
`black_box` barriers that Moss benchmark lowering inserts around mutating
append/push statements. Concatenation and builder result lengths are each
counted exactly once in both bodies.

## Corrected Moss release-provider results

Margo used five warmups, 31 samples, and 1,000 iterations per sample. Values are
nanoseconds per complete benchmark body.

| Workload | p25 | median | p75 |
| --- | ---: | ---: | ---: |
| Text find, 256 code points | 14,025 | 16,154 | 18,467 |
| Text slice, 128 of 256 code points | 15,941 | 17,001 | 19,910 |
| ASCII lower, 256 code points | 28,946 | 30,848 | 34,713 |
| Repeated `String +`, 128 segments | 7,256 | 8,400 | 8,927 |
| Segment builder, 128 segments | 5,512 | 5,926 | 6,879 |
| Strict UTF-8 validation, 1,024 ASCII bytes | 1,923 | 2,030 | 2,625 |

The corrected builder is about 29% faster than repeated concatenation on this
workload, reversing the invalid mixed-profile result. It remains a composition
abstraction rather than a universal speed guarantee.

## Equivalent Rust references

Command:

```sh
rustc --edition=2021 -D warnings -O -C debuginfo=1 \
  sdk/text-encoding-acceptance/bench_baseline.rs -o tmp/team-t-rust-bench
tmp/team-t-rust-bench
```

The primary Rust text rows deliberately emulate Moss's owned text
representation: code points and split fields are owned `String` values. Scalar
rows use ordinary Rust scalar/substring operations on the identical workload
to quantify representation cost rather than to claim an equivalent SDK
implementation.

| Workload | p25 | median | p75 |
| --- | ---: | ---: | ---: |
| Owned-String find | 409 | 427 | 481 |
| Owned-String slice | 5,274 | 5,499 | 6,010 |
| Owned-String ASCII lower | 17,557 | 18,500 | 20,446 |
| Scalar text find | 395 | 401 | 580 |
| Scalar text slice | 559 | 567 | 617 |
| Scalar ASCII lower | 424 | 438 | 699 |
| Repeated concat | 460 | 508 | 548 |
| Segment builder | 2,291 | 2,473 | 2,707 |
| Same strict integer UTF-8 validator | 1,135 | 1,219 | 1,353 |
| Rust standard UTF-8 validator | 1,089 | 1,141 | 1,263 |

These rows show that owned per-code-point Strings account for a large portion
of slicing and casing cost: even in optimized Rust, owned-String slice is about
10x its scalar counterpart and owned-String casing about 42x. The compiler's
semantic type query confirms that Moss `String.chars()` produces
`Vector[String]`, not a compact scalar vector. Remaining Moss-versus-owned-Rust
differences include Moss collection calls, cloning/joining, and runtime String
operations; no compiler-parity claim is made.

## Repeated-operation profiling

The repeated cases amortize setup. Median batch times were:

| Batch | Moss | Rust owned/same algorithm | Approximate Moss operation |
| --- | ---: | ---: | ---: |
| Find x8 | 14,501 ns | 2,182 ns | 1,813 ns |
| Slice x16 | 145,951 ns | 93,039 ns | 9,122 ns |
| ASCII lower x4 | 115,055 ns | 77,934 ns | 28,764 ns |
| Strict validation x64 | 65,394 ns | 21,408 ns | 1,022 ns |

The per-operation column is the batch median divided by its fixed count; it is
not a separately sampled distribution. Search retains an approximately 6.6x
gap after setup, while slice and casing are within about 1.6x and 1.5x of the
Rust implementation using the same owned-String strategy. Validation is about
3.1x the same arithmetic Rust loop after setup.

No system profiler was available in this environment, so the investigation
used release-profile decomposition, semantic type queries, source-level
allocation structure, and matched Rust implementations. It does not infer
allocation counts from `moss cost`, which reports only functional-pipeline
intermediates for these manual loops.

## SDK optimizations from the investigation

- For the measured late-single-match workload, `text.find` now uses the built-in split primitive and measures the first
  prefix instead of materializing both inputs as `Vector[String]` and scanning
  every candidate. The prebuilt x8 median fell from 290,986 ns to 14,501 ns
  (about 94%). Empty-pattern and overlapping first-match contracts still pass.
  This is not a general early-exit optimization: split scans and materializes
  all fields, and an independent 4,096-character probe found abundant early
  matches slower than an absent pattern. A dedicated substring primitive would
  be needed to avoid that representation cost.
- ASCII case conversion now reuses that optimized first-match path rather than
  rebuilding a 26-element alphabet vector for every input code point. The
  corrected 256-code-point end-to-end median fell from 194,667 ns to 30,848 ns
  (about 85%).
- `validate_utf8` now validates directly rather than allocating the decoded
  scalar vector and discarding it. Exact decoder error behavior remains in
  `decode_utf8`; validator and Python differential tests cover the duplicated
  acceptance logic. End-to-end validation is dominated by constructing the
  1,024-element input, so no isolated percentage improvement is claimed.
- No slicing rewrite was made. With no supported code-point substring or
  compact scalar API, the current implementation necessarily materializes
  `Vector[String]` and joins the selected elements. The matched owned-String
  baseline supports treating most of this gap as representation cost rather
  than an avoidable search/allocation mistake in the SDK.

## UTF-8 encode/decode harness and tooling boundary

`measure_codec.py` continues to measure 5,000 conversions per release process,
with five warmups and 31 samples, without subtracting process startup. Rust
uses standard-library scalar/UTF-8 operations, so those results are diagnostic,
not parity claims.

Direct `margo bench` calls to typed-error-raising encode/decode functions still
hit an internal benchmark-lowering error (`checked raising function call lacks
a Phase 21 outcome context`). The process-amortized harness remains an explicit
workaround, not claimed Margo benchmark coverage.

The final process-amortized run measured Moss encode at 1,587 ns per
256-scalar conversion (p25 1,521; p75 1,709) and decode at 1,275 ns (p25
1,234; p75 1,356). The corresponding Rust standard-library diagnostics were
1,207 ns for encode and 354 ns for decode. These include amortized process
startup and are not direct SDK parity claims.
