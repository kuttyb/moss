# Synthetic Build-Speed Experiment (Phase 25.1)

Standalone C++17 benchmark reproducing the compilation characteristics of monolithic `moss.cpp` and evaluating the speedup from translation unit splitting, precompiled headers (PCH), and explicit template instantiation (ETI).

## Directory Structure

- `generate.py`: Deterministic C++17 source generator. Produces ~50K lines of C++ organized into 8 logical subsystems.
- `run_experiment.py`: Benchmark runner. Executes 5 clean builds and 5 incremental rebuilds for both configurations, extracts Clang traces, and sweeps parallel scaling (`-j1`, `-j4`, `-j8`, `-j16`).
- `generated/`: Output directory containing generated C++ sources and headers:
  - `pch.hpp`: Common standard library and platform headers for precompilation.
  - `common_types.hpp`: Domain data structures (`AstNode`, `SymbolFact`, `TypeRecord`, `GraphEdge`, etc.).
  - `eti_declarations.hpp`: `extern template class` declarations for heavy STL containers.
  - `template_instantiations.cpp`: Centralized explicit template instantiations (`template class`).
  - `subsystem_*.{hpp,cpp}`: 8 logical subsystems (parsing, sema, types, graph, transform, indexing, serialize, runtime).
  - `main.cpp`: Orchestrates end-to-end workload and validates deterministic checksum.
  - `unity.cpp`: Monolithic translation unit including all subsystem sources.
- `data/results.json`: Machine-readable raw timing and rusage metrics.
- `traces/`: Raw Clang JSON execution traces from `-ftime-trace`.
- `REPORT.md`: Comprehensive evaluation report with quantitative analysis.

## Reproducing the Benchmark

1. Generate the synthetic codebase:
   ```bash
   python3 benchmarks/phase25_1_synthetic/generate.py --num-functions 34 --seed 42
   ```

2. Run the full experiment:
   ```bash
   python3 benchmarks/phase25_1_synthetic/run_experiment.py
   ```
