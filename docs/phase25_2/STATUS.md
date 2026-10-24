# Phase 25.2 compiler build acceleration status

**Branch:** `phase25-2-work`<br>
**Source candidate:** `1b8ccc489d9223229b54f360c655709c7a5a7e22`<br>
**State:** G1 complete; G2 has 10 production translation units; final validation green; near-final Architect review pending.

## Implementation

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
- Final TUs: `src/char_string_ordering_runtime.cpp`,
  `src/diagnostic_identity.cpp`, `src/display_runtime.cpp`,
  `src/executor_runtime.cpp`, `src/fileio_runtime.cpp`,
  `src/float_format_runtime.cpp`, `src/handler_runtime.cpp`,
  `src/numeric_runtime.cpp`, `src/program_identity.cpp`, and `src/moss.cpp`.
- ETI audit used `nm -C` on optimized objects. The only repeated weak/COMDAT
  template symbols across TUs are standard-library instantiations; no repeated
  expensive Moss-owned specialization is suitable for a justified
  `extern template` pair. No arbitrary standard-library ETI was introduced.

## Final validation

- `make --jobs="$(nproc)" check`: **137/137 passed**, 0 failed, 0 skipped
  (136 Phase 25.1 baseline jobs plus Make dependency coverage). The first run
  was 131/137 because tests still looked for extracted runtimes in headers and
  compiled `moss.cpp` alone; those harnesses were updated, and the final run is
  green.
- `make examples-optimized`: passed for all positive examples.
- Clang Debug/optimized PCH builds and optimized display smoke passed. GCC
  Debug production build/smoke and Clang switch-back relink passed.
- ABI/source-free gates passed: Phase 21 ABI-v9 source-free suite; R3B numeric
  Debug/Release hostile artifacts; M2b2 source-free corpus; ABI-v10 R3F
  source-deleted Debug/Release gate (13 rehashed hostile mutations rejected);
  H3 source-free chain; and ABI-v10 empty-vector provider gate.
- Focused extraction checks passed: same-module type resolution, structured
  diagnostic identity, display (9 accepted / 20 rejected per profile), G3
  ordering, S1.1/S2 equality and Char/String (880 randomized Unicode
  properties), handler 2PL, 10.6D/10.6E and Phase 20 domain checks, fair FIFO,
  executor/FileIO integration, all 40 FileIO runtime cases, and Phase 21 numeric
  parse parity. Feedback and diff validation pass.
- Clean build timings on this 16-job host, measured object/PCH compilation and
  link separately: Debug **18.89 s / 0.58 s**; optimized **94.95 s / 0.55 s**.
  Representative one-TU rebuild: Debug **0.52 s compile / 0.58 s link**;
  optimized **0.43 s / 0.49 s**. No-op `make moss-dev` / `make moss-opt` took
  **0.49 s / 0.47 s** and emitted no build commands.
- The first-extraction Architect reviewed exact commit
  `a763a74c0220b85216ef5a6bdb82640c555e70a8` and issued scoped GO with no
  blocker. Its GCC-override coverage recommendation is implemented.

## Closeout

- Obtain near-final independent Architect review of the frozen candidate and
  record its exact-SHA verdict. Then push normally to the configured
  `phase25-2-work` remote. Do not merge to `main`.
- Two optional direct C++ assertions (`check_phase25_generic_arithmetic_requirements.py`
  and `check_phase25_statement_call_occurrence_roles.py`) fail on their own
  assertions; both were reproduced at pre-extraction commit `19f05677`, so they
  are pre-existing and are not selected by `make check`. The declaration-boundary
  harness itself passes when compiled with the existing missing-initializer
  warning suppressed.
