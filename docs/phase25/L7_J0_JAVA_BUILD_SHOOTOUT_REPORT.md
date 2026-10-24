# MOSS PHASE 25.3 — J0-R1: Java Compiler Build-System Shootout
## Executive Briefing & Toolchain Determination for L7

**Branch:** `phase25-3-java-benchmark`  
**Repository:** `kuttyb/moss`  
**Full Technical Report:** [`docs/phase25/PHASE_25_3_J0_JAVA_BUILD_BENCHMARK.md`](PHASE_25_3_J0_JAVA_BUILD_BENCHMARK.md)  
**Reproducibility Entry Point:** [`benchmarks/java-compiler/run_all.sh`](../../benchmarks/java-compiler/run_all.sh)  
**Machine:** WSL2 Linux (ext4), 16 logical CPUs, 31 GiB RAM, OpenJDK 25.0.4.1, Mill 1.3.1, Eclipse JDT (Equinox 4.36), Eclipse Collections 13.0.0.

---

### 1. Executive Summary & Recommendation

Gate J0-R1 addressed the measurement-validity items identified in the initial L7 review. We evaluated:
- **Toolchain A:** OpenJDK 25 Direct `javac`
- **Toolchain B:** Mill 1.3.1 + Zinc Incremental Compilation
- **Toolchain C:** Eclipse JDT Headless Incremental Workspace Builder (persistent Equinox daemon)

#### Definitive Toolchain Verdict: Hybrid Toolchain Architecture

1. **Inner Loop (Developer Interactive Iteration): Eclipse JDT Headless Incremental Builder**
   - **Performance:** Verified rebuild latency of **13.9 ms to 32.7 ms** across all incremental mutation scenarios (B4–B10), and **2.4 ms** for no-op verification.
   - **Speedup:** **12x to 18x faster than Mill/Zinc**, and **37x to 86x faster than direct `javac`**.
   - **Language Compatibility:** Flawless compilation of Java 25 features (`sealed`, `records`, `pattern matching`) and Eclipse Collections 13.0.0 without boxing.
   - **Feedback Cycle:** Provides instant feedback on file save, matching interpreted language turnaround.

2. **Outer Loop (CI / Production Packaging): OpenJDK Direct `javac` 25**
   - **Performance:** Cold clean build in **1,543.0 ms** (3.17x faster than cold JDT and 5.90x faster than cold Mill).
   - **Simplicity:** Zero daemon management overhead or background service dependencies for automated CI runners.

3. **Mill + Zinc Evaluation:**
   - When real source recompilation occurs (as verified in J0-R1), Mill/Zinc takes **239.4 ms to 411.7 ms** per edit. Zinc analysis parsing and task DAG processing impose a noticeable latency floor, making Mill 12x–18x slower than Eclipse JDT for interactive developer iteration.

---

### 2. J0-R1 Correctness & Methodology Repairs

| Issue Identified by L7 | Root Cause in Initial J0 | J0-R1 Correction & Verification |
|---|---|---|
| **Repeated mutations become no-ops** | Baseline restored between trials; identical edits applied repeatedly caused Mill/JDT to encounter cached/no-op states. | Distinct deterministic source mutations generated for every trial $i \in [0, 14]$ without resetting to baseline during incremental runs. Source SHA-256 diff asserted before and after each trial. |
| **Verify actual compiler activity** | Mill reported 0 recompiled files; JDT resource deltas were not tied to output class emission. | Verified Zinc compilation logs and file counts (recompiles 1 file on B4/B5/B8/B10, 3 files on B6, 26 files on B7, 21 files on B9). JDT resource listeners capture both `refreshed_sources` and `changed_classes`. |
| **Strengthen test mutations** | B6 added unused overload; B9 modified dummy comments; B8/B10 lacked algorithmic depth. | B6 modifies public method signature (`lookup(String, int)`) and propagates to callers in `NameResolver` and `SemanticChecker`; B7 updates `TypeRef.typePrecedence()` across 26 classes; B8 changes CFA complexity budget logic; B9 modifies 20 real ABI codec/section files; B10 edits Eclipse Collections primitive operations. |
| **Repair correctness verification** | Fixture tests ran only after restoring baseline; `javac` could implicitly compile missing sources from disk. | Added `MutationVerifier.java` executing test assertions directly against toolchain-under-test class output directories via reflection with `-sourcepath ""` preventing implicit sourcepath compilation. Added `DEP_INVALIDATION` test (caller invalidation verified across all three toolchains). |
| **Correct clean-build comparisons** | B1 clean builds did not cleanly separate cold process invocation from persistent daemons. | B1 runs 5 cold clean builds with full cache/output deletion (`mill clean`, deleting JDT bin/index). B2 runs 15 warm clean builds with resident daemons. |

