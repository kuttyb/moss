# Perf preflight retry note

The first invocation stopped before timing because the corpus integrity verifier found generated Rust and `.mossmap` outputs in five torture module fixtures. The preceding merge-commit `make check` had left 86 ignored generated files in those fixture trees. The untracked/ignored generated files were removed; no tracked fixture source was changed. The subsequent full 77-fixture run validated all corpus hashes and completed three timing repetitions plus profiling. Its timing and work-counter evidence is in `LOCAL_REPORT.md` and the CSV files here.
