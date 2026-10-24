# Phase 25.2 compiler build acceleration status

> The G1 build-acceleration work summarized below is historical. The current
> assignment is L7 Phase 25.2 R2 Core Compiler Decomposition, resumed on
> `phase25-2-work` at `5f8056652668046bca59be5870bd110821d7e159`; its acceptance
> is not established by the earlier G1/G2 status or review.

## R2 core compiler decomposition — in progress — 2026-10-23

The Parser, lexer and source normalization implementation have been moved from
`src/moss.cpp` into independently compiled `src/parser.cpp`. The driver uses a
narrow `parse_source` / `parse_source_lines` API; `Parser` remains private to
its owning TU. Shared syntax helpers are declared in `src/parser_support.hpp`
and retain one implementation owner. `FunctionalOptimizer` is also extracted
intact into `src/functional_optimizer.cpp`; `optimize_program` is its narrow
entry point and `OptimizationPlan` remains a shared declaration. Checker and
Generator are not yet extracted.

Clang development build passes. Focused controls pass: Phase 21 numeric
parsing/conversion parity; generic collection facts; ABI-v10 empty-vector
provider emission; H3 source-free chain/hostile closure; Phase 21 peer-open,
try-placeholder and unary-not pipeline gates; and Phase 15.7 formatter
convergence. Direct C++ harnesses that constructed `Parser` were updated to
use the new frontend API; assertions are unchanged. Parser and optimizer source
edit/restore probes each rebuilt only their own object plus relink. The
independent Architect issued scoped GO on Parser checkpoint
`f4a1986982a45ffb94da234f4be201d65b9b5dbf`, with no P0/P1 findings. The review
confirmed Parser privacy, declaration-only interfaces, one shared Program/AST
identity, and unchanged direct-control assertions. It noted that shared helper
declarations should not expand as the other core owners move. The optional
`check_phase25_generic_arithmetic_requirements.py` control still aborts on
`checked ABI-v10 requirement provenance identity is forged`; the existing
status records this assertion failure as reproduced before extraction at
`19f05677`. It is not counted as a pass. No compiler semantic change or ABI
default change is intended. `src/moss.cpp` is 61,386 lines / 2,993,898 bytes.

**Branch:** `phase25-2-work`<br>
**Tested implementation SHA:** `1b20ca665fe61e1197dec7e1b14f6a584ae54d2c`<br>
**State:** G1 complete; G2 has 10 production translation units; final validation green; independent Architect GO.

The implementation branch includes the required remote checkpoint
`25789d6ec05039a04cbdbd75a11c83e16015fab3` as an ancestor. The checkpoint's
Clang+mold default and updated execution brief were preserved while integrating
the object build. The merge resolution keeps mold configurable through
`MOSS_LDFLAGS`; the effective compiler and linker commands remain unchanged
from the measured build.

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

- On tested SHA `1b20ca665fe61e1197dec7e1b14f6a584ae54d2c`,
  `make --jobs="$(nproc)" check`: **137/137 passed**, 0 failed, 0 skipped
  (136 Phase 25.1 baseline jobs plus Make dependency coverage). The first run
  was 131/137 because tests still looked for extracted runtimes in headers and
  compiled `moss.cpp` alone; those harnesses were updated, and the final run is
  green.
- `make --jobs="$(nproc)" examples-optimized`: passed for all positive
  examples. Rust emitted non-fatal jobserver descriptor warnings because the
  target was invoked under GNU Make parallelism.
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
- The near-final Architect reviewed exact implementation SHA
  `1b20ca665fe61e1197dec7e1b14f6a584ae54d2c` and issued **GO with zero open
  P0/P1/P2**. The review's P2 notes are closed: deleted PCH depfiles force PCH
  and dependent-object recovery, while a mold-version change relinks both
  executables without recompiling compiler objects. The isolated Make
  regression covers both cases.

## Closeout

- Push the reviewed candidate normally to the configured `phase25-2-work`
  remote. Do not merge to `main`.
- Two optional direct C++ assertions (`check_phase25_generic_arithmetic_requirements.py`
  and `check_phase25_statement_call_occurrence_roles.py`) fail on their own
  assertions; both were reproduced at pre-extraction commit `19f05677`, so they
  are pre-existing and are not selected by `make check`. The declaration-boundary
  harness itself passes when compiled with the existing missing-initializer
  warning suppressed.
