# Phase 25.2 compiler build acceleration status

**Branch:** `phase25-2-work`<br>
**Implementation checkpoint:** `9c48ac8b50cff4189aa71459881fd4f1ecd4d1db`<br>
**State:** G1 is implemented; G2 has 10 production translation units; Clang PCH is enabled.

## Completed

- Clang 18 and mold 2.30 are installed. Clang selects the valid local GCC 13
  C++ headers, `-fuse-ld=mold` selects `/usr/bin/ld.mold`, Debug uses `-O0`,
  optimized uses `-O2`, and `CXX=g++` remains a working override.
- GNU Make builds the explicit source list in profile/config-specific object
  directories, generates `-MMD -MP` dependencies, compiles in parallel, and
  uses profile-specific Clang PCH files from `src/compiler_pch.hpp`. Active-link
  configuration stamps ensure compiler/flag switches relink the fixed
  executable paths. The isolated regression covers implementation/header/PCH
  dependencies, no-op builds, GCC object isolation, and Clang→GCC→Clang relinks
  for both profiles.
- Current TUs: `src/char_string_ordering_runtime.cpp`,
  `src/diagnostic_identity.cpp`, `src/display_runtime.cpp`,
  `src/executor_runtime.cpp`, `src/fileio_runtime.cpp`,
  `src/float_format_runtime.cpp`, `src/handler_runtime.cpp`,
  `src/numeric_runtime.cpp`, `src/program_identity.cpp`, and `src/moss.cpp`.
- Focused validation passed: Make dependency/configuration/PCH checks; same-module
  type resolution; structured diagnostic identity; display checks in Debug and
  optimized (9 accepted and 20 rejected programs per profile); G3 ordering;
  S1.1/S2 equality and Char/String behavior (including 880 randomized Unicode
  properties); production handler 2PL; 10.6D/10.6E and Phase 20 domain checks;
  fair FIFO admission; executor/FileIO integration; all 40 FileIO runtime
  hardening cases; Phase 21 numeric parse parity; R3B numeric source-free and
  hostile cases; compiled-only ABI-v9 numeric consumer; GCC Debug production
  build and smoke; Clang switch-back smoke. Agent feedback and diff checks pass.
- The exact first-extraction commit `a763a74c0220b85216ef5a6bdb82640c555e70a8`
  received independent scoped GO with no blocker. Its GCC configuration coverage
  recommendation is implemented and tested.
- An `nm -C` audit of optimized objects found only standard-library weak/COMDAT
  template duplicates across the extracted units; no repeated expensive
  Moss-owned specialization is present for a justified ETI/extern-template pair.
  No arbitrary standard-library ETI was introduced.

## Remaining

- Run final full `make check` (accepted Phase 25.1 baseline 136/136), optimized
  examples, relevant ABI-v9/v10 source-free and hostile-artifact gates, and the
  final feedback/diff checks.
- Measure final clean and representative incremental builds with compile and
  link time separated. No final timing or performance claim is available yet.
- Obtain the near-final independent exact-SHA Architect review, commit final
  status, and push normally to the configured `phase25-2-work` remote. Do not
  merge to `main`.
