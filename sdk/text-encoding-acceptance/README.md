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
