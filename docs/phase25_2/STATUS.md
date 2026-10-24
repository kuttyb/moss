# Phase 25.2 compiler build acceleration status

> The G1 build-acceleration work summarized below is historical. The current
> assignment is L7 Phase 25.2 R2 Core Compiler Decomposition, resumed on
> `phase25-2-work` at `5f8056652668046bca59be5870bd110821d7e159`; its acceptance
> is not established by the earlier G1/G2 status or review.

## R2 core compiler decomposition — reviewed GO — 2026-10-23

The four priority compiler owners are independently compiled production TUs:
`parser.cpp` owns Parser/lexer/source normalization; `functional_optimizer.cpp`
owns `FunctionalOptimizer`; `native_generator.cpp` owns the stateful Generator
and its handler, borrowed-view, FileIO and executor `.inc` fragments; and
`semantic_checker.cpp` owns Checker, TypeEnv, inference/ownership/effect state,
checked-body facts and its existing `.inc` fragments. Each class remains private
to its owner. Narrow declaration-only entry points preserve one authoritative
Program, TypeContext, AST identity and pointer-keyed semantic relationships.
The driver, ABI codecs, semantic-query/developer tools, project/module handling
and CLI orchestration remain in `moss.cpp`; these are the principal regions
for a later decomposition round. ABI-v9 remains the default, ABI-v10 opt-in;
no semantic change is intended.

The first integrated run exposed an important lifetime boundary: `check_program`
returned warnings while destroying Checker immediately, before production
consumers finished using Checker-owned checked facts. The committed follow-up
adds declaration-only RAII `CheckedProgram`, retaining the private Checker for
project validation, checked interpreter setup and standalone compilation.
`check_program` remains as a short-lived convenience seam for direct controls.
This fix was present in the accepted full regression run.

Integrated validation on the Clang+mold default: `make --jobs="$(nproc)" check`
passed **137/137**, 0 failed and 0 skipped; `make --jobs="$(nproc)"
examples-optimized` passed. ABI-v9/v10, source-free, SDK, and hostile-artifact
gates included by the full check passed. The GCC development build passed.
Focused Parser, optimizer, Generator, Checker, executor/FileIO, generic
collection, ABI-v10 empty-vector, declaration-boundary and H3 source-free /
hostile controls passed. The feedback validator and `git diff --check` passed.
Two optional direct controls remain baseline failures reproduced before this
round: `check_phase25_generic_arithmetic_requirements.py` aborts on forged ABI-v10
requirement provenance, and `check_phase25_statement_call_occurrence_roles.py`
fails its private-fact assertion. The declaration-authority wrapper stops at
existing `-Werror=missing-field-initializers`; the extraction-specific
boundary harness passes with the existing warning suppression. These are not
counted as passing validation.

Incremental edit/restore probes compile exactly one affected object and relink:
`parser.o`, `semantic_checker.o`, or `native_generator.o`. Measured object compile
and relink times were Parser 2.08s/0.68s, Checker 10.98s/0.68s, Generator
3.94s/0.70s. Combined edit-to-relink times were 2.05s, 11.24s and 4.17s;
no-op development build was 0.65s. Clean Clang+mold builds on the detected
16-job host took 14.21s at `-O0` and 56.47s at `-O2` (compile plus link).

Current source sizes: `moss.cpp` 23,791 lines / 1,121,922 bytes;
`semantic_checker.cpp` 30,936 / 1,544,580; `native_generator.cpp` 6,846 /
332,957; `parser.cpp` 1,058 / 45,255; `functional_optimizer.cpp` 1,398 /
58,819. `moss.cpp` is substantially reduced from the initial 61,386 lines /
2,993,898 bytes. It now predominantly provides driver/project orchestration,
shared support, developer and semantic-query operations, and ABI codecs.

The Parser checkpoint received independent scoped GO at
`f4a1986982a45ffb94da234f4be201d65b9b5dbf`, with no P0/P1 findings. The
Architect's Generator seam guidance was followed: keep Generator and all its
fragments together and expose only narrow shared helper declarations. Final
integrated-candidate adversarial review is pending. Implementation checkpoints
are `83d6eb927a590318c2cce5fcda2343c334c72872` (optimizer),
`f0a81af2` (Generator), `20b4c120` (Checker), and `a73018dd` (Checker lifetime
fix), all on `phase25-2-work` and pushed to `origin/phase25-2-work`.

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

## R3 — ETI, header isolation and C++ benchmark contract — 2026-10-23

R3 follows the reviewed R2 decomposition on the same `phase25-2-work` branch.
Four non-template implementation regions were moved out of headers into
independently compiled production TUs: numeric literal parsing/format helpers
(`numeric_literal.cpp`), typed semantic projection (`typed_semantic_api.cpp`),
Type IR parsing and syntax validation (`type_ir.cpp`), and checked-body type,
expression, statement, canonical-body and hash encoding (`checked_body_ir.cpp`).
The bodies remain owned by their implementation TUs; declaration headers expose
only their APIs. Checker no longer includes `interpreter.hpp` and includes the
narrow numeric runtime interface it uses directly. Direct C++ test harnesses
were updated to link the new owner objects without weakening assertions.

The optimized symbol and Clang time-trace audit found no safe, measurable
Moss-authored ETI candidate. Repeated symbols are chiefly small standard-library
weak/COMDAT instantiations. No ETI was added. Header fanout for the measured
18-object production build is recorded in
[`FINAL_BUILD_COMPARISON.md`](FINAL_BUILD_COMPARISON.md); a shared `type_ir.hpp`
edit rebuilds ten dependents, while edits to the three R2 core implementation
files rebuild only their owner plus the final executable link.

The reproducible C++ benchmark driver, 21 raw timing records and Java behavior
fixture contract are checked in under `benchmarks/phase25_2/r3/`. On the
16-CPU i9-13900K WSL2 host, three-run medians are 12.71 s clean `-O0`, 50.97 s
clean `-O2`, 0.60 s no-op, and 1.90 / 9.59 / 4.66 s for Parser / Checker /
Generator implementation edit and relink. A `type_ir.hpp` header edit takes
12.39 s and recompiles exactly ten objects. The full method and limits are in
the build comparison document. Java remains an unexecuted behavior contract;
no parity or timing claim is made.

The integrated R3 candidate passed `make --jobs="$(nproc)" check` with
**137/137**, zero failed and zero skipped. Optimized examples, Clang optimized
build, GCC development build and smoke, feedback validation, and the focused
ABI-v9/v10, source-free, SDK and hostile-artifact gates passed. The full check
initially found direct test harnesses that omitted newly separated owner TUs;
the harness link lists were repaired, assertions preserved, and the complete
suite rerun successfully. Exact-SHA final Architect review is pending. R3
implementation commits through the tested code are on `phase25-2-work`; no
merge to `main` has been performed.
