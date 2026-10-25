# Portable Moss Torture Benchmark Suite

**Version:** 1.0.0  
**Language Specification:** Moss 0.1 (`moss-0.1`)  
**Design Target:** Fully portable, compiler-independent stress corpus for Moss compilers (C++ baseline and Java port).

## 1. Overview

The Portable Moss Torture Suite is a frozen collection of valid and stress-inducing Moss programs and projects designed to systematically evaluate compiler performance, semantic scaling, and type-system boundaries.

This suite is strictly independent of any compiler implementation:
- It contains only standard Moss source files (`.moss`) and project manifests (`Moss.toml`).
- It contains no references to C++ compiler internals, TypeSolver memory structures, or backend codegen artifacts.
- It is runnable unchanged against both the optimized C++ compiler and the Phase 25.3 Java Moss compiler.

## 2. Directory Structure

```text
benchmarks/compiler_torture/
  README.md
  generate.py
  manifest.json
  expected/
    validation_manifest.json
  fixtures/
    specialization/     # W2 (distinct) & W3 (repeated) concrete specialization
    inference/          # W4 (deep chains) & W5 (wide independent inference)
    collections/        # W6 (type count scaling) & W7 (nested collections)
    traits/             # W8 (structural trait & static method resolution)
    modules/            # Module Galaxy (multi-file, multi-module, deep chain, fan-out, diamond)
    examples/           # Real-world canonical Moss applications
    negative/           # Negative test corpus (expected semantic rejections)
    tests/              # Core language semantic regression fixtures
```

## 3. Workload Taxonomy

| Workload ID | Category | Description | Primary Complexity Dimension |
|---|---|---|---|
| **W2** | Specialization | $N$ concrete types, $N^2$ distinct specialization call sites `combine(Sensor_i, Sensor_j)` | Concrete specialization count ($O(N^2)$) |
| **W3** | Specialization | $N$ concrete types, $N^2$ call sites, but only 4 distinct call signatures repeated | Call-site volume vs. specialization caching |
| **W4** | Inference | Long dependent chains of untyped function calls `f_0(x) -> ... -> f_d(x)` | Dependent inference depth ($D$) |
| **W5** | Inference | $W$ independent inference chains of depth 2 | Independent inference width ($W$) |
| **W6** | Collections | $T$ distinct value types with fields, instantiations, and methods | Nominal type count ($T$) |
| **W7** | Collections | Vector collections with explicit nesting depth $K$ ($K=1..5$) | Type representation & element inference depth |
| **W8** | Traits | $C$ concrete types satisfying structural method requirements via untyped dispatch | Static method matching & dispatch resolution |
| **MG1** | Modules | 1 module split across 11 source files | Multi-file compilation unit parsing |
| **MG2** | Modules | 8 independent modules imported by application | Cross-module symbol resolution |
| **MG3** | Modules | Linear module dependency chain ($M_1 \to M_2 \to \dots \to M_8 \to \text{app}$) | Transitive module dependency traversal |
| **MG4** | Modules | Shared provider module imported by 8 consumers | Fan-out interface sharing |
| **MG5** | Modules | Diamond module dependency topology | Diamond dependency deduplication |
| **Examples** | Corpus | Canonical Moss application programs | Real-world idiomatic language patterns |
| **Negative** | Corpus | Standard semantic negative tests | Rejection latency & diagnostic handling |
| **Tests** | Corpus | Core regression test fixtures | Baseline semantic sanity |

## 4. Frozen Contract

Every fixture in `manifest.json` has:
- A stable identifier (`id`)
- Generator version (`1.0.0`) and seed (`20261025`)
- Relative source path
- Exact SHA256 digest
- Expected status (`pass` or `fail`)
- Structured metadata defining workload parameters

The Java Moss compiler benchmark must execute against these exact frozen fixtures, not regenerate them.

## 5. Usage

### Re-validating the Frozen Corpus
```bash
python3 benchmarks/compiler_torture/generate.py --validate-only --moss <path-to-moss-binary>
```

### Running with a Moss Compiler
Any conformant Moss compiler can run:
```bash
<moss-executable> check <fixture-path> --json
```
For project workloads under `fixtures/modules/`:
```bash
<moss-executable> check <fixture-path>/src/main.moss --json
# or
<moss-executable> check <fixture-path> --json
```
