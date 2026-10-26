# Phase 25.4 B1 — Permanent Compiler Performance Regression Target

> **Scope note (2026-10-25):** This certifies the **benchmark corpus, timing capture, regression infrastructure, and historical C++ ECT measurements**, not the accuracy of a fine-grained CPU-hotspot diagnosis. Expression-string reparsing and textual type conversion inside inference/effects were not isolated. Existing stage names and counters remain useful; future Java optimization decisions must use fresh internal CPU and allocation profiling. See [B1 merge-readiness note](PHASE_25_4_B1_MERGE_READINESS.md).


**Scope:** Automated, permanent performance, attribution, tracing, and integrity regression target  
**Baseline Certified Commit:** `199b9e6d17e7e98921a56043a34f8a508f77cd13`  
**Torture Corpus Version:** `1.0.0` (54 frozen fixtures, manifest digest: `20261025`)  
**Real-World Corpus Version:** `1.0.0` (23 verified real-world workloads, manifest digest: `20261025`)  
**Target Executable:** Optimized C++ Moss compiler (`moss-opt`, `-O2`)  

---

## 1. Overview & Purpose

The **Permanent Compiler Performance Regression Target** ensures that future compiler modifications (optimizations, feature additions, refactorings, or architectural transitions) do not silently degrade semantic compilation latency, explode memory usage, disrupt stage profiling attribution, or alter static specialization mechanics.

It delivers:
1. **Permanent `make compiler-perf` Target:** A comprehensive performance runner executing both the portable torture suite (Group A) and real-world Moss workloads (Group B).
2. **Permanent `make check-perf` Gate:** A fast (sub-10 second) regression gate integrated into repository testing (`make check` and `tests/run_parallel.py`).
3. **Structured Hierarchical Tracing:** Chrome Trace Event JSON emission with microsecond stage timelines, work counters, and quantified tracing overhead.
4. **5-Phase Regression Analysis:** Empirical breakdown of regression jobs (`make check`) into Moss checking, Moss codegen, `rustc` compilation, program execution, and harness overhead.

---

## 2. Invocation Interfaces

### A. Makefile Targets

```bash
# Run the fast permanent performance regression gate (<10s, runs in CI/make check)
make check-perf

# Run the full comprehensive performance baseline (Group A + Group B, timing + profiling)
make compiler-perf

# Run the 54-fixture portable torture suite
make bench-torture
```

`make compiler-perf` is the local repeatable timing suite: it runs the 54
portable torture fixtures plus the eight named attribution workloads, with
three untraced timing repetitions per fixture. Raw trials and
`LOCAL_REPORT.md` go to a unique directory under `tmp/compiler-perf/`; the
certified B1 reports are not overwritten. The report includes the sum of
per-fixture medians, all eight attribution workloads, and fixtures whose
median is over 5% slower. That threshold is informational; failed verdicts or
incomplete trials fail the command.

Set `MOSS_PERF_BASELINE` to compare a revision against the current checkout.
The baseline is built in a temporary detached worktree and both revisions are
measured sequentially on the same machine:

```sh
MOSS_PERF_BASELINE=c66fb76c make compiler-perf
```

To include all 23 curated repository Moss workloads, not just those selected
for attribution, set `MOSS_PERF_INCLUDE_REAL_WORLD=1`:

```sh
MOSS_PERF_INCLUDE_REAL_WORLD=1 MOSS_PERF_BASELINE=c66fb76c make compiler-perf
```

`MOSS_PERF_REPS` overrides the default three repetitions. Without a baseline
ref, the command records current-checkout timings only and makes no
before/after claim. The direct historical B1 runner remains available below
for the timing-plus-profiling report.

### B. Direct Runner Invocations

```bash
# Run comprehensive baseline across both Group A (torture) and Group B (real-world)
python3 benchmarks/phase25_4/cpp_baseline/run.py --moss build/compiler/moss-opt --suite all --mode both

# Run timing mode only (untraced, authoritative wall times)
python3 benchmarks/phase25_4/cpp_baseline/run.py --moss build/compiler/moss-opt --mode timing

# Run profiling mode only (generates Chrome Trace Event JSON in results/traces/)
python3 benchmarks/phase25_4/cpp_baseline/run.py --moss build/compiler/moss-opt --mode profiling

# Run the 5-phase regression inventory tool
python3 benchmarks/phase25_4/cpp_baseline/inventory_regression.py --compiler build/compiler/moss-opt
```

---

## 3. Workload Groups

