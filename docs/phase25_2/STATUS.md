# Phase 25.2 compiler build acceleration status

**Branch:** `phase25-2-work`  
**Implementation checkpoint:** `a763a74c0220b85216ef5a6bdb82640c555e70a8` plus uncommitted display runtime extraction  
**State:** G1 is implemented; G2 has 5 production translation units.

## Completed

- Clang 18 and mold 2.30 are installed. The build selects a valid local GCC
  standard-library installation for Clang and links through mold. Debug uses
  `-O0`; optimized uses `-O2`; `CXX=g++` remains an override.
- GNU Make builds an explicit source list in profile/config-specific object
  directories, emits `-MMD -MP` depfiles, and supports parallel compile/link.
  The configuration key includes compiler version/target, toolchain and compile
  flags, linker, and link flags.
- Current TUs are `src/diagnostic_identity.cpp`, `src/display_runtime.cpp`,
  `src/fileio_runtime.cpp`, `src/program_identity.cpp`, and the remaining
  `src/moss.cpp`.
- Focused checks pass: both compiler profiles, same-module type resolution,
  structured diagnostic code/provenance, FileIO semantics/effects, FileIO
  runtime hardening (40 cases), fair leaf admission, executor/FileIO
  integration, and the isolated Make dependency/incremental regression, now
  including GCC override object-tree isolation. The display runtime suite passes
  in both profiles (9 accepted and 20 rejected programs each).
- Feedback validation and `git diff --check` pass. The independent first-extraction Architect reviewed exact base SHA
  `a763a74c0220b85216ef5a6bdb82640c555e70a8` and issued scoped GO with no
  blocker; it recommended adding GCC override coverage, which now passes.

## Remaining

- Display runtime extraction is implemented locally and awaits a bounded
  commit after self-review. Continue cohesive G2 extraction toward approximately
  10–14 TUs, then evaluate
  ETI, out-of-line common-header implementations, and Clang PCH.
- Run final full `make check`, optimized examples, ABI-v9/v10 source-free and
  hostile-artifact gates, GCC compatibility, incremental/no-op checks, and
  clean/representative incremental timings with compile and link time
  separated. No full-suite or final timing result is claimed at this checkpoint.
- Obtain final near-complete exact-SHA Architect review, commit and push the
  candidate to `phase25-2-work`; do not merge to `main`.
