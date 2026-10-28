# Phase 26.1 T1 results

Implementation reviewed: `18c04f2ea965fc71c6b835dd6b8690310e514656`.

## Rule and implementation

An imported generic is not checked merely because its `.mossi` module is
imported. Its body is checked when the consumer instantiates it at concrete
types. Imported interface functions and methods are excluded from declaration-
scope inference/checking, and concrete specialization still diagnoses invalid
calls. Generic method admission is deferred only when open generic argument
types prevent a concrete compatibility decision.

The regression suite covers an unused `util.fill` import, a compatible
`fill(Int)` instantiation, and an incompatible `fill(String)` instantiation.
For `natural_app_attempt`, the consumer test seeds a current `.mossi` generated
from a scratch geolib whose `Triangle.area` body is repaired without changing
its exported signature. The original geolib owner check continues to reject
`Triangle.area`; the consumer check does not report diagnostics located in
geolib bodies. The scratch interface has a matching source hash, preserving
the R2 freshness rule. A successful minimal consumer runs immediately after
seeding and asserts Margo checks only the consumer and skips geolib on the
first transition from root to dependency.

## Corpus differential

Compared with C1 closeout `7bc75969`, full 64-hex SHA-256 hashes of generated
Rust and diagnostic codes matched for all unchanged results across 2,064
sources. Exactly these three intended rows changed from
`MOSS_COMPILE_ERROR` to accepted:

- `examples/swarm/modules/algo_chain/repro/generic_poisons_import/single/src/main.moss` — Rust SHA-256 `813f165fba79bf0b53bb8f466602b74d114d87503d8508c02761611949e280e2`
- `examples/swarm/modules/algo_chain/repro/generic_poisons_import/single/src/util.moss` — Rust SHA-256 `f5da1c7c5eb541ba44241023b633155473c1b3f39dc8dcd3de6f7f6362eae09f`
- `examples/swarm/modules/algo_chain/repro/generic_poisons_import/split/provider/src/util.moss` — Rust SHA-256 `af6a3179f7dab45c9d056ee89942f22126b678367caed08e544b57f4e9cb98fa`

The baseline had 1,336 accepted sources and the candidate 1,339. No other
verdict, diagnostic-code, or generated-Rust-hash changes were found. Both
corpus passes used the full-hash runner on the local C1 baseline compiler and
the candidate compiler at `18c04f2e`.

## Gates on the implementation SHA

The package/C1 gates and compiler timing use production-code commit `af9674af`;
reviewed SHA `18c04f2e` differs only by moving the skip regression before the
natural-app negative and updating status. The full regression suite, focused
T1 test, and full-hash corpus differential were run on exact SHA `18c04f2e`.

| Gate | Result |
|---|---|
| `make --jobs="$(nproc)" check` | PASS, 146/146 on exact `18c04f2e` |
| Fast Debug parity (`tests/shards/shard_fast_debug.sh`) | PASS |
| `MOSS_M1_ENFORCE_TARGETS=1 make package-perf` | PASS, 70/70 scenario rows |
| `make c1-corpus-gates` | PASS, 438 roots; G-a 660/660, G-b 67/67 |
| Corpus differential vs `7bc75969` | PASS, exactly the three listed intended transitions |
| Architect review | GO for exact implementation SHA `18c04f2ea965fc71c6b835dd6b8690310e514656` |

## Compiler performance guard

Run `20261027-104141` compared baseline `7bc75969ceea7988d45ed9ae619d17c4cde5f970`
with candidate Margo/compiler source SHA `af9674af5cfd1bdb5b5040ee85f68a646ff75725`
on `DESKTOP-0T772KP` (WSL2, 16 logical CPUs). The only later change in
`18c04f2e` reorders the regression to assert the first root-to-dependency skip;
compiler and Margo sources are unchanged. The run included all 77 fixtures,
three interleaved timing repetitions, and one separate profiling sample per
fixture. The sum of fixture wall-time medians was 5.438 s baseline and 5.415 s
candidate (-0.4%). No fixture exceeded the configured slowdown noise floor.

| Attribution fixture | Baseline | Candidate | Change | Candidate CPU | Candidate peak RSS |
|---|---:|---:|---:|---:|---:|
| `example_checkout` | 3.64 ms | 3.86 ms | +6.1% | <10 ms | 9,508 KiB |
| `rw_packages_regex` | 701.25 ms | 701.24 ms | -0.0% | 700 ms | 24,440 KiB |
| `rw_projects_rolling_window` | 47.52 ms | 47.98 ms | +1.0% | 40 ms | 13,656 KiB |
| `rw_sdk_datastructures` | 260.88 ms | 266.85 ms | +2.3% | 260 ms | 18,324 KiB |
| `w2_spec_distinct_n032` | 1,888.37 ms | 1,877.47 ms | -0.6% | 1,870 ms | 34,680 KiB |
| `w3_spec_repeated_n032` | 912.52 ms | 911.50 ms | -0.1% | 900 ms | 22,316 KiB |
| `w4_infer_deep_d018` | 25.42 ms | 26.33 ms | +3.6% | 20 ms | 10,668 KiB |
| `w5_infer_wide_w100` | 122.11 ms | 131.36 ms | +7.6% | 120 ms | 16,564 KiB |

W5 W100 is above 5% but only 9.25 ms slower, below the 20 ms threshold; its
sample ranges overlap. Other above-5% entries are below the noise floor, with
the largest absolute increase at 1.34 ms. The full report and work counters
are in `tmp/compiler-perf/20261027-104141/` on the work machine.

This is a performance guard run, **not a certified Phase 26.1 baseline**:
certification requires the candidate source to match `origin/main` and a clean
certification checkout. The implementation is still on the review branch, so
a certified run must follow integration. The run metadata records the tree
dirty while the closeout docs and test-order commit were being prepared; it
cannot be relabeled as certified.
