# Team T cross-package acceptance

This external consumer imports `moss.text`, `moss.encoding`, and all four
Foundation packages through real Margo path dependencies. It verifies text and
strict UTF-8 composition without inventing a String/byte conversion API.

Run from this directory:

```sh
../../margo run
../../margo --release run
../../margo debug
../../margo test
../../margo test --interp
../../margo bench --release --json
python3 validate_release_benchmarks.py
python3 measure_codec.py
```

All application modes print `text encoding acceptance ok`; the package tests
cover valid composition and invalid encoding boundaries.

The explicit benchmark `--release` flag is required: Moss always optimizes the
benchmark root, but Margo uses that flag to select optimized path-dependency
providers. `validate_release_benchmarks.py` runs the corrected command and
checks every reachable provider's `.mossbuild` metadata for the release profile
and `-O`, then SHA-256 matches each rlib copied into `build/bench` to that
provider's release artifact. Both Moss and Rust benchmark sources construct identical workloads
inside each timed body and assert their lengths, contents, result positions,
and checksums before a measurement can be accepted. The Rust reference also
mirrors benchmark-lowering barriers around builder appends and vector pushes.

The codec harness asserts identical 256-emoji contents and materialized output
checksums. It reports both idiomatic Rust UTF-8 operations and matching
`Vec<i64>` algorithms so representation cost is not confused with generated
code overhead. See `docs/PHASE_24_TEXT_ENCODING_UTF8_PERFORMANCE.md`.

The focused casing/builder corpus is in `benches/case_builder.moss`. It retains
the equivalent `Vec<String>` segment baseline and also compares the additive
Moss capacity builder with the same reserved Rust `String` workloads. Finish
(consume), append with a preconstructed segment, and reusable snapshot build
are timed separately. The allocation reference counts the Rust workloads in a
separate untimed run; it does not claim Moss allocation counts:

```sh
rustc --edition=2021 -D warnings -O -C debuginfo=1 case_builder_baseline.rs -o ../../tmp/team-t-case-builder
../../tmp/team-t-case-builder
rustc --edition=2021 -D warnings -O -C debuginfo=1 case_builder_allocations.rs -o ../../tmp/team-t-case-builder-allocations
../../tmp/team-t-case-builder-allocations
```

See `docs/PHASE_24_TEXT_ENCODING_CASE_BUILDER_PERFORMANCE.md` for the matched
workloads, results, and allocation/lowering interpretation.

The x128 Margo search/slice rows amortize input setup but still construct their
inputs inside each timed benchmark invocation; they are end-to-end rows, not
strictly operation-only despite historical `prebuilt` names. The independent
cross-rlib control constructs inputs before timing and compares the actual
optimized `moss.text` provider with the identical local Rust search:

```sh
rustc --edition=2021 -D warnings -O -C debuginfo=1 \
  cross_rlib_find_baseline.rs \
  -L dependency=../text/build/release \
  -L dependency=../foundations/core/build/release \
  --extern moss_text=../text/build/release/libmoss_text.rlib \
  --extern moss_core=../foundations/core/build/release/libmoss_core.rlib \
  -o ../../tmp/team-t-cross-rlib-find
../../tmp/team-t-cross-rlib-find
```