### Group A: Portable Moss Torture Suite (Frozen v1.0.0)
- Located under `benchmarks/compiler_torture/`.
- 54 frozen fixtures covering concrete specialization ($N=8..32$), dependent deep inference ($D=5..18$), independent wide inference ($W=10..100$), nominal types ($T=10..100$), nested collections ($K=1..5$), structural traits ($C=10..100$), module dependency topologies (MG1–MG5), and canonical examples/negatives.
- Cryptographically verified against `manifest.json` by `benchmarks/compiler_torture/verify.py`.

### Group B: Real-World Moss Corpus (v1.0.0)
- Located under `benchmarks/real_world/`.
- 23 verified production workloads spanning packages, SDK modules, multi-file projects, and language regressions:
  - `rw_packages_regex`: Production regex engine (`packages/regex/src/main.moss`) — 21.6s semantic compilation.
  - `rw_sdk_datastructures`: Core collections & datastructures (`sdk/datastructures/src/datastructures.moss`) — 18.3s semantic compilation.
  - `rw_projects_rolling_window`: Statistical rolling window processor (`projects/rolling_window/src/main.moss`).
  - `rw_projects_cache`: Multi-level transactional cache architecture (`projects/cache/src/main.moss`).
  - `rw_phase106d1_borrowed_reads`: Borrowed reference verification (`tests/phase106d1_borrowed_reads.moss`).
  - `rw_phase4_functional`: Higher-order functions and functional pipelines (`tests/phase4_functional.moss`).
  - `rw_phase106d_handler_2pl`: Two-phase locking concurrency handlers (`tests/phase106d_handler_2pl.moss`).
  - Canonical examples: `checkout.moss`, `counter.moss`, `functional_pipeline.moss`, `domains_and_messages.moss`.
- Cryptographically verified against `manifest.json` by `benchmarks/real_world/verify.py`.

---

## 4. Structured Hierarchical Compiler Tracing

The compiler supports opt-in hierarchical tracing emitting Google Catapult / Speedscope compliant Chrome Trace Event JSON:

```bash
# Opt-in trace file generation
MOSS_PROFILE_TRACE=tmp/trace.json ./build/compiler/moss-opt check <file.moss> --json
```

### Trace Event Schema
- Format: Chrome Trace Event JSON (`traceEvents` array + `metadata` block).
- Events (`ph: "X"`): Complete duration events with `ts` (us), `dur` (us), `pid`, `tid`, `name`, `cat`, and contextual `args`.
- Captured Stages:
  - `parsing` — AST parsing and lexical analysis.
  - `module_resolution` — Import path and interface resolution.
  - `declaration_and_symbol_analysis` — Type binding, symbols, and duplicate checks.
  - `type_inference` — Type deduction, including individual `inference_round_N` passes.
  - `open_callable_summaries` — Callable summary construction and export.
  - `effects` — Parameter mutation and effect inference.
  - `concrete_graph` — Concrete domain graph construction.
  - `specialization_checking` — Dispatch checking and body validation (`specialization_body_checking`).
  - `specialization_effects` — Transitive effect reconciliation across concrete specializations.
  - `occurrence_and_summary_projection` — Binding occurrences and assignment reconciliation.
  - `synchronization_plan` — Synchronization classes and 2PL lock ordering.
  - `typed_semantics_export` — Semantic facts projection and consistency assertions.
  - `functional_optimizer` — Functional IR transformation and fusion.
  - `rust_generation` — Native code generation (when compiling to Rust).

### Documented Work Counters
Counter events (`ph: "C"`) and `metadata.counters` record exact compiler workload metrics:
1. `functions_analyzed`: Total AST function declarations admitted into the semantic context.
2. `types_analyzed`: Total distinct objects, enums, traits, and domains declared.
3. `specialization_requests`: Total static specialization requests evaluated (`register_static_specialization`).
4. `distinct_specialization_keys`: Total unique canonical specialization keys encountered.
5. `actual_specialization_checks`: Specialization bodies compiled through `infer_statement_expressions` and `check_stmts`.
6. `reused_specialization_results`: Specialization requests satisfied by cache lookup without re-checking.
7. `inference_rounds`: Number of iterative signature and domain inference passes executed.
8. `inference_work_units`: Atomic callable, field, and handler evaluations across inference rounds.

---

## 5. Timing Mode vs. Profiling Mode (Overhead Quantification)

