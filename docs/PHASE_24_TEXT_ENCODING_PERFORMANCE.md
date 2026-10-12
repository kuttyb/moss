# Phase 24 Team T performance report

Measurements were taken on 2026-10-12 on Linux WSL2, a 16-vCPU 13th Gen Intel
Core i9-13900K system. Moss compiler version is `0.1.0`; release native code used
`rustc 1.98.1 (48a229cea 2026-09-01)`, LLVM 22.1.8, `-O`, and debuginfo level 1.
These are single-machine engineering measurements, not portable guarantees.

## In-process Margo benchmarks

Command:

```sh
cd sdk/text-encoding-acceptance
../../margo bench --json
```

Margo used five warmups, 31 samples, and 1,000 iterations per sample. Values are
nanoseconds per iteration from release providers.

| Workload | p25 | median | p75 |
| --- | ---: | ---: | ---: |
| Text find, 256 code points, late match | 70,099 | 71,821 | 73,614 |
| Text slice, 128 of 256 code points | 23,823 | 24,209 | 25,297 |
| Repeated `String +`, 128 eight-code-point segments | 7,008 | 7,252 | 7,858 |
| Segment builder, 128 eight-code-point segments | 13,683 | 14,184 | 15,200 |
| Strict UTF-8 validation, 1,024 ASCII bytes | 14,095 | 14,731 | 15,547 |

The segment builder is about 1.96x slower than repeated `String +` for this
workload. The current backend can reuse the owned left String efficiently, so
the common assumption that source-level repeated `+` is necessarily quadratic
does not hold here. Team T therefore documents the builder as a composition
abstraction, not a performance guarantee. This result also demonstrates why the
SDK does not infer allocation behavior from source shape alone.

## Equivalent Rust reference

Command:

```sh
rustc --edition=2021 -D warnings -O -C debuginfo=1 \
  sdk/text-encoding-acceptance/bench_baseline.rs -o tmp/team-t-rust-bench
tmp/team-t-rust-bench
```

The Rust text reference uses the same explicit code-point-vector search/slice
algorithms, while UTF-8 validation uses `std::str::from_utf8`. It uses the same
five/31/1,000 harness.

| Workload | p25 | median | p75 |
| --- | ---: | ---: | ---: |
| Equivalent text find | 492 | 497 | 530 |
| Equivalent text slice | 364 | 368 | 372 |
| Repeated concat | 140 | 148 | 162 |
| Segment builder | 1,221 | 1,262 | 1,332 |
| Rust std UTF-8 validation | 650 | 669 | 719 |

These numbers are useful optimization signals, not parity claims. Moss Strings
and generic calls carry different representation/specialization costs, and the
validation rows use different implementations (Team T strict scalar decoder
versus Rust's standard-library validator).

## UTF-8 encode/decode process-amortized measurements

Command:

```sh
python3 sdk/text-encoding-acceptance/measure_codec.py
```

Each timed process performs 5,000 conversions of 256 emoji scalars. Five process
warmups precede 31 samples. Reported nanoseconds divide raw process time by
5,000; process startup is not subtracted. Moss and Rust run the same scalar/byte
workloads, but Rust uses standard-library scalar/UTF-8 operations, so no parity
claim is made.

| Workload | p25 | median | p75 |
| --- | ---: | ---: | ---: |
| Moss UTF-8 encode, 256 scalars | 1,459 | 1,488 | 1,550 |
| Moss UTF-8 decode, 256 scalars | 1,232 | 1,247 | 1,288 |
| Rust std encode, 256 scalars | 1,097 | 1,120 | 1,171 |
| Rust std decode, 256 scalars | 344 | 355 | 382 |

The measurements support retaining the clear arithmetic source while profiling
decode and validation first in any future optimization pass. Team T does not
infer parity or propose a broad compiler/runtime change from these isolated
workloads.

## Tooling limitation encountered

Direct `margo bench` calls to typed-error-raising encode/decode functions hit an
internal benchmark lowering error (`checked raising function call lacks a Phase
21 outcome context`). Local/helper calls in benchmark units also produced
unresolved generated symbols. Encoding/decoding therefore use the explicit
process-amortized release harness above. The failure is preserved in developer-
experience feedback; it is not reported as successful Margo benchmark coverage.
