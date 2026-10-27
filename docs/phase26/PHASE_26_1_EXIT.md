# Phase 26.1 exit status

**Status: C1 closeout blocked by Architect NO-GO; T1 has not started.** This report records
the C1 gates on implementation commit `4b14c07a35e44ab3977f6f9eb2ef62e9eb9241f0`.

## C1 outcomes

- **`.mossi` is the final stop for dependency interfaces.** F1's freshness and
  source-root rules are covered by `check_phase26_1_f1_interface_rules` in the
  full `make check` suite.
- **`moss check` is file-scoped and semantic queries do not fall through to
  dependency source.** The allowlist regressions are in the same F1 suite;
  C1's reachability is covered by the partial-reach identity assertion and the
  corpus gates (`G-a 633/633`, `G-b 68/68`).
- **Margo fingerprints and skips unchanged packages.** The enforced M1 package
  suite passed all 70 scenario rows with
  `MOSS_M1_ENFORCE_TARGETS=1 make package-perf`.

The full local regression passed **145/145**. `make c1-corpus-gates` passed
with zero failures. The full corpus differential against `origin/main` did
**not** pass: 14 differences were observed. Twelve are the documented Calc
and Toolkit package-scope rows in `C1_RESULTS.md`; two more accepted on main
but reject here with `MODULE_IDENTITY_UNRESOLVED`:
`examples/priority_queue/src/main.moss` and
`examples/swarm/Julia/CircularBuffer/src/main.moss`. These remain unresolved;
the report does not characterize the corpus differential as green. Architect
reviewed exact commit `5197ae0b` and determined these are real
accepted-to-rejected regressions tied to C1's package identity handling, not
documented scope omissions. The user directed no further C1 code changes, so
C1 cannot be declared closed and T1 has not started.

## Known regression: cross-package Emacs xref

The cross-package source-location xref test is marked as an expected failure.
It currently resolves to the `.mossi` interface location rather than the
dependency source declaration. Phase 26.1.1 will record declaration locations
in a sidecar file, outside the interface digest. See `C1_RESULTS.md`.

`Triangle.area` and `util.fill` are classified as dependency bodies re-evaluated
inside consumer package checks, and belong to T1. Geolib's own
`margo build --check` rejects `Triangle.area` with `TYPE_MISMATCH` at
`shapes.moss:48` (`method:shapes__Triangle.area@46`).
