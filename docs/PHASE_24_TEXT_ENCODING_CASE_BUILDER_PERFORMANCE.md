# Team T ASCII case and builder performance

This P2 report covers `moss.text` ASCII case conversion and the public
`Vector[String]` text-builder contract after compiler/runtime commit
`43840b8041653f5dbf767452a6454002d743bbad`. That commit adds safe
`String.ascii_lower()` / `ascii_upper()` operations and lowers `String + ""`
to a direct clone. The SDK keeps every exported signature unchanged.

## Method

Moss results came from
`python3 sdk/text-encoding-acceptance/validate_release_benchmarks.py`. The
validator ran `margo bench --release --json`, verified rustc 1.98.1 with
`--edition=2021 -D warnings -O -C debuginfo=1`, and SHA-256 matched all six
benchmark-linked provider rlibs to release artifacts. Each result has five
warmups, 31 samples, and 1,000 iterations per sample.

Rust used the same compiler flags and harness shape from
`sdk/text-encoding-acceptance/case_builder_baseline.rs`. Both sources assert
contents, code-point lengths, segment counts, and repeated-operation checksums.
The old benchmark cases remain intact. The allocation program is separate from
the timing program so its counting allocator does not perturb reported times.
Moss allocation counts are not claimed because no native allocation
instrument was available.

## ASCII case results

Times are nanoseconds; each cell is p25 / median / p75.

| Workload | Moss | Idiomatic/equivalent Rust | Median ratio |
| --- | ---: | ---: | ---: |
| Lower, 16-code-point literal | 25 / 25 / 25 | 12 / 12 / 13 | 2.08x |
| Upper, constructed 256-code-point multibyte text | 3,004 / 3,076 / 3,228 | 251 / 253 / 260 | 12.16x |
| Lower, constructed 4,096-code-point text, 16 calls | 68,310 / 70,012 / 73,416 | 3,156 / 3,225 / 3,321 | 21.71x |
| Lower, 64-code-point literal, 16 calls | 156 / 164 / 166 | 159 / 161 / 173 | **1.02x** |

The literal repeated-operation case isolates casing from input construction and
meets the 1.5x target. Generated provider Rust is exactly
`value.to_ascii_lowercase()` / `to_ascii_uppercase()` behind the stable ABI-v9
export. Native, Fast Debug, and source-free tests agree on empty, punctuation,
ASCII-boundary, accented, CJK, and emoji inputs.

The two constructed-input cases intentionally remain as end-to-end evidence.
Their gap is not casing: Moss general String concatenation still uses formatting
for each nonempty `value + segment`, while Rust's owned `String + &str` can
reuse capacity. The 16-code-point 2.08x result is a 13 ns absolute delta at the
timer floor and is not presented as meeting the 2x ceiling. These exceptions
require L7 disposition if input construction is considered part of the primary
casing operation; changing their workload would hide the demonstrated general
concatenation cost.

## Builder results

The equivalent Rust builder preserves Moss's observable contract: it owns a
`Vec<String>`, clones each incoming independent segment, retains segment count,
and joins at build. The idiomatic comparison instead reserves one `String` and
uses `push_str`; it does not retain independently inspectable segments.

| Segments | Moss vector builder | Equivalent Rust `Vec<String>` | Ratio | Reserved Rust `String` | Ratio |
| ---: | ---: | ---: | ---: | ---: | ---: |
| 8 | 185 / 186 / 194 | 169 / 171 / 217 | **1.09x** | 28 / 32 / 33 | 5.81x |
| 128 | 3,343 / 3,505 / 3,726 | 2,338 / 2,387 / 2,480 | **1.47x** | 190 / 190 / 211 | 18.45x |
| 1,024 | 19,050 / 19,706 / 21,624 | 18,441 / 18,764 / 20,582 | **1.05x** | 1,481 / 1,507 / 1,608 | 13.08x |

Building an already populated 128-segment vector 16 times measured
5,760 / 5,863 / 6,049 ns in Moss and 5,914 / 5,953 / 6,274 ns in equivalent
Rust (0.98x median). The P4 clone lowering and the P2 direct indexed length
query remove avoidable formatting and inspection clones. No remaining SDK
algorithm overhead exceeds 1.5x for the representation-matched contract.

The allocation-only Rust run measured 263 allocations / 9,120 requested bytes
for the equivalent 128-segment builder, versus one allocation / 1,024 bytes for
the reserved builder. That aspirational 18.45x timing gap is a representation
and public-contract exception, not evidence that `join` is slow. Closing it
requires an approved mutable/reservable String-builder representation (or a new
opaque SDK type) while deciding what happens to segment-count inspection; it
cannot be achieved honestly by weakening the Rust baseline or silently changing
the existing public builder semantics.
