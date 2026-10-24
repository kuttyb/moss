# Phase 25.2 — Fast execution brief (L7 authorization)

**Starting branch:** `phase25-2-work` (created from accepted main `2420439cf1cbaa08c8cd45d0d77b436ea4fb43f0`).
**Clang-default commit:** `3b8fba8b3ff1f7982d2c9854956b9466be7c91fa`.
**Goal:** Shorten the edit → compile → link cycle by splitting the Moss C++ compiler into approximately 10–14 well-isolated translation units. Prioritize working changes over additional architecture documents or profiling.

## L7 decision: skip the extended G0 gate

Existing measurements and the synthetic proof-of-concept are sufficient to begin. Do not repeat exhaustive compile-time, template, CPU, RSS, or linker profiling now. Compare performance after the split is functional. Use the earlier `phase25-synthetic-build-experiment` branch for reference only.

## Step 1 — Make Clang the development default

The branch Makefile now defaults to `clang++` while respecting explicit `CXX` overrides. Verify `make moss-dev` and `make moss-opt` build correctly with Clang; check an explicit `make CXX=g++` build as a compatibility control, using clean output trees to avoid stale object reuse. Keep the C++17 and `-O0`/ `-O2` behavior unchanged. Do not spend time benchmarking compiler-versus-compiler speed.

## Step 2 — G1 minimum viable multi-TU build system

Introduce an explicit compiler `.cpp` source list, separate object and dependency files per profile (`-MMD -MP`), concurrent object builds, correct linking, and reliable no-op/incremental rebuilds. Preserve all existing Make targets and artifact paths. Ensure changing compiler, flags, or build profile cannot silently reuse incompatible objects. This work must be independently reviewable, and can proceed in parallel with symbol-boundary discovery.

## Step 3 — G2: actually split the compiler

Do not wait for a large architecture inventory document. Inspect and identify safe symbol ownership boundaries just ahead of extraction; record only the decisions necessary to make the next cut. Prefer cohesive header-dependency boundaries to equal source sizes. A provisional partition to validate against real definitions is:

1. driver/CLI
2. source parsing/AST
3. TypeRef/TypeSolver
4. semantic/type checking
5. ownership/effects
6. domains/synchronization
7. functional pipelines/collections
8. semantic API
9. ABI-v9 interface
10. ABI-v10 checked bodies/interface
11. native codegen
12. FileIO/executor lowering
13. Fast Debug interpreter
14. diagnostics and editor queries

Do not force this exact division if the dependency graph contradicts it. First extract a cohesive, lower-coupling subsystem and get a working multi-TU build, then continue in small groups toward 10–14 units. Avoid placing heavy implementations in commonly included headers merely to resolve linker failures.

Before each cut, account for file-local `static` declarations, anonymous namespaces, globals, `.inc` includes, ownership of definitions, and One Definition Rule hazards. Make newly exposed interfaces narrow. Keep one exclusive writer on `src/moss.cpp` while extraction is underway.

## Working cadence and tests

Use **one** L6 Coordinator (also normal Reviewer), **one** L5 compiler-extraction writer, and **one** independent L5 build-system/test worker. Use worktrees and non-overlapping file ownership. The coordinator integrates staged changes and may call an independent L6 adversarial reviewer at the first multi-TU milestone and final handoff, not on every small patch.

Every extraction must compile and link with Clang, pass focused tests for the subsystem moved, and preserve runtime/compiler behavior. Do not run the entire ~17-minute `make check` after each cut. At the integrated final checkpoint, run full `make check`, examples, SDK/source-free/ABI gates, and GCC compatibility, unless L7 explicitly chooses to run the full suite himself. Report any unrun gate accurately.

Preserve the Phase 25.1 accepted semantics and ABI-v9 default / ABI-v10 opt-in. No language refactoring, no default ABI cutover, no major testing-framework rewrite. No merge to main without L7 approval.

## After the split works

Apply **measured** explicit template instantiation with matching `extern template` declarations for the hottest specializations, and move high-cost non-template inline bodies out of shared headers as appropriate. Add PCH and benchmark `lld`/`mold` only after compilation is distributed. Keep GCC and Clang toolchains working.

## Final handoff

Report exact SHA, number/ownership of TUs, focused and full validation outcomes, before/after production clean and representative incremental build times under Clang, aggregate CPU/RSS where convenient, remaining hotspots, and independent review verdict. Use the known pre-phase `-O0` monolithic ~24-second edit baseline as historical context, clearly labeled rather than treated as a same-host controlled comparison. Do not merge to main.

**Priority:** working Clang build → multi-TU build infrastructure → first real extraction → complete split → optimize → measure at end.
