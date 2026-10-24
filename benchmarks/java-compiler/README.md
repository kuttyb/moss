# Moss Phase 25.3 J0: Java Compiler Build-System Shootout

This directory contains the complete implementation, benchmark harness, synthetic Moss compiler corpus, and experimental results for **Gate J0**: evaluating build systems for developing the Moss compiler in Java.

## Evaluated Toolchains

1. **Toolchain A: OpenJDK 25 Direct `javac`**
   - Clean and changed-file compilation with `--release 25`.
   - Baseline standard compiler invocation with `@sources.txt` filelists and classpath management.
2. **Toolchain B: Mill 1.3.1 + Zinc Incremental Compilation**
   - Persistent Mill daemon server (`build.mill`).
   - Zinc incremental analysis enabled via `zincIncrementalCompilation = Task { true }`.
   - Tracks fine-grained source dependencies and class member invalidation.
3. **Toolchain C: Eclipse JDT Headless Incremental Workspace Builder**
   - Headless Equinox OSGi application (`org.mosslang.jdt.builder_1.0.0.jar`).
   - Uses Eclipse workspace resource tree (`IWorkspace`, `IProject`, `IJavaProject`, `IncrementalProjectBuilder`).
   - Attaches `IResourceChangeListener` on build to record modified/generated `.class` files and compiler markers (`IMarker`).
   - Persistent Unix socket daemon interface eliminating Eclipse launcher startup latency.

---

## Directory Structure

```
benchmarks/java-compiler/
├── corpus/               # Deterministic synthetic Moss compiler corpus (201 files, 5,134 lines)
│   ├── manifest.sha256   # Cryptographic SHA-256 integrity manifest
│   ├── dependency_graph.json # Directed cross-subsystem dependency graph
│   └── org/mosslang/compiler/ # 8 subsystems (ast, types, symbols, semantics, generics, abi, ir, driver)
├── fixtures/             # Deterministic correctness fixture tests
│   └── CompilerFixtureTest.java
├── generator/            # Deterministic synthetic corpus generator
│   └── generate_corpus.py
├── javac/                # Toolchain A configuration and output
│   ├── sources.txt
│   └── bin/
├── mill/                 # Toolchain B configuration and outputs
│   └── build.mill
├── jdt/                  # Toolchain C configuration and bundle
│   ├── configuration/    # Equinox config and bundles.info
│   ├── org.mosslang.jdt.builder_1.0.0.jar # Headless Equinox OSGi builder bundle
│   ├── bin/              # JDT compilation output
│   └── workspace/        # Eclipse workspace state
├── lib/                  # External dependencies (Eclipse Collections 13.0.0)
├── scripts/              # Benchmark, mutation, scale, and tuning scripts
│   ├── mutations.py      # Deterministic mutation harness (B3-B10, syntax error)
│   ├── run_benchmarks.py # 15-trial primary benchmark matrix (B1-B10)
│   ├── run_scale_sensitivity.py # Scaling suite (~50, ~500, ~1000 files)
│   └── run_tuning_evaluation.py # Performance tuning evaluation
├── results/              # Experimental outputs and machine metadata
│   ├── raw_measurements.json
│   ├── summary_results.json
│   ├── summary_table.csv
│   ├── scale_sensitivity.json
│   ├── tuning_results.json
│   └── machine_metadata.json
├── run_all.sh            # One-step top-level reproduction script
└── README.md             # This documentation
```

---

## Quickstart & Reproduction

To run the complete benchmark suite end-to-end:

```bash
# Run complete verification and 15-trial benchmark matrix
./benchmarks/java-compiler/run_all.sh

# Or run with a custom number of warm trials (e.g. 5)
./benchmarks/java-compiler/run_all.sh 5
```

### Running Individual Benchmark Scripts

```bash
# 1. Regenerate synthetic corpus (201 files, deterministic seed 42)
python3 benchmarks/java-compiler/generator/generate_corpus.py --target-files 200 --seed 42 --output-dir benchmarks/java-compiler/corpus

# 2. Verify SHA-256 manifest
cd benchmarks/java-compiler/corpus && sha256sum -c manifest.sha256 && cd ../../..

# 3. Run fixture tests on compiled outputs
java -cp "benchmarks/java-compiler/lib/*:benchmarks/java-compiler/javac/bin" org.mosslang.compiler.fixtures.CompilerFixtureTest

# 4. Run scale sensitivity suite (~50, ~500, ~1000 files)
python3 benchmarks/java-compiler/scripts/run_scale_sensitivity.py

# 5. Run performance tuning evaluation
python3 benchmarks/java-compiler/scripts/run_tuning_evaluation.py
```

---

## Summary of Results

*Measured on 16 vCPU, 31 GiB RAM WSL2 Linux; OpenJDK 25.0.4.1, Mill 1.3.1, Eclipse JDT (Equinox 4.36).*

| Scenario | Description | `javac` (ms) | Mill/Zinc (ms) | Eclipse JDT (ms) | Winner | Speedup |
|---|---|---:|---:|---:|---|---:|
| **B1** | Cold clean build | 1,243.0 | 2,970.3 | 4,898.4 | **javac** | **2.39x** |
| **B2** | Warm clean build | 1,217.5 | 1,215.1 | 107.3 | **JDT** | **11.33x** |
| **B3** | No-op build | 1,243.2 | 109.4 | 2.2 | **JDT** | **49.06x** |
| **B4** | Private method edit | 1,223.3 | 106.8 | 15.8 | **JDT** | **6.78x** |
| **B5** | Public method impl edit | 1,231.1 | 109.7 | 21.9 | **JDT** | **5.02x** |
| **B6** | Public signature edit | 1,234.8 | 107.9 | 24.2 | **JDT** | **4.45x** |
| **B7** | TypeRef change | 1,230.7 | 108.5 | 12.7 | **JDT** | **8.55x** |
| **B8** | Semantic-checker change | 1,249.7 | 109.5 | 21.0 | **JDT** | **5.22x** |
| **B9** | Multi-file edit (~20 files) | 1,241.2 | 109.6 | 21.6 | **JDT** | **5.07x** |
| **B10**| Collection impl change | 1,131.9 | 139.0 | 16.0 | **JDT** | **8.68x** |

### Key Takeaways

1. **Incremental Latency Champion: Eclipse JDT**
   - For all active editing workflows (B4–B10), Eclipse JDT completes incremental builds in **12 ms – 24 ms**, delivering a **4.5x to 8.7x latency advantage over Mill** and a **50x–100x advantage over direct `javac`**.
   - No-op check latency is **2.2 ms** (instantaneous).
2. **Batch / Cold Clean Champion: Direct `javac`**
   - Direct `javac` has negligible launcher overhead and completes a cold clean build in **1.24 s**, outperforming cold Mill (2.97 s) and cold Eclipse launcher startup (4.90 s).
3. **Recommended Architecture: Hybrid Toolchain**
   - **Inner Loop (Developer Interactive):** Eclipse JDT headless incremental workspace builder (or JDT Language Server / Eclipse IDE) for instantaneous (<25ms) edit-test cycles.
   - **Outer Loop (CI / Releases):** Standard `javac` or Mill for clean builds and reproducible package artifacts.
