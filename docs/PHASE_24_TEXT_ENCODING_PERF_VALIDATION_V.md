# Phase 24 Text and Encoding independent performance validation (V)

**Reviewed implementation:** `85a9138bb1a324f3b0b759f739fb358566854285`

**Role:** independent validation/profiling agent V

## Verdict

Correctness, semantic equivalence, optimized-provider provenance, and ABI-v9
compatibility are **GO**. Overall performance acceptance is **HOLD / conditional
NO-GO** until L7 explicitly accepts the existing reserved-String builder
representation exception and either accepts or further stabilizes the
multibyte-search result at the 2x boundary. This is not a correctness defect.

I did not rely on Coordinator results. I built a compiler in an isolated
worktree, reran the release verifier twice, compiled the Rust references with
strict warnings, reran the complete three-mode oracle and compiled-only
consumers, inspected release-generated Rust, and added fresh cases.

## Reproduced evidence

`python3 sdk/text-encoding-acceptance/validate_release_benchmarks.py` reported
profile `release`, `-O`, six providers, and exact SHA-256 equality between every
`build/bench` provider rlib and its provider release artifact. Both executions
completed successfully. The workload audit confirmed identical source contents,
code-point lengths, search positions, slice ranges, repetition counts, and
checksums. Generated benchmark Rust retains whole-result assertions and final
`black_box` barriers. The maintained Rust references compiled with:

```text
rustc --edition=2021 -D warnings -O -C debuginfo=1
```

Fresh representative medians in nanoseconds (31 samples, 1,000 iterations) were:

| Repeated operation | Moss | Idiomatic Rust | Ratio |
| --- | ---: | ---: | ---: |
| 256-code-point find x128 | 45,899 | 32,221 | 1.42x |
| long ASCII early/late/absent find x128 | 214,210 | 118,297 | 1.81x |
| multibyte early/late/absent find x128 | 103,400 | 51,167 | 2.02x |
| long ASCII slice x128 | 183,618 | 141,827 | 1.29x |
| long multibyte slice x128 | 161,114 | 164,152 | 0.98x |

The first independent search run measured the multibyte row at 98,186 ns Moss
and 44,434 ns Rust. These separately executed harnesses show enough host noise
that the result cannot honestly be stated as durably below 2x. A newly added
same-crate `#[inline(never)]` Rust control measured 52,919 ns versus 51,167 ns
for the ordinary Rust function, showing a small call-optimization component but
not explaining the entire Moss/Rust difference. The generated Moss provider
body is the intended native byte search plus prefix `chars().count()`, behind a
separately compiled stable export. Further attribution requires an interleaved
or cross-rlib harness rather than inference from elapsed time.

The UTF-8 process-amortized harness (`measure_codec.py`) independently reported
Moss encode/decode/validate medians of 709/904/659 ns. Idiomatic Rust was
1,197/571/339 ns and representation-matched `Vec<i64>` Rust was 662/636/588 ns.
Thus Moss was 1.07x/1.42x/1.12x representation-matched; idiomatic decode was
1.58x and validation 1.94x. Output contents and checksums were asserted.

The public vector builder reproduced near representation-matched Rust (3,617 ns
versus 3,570 ns at 128 segments on these runs), while reserved Rust was 195 ns.
The separate Rust counting allocator reproduced 263 allocations / 9,120 bytes
for the equivalent retained-segment contract and one allocation / 1,024 bytes
for reserved `String`. No Moss allocation count is claimed. This is a real
representation/API exception and still requires the charter's L7 disposition.

## Semantic and error-contract challenge

`python3 sdk/text-encoding-acceptance/run_all.py --full` passed native debug,
native release application, Fast Debug, both compiled-only consumers, and all
66 valid / 15 invalid oracle cases in native debug, native release, and Fast
Debug. Fresh permanent tests additionally cover mixed emoji/accent/combining
search indices, combining-boundary slicing, overflowing and negative slice
bounds, non-ASCII characters resembling case mappings, late invalid UTF-8
continuations, truncation offsets, an overlong scalar, and `U+10FFFF`. Native
and Fast Debug both pass those additions.

The direct String lowering regression, semantic IDE, agent skill, feedback, and
whitespace checks also pass. Release-generated `moss.text` uses `str::find`, an
ASCII direct byte-slice guarded by `is_ascii()`, Unicode `char_indices()`
fallback, and Rust ASCII-only case operations; generated `moss.encoding` uses
local integer checks and direct vector pushes.

## Independent conclusion

The implementation is semantically sound and the benchmark infrastructure is
now reproducible and candid. Search, slicing, case conversion, UTF-8, and the
representation-matched builder are credible integration candidates. I cannot
issue an unconditional performance GO because the idiomatic reserved builder
gap is intentionally unresolved and the multibyte search matrix straddles the
2x ceiling across fresh executions. An exact-SHA Architect should independently
repeat those two decisions; L7 must disposition the representation exception.
