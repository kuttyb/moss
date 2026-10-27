# Phase 26.1 G1 results

- Implementation: `76101e6be2ee216e467e25b1df569571c9ff4d67` on `phase26-1-m1`.
- Main baseline: `d109b8f17276f70b591432fd345d57e727f17501`.
- Architect: GO for the exact implementation SHA above.

## Package behavior

Margo fingerprints package identity and manifest, Moss source bytes, compiler
and native toolchain identity, build mode and flags, dependency semantic
interface hashes, and native dependency artifacts for native builds. Check
builds have a separate output profile; they emit current `.mossi` interfaces
without Rust generation, rustc discovery, or rustc execution. An unchanged
package skips only when its fingerprint and output hashes match. Dependents
are checked when an upstream semantic interface changes; native builds still
relink when a provider rlib changes.

Inherited `MOSS_MODULE_PATH` and package-local `deps/` interfaces are included
recursively in package fingerprints. Their bytes are hashed directly because
ABI-v10 interfaces contain binary records. Provider manifest and source state
are recorded, including empty source trees, so a missing, changed, or restored
provider source cannot leave a consumer on a stale cache key.

## Gates

- `make --jobs="$(nproc)" check`: **143/143 passed**.
- Corpus differential against main: **2,064 sources**, **1,325 accepted**;
  zero verdict or diagnostic changes, and baseline/candidate TSVs were
  byte-identical with generated-Rust hashes enabled.
- Fast Debug parity shard: passed.
- `MOSS_M1_ENFORCE_TARGETS=1 make package-perf`: **68 scenario rows**, every
  R-rule package-count target met. This includes chain propagation (E2 = 2,
  E3 = 4), fan-in checkstop (hub E2 = 9 packages), generic propagation
  (E2 = 3, E3 = 4), and top-application E1–E3 = 1 package. Full counts are in
  [G1_M1_RESULTS.md](G1_M1_RESULTS.md) and
  [m1_phase26_g1.csv](../../benchmarks/phase26_1/results/m1_phase26_g1.csv).
- Compiler performance run `20261026-195844`: 77 fixtures, three interleaved
  timing repetitions, 5.551 s baseline vs 5.559 s candidate total (+0.2%).
  There were no slowdowns beyond the suite's noise floor. The eight attribution
  workloads were:

| Workload | Main median | G1 median | Change |
|---|---:|---:|---:|
| `example_checkout` | 3.65 ms | 3.90 ms | +6.9% |
| `rw_packages_regex` | 757.54 ms | 760.76 ms | +0.4% |
| `rw_projects_rolling_window` | 51.80 ms | 52.41 ms | +1.2% |
| `rw_sdk_datastructures` | 290.11 ms | 290.87 ms | +0.3% |
| `w2_spec_distinct_n032` | 1888.80 ms | 1883.12 ms | -0.3% |
| `w3_spec_repeated_n032` | 929.38 ms | 920.39 ms | -1.0% |
| `w4_infer_deep_d018` | 27.03 ms | 27.12 ms | +0.3% |
| `w5_infer_wide_w100` | 123.38 ms | 130.27 ms | +5.6% |

Small-file latency is included in the run: checkout was 3.65 ms vs 3.90 ms;
rolling window was 51.80 ms vs 52.41 ms. The report classified both increases
below the noise floor. This was a regular comparison run; Phase 26.1 exit will
publish a separate certified compiler-perf run after C1 and T1.

## Review note

The ambient-provider snapshot is conservative: a change to an unrelated
ambient interface may recheck a package. This is safe and is outside the
measured M1 package graphs; narrower selection can be considered if real
package data shows excess work.
