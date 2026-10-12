# Phase 24 UTF-8 codec performance sprint

This P3 investigation compares the accepted Team T codec at `4a9b9be` with a
source-only optimization. It does not change UTF-8 semantics, public
`Vector[Int]` byte-buffer compatibility, error variants, or payloads.

## Root cause

The original release-generated Rust called the separately compiled
`bytes__is_byte` symbol once per inspected or emitted byte. Cross-package
compilation does not use LTO, so the trivial range predicate was not inlined.
Encoding additionally called a local fallible `append_encoded_byte` for every
output byte. Its generated cold error path implemented `values |> count` as an
iterator rather than `Vec::len`, and every successful call crossed a `Result`
boundary before pushing.

The SDK now performs `0..255` range checks directly in the codec loops and
pushes bytes whose arithmetic construction is already proven by the validated
scalar branch. This removes the now-unused `moss.bytes` dependency rather than
retaining an artificial package edge. Release-generated Rust contains direct
comparisons and `Vec<i64>::push` calls with no `bytes__is_byte` or
`append_encoded_byte` symbol.

A controlled variant retained direct pushes and restored only
`moss.bytes.is_byte`. Its decode median was 1,469 ns and validation median
1,481 ns, versus 772 ns and 619 ns with local checks: the cross-rlib predicate
alone cost about 1.90x and 2.39x respectively on this workload.

Both encoder and decoder still allocate growable result vectors with
`Vec::new`. Moss v0.1 has no `Vector.reserve` or capacity constructor, so
geometric growth cannot be avoided from SDK source. A small capacity primitive
would permit the encoder to reserve at most four times the scalar count and the
decoder at most the byte count; no unsafe operation or source annotation is
needed.

## Comparable benchmark

Run:

```sh
python3 sdk/text-encoding-acceptance/measure_codec.py
```

The harness builds optimized provider and consumer artifacts, performs 5,000
operations per process, uses five warmups and 31 samples, and reports raw
process time divided by operations without subtracting startup. Inputs are 256
`U+1F642` scalars / 1,024 UTF-8 bytes. Every implementation materializes its
promised output and contributes length plus first and last values to a printed
checksum. Validation contributes its Boolean result. Rust has two references:
idiomatic `char`/`str` UTF-8 operations and matching `Vec<i64>` arithmetic.

Final nanoseconds per operation on the sprint machine (`rustc 1.98.1`, LLVM
22.1.8, `-O`, debuginfo 1):

| Operation | Moss p25 / median / p75 | Rust idiomatic median | Rust `i64` median | Moss / idiomatic | Moss / `i64` |
| --- | ---: | ---: | ---: | ---: | ---: |
| Encode | 572 / 614 / 751 | 1,173 | 570 | 0.52x | 1.08x |
| Decode | 657 / 680 / 899 | 560 | 602 | 1.21x | 1.13x |
| Validate | 610 / 625 / 751 | 319 | 563 | 1.96x | 1.11x |

The accepted baseline harness, before this edit, measured Moss encode at
1,467 ns (p25 1,439; p75 1,548) and decode at 1,305 ns (p25 1,265; p75
1,336). Its Rust decoder only counted characters and did not materialize the
decoded result, so that old cross-language ratio is historical timing, not a
fair parity claim.

The existing complete ASCII validation benchmark also improved: the prebuilt
1,024-byte x64 batch measured 27,606 ns median (p25 25,877; p75 30,484), versus
60,994 ns in the prior stabilization report. Dividing a batch median is useful
for attribution but is not a separately sampled distribution.

## Interpretation and limits

Against matching `Vec<i64>` algorithms all three operations are within 1.13x.
Idiomatic Rust validation remains about 2x faster because it operates on
compact `u8` storage and uses the standard library's optimized validator;
Moss's public boundary must reject arbitrary signed `Int` values. Decode is
within 1.5x idiomatic Rust while preserving exact typed errors.

No `perf`, Valgrind, or heaptrack executable was available. Allocation claims
therefore come only from visible `Vec::new`/push structure in release generated
Rust, not inferred counts. The compiler `cost` query reports zero functional
intermediates for these manual loops but does not report vector allocation or
growth, so it was not used as allocation evidence.
