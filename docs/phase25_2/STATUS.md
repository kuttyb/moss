# Phase 25.2 compiler build acceleration status

**Branch:** `phase25-2-work`<br>
**Implementation checkpoint:** `d97ee8922238e95d2c85167da85a9f16d74c0b3f`<br>
**State:** G1 is implemented; G2 has 9 production translation units.

## Completed

- Clang 18 and mold 2.30 are installed. The build selects a valid local GCC
  standard-library installation for Clang and links through mold. Debug uses
  `-O0`; optimized uses `-O2`; `CXX=g++` remains an override.
- GNU Make builds an explicit source list in profile/config-specific object
  directories, emits `-MMD -MP` depfiles, supports parallel compilation, and
  includes compiler/toolchain/flags in the object-tree configuration key.
- Current TUs are `src/char_string_ordering_runtime.cpp`,
  `src/diagnostic_identity.cpp`, `src/display_runtime.cpp`,
  `src/executor_runtime.cpp`, `src/fileio_runtime.cpp`,
  `src/float_format_runtime.cpp`, `src/handler_runtime.cpp`,
  `src/program_identity.cpp`, and the remaining `src/moss.cpp`.
- Focused validation passed: object dependency/incremental/config isolation
  (including `CXX=g++`), same-module type resolution, structured diagnostic
  identity, display contract in Debug and optimized (9 accepted and 20
  rejected programs each), G3 ordering, S1.1/S2 equality and Char/String
  behavior (including 880 randomized Unicode properties), production handler
  2PL, domain 10.6D/10.6E and Phase 20 physical domain checks, fair FIFO
  admission, executor/FileIO integration, and all 40 FileIO runtime hardening
  cases. Clang Debug and optimized builds have both succeeded.
- The first-extraction Architect reviewed exact commit
  `a763a74c0220b85216ef5a6bdb82640c555e70a8` and issued scoped GO with no
  blocker. Its low-priority GCC-override coverage note was implemented and
  passes the isolated Make regression.

## Remaining

- Continue cohesive G2 extraction toward approximately 10–14 TUs. Then evaluate
  ETI from emitted evidence, move expensive common inline implementations out
  of headers where useful, and add Clang PCH incrementally.
- Run final full `make check`, optimized examples, ABI-v9/v10 source-free and
  hostile-artifact gates, GCC compatibility, incremental/no-op checks, and
  clean/representative incremental timings with compile and link time
  separated. Final full-suite results and timings are not yet available.
- Obtain final near-complete exact-SHA Architect review, commit and push the
  candidate to `phase25-2-work`; do not merge to `main`.
