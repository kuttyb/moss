# Phase 26.1 exit status

**Status: T1 implementation gates passed; Architect GO is recorded. Certified post-integration compiler-perf remains.**
The C1 implementation checkpoint is `7bc75969ceea7988d45ed9ae619d17c4cde5f970`; Architect GO is for T1 SHA `18c04f2ea965fc71c6b835dd6b8690310e514656`.

## T1 outcome

Imported generic bodies are not checked merely because the interface module was
imported. They are checked when concretely instantiated; imported `.mossi`
bodies do not re-enter declaration-scope inference/checking. The regression
test demonstrates unused import acceptance plus compatible and incompatible
concrete instantiations. For `natural_app_attempt`, the test seeds a fresh,
source-hash-current `.mossi` from a repaired scratch copy of geolib; the
consumer produces no geolib-body diagnostic, while geolib's own check still
rejects `Triangle.area`.

The final C1-baseline differential covers 2,064 sources. Exactly three
`generic_poisons_import` files change from rejection to acceptance; no other
verdict, diagnostic-code, or generated-Rust-hash changes occurred. The rows
and complete gates are recorded in [T1_RESULTS.md](T1_RESULTS.md).

On T1 SHA `18c04f2ea965fc71c6b835dd6b8690310e514656`, `make check` passed
146/146, including Fast Debug parity. Enforced package-perf passed 70/70 and
corrected C1 corpus gates passed across 438 roots (G-a 660/660 and G-b 67/67)
on its production-code-identical parent `af9674af`; the only intervening code-
tree change reordered a regression assertion. The final full suite passed
again on the exact SHA. Compiler-perf run
`20261027-104141` compared all 77 fixtures over three repetitions: 5.438 s
baseline versus 5.415 s candidate (-0.4%), with no noise-floor slowdowns.
Architect GO is recorded for exact T1 SHA `18c04f2e`.

The M1 edit/revert wall-time comparison remains the three-repetition result in
[M1_WALL_TIME_RESULTS.md](M1_WALL_TIME_RESULTS.md):

| Edit | Baseline median (`margo build`) | Candidate median (`margo build --check`) |
|---|---:|---:|
| E0 source touch | 1,208.8 ms | 36.5 ms |
| E1 body edit, interface unchanged | 1,209.7 ms | 57.6 ms |
| E2 inferred interface edit | 1,207.5 ms | 85.8 ms |
| E3 explicit signature edit | 1,217.3 ms | 117.7 ms |

The M1 R-rule target suite also passes all 70 rows on the T1 production code;
this confirms package counts, while the cited before/after wall-time study is
the earlier three-repetition snapshot and was not rerun for T1.

This compiler-perf run is not certified: certification requires a clean
checkout whose source SHA equals `origin/main`. The T1 implementation remains
on the review branch; run the certified benchmark after integration rather
than marking this branch run as certified.

## C1 outcomes

- **`.mossi` is the final stop for dependency interfaces.** F1's freshness and
  source-root rules are covered by `check_phase26_1_f1_interface_rules` in the
  full `make check` suite.
- **`moss check` is file-scoped and semantic queries do not fall through to
  dependency source.** The allowlist regressions are in the same F1 suite;
  C1's reachability is covered by the partial-reach identity assertion and the
  corpus gates (`G-a 660/660`, `G-b 67/67`) over all 438 roots, including
  lowercase `moss.toml` manifests.
- **Margo fingerprints and skips unchanged packages.** The enforced M1 package
  suite passed all 70 scenario rows with
  `MOSS_M1_ENFORCE_TARGETS=1 make package-perf`.

The final local regression passed **146/146**. Enforced `package-perf` passed
all 70 scenarios, and `make c1-corpus-gates` passed with zero failures on the
T1 production-code checkpoint. The C1 differential against `origin/main`
records exactly the 12 documented Calc/Toolkit rows; the T1 differential adds
the three intended generic-import flips listed above. The lowercase-manifest
identity regressions are fixed; the bisect, error site, resolution, and C1
Architect review are recorded in `C1_RESULTS.md`.

## Known regression: cross-package Emacs xref

The cross-package source-location xref test is marked as an expected failure.
It currently resolves to the `.mossi` interface location rather than the
dependency source declaration. Phase 26.1.1 will record declaration locations
in a sidecar file, outside the interface digest. See `C1_RESULTS.md`.

`Triangle.area` and `util.fill` are classified as dependency bodies re-evaluated
inside consumer package checks, and belong to T1. Geolib's own
`margo build --check` rejects `Triangle.area` with `TYPE_MISMATCH` at
`shapes.moss:48` (`method:shapes__Triangle.area@46`).
