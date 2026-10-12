# Phase 24 Team T delivery report

## Delivered scope

Team T forked `phase24-text` from feedback-enabled integration commit
`610f2d632b53541cc31153174e37e1b1640d6f01`. The branch delivers:

- `moss.text`: code-point prefix/suffix/search, exact safe slicing, ASCII trim,
  tokenization and case conversion, non-overlapping replacement, safe split,
  joins, and a vector-backed segment builder;
- `moss.encoding`: strict UTF-8 validation, decoding to Unicode scalar
  integers, encoding from scalar integers, typed first-error evidence, and
  `moss.bytes` interoperability;
- external multi-package acceptance, examples, compiled-only ABI-v9 consumers,
  a deterministic Python oracle, release benchmarks, architecture and API
  documentation, and project/workstream feedback.

Every text index and length counts Unicode code points. No API claims grapheme
segmentation, normalization, locale-aware case behavior, or replacement decode.

## Validation evidence

The candidate was exercised with these actual commands:

```sh
python3 sdk/text-encoding-acceptance/run_all.py --full
python3 sdk/text-encoding-acceptance/validate_release_benchmarks.py
cd sdk/text-encoding-acceptance && ../../margo bench --release --json
python3 sdk/text-encoding-acceptance/measure_codec.py
python3 tests/tooling/check_agent_feedback.py
TMPDIR="$PWD/tmp" make check
make examples
```

The bounded acceptance runner is wired into `tests/run.sh`. It runs both
package suites in native and Fast Debug, the combined external consumer in
native debug/release/Fast Debug, and the two compiled-only provider checks.
Full mode additionally runs 66 valid and 15 invalid Python-oracle cases in all
three execution modes.

Semantic queries confirm `text.builder_append` infers `WRITE` for the builder
and `READ` for the appended String. `encoding.decode_utf8` infers `READ` for its
byte vector and reports the complete typed `Utf8Error` raise set. Performance
methodology and results are in `PHASE_24_TEXT_ENCODING_PERFORMANCE.md`.

`TMPDIR="$PWD/tmp" make check` passed with all Moss v0.1 tests, including
54/54 Executor tests and the wired Team T smoke suite. `make examples` built
every positive repository example. The feedback validator passed, as did the
repository whitespace check.

The original independent Team T L6 Architect issued **GO** for exact
implementation candidate `1c01a899819d495347fd99e05500bb50e50e7da1`. Its
fresh consumer added
scalar boundary encodings, malformed sequences, invalid byte integers after
valid prefixes, overlapping searches/replacements, max-Int slice bounds,
ASCII/NBSP trimming, and combining-sequence builder cases; native debug,
native release, and Fast Debug all passed. The Architect's first review had
correctly rejected `a2f0cd7` because the documented Rust baseline was hidden
by `*.rs` ignore rules and absent from Git; `1c01a89` tracks and reproduces it.

That verdict predates the L7 benchmark-stabilization request. The stabilized
candidate must receive a separate exact-SHA Architect verdict after the full
suite below passes; the final delivery commit records that result.

The stabilization pass also verifies every `Utf8Error` variant's exact index
and payload in package tests, Fast Debug, and a compiled-only ABI-v9 consumer.
UTF-8 validation now validates directly instead of allocating and discarding a
decoded scalar vector. Text search uses the built-in split implementation,
which removes the former per-search character-vector scan and substantially
reduces ASCII case-conversion work.

## Limitations and deferrals

Moss v0.1 has no supported String UTF-8 byte accessor, integer-scalar String
constructor, or strict byte-vector-to-String constructor. Consequently the
complete pure codec (Layer A) is delivered, while String interoperability
(Layer B) is explicitly blocked and not represented as implemented. The
architecture note specifies the smallest proposed conversion pair.

The standard benchmark backend cannot currently lower the codec's typed-error
calls, so encode/decode use a disclosed process-amortized release harness.
ASCII, hexadecimal, and Base64 codecs were optional and are deferred in favor
of UTF-8 correctness and validation. Regex, JSON, CSV, normalization, and full
Unicode casing remain out of scope.

Only Team I may merge this branch into `phase24-integration`.