---

### 3. Comprehensive Shootout Matrix (Verified J0-R1 Results)

*Median wall-clock latency across 15 distinct warm trials (5 trials for B1) on the representative 201-file Moss compiler synthetic corpus (5,199 lines, 8 subsystems).*

| Scenario ID | Scenario Description | Toolchain A (`javac` 25) | Toolchain B (Mill 1.3.1 + Zinc) | Toolchain C (Eclipse JDT Headless) | Winner | Speedup over Runner-up |
|---|---|---:|---:|---:|---|---:|
| **B1** | Cold clean build (fresh process) | **1,543.0 ms** | 9,103.5 ms | 4,897.7 ms | **javac** | **3.17x** vs JDT |
| **B2** | Warm clean build (resident daemon) | 1,203.7 ms | 1,161.0 ms | **116.7 ms** | **JDT** | **9.95x** vs Mill |
| **B3** | No-op build (0 files changed) | 1,215.4 ms | 108.9 ms | **2.4 ms** | **JDT** | **46.13x** vs Mill |
| **B4** | Private method edit (internal logic) | 1,199.4 ms | 248.9 ms | **13.9 ms** | **JDT** | **17.89x** vs Mill |
| **B5** | Public method body edit (preserving API) | 1,217.1 ms | 245.2 ms | **19.2 ms** | **JDT** | **12.78x** vs Mill |
| **B6** | Public signature edit (caller propagation) | 1,200.7 ms | 309.8 ms | **22.0 ms** | **JDT** | **14.06x** vs Mill |
| **B7** | Central TypeRef change (26 files affected) | 1,206.3 ms | 336.9 ms | **28.0 ms** | **JDT** | **12.02x** vs Mill |
| **B8** | Semantic checker change (CFA budget logic) | 1,246.2 ms | 309.4 ms | **21.3 ms** | **JDT** | **14.51x** vs Mill |
| **B9** | Multi-file edit (20 ABI files modified) | 1,243.3 ms | 411.7 ms | **32.7 ms** | **JDT** | **12.59x** vs Mill |
| **B10**| Primitive collection edit (Eclipse Coll.) | 1,222.4 ms | 239.4 ms | **17.3 ms** | **JDT** | **13.84x** vs Mill |

---

### 4. Scale Sensitivity (~50 to 1,000 files)

*Evaluated across 86 files (2.2k LOC), 500 files (13.0k LOC), and 1,000 files (26.0k LOC).*

- **Clean Warm Scaling:**
  - 86 files: JDT **98.3 ms** vs javac 1,014.0 ms vs Mill 1,152.0 ms
  - 500 files: JDT **184.3 ms** vs javac 1,604.4 ms vs Mill 1,727.4 ms
  - 1,000 files: JDT **265.2 ms** vs javac 2,073.6 ms vs Mill 2,148.1 ms
- **Incremental Scaling (Single Method Edit B4):**
  - 86 files: JDT **18.0 ms** vs Mill 251.3 ms
  - 500 files: JDT **17.3 ms** vs Mill 246.5 ms
  - 1,000 files: JDT **16.3 ms** vs Mill 247.9 ms
- **Incremental Scaling (TypeRef Change B7):**
  - 86 files: JDT **43.0 ms** vs Mill 406.0 ms
  - 500 files: JDT **35.4 ms** vs Mill 412.8 ms
  - 1,000 files: JDT **32.9 ms** vs Mill 409.9 ms

---

### 5. Repository Verification & Pushed Commits

- **Branch:** `phase25-3-java-benchmark`
- **Pushed Remote:** `origin/phase25-3-java-benchmark`
- **Key Implementation Files:**
  - `benchmarks/java-compiler/generator/generate_corpus.py` (updated with baseline signatures)
  - `benchmarks/java-compiler/scripts/mutations.py` (deterministic multi-trial mutation engine)
  - `benchmarks/java-compiler/fixtures/MutationVerifier.java` (bytecode reflection verifier with `-sourcepath ""`)
  - `benchmarks/java-compiler/jdt/bundle/src/org/mosslang/jdt/builder/HeadlessBuilderApp.java` (resource delta listener tracking refreshed sources and changed classes)
  - `benchmarks/java-compiler/run_all.sh` (end-to-end reproducibility script)
