# Phase 24 Text and Encoding performance Architect review

**Reviewed engineering candidate:**
`c5f55df73389e4959c097d1145aee09c8f1937dd`

**Reviewer:** independent Team T L6 Architect

## Verdict

**Correctness, semantics, optimized-provider provenance, and ABI-v9: GO.**

**Overall performance: CONDITIONAL GO / unconditional HOLD.** The candidate is
fit for Team I integration review only if L7 explicitly accepts the current
retained-segment builder as a representation exception. The existing builder
does not satisfy the charter's idiomatic reserved-Rust target: its public
`Vector[String]` contract retains independently owned, countable segments and
is 5.8x–21.3x slower than a one-allocation reserved Rust `String` in the
reviewed evidence. Closing that gap requires an approved reservable builder
representation rather than a benchmark concession.

The previously borderline multibyte-search result is not a `String.find`
blocker. Independent operation-only measurement of the actual release
`moss.text` rlib is within 1.17x of the identical local Rust algorithm across
five warmed executions. The
maintained Margo benchmark row includes input construction inside its timed
body and must be described as end-to-end construction plus search, not as a
strictly preconstructed-input measurement.

## Independent semantic and ABI challenge

The complete acceptance runner passed independently in this worktree:

- 10 native and 10 Fast Debug Text cases;
- 5 native and 5 Fast Debug Encoding cases;
- 3 native and 3 Fast Debug cross-package acceptance cases;
- native debug and release applications;
- compiled-only `moss.text` use;
- source-free ABI-v9 `moss.encoding` use with exact error variant, index, and
  payload fingerprints;
- 66 valid and 15 invalid Python UTF-8 oracle cases in native debug, native
  release, and Fast Debug.

A fresh, separately authored probe exercised mixed ASCII, accented text,
emoji, a decomposed combining sequence, empty and absent search patterns,
whole and combining-boundary slices, end-empty slices, negative bounds,
out-of-range bounds, signed-add overflow, and ASCII-only case conversion.
Native optimized Rust and Fast Debug produced identical results.

The String compiler change is narrow and semantics-preserving. Descriptors
mark all four operations read-only. Native lowering evaluates receivers and
arguments once, returns code-point indices, guards ASCII byte slicing with
`is_ascii()`, uses `char_indices()` otherwise, and uses `checked_add` for slice
bounds. Fast Debug implements the same visible contracts. Empty-concatenation
clone lowering drops only a side-effect-free empty literal and preserves the
other operand's single evaluation. No unsafe operation, byte-index source API,
effect exception, or native ABI revision was introduced.

## Build-profile and workload audit

`python3 sdk/text-encoding-acceptance/validate_release_benchmarks.py` passed on
three fresh executions. It invoked `margo bench --release --json`, observed
release profile and `-O`, and SHA-256 matched all six benchmark-linked rlibs to
their release artifacts: `moss.text`, `moss.encoding`, `moss.core`,
`moss.bytes`, `moss.collections`, and `moss.algorithms`.

The Moss and Rust sources assert equal logical contents, code-point lengths,
positions, slice ranges, repetitions, checksums, and result boundaries. The
strict Rust references compiled with:

```text
rustc --edition=2021 -D warnings -O -C debuginfo=1
```

There is one material naming/methodology correction. The Margo cases called
"prebuilt" build their inputs at the start of each benchmark function, and the
framework times the whole function 1,000 times. Repeating the operation 128
times amortizes construction but does not remove it. This matters because Moss
general nonempty concatenation lowers to repeated formatting while the Rust
construction can reuse capacity. The inputs are logically equivalent, but the
row is not an operation-only search measurement.

To isolate the disputed primitive, the review added
`cross_rlib_find_baseline.rs`. It constructs all inputs before timing,
alternates measurement order, calls the real optimized `moss.text` rlib, and
compares it with the identical local Rust algorithm. Five executions reported
provider/local medians of:

| Run | Local Rust ns | `moss.text` rlib ns | Ratio |
| ---: | ---: | ---: | ---: |
| 1 | 39,134 | 44,092 | 1.127x |
| 2 | 39,144 | 43,856 | 1.120x |
| 3 | 39,317 | 43,991 | 1.119x |
| 4 | 39,420 | 45,926 | 1.165x |
| 5 | 39,507 | 43,916 | 1.112x |

