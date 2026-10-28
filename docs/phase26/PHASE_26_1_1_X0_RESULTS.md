# Phase 26.1.1 X0 — Fast Debug work counters

- Branch: `phase26-1-m1`
- Implementation and measurement SHA: `44ee8233` (`fix(phase26.1.1): align Fast Debug artifact columns`)
- Work machine: local checkout; no hosted jobs.
- Harness: `make package-perf`; full CSV: `benchmarks/phase26_1/results/m1_phase26_1_1_x0.csv`.
- `make package-perf` completed successfully in baseline/report mode. The existing M1 package targets passed. The X0 Fast Debug selected-entry targets are recorded as unmet baseline data until X2.
- `MOSS_M1_ENFORCE_TARGETS=1 make package-perf` was also run before checkpointing and failed on the first intended Fast Debug mismatch: eight observed function identities versus the three-function `baseline_reach` target. This confirms the new target is enforced. The same harness and compiler tree were committed at the measurement SHA.

Architect GO: exact implementation SHA `44ee82331080c0072a9c2416913e6a0fe66660c8`. This review approves X0 counters only; it does not approve X1/X2 architecture or implementation.

## Counts

The two-package fixture contains four app functions and four library functions. The selected `reaches library` test should check exactly the app's `reaches_library` plus the library's `helper` and `target_value` (3 identities). The selected `does not reach library` test should check only app `app_only` (1 identity). E3 selects the separate explicit path (2 identities: app `reaches_explicit` and library `explicit_value`). Test declarations are not source-function profile identities.

| Scenario | Margo package checks | Existing package artifacts changed | Fast Debug functions observed / target | Dependency functions observed | Dependency packages fully checked | Fast Debug target |
|---|---:|---|---:|---:|---:|:---:|
| Initial selected reach | — | — | 8 / 3 | 4 | 1 | Fail |
| Initial app-only selected test | — | — | 8 / 1 | 4 | 1 | Fail |
| E0 dependency touch | 0 | none | 8 / 3 | 4 | 1 | Fail |
| E1 dependency body edit | 1 | `library` | 8 / 3 | 4 | 1 | Fail |
| E2 inferred result edit | 2 | `app`, `library` | 8 / 3 | 4 | 1 | Fail |
| E3 explicit signature edit | 2 | `app`, `library` | 8 / 2 | 4 | 1 | Fail |
| App-only body edit | 1 | `app` | 8 / 1 | 4 | 1 | Fail |

For every selected Fast Debug run, the observed identities are the same eight:

- `m1_fd_app_api.moss:m1_fd_app_api__app_only`
- `m1_fd_app_api.moss:m1_fd_app_api__app_unused`
- `m1_fd_app_api.moss:m1_fd_app_api__reaches_explicit`
- `m1_fd_app_api.moss:m1_fd_app_api__reaches_library`
- `m1_fd_lib.moss:m1_fd_lib__explicit_value`
- `m1_fd_lib.moss:m1_fd_lib__helper`
- `m1_fd_lib.moss:m1_fd_lib__target_value`
- `m1_fd_lib.moss:m1_fd_lib__unrelated`

This proves the current test filter changes which test executes, but not the checker scope. The Margo package-edit counts align with G1's targets for this two-package graph: E0 0; E1 provider only; E2/E3 provider plus direct app dependent; an app edit checks only the app.

## Measurement limits and X1 architecture stop

`package_artifacts_changed` in the CSV hashes the current `.mossi` interface and Margo's existing `.moss/semantic-cache-v1/*.snapshot` artifacts. These are not the planned Fast Debug checked-program snapshot; they serve only as the current package artifact churn proxy. The harness asserts that each filter passes exactly the single requested test. It does not claim to measure R9 refresh behavior or prove R8 source-free execution. X2 needs separate tests for automatic refresh and execution with no `MOSS_FAST_DEBUG_SOURCE_ROOTS`.

Architect's ABI feasibility review at `fcc797b2` found that production ABI v9 already stores generic bodies in `.mossi`, and staged ABI v10 adds only partial checked-body records there. A separate body-bearing Fast Debug snapshot would duplicate those bodies. Complete R13 requires a Bank decision on making `.mossi` the authoritative body store and extending the checked-body format/coverage, with only a bodyless fingerprint/reference index if an index is needed. Work stops before X1 pending that decision.
