# Phase 25.3 — Java Compiler Subsystem Build Timing Report

This document tracks clean, full, and incremental build times across the Moss Java compiler subsystems (`compiler-java`), comparing standard `javac` (`scripts/java/build.sh`) against the persistent Eclipse JDT headless builder daemon (`scripts/java/dev-build.sh`).

All live build measurements are automatically logged as newline-delimited JSON to `tmp/build_times.jsonl` on every invocation of `scripts/java/build.sh` and `scripts/java/dev-build.sh`.

---

## 1. Summary Comparison: Clean vs. Full vs. Incremental Builds

| Build Mode | Builder | Target Subsystem | Classes Recompiled | Compile Time (`ms`) | Total Client Wall Time (`ms`) | Speedup vs. Clean `javac` |
|---|---|---|---:|---:|---:|---:|
| **Clean Build (`--clean`)** | `javac` (`build.sh`) | `all` | 111 | 1,477.0 | 1,477.0 | 1.0x |
| **Clean Build (`--clean`)** | Eclipse JDT (`dev-build.sh`) | `all` | 111 | 901.0 | 1,227.7 | 1.2x – 1.6x |
| **Full Rebuild (`--full`)** | Eclipse JDT (`dev-build.sh`, warm) | `all` | 111 | 363.0 – 893.0 | 369.8 – 1,150.9 | 1.3x – 4.0x |
| **Incremental (No-Op)** | Eclipse JDT (`dev-build.sh`) | `all` / `backend` | 0 | **9.0** | **14.3 – 15.0** | **98.5x** |

---

## 2. Incremental Build Times by Compiler Subsystem (Persistent JDT Daemon)

Measured with the persistent Eclipse JDT daemon active on port `45290` (`org.mosslang.jdt.builder.headlessBuilder`) after modifying the primary source file in each subsystem:

| Subsystem | Representative Source Modified | Changed `.class` Outputs | JDT Refresh (`ms`) | JDT Compile (`ms`) | Total Build Time (`ms`) | Speedup vs. Clean `javac` |
|---|---|---:|---:|---:|---:|---:|
| **`query`** | `query/SemanticQueryEngine.java` | 1 | 2.3 | **22.0** | **27.1** | **54.5x** |
| **`semantics`** | `semantics/SemanticChecker.java` | 3 | 3.0 | **45.0** | **49.8** | **29.7x** |
| **`project`** | `project/ProjectBuilder.java` | 1 | 2.3 | **52.0** | **55.7** | **26.5x** |
| **`parser`** | `parser/Parser.java` | 1 | 2.7 | **57.0** | **61.9** | **23.9x** |
| **`interpreter`** | `interpreter/FastInterpreter.java` | 6 | 2.0 | **69.0** | **73.4** | **20.1x** |
| **`backend`** | `backend/NativeGenerator.java` | 2 | 3.6 | **92.0** | **98.6** | **15.0x** |

---

## 3. Cold-Daemon vs. Warm-Daemon Observation

During automated development, we observed an important operational distinction recorded in `tmp/build_times.jsonl`:
- **Cold JDT Daemon (when daemon process is re-spawned per subshell)**: Starting the Equinox/OSGi container and running `INIT` + initial 111-class compilation takes **~888–1,262 ms** (`changed_classes=111`).
- **Warm Persistent JDT Daemon (kept alive across sessions)**: Once initialized, incremental single-subsystem edits recompile only the affected `.class` files (`changed_classes=1..6`) in **22–92 ms** (**27–99 ms** total wall time including socket round-trip and resource sync).

---

## 4. Usage

```bash
# Clean javac build (logged to tmp/build_times.jsonl)
./scripts/java/build.sh --clean --subsystem=all

# Clean or full JDT build
./scripts/java/dev-build.sh --clean --subsystem=all
./scripts/java/dev-build.sh --full --subsystem=all

# Incremental subsystem JDT build during development
./scripts/java/dev-build.sh --subsystem=semantics
./scripts/java/dev-build.sh --subsystem=backend
./scripts/java/dev-build.sh --subsystem=interpreter
./scripts/java/dev-build.sh --subsystem=query
./scripts/java/dev-build.sh --subsystem=project
./scripts/java/dev-build.sh --subsystem=parser
```
