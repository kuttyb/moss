# Phase 25.2 G0/G1 Execution Plan

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
| Production build measurements | L5 worker in `tmp/phase25-2-bench` | Reproducible scripts, raw measurements, `BUILD_BASELINE.md` | No production source or Makefile edits |
| Compiler architecture inventory | L5 worker in `tmp/phase25-2-arch` | Evidence-backed `TU_DEPENDENCY_MAP.md` | No production source or Makefile edits |
| Integration, G1 and release evidence | L6 Coordinator | Integrate G0; own `Makefile`, dependency regression, execution/validation docs and final branch | Exclusive `Makefile` writer; no `src/moss.cpp` edits |

The workers use independent Git worktrees based on the accepted checkpoint.
Their results will be reviewed and cherry-picked only after G0 evidence is
complete. No more than two L5 workers are active.

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
   dependency edges and risk coverage. G1 may start only after the G0 artifacts
   are integrated and the production extraction sequence is concrete.
4. **G1:** Replace the single compile/link recipe with separate profile-scoped
   objects and compiler-generated depfiles; preserve flags, executable paths,
   aliases and all existing targets. Add automated incremental-build behavior
   checks to the existing tooling regression manifest. Do not split the
   production compiler source list beyond `src/moss.cpp`.
5. **G1 qualification:** Build Debug and optimized binaries, run dependency
   scenarios and the full 136-job `make check`, optimized examples, ABI-v9/v10
   controls, source-free/hostile-artifact gates and parity checks. Capture
   performance on the same host and compare with G0. Review the exact
   integrated SHA, commit and push `phase25-2-work`; do not merge to main.

## Initial risk ledger

| Risk | Evidence / impact | Mitigation and exit condition |
|---|---|---|
| Build configuration changes can leave stale objects | Existing Makefile recompiles directly from source but has no object configuration identity | Preserve override variables and use profile/config-specific object paths or detect configuration changes; test profile isolation |
| Compiler dependency graph is hidden by broad prerequisites | Existing rule depends on every `src/*.hpp` and `src/*.inc` | Use `-MMD -MP` depfiles and prove direct, transitive, removed and unrelated dependency cases |
| Multiple profile builds can race on shared outputs | Existing Makefile has distinct final names but one source compile per profile | Distinct O0/O2 object directories and independent link targets; test parallel combined requests |
| G0 instrumentation can distort timing | Phase reports and memory/time instrumentation alter compile cost | Keep phase reports separate from primary five-run timing; record exact command and tools |
| Available local compiler differs from reference synthetic experiment | Host has GCC 13.3 and no Clang; synthetic report used Clang 18 and simulated source | Report production measurements with complete local identity; do not transfer synthetic ratios to Moss |
| ABI/semantic regressions can be obscured by successful linking | Phase 25.1 added source-free ABI-v10 and hostile artifact guarantees | Run full regression and existing dedicated ABI controls against accepted baseline and G1 build |

## Non-goals and stop point

No compiler extraction, language/ABI changes, PCH, ETI, linker replacement, or
main merge is authorized. Stop after G1 validation and L7 handoff.