The three fresh separately executed end-to-end rows were 1.94x, 1.85x, and
1.63x. Together with V's earlier 2.02x and 2.21x observations, these results
show host and construction sensitivity around the threshold; they do not show
an intrinsic 2x search primitive gap.

## Fresh performance results

The first fresh full run produced these medians; ratios use independently
compiled idiomatic Rust:

| Workload | Moss ns | Rust ns | Ratio |
| --- | ---: | ---: | ---: |
| Long ASCII early/late/absent search x128 | 193,554 | 116,871 | 1.66x |
| Multibyte early/late/absent search x128 | 98,140 | 50,716 | 1.94x |
| Long ASCII slice x128 | 159,543 | 131,633 | 1.21x |
| Long multibyte slice x128 | 154,769 | 148,036 | 1.05x |
| Isolated ASCII lower x16 | 266 | 164 | 1.62x |
| Segment builder, 128 segments | 3,941 | 2,850 matched | 1.38x |
| Segment builder, 128 segments | 3,941 | 185 reserved | 21.30x |

The isolated case result is at a sub-10-nanosecond-per-call timer scale and is
not robustly below 1.5x on every run, although both generated/provider bodies
use Rust's `to_ascii_lowercase`. This is a measurement-floor caveat, not an
unsupported parity claim.

The process-amortized 256-emoji codec run measured Moss encode/decode/validate
at 584/685/623 ns, representation-matched `Vec<i64>` Rust at 526/592/591 ns
(1.11x/1.16x/1.05x), and idiomatic Rust at 1,134/551/318 ns
(0.52x/1.24x/1.96x). Content, byte count, scalar count, and checksums were
asserted.

The independent counting allocator reproduced 263 allocations and 9,120
requested bytes for the matched 128-segment retained-vector Rust contract,
versus one allocation and 1,024 bytes for reserved Rust. It also reproduced
two allocations / 8,192 bytes for the 4,096-byte case setup plus output. No
Moss allocation count is claimed because no supported Moss allocation
instrument was available.

## Commands executed

```text
./moss agent bootstrap --json
python3 sdk/text-encoding-acceptance/validate_release_benchmarks.py  # three runs
python3 sdk/text-encoding-acceptance/measure_codec.py
python3 sdk/text-encoding-acceptance/run_all.py --full
rustc --edition=2021 -D warnings -O -C debuginfo=1 \
  sdk/text-encoding-acceptance/bench_baseline.rs
rustc --edition=2021 -D warnings -O -C debuginfo=1 \
  sdk/text-encoding-acceptance/case_builder_baseline.rs
rustc --edition=2021 -D warnings -O \
  sdk/text-encoding-acceptance/case_builder_allocations.rs
rustc --edition=2021 -D warnings -O -C debuginfo=1 \
  sdk/text-encoding-acceptance/cross_rlib_find_baseline.rs \
  -L dependency=sdk/text/build/release \
  -L dependency=sdk/foundations/core/build/release \
  --extern moss_text=sdk/text/build/release/libmoss_text.rlib \
  --extern moss_core=sdk/foundations/core/build/release/libmoss_core.rlib
python3 tests/tooling/check_phase15_15_string_api.py
python3 tests/tooling/check_phase225_semantic_ide.py ./moss
python3 tests/tooling/check_agent_skills.py
python3 tests/tooling/check_agent_feedback.py
python3 sdk/encoding/tests/check_source_free.py
python3 sdk/text/tests/check_source_free.py
git diff --check
```

All commands above passed. The cross-rlib control was compiled with both
release provider paths supplied through `--extern`; its source contains the
complete reproducible workload and assertions.

## Architect disposition

For exact engineering SHA
`c5f55df73389e4959c097d1145aee09c8f1937dd`:

- semantic correctness: **GO**;
- native/Fast Debug/source-free ABI-v9 equivalence: **GO**;
- optimized build and provider provenance: **GO**;
- search, slicing, case primitive, and UTF-8 implementation: **GO** with the
  measurement qualifications above;
- idiomatic reserved-String builder target: **HOLD, explicit L7 exception
  required**;
- overall Architect verdict: **CONDITIONAL GO / unconditional HOLD**.

This verdict is independently attributable to the Team T L6 Architect and does
not authorize a merge to `phase24-integration` or `main`. Team I remains the
integration owner.
