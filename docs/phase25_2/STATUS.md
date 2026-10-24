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
entry point and `OptimizationPlan` remains a shared declaration.

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
declarations should not expand as the other core owners move. FunctionalOptimizer
is implemented and its focused controls pass; its extraction is committed at
`83d6eb927a590318c2cce5fcda2343c334c72872`.

The Native Generator is now independently compiled in `src/native_generator.cpp`.
Its stateful `Generator` class and its handler, borrowed-view, FileIO and
executor lowering fragments remain together in that owner. The driver calls
`generate_native` through a declaration-only interface. Shared parsing records
and backend helpers are declaration-only; canonical helper implementations,
staged ABI configuration and builtin registries remain single-owned in
`moss.cpp`. The initial 50 missing-helper compile errors were closed with this
bounded interface after the Architect advised keeping the Generator whole and
exporting only helpers it actually calls. `src/interpreter.hpp` now gives the
shared staged-ABI query external linkage, preserving its one implementation
and process-wide environment snapshot. Clang development build,
`check_phase20_executor_fileio.py`, and the H3 source-free/hostile chain pass.
A source edit/restore probe rebuilt only `native_generator.o` plus relink.

Checker is now one independently compiled `src/semantic_checker.cpp`, with its
stateful class, `TypeEnv`, checked-body state and existing `.inc` files kept in
the owner. The production API is declaration-only `check_program`; direct C++
controls use that seam and narrow wrappers for existing Checker-owned unit
probes. No Program or AST objects are copied across the boundary. Clang
development build and focused generic-collection, empty-vector ABI-v10,
declaration-boundary, H3 source-free/hostile, and executor/FileIO controls pass.
GCC direct-C++ harnesses also build against the extracted translation units.
A Checker source edit/restore probe rebuilt only `semantic_checker.o` plus
relink.

Two optional direct C++ harnesses remain known failures, reproduced on the
pre-extraction baseline: `check_phase25_generic_arithmetic_requirements.py`
aborts on `checked ABI-v10 requirement provenance identity is forged`, and
`check_phase25_statement_call_occurrence_roles.py` fails its existing private
fact assertion. A separate declaration-authority wrapper stops at
`-Werror=missing-field-initializers` in existing `TypeSolver::Undo` records;
the extraction-specific declaration-boundary harness passes with the existing
warning suppression. These failures are not counted as passes.

Current source distribution: `moss.cpp` 23,786 lines / 1,121,792 bytes;
`semantic_checker.cpp` 30,920 / 1,544,010; `native_generator.cpp` 6,846 /
332,957; `parser.cpp` 1,058 / 45,255; `functional_optimizer.cpp` 1,398 /
58,819. `moss.cpp` now holds shared support, semantic-query/developer tools,
ABI codecs and project/module/CLI orchestration; those remaining regions are
recorded for a later round rather than split into tiny helper TUs. No compiler
semantic change or ABI default change is intended. The Checker checkpoint is
ready to commit before full integrated validation.

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
