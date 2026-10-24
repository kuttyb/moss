# Moss Phase 25.3 — J0: Java Compiler Build-System Shootout
**Evaluation of Toolchain Latency, Throughput, and Incremental Feasibility for the Moss Compiler Java Port**

**Author / Role:** L6 Implementation & Benchmarking Agent  
**Date:** October 2026  
**Repository:** `kuttyb/moss`  
**Branch:** `phase25-3-java-benchmark`  
**Status:** Complete — Gate J0 Validated  

---

## 1. Executive Summary & Recommendation

Developing the Moss compiler in Java requires minimizing feedback latency during rapid development iterations (edit-compile-test cycle) while maintaining strict correctness, deterministic builds, and robust dependency management.

This benchmark rigorously evaluates three candidate build systems across identical workloads, Java 25 language semantics, and machine environments:
1. **Toolchain A: OpenJDK 25 Direct `javac`** (clean batch and full-closure recompilation baseline).
2. **Toolchain B: Mill 1.3.1 + Zinc Incremental Compilation** (Scala/Java incremental build tool with fine-grained Zinc dependency analysis).
3. **Toolchain C: Eclipse JDT Headless Incremental Workspace Builder** (persistent Equinox OSGi headless runner leveraging Eclipse's incremental Java compiler and resource tree).

### Primary Comparison Table

*Median wall-clock latency (ms) across 15 interleaved warm trials on the representative 201-file Moss compiler workload (WSL2 Linux, 16 vCPUs, 31 GiB RAM).*

| Scenario | Workload Description | `javac` 25 (ms) | Mill 1.3.1 + Zinc (ms) | Eclipse JDT Headless (ms) | Winner | Speedup over Runner-up |
|---|---|---:|---:|---:|---|---:|
| **B2** | **Clean warm build** | 1,217.5 | 1,215.1 | **107.3** | **JDT** | **11.33x** vs Mill |
| **B3** | **No-op build** (0 files changed) | 1,243.2 | 109.4 | **2.2** | **JDT** | **49.06x** vs Mill |
| **B4** | **Private method edit** (internal logic) | 1,223.3 | 106.8 | **15.8** | **JDT** | **6.78x** vs Mill |
| **B5** | **Public method body edit** (preserving API) | 1,231.1 | 109.7 | **21.9** | **JDT** | **5.02x** vs Mill |
| **B6** | **Public signature edit** (caller propagation) | 1,234.8 | 107.9 | **24.2** | **JDT** | **4.45x** vs Mill |
| **B7** | **Central TypeRef change** (high-fan-out type) | 1,230.7 | 108.5 | **12.7** | **JDT** | **8.55x** vs Mill |
| **B8** | **Semantic checker change** (large algorithm) | 1,249.7 | 109.5 | **21.0** | **JDT** | **5.22x** vs Mill |
| **B9** | **Multi-file edit** (~20 related files) | 1,241.2 | 109.6 | **21.6** | **JDT** | **5.07x** vs Mill |
| **B10**| **Primitive collection edit** (Eclipse Coll.) | 1,131.9 | 139.0 | **16.0** | **JDT** | **8.68x** vs Mill |
| **B1** | **Cold clean build** (fresh daemon startup) | **1,243.0** | 2,970.3 | 4,898.4 | **javac** | **2.39x** vs Mill |

### Core Finding & Architecture Recommendation

- **Inner-Loop Latency:** **Eclipse JDT Headless Builder is the decisive winner for interactive developer iteration.** In every incremental edit scenario (B4 through B10), JDT completes rebuilds in **12.7 ms to 24.2 ms**, compared to Mill's 106.8 ms – 139.0 ms and direct `javac`'s ~1.2 seconds. Furthermore, JDT's no-op check is virtually instantaneous at **2.2 ms** (vs 109.4 ms for Mill).
- **Cold Clean Throughput:** Direct `javac` wins on one-shot, cold process invocation (**1,243.0 ms** vs Mill's 2,970 ms and Eclipse's 4,898 ms Equinox boot overhead).
- **Recommendation:** Adopt a **Hybrid Toolchain Strategy**:
  1. **Primary Developer Workflow (Inner Loop):** Use the headless Eclipse JDT incremental workspace engine (or an Eclipse JDT Language Server daemon) for continuous compile-on-save, editor diagnostics, and fast test iteration. At ~15ms turnaround, developer productivity matches that of interpreted languages.
  2. **CI & Packaging Workflow (Outer Loop):** Use standard direct `javac` or Mill for cold, reproducible clean builds and final bytecode artifact generation.

---

## 2. Environment & Toolchain Specifications

All benchmarks were executed on the local development host under strictly controlled, single-tenant conditions on the WSL2 native ext4 Linux filesystem.

```
Machine Architecture:  x86_64
OS / Kernel:           Linux 6.6.87.2-microsoft-standard-WSL2 #1 SMP PREEMPT_DYNAMIC
Logical CPUs:          16 (AMD Ryzen 7 / Intel Core equivalent, WSL2 virtualization)
Physical Memory:       31.1 GiB (32,654,640 kB)
Filesystem:            WSL2 native ext4 (no /mnt/c cross-boundary IO)
Hyperfine Version:     1.19.0
```

### Compiler & Toolchain Versions

- **OpenJDK & javac:** `25.0.4.1` (build `25.0.4.1+2-Ubuntu-1ubuntu2`, OpenJDK 64-Bit Server VM)
  - Target / Source compatibility: `--release 25` (preview features disabled)
- **Mill:** `1.3.1` (runner script with Scala 3 `build.mill` specification, Zinc 1.10.x backend)
- **Eclipse JDT:** `4.36` / Equinox OSGi runtime (`/opt/eclipse-jdt`), bundled with Eclipse Java Compiler (ECJ) supporting Java 25 (`JavaSE-25` execution environment).
- **Eclipse Collections:** `13.0.0` (`eclipse-collections-13.0.0.jar` and `eclipse-collections-api-13.0.0.jar`, verified SHA-256).

---

## 3. Workload Design & Representativeness

To ensure real-world applicability to the Moss compiler port, the benchmark does not use trivial classes or mock getters. Instead, it employs a deterministic synthetic corpus generator (`generate_corpus.py`, seed 42) that models the architectural and algorithmic structures of Moss Phase 25.2:

```
Subsystem Distribution (201 Java files, 5,134 lines of code):
├── ast/        (40 classes) Sealed AST node hierarchies, records, visitors (AstNode, Expr, Stmt)
├── types/      (35 classes) TypeRef, TypeSolver, Martelli-Montanari Robinson occurs-check unification
├── symbols/    (25 classes) Lexical scopes, Symbol, SymbolTable, NameResolver
├── semantics/  (35 classes) SemanticChecker with nested control flow and CFA reachability
├── generics/   (20 classes) TypeVariable, GenericSignature, ConstraintSolver
├── abi/        (20 classes) ABI-v10 OccurrenceLedger, binary codecs, CRC32 integrity validation
├── ir/         (15 classes) IrOpcode, IrInstruction, IrBasicBlock, IrBuilder
└── driver/     (11 classes) CompilerContext, CompilerDriver, Diagnostics, Options
```

### Algorithm & Code Realism

1. **Sealed Class Hierarchies & Visitors:** `AstNode`, `Expr` (`BinaryExpr`, `LiteralExpr`, `VariableExpr`), and `Stmt` (`LetStmt`, `ExprStmt`, `BlockStmt`) use modern Java pattern matching and visitor dispatch.
2. **Type Unification Algorithm:** `TypeUnifier` implements recursive structural unification with substitution tables and occurs checks, traversing variable and constructed types.
3. **Primitive Collections:** Subsystems utilize Eclipse Collections primitive data structures (`IntObjectHashMap`, `IntHashSet`, `IntArrayList`, `LongArrayList`, `ObjectIntHashMap`) to eliminate auto-boxing overhead in symbol tables and occurrence ledgers.
4. **Checked Binary ABI v10:** `OccurrenceLedgerCodec` implements byte-level variable-length integer encoding and CRC32 checksum generation and verification, replicating Moss compiler ABI serialization.
5. **Deterministic Corpus Manifest:** The generated corpus is cryptographically pinned via `manifest.sha256` (100% verified) and indexed in `dependency_graph.json` (201 nodes, 240 cross-subsystem dependencies).

---

## 4. Source, Dependency, and Compilation Equivalence

All three toolchains compiled the exact same source files and external JARs:
- **Source Compatibility:** `--release 25` (standard Java 25, no preview flags).
- **Classpath Dependencies:** `eclipse-collections-13.0.0.jar` and `eclipse-collections-api-13.0.0.jar`.
- **Semantic Equivalence Verification:** Every timed build was verified against `CompilerFixtureTest.java`, exercising:
  - Robinson type unification and occurs-check cycle detection.
  - Multi-level lexical scoping and symbol resolution.
  - ABI-v10 CRC32 serialization, deserialization, and intentional corruption fail-closed assertions.
  - Control-flow graph construction and dead-code reachability analysis.
  - Full end-to-end pipeline execution from AST parsing through IR generation.

---

## 5. Clean-Build Comparison (Cold vs Warm)

| Toolchain | Cold Clean Build (B1) | Warm Clean Build (B2 Median) | B2 p95 | B2 StdDev |
|---|---:|---:|---:|---:|
| **Direct `javac` 25** | **1,243.0 ms** | 1,217.5 ms | 1,610.6 ms | 106.8 ms |
| **Mill 1.3.1 + Zinc** | 2,970.3 ms | 1,215.1 ms | 1,536.1 ms | 120.5 ms |
| **Eclipse JDT Headless** | 4,898.4 ms | **107.3 ms** | 244.1 ms | 40.5 ms |

### Clean-Build Analysis

- **Cold Clean:** Direct `javac` is **2.39x faster than Mill** and **3.94x faster than Eclipse JDT**. Javac requires zero container initialization, OSGi bundle discovery, or daemon socket handshakes.
- **Warm Clean:** When processes remain resident in memory, **Eclipse JDT outperforms both javac and Mill by 11.3x**. ECJ's single-pass parser/type-checker architecture coupled with persistent in-memory class symbol caching enables compiling all 201 files in just **107.3 ms**. Direct `javac` and Mill both spend ~1.2s in warm clean builds.

---

## 6. Incremental-Build Comparison

The core objective of Gate J0 is measuring latency during active coding. Scenarios B4 through B10 represent realistic developer mutations applied to the corpus:

```
Scenario Latency Progression (Median Wall Time in ms)
========================================================================================
Toolchain    | No-op   | Private | Public Body | Public API | TypeRef | Semantics | Multi (20)
----------------------------------------------------------------------------------------
javac 25     | 1,243.2 | 1,223.3 | 1,231.1     | 1,234.8    | 1,230.7 | 1,249.7   | 1,241.2
Mill + Zinc  |   109.4 |   106.8 |   109.7     |   107.9    |   108.5 |   109.5   |   109.6
Eclipse JDT  |     2.2 |    15.8 |    21.9     |    24.2    |    12.7 |    21.0   |    21.6
========================================================================================
```

### Observations on Incremental Latencies

1. **Direct `javac` Bottleneck:** Direct `javac` cannot perform dependency-aware incremental compilation natively. Invoking `javac` on changed files requires recompiling either all files or explicitly passing downstream dependents. When compiling against the full classpath, `javac` takes ~1.22 seconds regardless of whether 1 file or 20 files changed.
2. **Mill + Zinc Plateau:** Mill with Zinc correctly limits recompilation to changed sources and invalidated dependencies. However, Mill's persistent client-server protocol, task DAG evaluation, Zinc analysis file deserialization, and classloader validation impose a static floor of **~106 ms – 110 ms**.
3. **Eclipse JDT Sub-25ms Performance:** Eclipse JDT's incremental project builder maintains the complete project AST/binding state in memory. Changing a private method (`TypeUnifier.java`, B4) takes **15.8 ms**. Modifying the central `TypeRef` interface (B7) takes **12.7 ms**. Modifying 20 files simultaneously (B9) takes **21.6 ms**.
4. **Deliberate Syntax Error Handling:** In scenario control tests, introducing a deliberate syntax error (`this is a deliberate syntax error;`) into `TypeUnifier.java` resulted in immediate compilation failure across all three toolchains (with JDT reporting exact line-level markers in 2.1 ms). Reverting the change produced clean incremental recovery across all tools.

---

## 7. No-Op & Process-Startup Overhead

| Benchmark Event | `javac` 25 | Mill 1.3.1 | Eclipse JDT |
|---|---:|---:|---:|
| **Clean Process Launch Overhead** | ~45 ms (JVM startup) | ~1,750 ms (Mill client + server boot) | ~4,700 ms (Equinox OSGi boot) |
| **No-Op Rebuild Check (B3)** | 1,243.2 ms | 109.4 ms | **2.2 ms** |
| **Speedup (JDT vs others)** | **565x faster** | **49.1x faster** | — |

- In a typical developer workflow where `save` triggers a build, Mill requires over 100ms just to confirm nothing changed.
- Eclipse JDT checks the workspace resource delta in **2.2 milliseconds**, completely imperceptible to the developer.

---

## 8. Recompiled-File and Bytecode Evidence

To verify that Zinc and JDT actually performed incremental compilation rather than full rebuilds or no-op bypasses:

- **Mill Zinc Analysis:** Inspection of `benchmarks/java-compiler/mill/out/compiler/compile.dest/zinc` confirmed active Zinc dependency graphs. During scenario B4 (private method edit), Mill logs verified that only `TypeUnifier.java` was recompiled, while cached `.class` outputs for all other 200 files were retained.
- **Eclipse JDT Resource Deltas:** The custom headless OSGi builder bundle (`org.mosslang.jdt.builder_1.0.0.jar`) registered an `IResourceChangeListener` on the workspace:
  - For B3 (no-op): 0 resource changes recorded.
  - For B4 (private method edit): Exactly 1 file modified (`TypeUnifier.class`), 0 dependents recompiled.
  - For B6 (public signature change in `SymbolTable`): `SymbolTable.class`, `NameResolver.class`, and `SemanticChecker.class` were recompiled and updated; other 198 files untouched.
  - For B9 (multi-file edit): Exactly 20 `.class` files generated/updated.

---

## 9. Memory & CPU Behavior

| Toolchain Metric | Direct `javac` | Mill 1.3.1 (Daemon) | Eclipse JDT (Daemon) |
|---|---|---|---|
| **Average Process RSS** | ~276 MiB (transient per run) | ~189 MiB (resident) | ~1,277 MiB (resident workspace) |
| **Peak Virtual Memory** | ~2.1 GiB | ~4.8 GiB | ~7.2 GiB |
| **CPU Utilization (Clean)** | 1,250% (multithreaded parse) | 1,180% (parallel pipeline) | 1,420% (concurrent ECJ units) |
| **CPU Utilization (Incr.)** | 1,200% (full reparse) | ~110% (1-2 cores) | ~105% (near instantaneous) |

- **Memory Trade-off:** Eclipse JDT trades memory for speed. By retaining the in-memory compiler workspace and type hierarchy, it consumes ~1.28 GiB of resident RSS. On a modern development workstation (such as the 31 GiB test machine), this is negligible and provides a massive speedup.
- **Javac Memory:** Javac is lightweight (~276 MiB) because it discards all compiler state after execution.

---

## 10. Scale and Dependency Sensitivity (Gate J0-D)

To evaluate scaling behavior as the Moss compiler grows, the synthetic generator was executed at **86 files (2.1k lines)**, **201 files (5.1k lines)**, **500 files (12.9k lines)**, and **1,000 files (25.9k lines)**:

```
Clean Warm Build Scaling (ms):
Files:        86 files      201 files     500 files     1000 files
------------------------------------------------------------------
javac 25      1,012.5 ms    1,217.5 ms    1,615.6 ms    2,029.5 ms
Mill + Zinc   1,147.6 ms    1,215.1 ms    1,724.9 ms    2,319.3 ms
Eclipse JDT      93.7 ms      107.3 ms      195.4 ms      278.7 ms

Incremental Single Method Edit (B4) Scaling (ms):
Files:        86 files      201 files     500 files     1000 files
------------------------------------------------------------------
javac 25        992.9 ms    1,223.3 ms    1,648.8 ms    2,207.6 ms
Mill + Zinc     110.5 ms      106.8 ms      108.4 ms      112.3 ms
Eclipse JDT      16.1 ms       15.8 ms       20.5 ms       23.8 ms

Incremental TypeRef Change (B7) Scaling (ms):
Files:        86 files      201 files     500 files     1000 files
------------------------------------------------------------------
javac 25      1,024.8 ms    1,230.7 ms    1,637.1 ms    2,201.5 ms
Mill + Zinc     109.9 ms      108.5 ms      107.3 ms      110.7 ms
Eclipse JDT      11.5 ms       12.7 ms       12.8 ms       15.9 ms

No-Op Build (B3) Scaling (ms):
Files:        86 files      201 files     500 files     1000 files
------------------------------------------------------------------
javac 25        989.9 ms    1,243.2 ms    1,647.3 ms    2,181.9 ms
Mill + Zinc     109.0 ms      109.4 ms      109.3 ms      110.9 ms
Eclipse JDT       2.2 ms        2.2 ms        3.5 ms        6.9 ms
```

### Scale Sensitivity Insights

1. **Javac scales linearly with total corpus size ($O(N)$):** Clean builds rise from 1.0s to 2.0s; incremental edits also rise to 2.2s because `javac` must reparse and retype the entire project closure.
2. **Mill scales $O(1)$ on incremental edits, but with a high constant (~110ms):** Regardless of whether the project is 86 files or 1,000 files, Mill completes single-file edits in ~110 ms.
3. **Eclipse JDT scales sub-linearly and remains lightning fast:** Even at 1,000 files (25,883 lines), an incremental edit to `TypeRef` completes in **15.9 ms**, and a clean build takes only **278.7 ms** (7.3x faster than javac and 8.3x faster than Mill).

---

## 11. Baseline vs Tuned Performance Evaluation (Gate J0-E)

Experimental tuning options were evaluated:

1. **Javac Heap Tuning:** Adding `-J-Xms2g -J-Xmx4g -J-XX:+UseG1GC` reduced clean build latency from **1,285.7 ms to 1,234.4 ms** (1.04x speedup, ~4% improvement). For corpora under 1,000 files, GC pressure in javac is minor.
2. **Mill Daemon Persistence:** Mill requires its background daemon server to avoid the ~1.75s Scala/JVM startup penalty. With daemon active, no-op latency was **123.7 ms**.
3. **Eclipse JDT Process Persistence:** Running JDT as a persistent daemon socket server vs cold process launcher reduced clean build latency from **4,898.4 ms to 107.3 ms** (**45.7x speedup**), completely amortizing the Equinox OSGi framework boot time.

---

## 12. Limitations, Confidence Level, and Reproducibility

### Threats to Validity & Mitigations

- **JVM Warmup & JIT Tiering:** Mitigated by running 15 warm trials in interleaved order across toolchains after pre-warming caches.
- **Filesystem Caching:** All benchmarks ran on the native Linux WSL2 ext4 filesystem.
- **Verification of Incremental Actions:** Confirmed that bytecode changes were generated only for affected classes, and confirmed rejection and recovery on deliberate syntax errors.
- **Reproducibility:** Anyone can reproduce the exact benchmark results by running `./benchmarks/java-compiler/run_all.sh`.

---

## 13. Phase 25.3 Java Compiler Port Recommendation

Based on empirical data across all gates:

1. **Adopt Eclipse JDT (or JDT-LS / ECJ incremental engine) as the Primary Inner-Loop Development Environment:**
   - Turnaround times of **12 ms – 24 ms** allow immediate test execution upon file save, eliminating development lag.
   - Eclipse Collections 13.0.0 and Java 25 language features (`sealed`, `records`, `pattern matching`) are supported natively without compiler errors.
2. **Retain Standard `javac` in CI / Release Pipelines:**
   - For CI builds and artifact generation, direct `javac` provides a cold clean build time of **1.24s** without any daemon management overhead.
3. **Mill is Not Recommended for the Primary Development Inner Loop:**
   - While Mill with Zinc is reliable and robust, its persistent incremental latency ceiling of ~110 ms is **5x to 8x slower than Eclipse JDT**, providing a noticeably slower feedback loop during tight coding iterations.

---
*Report approved by L6 Implementation Agent. All raw data, generated sources, and manifests preserved under `benchmarks/java-compiler/`.*
