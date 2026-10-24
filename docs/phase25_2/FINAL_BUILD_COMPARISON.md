# Phase 25.2 R3 C++ build comparison

This records the R3 C++ build and object-isolation baseline. The checked-in
three-sample records and exact invocation are in
[`benchmarks/phase25_2/r3/results.csv`](../../benchmarks/phase25_2/r3/results.csv)
and [`benchmark_cpp_build.py`](../../benchmarks/phase25_2/r3/benchmark_cpp_build.py).
The Java behavior contract is
[`java_behavior_fixtures.json`](../../benchmarks/phase25_2/r3/java_behavior_fixtures.json);
it defines comparable observations, not Java results.

## Environment and method

- Tested implementation: `b4bc3b7ec9ad74c535ece2aacbcac6fb144e8edd`. Later
  changes through the regression candidate only repaired direct C++ harness
  link lines; no production C++ source changed after the measured SHA.
- Host: WSL2 Linux 6.18.33, Intel Core i9-13900K, 16 online CPUs; GNU Make
  invoked with `--jobs=16`.
- Toolchain: Ubuntu Clang 18.1.3, mold 2.30.0, GCC 13 system headers.
- Command: `python3 benchmarks/phase25_2/r3/benchmark_cpp_build.py --jobs 16
  --samples 3`.
- Every clean sample used a fresh isolated compiler build directory. Compiler
  caches were disabled with `CCACHE_DISABLE=1` and `SCCACHE_DISABLE=1`; OS file
  cache policy was left system-managed. The build generates its PCH inside the
  isolated profile directory. The driver captures wall/user/system time, peak
  RSS, compile/link command counts and the object source list. Incremental
  probes append a temporary comment to an implementation or shared header and
  restore exact source bytes in a `finally` block.
- Clean-build wall time is the complete make invocation, including PCH
  generation, object compilation and final link. Logs distinguish compile and
  link commands, but do not claim separate phase durations.

## C++ results

Wall times are medians and ranges over three runs; CPU and RSS are medians.

| Scenario | Wall median (range) | User / system CPU | Peak RSS | Build work |
|---|---:|---:|---:|---|
| Clean `-O0` | 12.71 s (12.64–13.04) | 35.67 / 2.15 s | 731,196 KiB | 18 object compiles, one PCH, one executable link |
| Clean `-O2` | 50.97 s (50.37–51.35) | 132.94 / 2.08 s | 769,532 KiB | 18 object compiles, one PCH, one executable link |
| No-op `moss-dev` | 0.60 s (0.58–0.60) | 0.39 / 0.28 s | 70,264 KiB | no compile or link |
| Parser implementation edit | 1.90 s (1.89–1.91) | 1.61 / 0.37 s | 237,308 KiB | `parser.o` only, then link |
| Checker implementation edit | 9.59 s (9.58–9.76) | 9.11 / 0.56 s | 731,072 KiB | `semantic_checker.o` only, then link |
| Generator implementation edit | 4.66 s (3.98–5.10) | 4.14 / 0.54 s | 362,656 KiB | `native_generator.o` only, then link |
| Shared `type_ir.hpp` edit | 12.39 s (12.35–12.55) | 35.93 / 1.91 s | 731,160 KiB | 10 dependent objects, then link |

The three core implementation edits each rebuilt only their owning production
object before relinking. The shared type header correctly rebuilt its ten
consumers. The R2 single-sample measurements on the same host were 14.21 s at
`-O0`, 56.47 s at `-O2`, and 0.65 s for a no-op. The R3 clean medians are lower,
but this is not a controlled paired before/after matrix; no causal speedup
claim is made from that comparison. The structural result is the isolated
incremental object rebuild.

The benchmark script counts only executable `moss-dev`/`moss-opt` link outputs;
PCH temporary output is not counted as a link. The CSV is the raw checked-in
measurement record. Logs and isolated build directories remain disposable under
`tmp/` and `build/`.

## ETI and header isolation

The optimized object symbol audit (`nm -C -S`) and Clang `-ftime-trace` review
found repeated weak/COMDAT template symbols primarily in standard-library
container operations and destructors. No repeated, sufficiently expensive
Moss-authored template specialization offered a safe, measurable explicit
instantiation (ETI) candidate. No ETI declarations or instantiation units were
added; this avoids arbitrary standard-library instantiation policy.

The R3 implementation moved non-template numeric literal, typed semantic
projection, Type IR parsing/validation, and checked-body encoding/hash bodies
out of headers and into owning production TUs. Remaining notable header
coupling includes `type_ir.hpp` (10 of 18 objects), `checked_body_ir.hpp` (4),
`typed_semantic_api.hpp` (3), `abi_v10_records.hpp` (3), and
`numeric_literal.hpp` (5). `interpreter.hpp` is now consumed by one production
object after Checker stopped including it. These counts describe Make depfiles
for the measured production object set. The header edit result is evidence of
correct dependency tracking as well as the remaining fanout boundary.

## Java comparison contract

The fixture contract records stable CLI observations for interpreter output,
exact generated Rust bytes in debug mode, a structured ownership diagnostic,
and source-free generic callback execution under ABI-v9 and opt-in ABI-v10.
Java has not run these fixtures, so there is no Java performance or behavioral
parity result to report. A valid future timing comparison must use equivalent
fixtures and host CPU/core budget, separate cold and warm JVM/Gradle-daemon
states, control caches, distinguish startup/compilation from execution, and
report each implementation's actual toolchain and command. The fixture file
explicitly marks Java equivalence unverified.
