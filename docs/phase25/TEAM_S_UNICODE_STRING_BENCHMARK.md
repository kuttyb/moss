# Phase 25 Team S — Unicode String traversal benchmark (G4)

**Branch:** `phase25-semantics-engines`
**Measured compiler:** `7bf962d287` (String lowering identical to the
submission SHA; later commits change Fast Debug and documentation only)
**Toolchain:** `rustc 1.98.1 (48a229cea 2026-09-01)`, x86_64 Linux (WSL2),
13th Gen Intel Core i9-13900K, 16 logical CPUs
**Raw data:** [`data/team_s_unicode_traversal.json`](data/team_s_unicode_traversal.json)
**Reproduce:** `python3 tests/benchmarks/phase25_unicode_traversal.py ./moss --json out.json`

## Question

L7 finding 2: the String `char_at` generated at 983acb2 still scanned from
the start of every non-ASCII String, and `length()` lowered to
`chars().count()`. A JSON-style cursor loop
`while i < s.length(): c = s.char_at(i)` was therefore quadratic. This
benchmark measures that loop over large Unicode JSON for:

- **plain**: `MossStr = String` (the native ABI v9 representation, still the
  default); and
- **indexed-v1**: `MossStr = MossString`, the staged Team S String record
  (`MOSS_STAGED_STRING_REPR=indexed-v1`): UTF-8 bytes, an eager code-point
  count, and a `u32` byte offset for every 64th code point.

## Method

- Release flags identical to `margo --release`:
  `--edition=2021 -O -C debuginfo=1`.
- Input: a 25-code-point / 40-byte fragment mixing ASCII, Latin-1, CJK, and
  an emoji (`{"名前":"値😀","é":[1,2],"k":"ü"},`), appended at run time in a
  loop to 1K, 10K, 100K, and 1M code points (≈1.37 MB at 1M).
- Per size and representation, two binaries from one source: build only, and
  build plus scan. Scan time = median(build+scan) − median(build). The scan
  counts structural characters and prints the count, and the document is
  built at run time, so neither step can be folded away. All modes print
  identical output (asserted).
- Five runs per binary; median reported. Whole-process wall time is used, so
  results below roughly 1 ms are dominated by process start-up noise.

## Results

| representation | code points | build (s) | build + scan (s) | scan (s) | ×previous size |
|---|---:|---:|---:|---:|---:|
| plain | 990 | 0.0005 | 0.0007 | 0.000195 | — |
| plain | 9,990 | 0.0004 | 0.0070 | 0.006613 | 33.9 |
| plain | 99,990 | 0.0006 | 0.6789 | 0.678304 | 102.6 |
| plain | 999,990 | 0.0009 | 67.7900 | 67.789119 | 99.9 |
| indexed-v1 | 990 | 0.0005 | 0.0005 | 0.000002 | — |
| indexed-v1 | 9,990 | 0.0005 | 0.0006 | 0.000182 | (noise) |
| indexed-v1 | 99,990 | 0.0007 | 0.0027 | 0.002031 | 11.2 |
| indexed-v1 | 999,990 | 0.0016 | 0.0175 | 0.015916 | 7.8 |

- **plain** scales by ≈100× per 10× input from 10K upward: quadratic, as the
  L7 review stated. 1M code points take 67.8 s.
- **indexed-v1** scales by ≈8–11× per 10× input where the measurement is
  above the noise floor: linear. 1M code points take 0.016 s, about 4,000×
  faster than plain. The 1K/10K rows are below process-timing resolution and
  their ratio is not meaningful.
- Building the document is linear in both representations. Indexed append
  scans only the appended part (0.0016 s vs 0.0009 s at 1M code points,
  including offset maintenance).

## Metadata cost

Measured from `MossString::metadata_bytes()` (count field, offsets `Vec`
header, and offset capacity) for strings built by repeated append:

| text | code points | UTF-8 bytes | metadata bytes | % of payload |
|---|---:|---:|---:|---:|
| ascii | 988 | 988 | 32 | 3.239 |
| ascii | 999,986 | 999,986 | 32 | 0.003 |
| json-mixed | 990 | 1,353 | 96 | 7.095 |
| json-mixed | 999,990 | 1,366,653 | 65,568 | 4.798 |
| cjk | 1,000 | 3,000 | 96 | 3.2 |
| cjk | 1,000,000 | 3,000,000 | 65,568 | 2.186 |
| emoji | 999 | 3,996 | 96 | 2.402 |
| emoji | 999,999 | 3,999,996 | 65,568 | 1.639 |

- ASCII strings carry no offsets; their metadata is the fixed 32 bytes.
- Non-ASCII offsets cost 4 bytes per 64 code points (≤ 6.25% of the code
  point count). `Vec` growth leaves up to 2× spare capacity, which is why
  the 1M rows exceed 4/64 of the code points.
- The Rust value is 56 bytes instead of `String`'s 24: **+32 bytes per String
  value**, independent of length. This is the most visible cost for
  workloads holding many short Strings (for example `Vector[String]` of
  tokens) and should be weighed in the joint ABI-v10 decision. A boxed or
  inline-small-offset layout could reduce it later without changing the
  helper interface.
- Offsets are `u32`; a non-ASCII String above 4 GiB panics with a defined
  message rather than wrapping.

## Limits of this evidence

- Single machine, whole-process timing, five runs. Ratios at 100K and 1M are
  well separated from noise; smaller sizes are not.
- The staged representation is measured in single-crate programs. Cross-crate
  use needs the shared ABI-v10 runtime crate (see the repair report), which
  is not part of this measurement.
