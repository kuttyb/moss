# Phase 25.2 G0/G1 Kickoff Plan (superseded by active L7 assignment)

This kickoff plan predates the active L7 compiler acceleration assignment. The
current scope includes Clang+mold object builds, approximately 10–14 production
translation units, ETI/PCH evaluation, and final qualification. The L6
Coordinator is the sole implementation agent; use [`STATUS.md`](STATUS.md) for
the current implementation checkpoint and gates.

## Accepted starting point

- Integration branch: `phase25-2-work`.
- Accepted Phase 25.1 checkpoint: `2420439cf1cbaa08c8cd45d0d77b436ea4fb43f0`.
- Fetched `origin/main` on 2026-10-23; its exact HEAD was
  `2420439cf1cbaa08c8cd45d0d77b436ea4fb43f0`, identical to the local starting
  HEAD. No unrelated main changes were present.
- The live compiler bootstrap reports `language_version: moss-0.1`.
- Host discovery: Ubuntu 24.04 x86_64, 16 available CPUs, GCC 13.3.0;
  `clang++`, `ccache`, and `sccache` are not installed.
- The referenced `benchmarks/phase25_1_synthetic/REPORT.md` was read from
  `origin/phase25-synthetic-build-experiment`. It describes a synthetic Clang
  model and reports multi-TU/PCH/ETI ratios for that model only; none of those
  figures are production Moss measurements or an acceptance substitute.

## Workstreams and ownership

| Workstream | Owner | Scope | Shared-file lease |
|---|---|---|---|
| Compiler implementation and release evidence | L6 Coordinator | Own Makefile, source extraction, focused gates, final branch and status | Sole source and Makefile writer |

## Gate sequence

1. **G0-A:** Measure actual production compiler builds at `-O0` and `-O2` on
   the accepted baseline, with five samples for each important scenario,
   captured raw values and reproducible commands. Separate uninstrumented
   timing from compiler phase instrumentation. Keep synthetic Phase 25.1
   measurements as context only.
2. **G0-B/C:** Inventory compiler ownership, headers, linkage, major semantic
   pipelines, and costs; propose a dependency-driven 10–14 TU architecture,
   interfaces, move order and rollback boundaries. Do not extract code or
   implement ETI/PCH.
3. **G0 exit review:** Coordinator checks arithmetic, provenance, repeatability,
   dependency edges and risk coverage. The user explicitly stopped the clean
   O2 measurement batch. Therefore the production measurement gate is
   incomplete, with the clean O0 matrix and partial O2 evidence preserved. The
   architecture map is complete. G1 proceeds at the user's direction with no
   performance-gain conclusion.
4. **G1:** Replace the single compile/link recipe with separate profile-scoped
   objects and compiler-generated depfiles; preserve flags, executable paths,
   aliases and all existing targets. Add automated incremental-build behavior
   checks to the existing tooling regression manifest.
5. **G2:** Extract cohesive compiler units incrementally; then evaluate ETI,
   out-of-line header implementations and Clang PCH using build evidence.
6. **Final qualification:** Build both profiles, run full `make check`,
   optimized examples, ABI-v9/v10 source-free and hostile-artifact gates,
   incremental behavior checks, GCC compatibility and separate compile/link
   timings. Obtain exact-SHA Architect review, commit and push
   `phase25-2-work`; do not merge to main.

## Initial risk ledger

| Risk | Evidence / impact | Mitigation and exit condition |
|---|---|---|
| Build configuration changes can leave stale objects | Existing Makefile recompiles directly from source but has no object configuration identity | Preserve override variables and use profile/config-specific object paths or detect configuration changes; test profile isolation |
| Compiler dependency graph is hidden by broad prerequisites | Existing rule depends on every `src/*.hpp` and `src/*.inc` | Use `-MMD -MP` depfiles and prove direct, transitive, removed and unrelated dependency cases |
| Multiple profile builds can race on shared outputs | Existing Makefile has distinct final names but one source compile per profile | Distinct O0/O2 object directories and independent link targets; test parallel combined requests |
| G0 instrumentation can distort timing | Phase reports and memory/time instrumentation alter compile cost | Keep phase reports separate from primary five-run timing; record exact command and tools |
| Available local compiler differs from reference synthetic experiment | Host has GCC 13.3 and no Clang; synthetic report used Clang 18 and simulated source | Report production measurements with complete local identity; do not transfer synthetic ratios to Moss |
| ABI/semantic regressions can be obscured by successful linking | Phase 25.1 added source-free ABI-v10 and hostile artifact guarantees | Run full regression and existing dedicated ABI controls against accepted baseline and G1 build |

## Scope boundary

Preserve compiler semantics, C++17, ABI schemas, and ABI-v9 default. Do not
merge to `main`.
