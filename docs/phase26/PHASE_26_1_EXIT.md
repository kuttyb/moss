# Phase 26.1 exit status

**Status: C1 complete; Architect GO; T1 starts after the reviewed checkpoint is pushed.**
The implementation commit reviewed was `7bc75969ceea7988d45ed9ae619d17c4cde5f970`.

## C1 outcomes

- **`.mossi` is the final stop for dependency interfaces.** F1's freshness and
  source-root rules are covered by `check_phase26_1_f1_interface_rules` in the
  full `make check` suite.
- **`moss check` is file-scoped and semantic queries do not fall through to
  dependency source.** The allowlist regressions are in the same F1 suite;
  C1's reachability is covered by the partial-reach identity assertion and the
  corpus gates (`G-a 654/654`, `G-b 70/70`) over all 438 roots, including
  lowercase `moss.toml` manifests.
- **Margo fingerprints and skips unchanged packages.** The enforced M1 package
  suite passed all 70 scenario rows with
  `MOSS_M1_ENFORCE_TARGETS=1 make package-perf`.

The full local regression passed **145/145**. Enforced `package-perf` passed
all 70 scenarios, and `make c1-corpus-gates` passed with zero failures. The
full corpus differential against `origin/main` reports exactly the 12
documented Calc/Toolkit rows and no other differences. The lowercase-manifest
identity regressions are fixed; the bisect, error site, and resolution are
recorded in `C1_RESULTS.md`. These results await Architect review on the final
commit before C1 closeout; T1 has not started.

## Known regression: cross-package Emacs xref

The cross-package source-location xref test is marked as an expected failure.
It currently resolves to the `.mossi` interface location rather than the
dependency source declaration. Phase 26.1.1 will record declaration locations
in a sidecar file, outside the interface digest. See `C1_RESULTS.md`.

`Triangle.area` and `util.fill` are classified as dependency bodies re-evaluated
inside consumer package checks, and belong to T1. Geolib's own
`margo build --check` rejects `Triangle.area` with `TYPE_MISMATCH` at
`shapes.moss:48` (`method:shapes__Triangle.area@46`).
