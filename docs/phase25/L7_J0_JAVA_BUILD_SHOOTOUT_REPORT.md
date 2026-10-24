# MOSS PHASE 25.3 — J0: Java Compiler Build-System Shootout
## Executive Briefing & Toolchain Determination for L7

**Branch:** `phase25-3-java-benchmark`  
**Repository:** `kuttyb/moss`  
**Full Technical Report:** [`docs/phase25/PHASE_25_3_J0_JAVA_BUILD_BENCHMARK.md`](PHASE_25_3_J0_JAVA_BUILD_BENCHMARK.md)  
**Reproducibility Entry Point:** [`benchmarks/java-compiler/run_all.sh`](../../benchmarks/java-compiler/run_all.sh)  
**Machine:** WSL2 Linux (ext4), 16 logical CPUs, 31 GiB RAM, OpenJDK 25.0.4.1, Mill 1.3.1, Eclipse JDT (Equinox 4.36), Eclipse Collections 13.0.0.

---

### 1. Executive Summary & Recommendation

The objective of Gate J0 was to empirically determine the fastest practical Java compilation toolchain for developing the Moss compiler in Java (Phase 25.3), comparing:
- **Toolchain A:** OpenJDK 25 Direct `javac`
- **Toolchain B:** Mill 1.3.1 + Zinc Incremental Compilation
- **Toolchain C:** Eclipse JDT Headless Incremental Workspace Builder (persistent Equinox daemon)

#### Definitive Toolchain Verdict: Adopt a Hybrid Toolchain Architecture

1. **Inner Loop (Developer Interactive Iteration): Eclipse JDT Headless Incremental Builder**
   - **Performance:** Rebuild latency of **12.7 ms to 24.2 ms** across all incremental mutation scenarios (B4–B10), and **2.2 ms** for no-op verification.
   - **Speedup:** **4.5x to 8.7x faster than Mill/Zinc**, and **50x to 100x faster than direct `javac`**.
   - **Language Compatibility:** Flawless compilation of Java 25 features (`sealed`, `records`, `pattern matching`) and Eclipse Collections 13.0.0 without boxing.
   - **Feedback Cycle:** Provides instant feedback on file save, matching interpreted language turnaround.

2. **Outer Loop (CI / Production Packaging): OpenJDK Direct `javac` 25**
   - **Performance:** Cold clean build in **1,243.0 ms** (2.39x faster than cold Mill and 3.94x faster than cold Eclipse launcher startup).
   - **Simplicity:** Zero daemon management overhead or background service dependencies for automated CI runners.

3. **Mill + Zinc Evaluation:**
   - Mill provides dependable incremental compilation with Zinc dependency analysis, but its static DAG evaluation and client-daemon IPC ceiling imposes a **~108 ms** floor on every build, regardless of whether 1 file or 20 files changed. It is noticeably more sluggish than Eclipse JDT for interactive edit-compile-test cycles.

---

### 2. Comprehensive Shootout Matrix

*Median wall-clock latency across 15 interleaved warm trials per scenario on the representative 201-file Moss compiler synthetic corpus (5,134 lines, 8 subsystems).*

