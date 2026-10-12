# Foundations cross-package acceptance

This package imports all four Foundations SDK packages through relative Margo
path dependencies. It tests the public contract from a consumer rather than
compiling provider functions in their own package. The normal project prints
`foundations acceptance ok` in native O0, native release, and Fast Debug.

From the repository root, run:

```sh
cd sdk/foundations/acceptance
../../../margo run
../../../margo run --release
../../../margo test --json
../../../margo debug
python3 check_acceptance.py
```

`python3 sdk/foundations/acceptance/run_all.py` is the single root-level
acceptance command. Its default corpus is bounded for `make check`; pass
`--full` to run all 44 oracle cases. The Coordinator wires the regular
command into the repository test runner after integration.

`check_acceptance.py` also executes two **expected-failure** programs:
invalid existing byte data passed to `copy_bytes`, and inverted bounds passed
to `clamp_int`. They must fail the documented assertion in both native
profiles and Fast Debug. The two `nested_generic_*` programs are **positive**
conformance tests for a negated imported generic Bool call. At baseline main
`4c07e31`, Moss checking and Fast Debug accept them, but native compilation
fails with Rust E0425. The compiler repair at integration `05993ab` passes
the targeted regression; this independent gate must also pass on the final
combined tree. The original finding is recorded in
`examples/swarm/phase24_foundations_validation/FEEDBACK.jsonl`.

The deterministic independent oracle tests 44 sorted/search cases, including
empty, duplicate, ordered, reversed, random, and signed-extreme vectors, plus
eight owned-byte copying cases. Python generates both inputs and expected
results. With work under the repository's `tmp/` directory:

```sh
python3 sdk/foundations/acceptance/validate_corpus.py \
  --foundations-root sdk/foundations \
  --workdir tmp/foundations-oracle \
  --margo ./margo
python3 sdk/foundations/acceptance/validate_source_free.py \
  --foundations-root sdk/foundations \
  --workdir tmp/foundations-sourcefree \
  --moss ./moss
```

The oracle uses seed `240024`; the generated Moss source SHA-256 from the
full staged run was
`546807815db9d61fc0ed510620cf50be0bb9e7a2b2f3131580ebc34402b53828`.
The source-free gate builds a `collections` provider, removes its `.moss`
source, then compiles and executes a fresh Int/String generic consumer against
the ABI-v9 `.mossi` and `.rlib`. It invokes Moss directly because current
Margo path dependencies require a source project; no source-free Fast Debug
execution is claimed.

## Benchmarks

Run `../../../margo bench --release --json` from this package. **The explicit
`--release` matters:** without it, the benchmark root is optimized but path
dependency providers are built in debug mode. The plain `margo bench` numbers
are a mixed-profile measurement and cannot be compared with optimized Rust.
For the independent baseline, compile and run `bench_baseline.rs` with
`rustc --edition=2021 -D warnings -O -C debuginfo=1`.

The same-algorithm Rust baseline uses heapsort and validated push-copy, while
the standard-library reference uses `Vec::sort_unstable` and `Vec::clone`.
Each benchmark constructs its input inside the measured body, then observes a
result through `black_box`. The Rust baseline additionally passes the full
input vector through `black_box` before the measured algorithm. Moss uses its
built-in benchmark harness. Both use five warmup rounds of 1,000 invocations
and 31 samples of 1,000 invocations. These timings include allocation and
construction, not only sorting or copying.

First staged run on x86_64, Rust 1.98.1, release providers, medians in ns:

| Workload | Moss SDK | Rust same algorithm | Rust standard library |
| --- | ---: | ---: | ---: |
| Sort 256 descending Ints | 1,638 | 1,547 | 198 |
| Sort 256 permuted Ints | 1,659 | 1,777 | 912 |
| Validate and copy 512 bytes | 978 | 872 | 351 |

The descending standard-library sort recognizes ordered input; that algorithm
is not equivalent to heapsort. The measurements are observations from a shared
machine and should be repeated on the final integrated SHA before quoting a
performance verdict. Benchmark source SHA-256 values:
`e4d23758f67d7a142cae1b1abfa7b020af993fdbf6e2fd66231f837e81ad04fc`
for `benches/foundations.moss`, and
`a295642d1e60b5bcb72d0df57f06c802de3d2493556d2236c3ed43882bd3f9e1`
for `bench_baseline.rs`.
