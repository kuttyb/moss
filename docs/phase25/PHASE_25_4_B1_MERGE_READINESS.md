# Phase 25.4 B1 — C++ ECT benchmark preservation and merge readiness

**Status:** Merge candidate; review and verification gates outstanding.  
**Scope:** Historical, portable benchmarks and C++ `moss check --json` performance instrumentation only. No Java compiler changes, language semantics changes, native Rust optimization, or Fast Debug timing.  
**Source branch:** `phase25-4-cpp-torture-b1` (based on `main` commit `683beccf54a68f4eaa09f0db13ff8443f3396c92`).  
**Related reports:** [B1 baseline](../../benchmarks/phase25_4/cpp_baseline/REPORT.md), [whole-inventory profiling](COMPILER_HOTSPOT_INVESTIGATION.md), [permanent benchmark targets](PERMANENT_COMPILER_PERFORMANCE_REGRESSION_TARGET.md), [historical architect audit](PHASE_25_4_B1_ARCHITECT_AUDIT.md).

## Decision: retain evidence, retract premature diagnosis

The Phase 25.4 B1 **measurements, benchmark corpus, and infrastructure remain useful** and are intended for merger. However, its CPU **root-cause explanation is incomplete**:

- `type_inference`, `effects`, and `specialization_*` denote **compiler phases**, not exclusive costs of the algorithms named by those phases. Expression-text parsing and string-based type manipulation occur *inside* them.
- The `parsing` timer measures initial source parsing. It does **not** isolate parsing of expression strings during checking.
- Peer-reported C++ Callgrind measurements attribute **42–58% of executed instructions** to expression-text parsing across selected examples, with another **7–22%** reported for string-based type handling and **1–5%** for TypeSolver. These values are **not independently reproduced or verified by this B1 deliverable**; instruction shares are not wall-clock shares and category overlap has not been ruled out.
- The parser/checker currently retain textual expressions in statement fields. The Java port has `TypeRef`, but ordinary source expressions are also still primarily textual. Neither the C++ baseline nor this cleanup has implemented a general parse-once expression AST.
- The observed fixed-point rounds, duplicated checks, and effects traversals remain **measurements of repeated work**, not proof that their corresponding algorithmic remedies will produce advertised speedups. Historical performance projections such as 10×–1000× are **withdrawn as acceptance targets**.
- Hierarchical timing stages overlap. The inventory profiler's capped attribution estimate must not be presented as disjoint, instruction-level, or rigorous full-process cost accounting.

## Retained deliverables

1. **Frozen compiler-neutral torture corpus:** 54 fixtures (W2–W8, MG1–MG5, examples, negatives, regression fixtures) with manifest and checksum validation; do not regenerate or modify fixtures during cleanup.
2. **Verified real-world benchmark manifest:** 23 selected workloads, separate from the exploratory 343-root inventory.
3. **C++ performance baseline:** preserved original runs, process timings, counters, Chrome traces, and generated reports. This is a historical `-O2` C++ baseline, **not a measured Java baseline**.
4. **Permanent commands:** `make check-perf`, `make compiler-perf`, `make bench-torture`; workflow and supporting integrity/report checks remain available.
5. **Exploratory inventory:** 343 attempted compilation roots, of which **289 succeeded**, **9 were expected negative rejections**, and **45 failed**. Do not describe all 343 as successful or validated project entrypoints. Failure causes and correct standalone invocation contexts were not exhaustively adjudicated.
6. **Correction to process-level profiling tool:** fix the wait4 pipe deadlock and unenforced `--timeout` by writing child output to temporary files, nonblocking wait4 polling, killing timed-out process groups, and retaining per-child CPU/RSS usage. Focused subprocess tests accompany the fix.

## What this phase does NOT claim

- No precise separation of AST parsing, expression-text reparsing, string-type manipulation, or TypeSolver CPU/allocations in C++ or Java.
- No fully proven disjoint timing attribution; nested stage timings may overlap.
- No accepted recommendation to prioritize fixed-point convergence over parse-once expressions without remeasurement.
- No evidence of Java ECT speedup, incremental performance, or parallel performance from this C++ baseline.
- No compiler semantic change, AST migration, or Rust emit optimization in B1.

## Merge checks

Run from a clean checkout of the **merge candidate exact SHA**; record environment and compiler identity.

```sh
python3 -m unittest discover -s benchmarks/phase25_4 -p 'test_profile_moss.py'
python3 benchmarks/compiler_torture/verify.py
python3 benchmarks/real_world/verify.py
python3 benchmarks/phase25_4/cpp_baseline/check_report_consistency.py
make check-perf
git diff --check main...HEAD
```

The torture generator's `--validate-only` and full `make compiler-perf` remain optional, more expensive follow-ups. Keep benchmarks' immutable baselines separate from machine- or JVM-specific comparisons. Independent reviewer should confirm that all added C++ instrumentation is opt-in and semantics-preserving.

The historic audit was of an earlier SHA and did **not** verify these cleanup edits or the unmeasured expression-string CPU costs. **Do not mark the final merge candidate as independently approved until reviewed.**

## Follow-up after Java parity is accepted (not part of this merge)

1. **Profile Java ECT** (AST parse + checker only) using warm JVM measurements and CPU/allocation profiling, isolating expression-string scanning/splitting, textual type parsing/canonicalization, TypeSolver, effects, and specializations. Count expression reparses per stable source occurrence and context.
2. Establish a **parse-once, immutable source expression AST**, separate from context-specific `TypeRef` results and the existing ABI checked-body `CheckedExpr` projection. Preserve diagnostics, semantic identities, and source spans.
3. Re-run the *unchanged* 54-fixture torture suite and 23 real-world examples. Only then choose incremental semantic analysis, worklist inference, effects memoization, and parallel batch optimization order from measured residuals.

This note supersedes earlier B1 reports **only regarding CPU hotspot interpretation and future speedup projections**; it does not alter historical raw data.
