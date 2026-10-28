# Phase 26.1 exit status

**Status: 26.1 + 26.1.1 complete; awaiting Bank go.**

The Phase 26.1.1 X1–X4 closeout and exact-commit gates are recorded in
[PHASE_26_1_1_X2_RESULTS.md](PHASE_26_1_1_X2_RESULTS.md). The branch is
`phase26-1-m1`; its latest implementation commit is `e0b8dcc2`, with the
Bank-directed audit and backlog recorded in docs-only follow-up commits. The
12 native-failing v9 packages were checked with native `margo build` and
`margo test` at `origin/main` `543722a9` and branch implementation
`e0b8dcc2`. All 24 command/package exit statuses matched; no package passes on
main and fails on the branch. These are pre-existing native-backend issues,
not Fast Debug defects. The docs-only follow-up checkpoint `a3954019` received
Architect GO, confirming that implementation, test, and build files did not
change and that prior gate evidence carries under Bank's explicit exception.
The exact-SHA `e0b8dcc2` record includes full `make check`, C1 gates, focused
X2/native agreement, and the v9/v10 corpus audit. The broader compiler-corpus
differential, package-perf, and compiler-perf results remain cited at their
recorded earlier checkpoint below; those measurements were not rerun for this
docs-only audit. Do not merge until Bank explicitly authorizes it; certified
compiler-perf on clean `main` remains post-merge.

## Known issues

- **Native ownership analysis is less strict than Rust borrow checking.** Moss
  accepts the reachable programs in `push_self_field_read/single` and
  `field_self_assign_struct/single`, which rustc rejects with E0502, and both
  `mutate_loop_element` variants, which rustc rejects with E0596. The exact
  native build and test commands fail the same way on `origin/main` and
  `e0b8dcc2`.
- **Native test generation omits test-only support.** On both revisions,
  `margo test` fails to emit/link helpers in `test_helper_fn/split`,
  `test_file_helper/split`, and `test_module_mismatch/implicit_test`, and
  test-only imported rlibs/types in `test_imports_not_in_main`,
  `test_import/split`, and `test_local_type/split`.
- **Other pre-existing native build failures.** The generic typed callee
  fixture `exported_generic_typed_callee/split` fails native linking on both
  revisions. `trait_only_module/split` reports its missing `traits` provider
  on both. These are native-backend/package-resolution backlog items.
- **ABI v10 Fast Debug corpus-only failures, out of scope by Bank decision.**
  The 12 Margo `UnicodeDecodeError` rows and five
  `SOURCE_FREE_CONTRACT_INVALID` rows remain on the backlog and are not to be
  fixed in this phase. The `06j-proof-forged-interface/app` row is also out of
  scope: Fast Debug skips v10 key-proof validation. ABI-v10 `make check` suites
  remain in scope and pass; the v10 corpus run is report-only.
- **Implicit-module identity uses two keys.** The empty string and manifest
  package name can both identify the implicit module; this identity split is
  recorded for follow-up.

At `f4b1686a`, uninterrupted `make check` passed 150/150, C1 corpus gates
passed G-a 669/669 and G-b 67/67, package-perf passed with target enforcement,
the Fast Debug comparison against origin/main had only the documented three
differences, and the compiler corpus differential contained only the 12 C1
and 3 T1 rows with unchanged Rust hashes for 1,325 jointly accepted sources.
Five-repetition compiler-perf against `0872d642` measured 5.110 s baseline and
5.135 s candidate (+0.5%), with no slowdown beyond the noise floor. The
nonblocking ABI-v10 Fast Debug run had zero R12 guard hits. The earlier
LLDB-DAP timeout is retained in the historical record; the full Phase 5
tooling shard later passed with `/usr/bin/lldb-dap-18`.

No standalone Fast Debug timing study was run under Bank's no-timing-studies
instruction. Compiler-perf's `rw_packages_regex` and `rw_sdk_datastructures`
candidate medians were 683.78 ms and 261.51 ms; they are compiler check
workloads, not Fast Debug timings. M1's single-run E0–E3 Margo check times and
the Fast Debug identity/count table are in the linked X2 results and package-
perf report.

M1 dependency-chain/core E-scenario observations from package-perf were:

| Edit | `margo build --check` wall time |
|---|---:|
| E0 | 37.565 ms |
| E1 | 58.472 ms |
| E2 | 76.954 ms |
| E3 | 119.787 ms |

Fast Debug count and artifact targets from the package-perf report:

| Scenario | Functions / target | Dependency functions | Dependency packages fully checked | Artifacts changed / target |
|---|---:|---:|---:|---:|
| baseline reach | 1 / 1 | 0 | 0 | 0 / 0 |
| baseline local | 1 / 1 | 0 | 0 | 0 / 0 |
| dependency E0 | 0 / package | 0 | 0 | 0 / 0 |
| E0 reach | 1 / 1 | 0 | 0 | 0 / 0 |
| E0 local | 1 / 1 | 0 | 0 | 0 / 0 |
| dependency E1 | 4 / package | 0 | 0 | 1 / 1 |
| E1 reach | 1 / 1 | 0 | 0 | 0 / 0 |
| E1 local | 1 / 1 | 0 | 0 | 0 / 0 |
| dependency E2 | 8 / package | 0 | 0 | 2 / 2 |
| E2 reach | 1 / 1 | 0 | 0 | 0 / 0 |
| E2 local | 1 / 1 | 0 | 0 | 0 / 0 |
| dependency E3 | 8 / package | 0 | 0 | 2 / 2 |
| E3 reach | 1 / 1 | 0 | 0 | 0 / 0 |
| E3 local | 1 / 1 | 0 | 0 | 0 / 0 |
| app-only edit | 4 / package | 0 | 0 | 1 / 1 |
| app-only reach | 1 / 1 | 0 | 0 | 0 / 0 |

---

## Prior Phase 26.1 / T1 status

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
