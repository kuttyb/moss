# Phase 24 Team T performance sprint delivery

## Scope and ancestry

`phase24-text-perf` was forked exactly from accepted Team T commit
`4a9b9be12fd230dcbd0b176d539937c29248d2f4`. The sprint changes Text and
Encoding performance, compiler-owned String operations, benchmark evidence,
tests, and developer-experience records only. Team I remains the integration
owner; this branch is not merged into `phase24-integration` or `main`.

## Implemented optimizations

- `String.find` lowers to Rust's native UTF-8 substring search and converts the
  matching byte offset to Moss's Unicode code-point index.
- `String.slice` performs one output copy. ASCII input uses validated byte
  offsets directly; non-ASCII input retains a `char_indices` boundary walk.
- `String.ascii_lower` and `String.ascii_upper` lower to Rust's ASCII-only
  transforms and preserve every non-ASCII scalar.
- Concatenation with an empty String lowers to a clone rather than formatting.
- `moss.text` delegates search, slicing, and ASCII casing to those primitives.
- UTF-8 codec hot loops use local byte-range checks and direct proven-byte
  pushes, eliminating a stable cross-package call and fallible helper call per
  byte.

All new String operations are read-only, have native and Fast Debug behavior,
are described by bootstrap/completion/type/effect surfaces, and preserve
source-free ABI-v9 package use. No annotation, unsafe operation, or general
optimizer was introduced.

## Corrected performance evidence

The maintained harnesses assert identical contents, code-point and byte
lengths, search positions, slice ranges, repetitions, results, and checksums.
Moss uses `margo bench --release --json`; the validator SHA-256 binds all six
linked provider rlibs to their release artifacts. Rust uses rustc 1.98.1 with
`--edition=2021 -D warnings -O -C debuginfo=1`. Rows use five warmups, 31
samples, and 1,000 iterations. Times below are p25 / median / p75 nanoseconds
from the Coordinator's final pre-review run.

| Preconstructed operation | Moss | Idiomatic Rust | Median ratio |
| --- | ---: | ---: | ---: |
| find 256 code points x128 | 37,249 / 38,731 / 40,194 | 25,275 / 27,827 / 30,530 | 1.39x |
| long ASCII early/late/absent find x128 | 172,807 / 192,415 / 200,206 | 112,953 / 125,867 / 129,425 | 1.53x |
| multibyte early/late/absent find x128 | 99,931 / 104,050 / 108,437 | 45,110 / 48,978 / 51,036 | 2.12x |
| slice 256 code points x128 | 29,386 / 30,245 / 31,059 | 51,723 / 54,826 / 60,085 | 0.55x |
| long ASCII slice x128 | 181,074 / 189,457 / 198,985 | 111,298 / 113,793 / 120,688 | 1.66x |
| long multibyte slice x128 | 162,346 / 175,346 / 183,452 | 131,138 / 134,937 / 142,155 | 1.30x |
| isolated ASCII lower x16 | 155 / 160 / 164 | 162 / 163 / 165 | 0.98x |

Independent V runs found multibyte search at 2.02x and, on an earlier run,
2.21x. A fresh same-crate `#[inline(never)]` Rust control was only 1.03x its
ordinary control. The generated Moss provider already contains the same native
`find` plus prefix `chars().count()` algorithm. The residual result therefore
straddles, but is not demonstrated to remain within, the 2x ceiling. It is
reported as an evidence-backed stable-ABI/measurement exception rather than a
parity claim.

The process-amortized codec harness measured Moss encode/decode/validate at
597/727/635 ns median, representation-matched `Vec<i64>` Rust at 577/608/594
ns (1.03x/1.20x/1.07x), and idiomatic Rust at 1,156/551/322 ns
(0.52x/1.32x/1.97x). Its 256-scalar/1,024-byte content and output checksums are
asserted. Independent V reruns remained within 1.42x matched Rust and 1.94x
idiomatic Rust for the slower directions.

The existing segment builder is within approximately 1.5x of equivalent
optimized Rust retaining a `Vec<String>` across 8, 128, and 1,024 segments.
It is 5.8x–18.5x slower than an idiomatic capacity-reserved Rust `String`.
Rust allocation instrumentation attributes the 128-segment contrast to 263
allocations / 9,120 requested bytes for the retained-segment contract versus
one allocation / 1,024 bytes for the reserved builder. Moss allocation counts
are not claimed. Closing this gap requires an approved opaque/mutable
capacity-reserving builder and a compatibility decision for observable segment
count; weakening the Rust baseline or silently changing the public contract is
not acceptable.

End-to-end casing benchmarks that construct large Strings inside the timed body
remain 12x–22x behind Rust because general nonempty String concatenation uses
formatting while Rust can reuse capacity. Isolated casing itself is at parity.
This construction gap is retained as evidence and is not relabeled as a case
conversion result.

## Validation

- `python3 sdk/text-encoding-acceptance/run_all.py --full`: passed native
  debug/release, Fast Debug, external consumers, compiled-only `moss.text`,
  source-free ABI-v9 `moss.encoding`, and the 66-valid/15-invalid Python oracle.
- `python3 sdk/text-encoding-acceptance/validate_release_benchmarks.py`: passed
  release profile, `-O`, six provider hashes, strict Rust, and workload checks.
- `python3 sdk/text-encoding-acceptance/measure_codec.py`: passed asserted
  process-amortized Moss, idiomatic Rust, and representation-matched Rust runs.
- `TMPDIR="$PWD/tmp" make check`: passed, ending with
  `all Moss v0.1 tests passed`.
- Feedback schema/self-tests, agent skill tests, String lowering regressions,
  native/Fast Debug semantic cases, source-free consumers, and `git diff
  --check`: passed.

Agent V independently issued correctness/methodology/provider/ABI **GO** for
implementation `85a9138bb1a324f3b0b759f739fb358566854285`, but performance
**HOLD / conditional NO-GO** pending L7 disposition of the reserved-builder
exception and the Architect's treatment of multibyte search around 2x. Its
fresh tests and full evidence are in
`docs/PHASE_24_TEXT_ENCODING_PERF_VALIDATION_V.md`.

## Engineering disposition

Engineering L6: **conditional GO** for integration review. Correctness and
representation-matched primary operations meet the target. Unconditional
performance acceptance is withheld for the two explicit exceptions above.
The independent Architect verdict and exact final SHA are recorded separately
after this candidate is frozen.
