# Phase 25.2 G0 production build baseline (partial)

**Status: incomplete; this is not G0 exit evidence.** L7 instructed the worker to stop further measurement after the clean rerun reached O2 clean-build run 5/5. That sample was interrupted before it produced a raw record. No additional runs were started.

## Source and method

The target source is the accepted Phase 25.1 checkpoint `2420439cf1cbaa08c8cd45d0d77b436ea4fb43f0`, verified from `origin/main`. `src/moss.cpp` is 3,099,343 bytes. The harness exports that exact Git revision with `git archive` into isolated copies under `tmp/`; the checked-out tree is not edited. It uses the existing Makefile targets (`moss-dev` for O0 and `moss-opt` for O2), with one Make job. Compiler cache wrappers are not used and `CCACHE_DISABLE=1`, `SCCACHE_DISABLE=1` are set. Timed compiler/link samples use `/usr/bin/time` for wall time, user CPU, system CPU, and maximum RSS. The clean rerun records timestamps and compiler-process/load snapshots before and after each sample, and polls for external compiler processes once per second during a sample. Any sample with detected external compiler activity is marked unusable and automatically repeated.

The controlled rerun did not detect external compiler processes in any of its 44 recorded samples. Each sample has `usable: true`; there were no retries. The matrix was nevertheless stopped before completion. The raw partial results are in [`raw_results.jsonl`](../../benchmarks/phase25_2/raw_results.jsonl), metadata in [`environment.json`](../../benchmarks/phase25_2/environment.json), per-command logs in [`logs/`](../../benchmarks/phase25_2/logs/), and complete-scenario-only medians in [`summary.json`](../../benchmarks/phase25_2/summary.json).

The first matrix is preserved under [`contaminated/`](../../benchmarks/phase25_2/contaminated/). Its environment snapshot showed three unrelated `moss-opt` processes at 90–100% CPU when that matrix began; overlap duration could not be established. All 80 records from that matrix are excluded from performance claims. The clean rerun replaced the active raw files and logs, while preserving the first set under `contaminated/`.

Environment for the clean rerun: GCC 13.3.0 (`/usr/bin/g++`), `-std=c++17 -Wall -Wextra -pedantic` plus the profile optimization flag, Linux WSL2 x86_64, 13th Gen Intel Core i9-13900K, 16 online logical CPUs, one build job. The fresh environment snapshot showed load averages 0.12 / 0.67 / 0.91 and no active compiler process. O0 completed in about 8 minutes; the O2 clean batch was stopped during its fifth sample.

## Clean rerun results

Wall time, user CPU, system CPU, and RSS are medians. GNU `time` reports wall time to 0.01 second, so no-op results below its resolution are stated as `<0.01s`. RSS is in KiB. “Runs” counts usable samples only.

| Profile | Scenario | Runs | Median wall | Median user CPU | Median system CPU | Median max RSS |
| --- | --- | ---: | ---: | ---: | ---: | ---: |
| O0 | Clean `make moss-dev` compile and link | 5/5 | 31.98s | 30.25s | 1.67s | 2,760,924 KiB |
| O0 | Implementation content edit and rebuild | 5/5 | 32.36s | 30.67s | 1.61s | 2,760,892 KiB |
| O0 | No-op `make moss-dev` | 5/5 | <0.01s | 0.00s | 0.00s | 2,508 KiB |
| O0 | Common header `src/ast.hpp` edit and rebuild | 5/5 | 31.99s | 30.44s | 1.37s | 2,760,872 KiB |
| O0 | Low-fanout header `src/rust_float_display.hpp` edit and rebuild | 5/5 | 31.89s | 30.53s | 1.31s | 2,761,084 KiB |
| O0 | Direct compile only (`-c`) | 5/5 | 31.18s | 30.01s | 1.23s | 2,761,092 KiB |
| O0 | Link existing object only | 5/5 | 0.37s | 0.25s | 0.08s | 153,532 KiB |
| O0 | GCC `-ftime-report` instrumented compile | 5/5 | 37.99s | 32.16s | 5.76s | 2,760,996 KiB |
| O2 | Clean `make moss-opt` compile and link | 4/5 | **not reported** | **not reported** | **not reported** | **not reported** |

The four O2 clean records are retained for traceability but are not summarized as a baseline because five clean samples were not completed. No other O2 scenario has a clean sample. The fifth O2 clean build was terminated before the sample could be written. `summary.json` therefore omits O2 clean from `complete_scenarios` and lists its four run IDs under `incomplete_scenarios`.

The header experiments made actual comment-only content edits inside the isolated copies and restored the original bytes after each sample. `ast.hpp` is directly included by `moss.cpp` and four other headers (`typed_semantic_api.hpp`, `checked_body_ir.hpp`, `interpreter.hpp`, and `synchronization_lowering.hpp`). `rust_float_display.hpp` is included by `interpreter.hpp` and reached from the root through `moss.cpp → interpreter.hpp → rust_float_display.hpp`. It is low-fanout, not independent of the translation unit. Both changes trigger the same whole-TU compile under the original broad wildcard prerequisite.

## Compiler phase evidence available

GCC 13.3 `-ftime-report` is instrumentation and is reported separately from the uninstrumented medians above. The median of its five O0 reports is:

| GCC report row | Median wall time | Share of GCC report total |
| --- | ---: | ---: |
| `phase parsing` | 6.04s | 17% |
| `template instantiation` | 6.38s | 18% |
| `phase lang. deferred` | 5.01s | 14% |
| `phase opt and generate` | 24.52s | 69% |
| GCC report `TOTAL` | 35.60s | 100% |

GCC reports `phase opt and generate` as one combined row, so this environment does not separate optimizer time from machine code generation. GCC is not LLVM: LLVM IR generation timing is unsupported here. The G0 O2 phase report was not collected in the clean rerun. GCC’s reported GGC byte totals are allocation-accounting, not peak resident memory; peak RSS is recorded separately by `/usr/bin/time`.

The completed O0 evidence points to compilation as the build bottleneck: direct linking took a median 0.37s compared with 31.18s compile-only; the larger `phase opt and generate` row consumed 69% of instrumented GCC time. These are O0 observations only. The required clean O2 and O2 phase baseline remains open, so no O0-to-O2 comparison or whole-project G0 performance conclusion is made.

## Reproduction and limitations

From a quiet host at the exact accepted revision, run:

```sh
python3 benchmarks/phase25_2/run_baseline.py --runs 5
```

The runner stores all inputs/results/logs under the repository (`tmp/phase25_2_build_baseline/` for disposable source copies). It accepts `--revision` and `--output` overrides. A complete run writes one JSONL record per sample, complete-scenario medians, logs, and an environment file. The first matrix used a pre-run snapshot only and is preserved as contaminated; its raw samples must not be combined with the clean rerun. This worker did not run a complete O2 baseline after the explicit stop instruction, so G0 measurement remains incomplete.