| Scenario ID | Scenario Description | Toolchain A (`javac` 25) | Toolchain B (Mill 1.3.1 + Zinc) | Toolchain C (Eclipse JDT Headless) | Winner | Speedup over Runner-up |
|---|---|---:|---:|---:|---|---:|
| **B1** | Cold clean build | **1,243.0 ms** | 2,970.3 ms | 4,898.4 ms | **javac** | **2.39x** vs Mill |
| **B2** | Warm clean build | 1,217.5 ms | 1,215.1 ms | **107.3 ms** | **JDT** | **11.33x** vs Mill |
| **B3** | No-op build (0 files changed) | 1,243.2 ms | 109.4 ms | **2.2 ms** | **JDT** | **49.06x** vs Mill |
| **B4** | Private method edit (internal logic) | 1,223.3 ms | 106.8 ms | **15.8 ms** | **JDT** | **6.78x** vs Mill |
| **B5** | Public method body edit (preserving API) | 1,231.1 ms | 109.7 ms | **21.9 ms** | **JDT** | **5.02x** vs Mill |
| **B6** | Public signature edit (caller propagation) | 1,234.8 ms | 107.9 ms | **24.2 ms** | **JDT** | **4.45x** vs Mill |
| **B7** | Central TypeRef change (high-fan-out type) | 1,230.7 ms | 108.5 ms | **12.7 ms** | **JDT** | **8.55x** vs Mill |
| **B8** | Semantic checker change (large algorithm) | 1,249.7 ms | 109.5 ms | **21.0 ms** | **JDT** | **5.22x** vs Mill |
| **B9** | Multi-file edit (~20 related files) | 1,241.2 ms | 109.6 ms | **21.6 ms** | **JDT** | **5.07x** vs Mill |
| **B10**| Primitive collection edit (Eclipse Coll.) | 1,131.9 ms | 139.0 ms | **16.0 ms** | **JDT** | **8.68x** vs Mill |

---

### 3. Scale Sensitivity (~50 to 1,000 files)

*Evaluated across 86 files (2.1k LOC), 201 files (5.1k LOC), 500 files (12.9k LOC), and 1,000 files (25.9k LOC).*

- **Clean Warm Scaling:**
  - 86 files: JDT **93.7 ms** vs javac 1,012.5 ms vs Mill 1,147.6 ms
  - 500 files: JDT **195.4 ms** vs javac 1,615.6 ms vs Mill 1,724.9 ms
  - 1,000 files: JDT **278.7 ms** vs javac 2,029.5 ms vs Mill 2,319.3 ms
- **Incremental Scaling (Single Method Edit B4):**
  - 86 files: JDT **16.1 ms** vs Mill 110.5 ms
  - 500 files: JDT **20.5 ms** vs Mill 108.4 ms
  - 1,000 files: JDT **23.8 ms** vs Mill 112.3 ms
- **Incremental Scaling (TypeRef Change B7):**
  - 86 files: JDT **11.5 ms** vs Mill 109.9 ms
  - 500 files: JDT **12.8 ms** vs Mill 107.3 ms
  - 1,000 files: JDT **15.9 ms** vs Mill 110.7 ms

---

### 4. Gate Verification & Correctness Evidence

1. **Gate J0-A (Workload):** Cryptographically pinned via `manifest.sha256` and `dependency_graph.json`. 100% pass on all 6 fixture tests (`CompilerFixtureTest.java`) covering type unification, occurs-checks, lexical scoping, ABI-v10 CRC32 codec integrity and corruption assertions, and end-to-end IR compilation.
2. **Gate J0-B (Implementations):** All three toolchains compile the exact same source tree and pass the exact same runtime test fixtures.
3. **Gate J0-C (Matrix):** 15 warm trials interleaved; deliberate syntax error rejection and recovery verified across all three toolchains.
4. **Gate J0-D (Scale Sensitivity):** Verified scaling up to 1,000 files / 25,883 lines.
5. **Gate J0-E (Tuning):** Persistent OSGi daemon eliminates 4.8s launcher startup (45.7x speedup).
6. **Gate J0-F (Report & Delivery):** Complete data, raw measurements, CSV tables, scripts, and documentation checked into Git.

---

### 5. Repository Verification & Pushed Commits

- **Branch:** `phase25-3-java-benchmark`
- **Pushed Remote:** `origin/phase25-3-java-benchmark`
- **Commit History:**
  - `0c5cfcf8`: `Phase 25.3 J0: Complete Java compiler build-system shootout benchmark` (all corpus, toolchains, scripts, measurements, report)
  - `9d13b6b8`: `docs(status): record completion of Phase 25.3 J0 Java compiler shootout benchmark`
  - *(This commit)*: `docs(phase25): add L7 briefing and team efficiency tracking closeout`