To avoid measurement distortion, the benchmark runner strictly isolates:
1. **Timing Mode (Untraced):** Runs without profiling or tracing flags. Measures authoritative wall time, user CPU, system CPU, and peak RSS using `/usr/bin/time`. Repeated measurements report median, min, max, IQR, and p95.
2. **Profiling Mode (Traced):** Runs with `MOSS_PROFILE_COMPILER=1` and `MOSS_PROFILE_TRACE`. Captures detailed stage attribution and Chrome traces in `results/traces/`.
3. **Overhead Quantification:** The runner computes `((profiling_median - timing_median) / timing_median) * 100.0` for all fixtures. Tracing overhead is verified to be **< 5%** across all workloads.
4. **Invariant:** Traced runs are never conflated with untraced runs in published latency tables.

---

## 6. 5-Phase Regression Analysis & Root-Cause Findings

Regression jobs in `make check` were analyzed using wrapper interceptors for `moss` and `rustc` to quantify the 5 execution phases:

| Phase | Activity | Description |
|---|---|---|
| **Phase 1** | `moss check` | Moss semantic typechecking and effect inference |
| **Phase 2** | `moss -o rs` | Moss-to-Rust native code generation |
| **Phase 3** | `rustc` | Rust compiler compilation and system linking |
| **Phase 4** | Binary Execution | Execution of produced native test binaries |
| **Phase 5** | Harness Overhead | Python test scheduling, git worktree isolation, file diffing, assertions |

### Key Empirical Findings
- **Root Cause of Long Tests:** Regression tests taking 20–50 seconds (such as `shard_phase4_diff.sh` or `check_phase25_char_string_sdk_packages.py`) are **heavily dominated by `rustc` compilation (Phase 3)**, which accounts for **70%–90%** of total runtime. In `shard_phase4_diff`, 24 separate `rustc` invocations consume **18.33 seconds** of the total 23.16 seconds.
- **Moss Efficiency:** The actual Moss semantic compiler (`moss check`) consumes less than **3%** of total runtime across the entire regression suite, demonstrating that Moss compilation is not the source of slow regression tests.
- **Harness Cost:** Python worktree management, file copies, and diffing account for ~10–15% of total test runtime.

---

## 7. Permanent Regression Gate Thresholds

`make check-perf` executes a representative cohort covering all primary stress dimensions within baseline bounds:

| Workload ID | Dimension Stressed | Baseline Median | Latency Ceiling | Peak RSS Limit | Expected Specs | Expected Status |
|---|---|---:|---:|---:|---:|:---:|
| `w2_spec_distinct_n016` | Distinct specializations ($N=16$, 256 calls) | 728.0 ms | **1,150.0 ms** | 25,000 KiB | **272** | `pass` |
| `w3_spec_repeated_n016` | Repeated specializations ($N=16$, 256 calls) | 556.0 ms | **900.0 ms** | 25,000 KiB | **6** | `pass` |
| `w4_infer_deep_d010` | Dependent chained inference (Depth 10) | 144.0 ms | **250.0 ms** | 20,000 KiB | **11** | `pass` |
| `w5_infer_wide_w025` | Independent wide inference (Width 25) | 734.0 ms | **1,200.0 ms** | 20,000 KiB | **50** | `pass` |
| `w6_types_count_t050` | Nominal type layouts (50 types) | 120.0 ms | **200.0 ms** | 20,000 KiB | — | `pass` |
| `w7_collections_nested_k3` | Nested generic collection (`Vector^3[Int]`) | 3.1 ms | **15.0 ms** | 18,000 KiB | — | `pass` |
| `w8_traits_dispatch_c050` | Structural trait dispatch (50 types) | 120.0 ms | **200.0 ms** | 20,000 KiB | **50** | `pass` |
| `mg4_fan_out` | Multi-module provider fan-out (10 modules) | 60.0 ms | **120.0 ms** | 20,000 KiB | — | `pass` |
| `example_checkout` | Idiomatic application baseline | 10.6 ms | **30.0 ms** | 18,000 KiB | — | `pass` |
| `negative_await_retired` | Rejection latency & error attribution | 1.0 ms | **10.0 ms** | 18,000 KiB | — | `fail` |

---

## 8. Verification and Consistency Checklist

Before any integration or commit:
- [x] `python3 benchmarks/compiler_torture/verify.py` passes 100% (54/54 fixtures).
- [x] `python3 benchmarks/real_world/verify.py` passes 100% (23/23 fixtures).
- [x] `python3 benchmarks/phase25_4/cpp_baseline/check_report_consistency.py` passes 100% (57 invariant checks, 0 errors).
- [x] `make check-perf` passes all representative workloads within baseline bounds.
- [x] Structured Chrome Trace Event JSON validated with `python3 -m json.tool`.
