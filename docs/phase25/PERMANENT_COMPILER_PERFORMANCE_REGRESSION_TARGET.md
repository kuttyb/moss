# Phase 25.4 B1 — Permanent Compiler Performance Regression Target

**Scope:** Automated, permanent performance, attribution, and integrity regression gate  
**Baseline Certified Commit:** `199b9e6d17e7e98921a56043a34f8a508f77cd13`  
**Torture Corpus Version:** `1.0.0` (54 frozen fixtures, manifest digest: `20261025`)  
**Target Executable:** Optimized C++ Moss compiler (`moss-opt`, `-O2`)  

---

## 1. Overview & Purpose

The **Permanent Compiler Performance Regression Target** ensures that future compiler modifications (optimizations, feature additions, refactorings, or architectural transitions) do not silently degrade semantic compilation latency, explode memory usage, disrupt stage profiling attribution, or alter static specialization mechanics.

It establishes an automated, fast (sub-10 second) regression gate that runs as part of repository testing and CI pipelines.

---

## 2. Invocation Interfaces

### A. Makefile Targets

```bash
# Run the permanent performance regression gate against build/compiler/moss-opt
make check-perf

# Run the complete 54-fixture torture benchmark suite (270 trials across 5 reps)
make bench-torture
```

### B. Direct Test Tooling

```bash
# Run standalone with explicit compiler binary
python3 tests/tooling/check_phase25_4_performance_baseline.py build/compiler/moss-opt

# Run in parallel test runner
python3 tests/run_parallel.py --filter check_phase25_4_performance_baseline
```

---

## 3. Three-Stage Verification Architecture

The permanent target executes three strict verification phases:

```
[ Step 1: Corpus Integrity ] ──> [ Step 2: Report Consistency ] ──> [ Step 3: Performance & Invariants ]
   (verify.py: 54/54 SHA256)      (check_report_consistency.py)     (Representative Cohort Execution)
```

### Step 1: Corpus SHA256 Integrity Verification
- Executes `benchmarks/compiler_torture/verify.py`.
- Verifies suite name (`moss_compiler_torture`), version (`1.0.0`), and generator seed (`20261025`).
- Verifies every fixture's exact SHA256 digest against `manifest.json`.
- Verifies complete sorted directory tree digests for multi-module projects (`MG1`–`MG5`).
- **Fail-Closed Policy:** If any benchmark source is modified or missing, the gate aborts immediately.

### Step 2: Data & Report Consistency Audit
- Executes `benchmarks/phase25_4/cpp_baseline/check_report_consistency.py`.
- Recomputes medians, min/max ranges, peak RSS, specialization counts, and stage timings directly from `results/results.csv`.
- Confirms zero contradictions between raw CSV trial observations and published `REPORT.md` claims across 57 invariant checks.

### Step 3: Performance Regression & Semantic Invariant Cohort
- Executes a representative cohort of torture workloads covering all primary scaling axes.
- Measures wall-clock latency, peak RSS, and checked specializations using fresh-process execution with `MOSS_PROFILE_COMPILER=1`.
- Enforces strict acceptance criteria:
  1. **Latency Threshold:** Wall time must not exceed the certified B1 ceiling (+50% margin above baseline median to accommodate CI VM load).
  2. **Memory Ceiling:** Peak RSS must remain within bounded limits (< 18–25 MiB).
  3. **Specialization Invariant:** Checked specialization counts must match ground truth exactly (e.g. W2 N16 = 272; W3 N16 = 6; W8 C50 = 50).
  4. **Status Parity:** Positive workloads must compile with `ok=true`, and negative workloads must fail with their exact expected error codes.

---

## 4. Representative Cohort & Certified Thresholds

The regression cohort spans all key stress dimensions while running in under 6 seconds:

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

## 5. Maintenance and Phase 25.4 B2 Optimization Contract

When Phase 25.4 B2 introduces compiler optimizations (e.g. parallel wide inference, effect set memoization, or specialization caching):

1. **Frozen Comparison:** All optimizations in B2 must be evaluated against the frozen B1 baseline in `results/results_b1_initial.csv` without modifying the B1 reference files.
2. **Threshold Tightening:** Once B2 optimizations are approved and merged, this target's latency thresholds should be tightened to lock in the measured speedups and prevent performance backsliding.
3. **Java Moss Porting:** When the Phase 25.3 Java compiler is measured, `check_phase25_4_performance_baseline.py` can be invoked with the Java binary adapter to determine whether the Java implementation meets or exceeds the C++ performance baseline.
