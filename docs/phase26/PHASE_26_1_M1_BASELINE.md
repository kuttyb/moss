# Phase 26.1 M1 package work counters

- Main SHA: `d109b8f17276f70b591432fd345d57e727f17501`
- Measurement revision SHA: `90c00a5a9510a21cf042e9e9dae6ca648d698c52`
- The harness checks whether `margo build --check` is supported. On the pre-feature baseline it records the rejection and measures `margo build` as a full-work fallback.
- Target enforcement: `off` (automatic when `margo build --check` is supported; `MOSS_M1_ENFORCE_TARGETS=1` enables it during rollout). `expected_packages` is always the R-rule target; `packages_checked` is observed work.
- Counter source: one `MOSS_PROFILE_COMPILER=1` compiler invocation per checked package; modules count source files in checked packages (these fixtures use one source module per file). `functions_checked` counts source function checker entries, summed across Checker instances. `inference_function_visits` is total inference visits, not unique functions.
- The Margo rows are package-graph totals. The file-check rows profile the edited file with the built dependency interfaces on `MOSS_MODULE_PATH`.

## Package graph fixtures

| Fixture | Packages | Baseline packages checked | Modules | Functions checked | Inference visits |
|---|---:|---:|---:|---:|---:|
| `application_library` | 2 | 2 | 2 | 6 | 40 |
| `dependency_chain` | 4 | 4 | 4 | 20 | 272 |
| `fan_in_eight` | 10 | 10 | 10 | 22 | 524 |
| `generic_cross_package` | 4 | 4 | 4 | 18 | 292 |

## Edit and revert scenarios

| Fixture / position | Edit | Command | Actual | R-rule target | Target met | Modules | Functions | Inference visits |
|---|---|---|---:|---:|:---:|---:|---:|---:|
| dependency_chain / core | E0 | `margo build (build --check unsupported)` | 4 | 0 | False | 4 | 10 | 136 |
| dependency_chain / core | E0 | `moss check <edited file>` | 1 | 1 | True | 1 | 6 | 30 |
| dependency_chain / core | E0-revert | `margo build (build --check unsupported) (reverted)` | 4 | 0 | False | 4 | 10 | 136 |
| dependency_chain / core | E0-revert | `moss check <reverted file>` | 1 | 1 | True | 1 | 6 | 30 |
| dependency_chain / core | E1 | `margo build (build --check unsupported)` | 4 | 1 | False | 4 | 16 | 166 |
| dependency_chain / core | E1 | `moss check <edited file>` | 1 | 1 | True | 1 | 6 | 30 |
| dependency_chain / core | E1-revert | `margo build (build --check unsupported) (reverted)` | 4 | 1 | False | 4 | 16 | 166 |
| dependency_chain / core | E1-revert | `moss check <reverted file>` | 1 | 1 | True | 1 | 6 | 30 |
| dependency_chain / core | E2 | `margo build (build --check unsupported)` | 4 | 2 | False | 4 | 16 | 166 |
| dependency_chain / core | E2 | `moss check <edited file>` | 1 | 1 | True | 1 | 6 | 30 |
| dependency_chain / core | E2-revert | `margo build (build --check unsupported) (reverted)` | 4 | 2 | False | 4 | 16 | 166 |
| dependency_chain / core | E2-revert | `moss check <reverted file>` | 1 | 1 | True | 1 | 6 | 30 |
| dependency_chain / core | E3 | `margo build (build --check unsupported)` | 4 | 4 | True | 4 | 16 | 166 |
| dependency_chain / core | E3 | `moss check <edited file>` | 1 | 1 | True | 1 | 6 | 30 |
| dependency_chain / core | E3-revert | `margo build (build --check unsupported) (reverted)` | 4 | 4 | True | 4 | 16 | 166 |
| dependency_chain / core | E3-revert | `moss check <reverted file>` | 1 | 1 | True | 1 | 6 | 30 |
| dependency_chain / app | E0 | `margo build (build --check unsupported)` | 4 | 0 | False | 4 | 10 | 136 |
| dependency_chain / app | E0 | `moss check <edited file>` | 1 | 1 | True | 1 | 2 | 40 |
| dependency_chain / app | E0-revert | `margo build (build --check unsupported) (reverted)` | 4 | 0 | False | 4 | 10 | 136 |
| dependency_chain / app | E0-revert | `moss check <reverted file>` | 1 | 1 | True | 1 | 2 | 40 |
| dependency_chain / app | E1 | `margo build (build --check unsupported)` | 4 | 1 | False | 4 | 12 | 176 |
| dependency_chain / app | E1 | `moss check <edited file>` | 1 | 1 | True | 1 | 2 | 40 |
| dependency_chain / app | E1-revert | `margo build (build --check unsupported) (reverted)` | 4 | 1 | False | 4 | 12 | 176 |
| dependency_chain / app | E1-revert | `moss check <reverted file>` | 1 | 1 | True | 1 | 2 | 40 |
| dependency_chain / app | E2 | `margo build (build --check unsupported)` | 4 | 1 | False | 4 | 12 | 176 |
| dependency_chain / app | E2 | `moss check <edited file>` | 1 | 1 | True | 1 | 2 | 40 |
| dependency_chain / app | E2-revert | `margo build (build --check unsupported) (reverted)` | 4 | 1 | False | 4 | 12 | 176 |
| dependency_chain / app | E2-revert | `moss check <reverted file>` | 1 | 1 | True | 1 | 2 | 40 |
| dependency_chain / app | E3 | `margo build (build --check unsupported)` | 4 | 1 | False | 4 | 12 | 176 |
| dependency_chain / app | E3 | `moss check <edited file>` | 1 | 1 | True | 1 | 2 | 40 |
| dependency_chain / app | E3-revert | `margo build (build --check unsupported) (reverted)` | 4 | 1 | False | 4 | 12 | 176 |
| dependency_chain / app | E3-revert | `moss check <reverted file>` | 1 | 1 | True | 1 | 2 | 40 |
| fan_in_eight / hub | E0 | `margo build (build --check unsupported)` | 10 | 0 | False | 10 | 11 | 262 |
| fan_in_eight / hub | E0 | `moss check <edited file>` | 1 | 1 | True | 1 | 3 | 15 |
| fan_in_eight / hub | E0-revert | `margo build (build --check unsupported) (reverted)` | 10 | 0 | False | 10 | 11 | 262 |
| fan_in_eight / hub | E0-revert | `moss check <reverted file>` | 1 | 1 | True | 1 | 3 | 15 |
| fan_in_eight / hub | E1 | `margo build (build --check unsupported)` | 10 | 1 | False | 10 | 14 | 277 |
| fan_in_eight / hub | E1 | `moss check <edited file>` | 1 | 1 | True | 1 | 3 | 15 |
| fan_in_eight / hub | E1-revert | `margo build (build --check unsupported) (reverted)` | 10 | 1 | False | 10 | 14 | 277 |
| fan_in_eight / hub | E1-revert | `moss check <reverted file>` | 1 | 1 | True | 1 | 3 | 15 |
| fan_in_eight / hub | E2 | `margo build (build --check unsupported)` | 10 | 9 | False | 10 | 14 | 277 |
| fan_in_eight / hub | E2 | `moss check <edited file>` | 1 | 1 | True | 1 | 3 | 15 |
| fan_in_eight / hub | E2-revert | `margo build (build --check unsupported) (reverted)` | 10 | 9 | False | 10 | 14 | 277 |
| fan_in_eight / hub | E2-revert | `moss check <reverted file>` | 1 | 1 | True | 1 | 3 | 15 |
| fan_in_eight / hub | E3 | `margo build (build --check unsupported)` | 10 | 10 | True | 10 | 14 | 277 |
| fan_in_eight / hub | E3 | `moss check <edited file>` | 1 | 1 | True | 1 | 3 | 15 |
| fan_in_eight / hub | E3-revert | `margo build (build --check unsupported) (reverted)` | 10 | 10 | True | 10 | 14 | 277 |
| fan_in_eight / hub | E3-revert | `moss check <reverted file>` | 1 | 1 | True | 1 | 3 | 15 |
| generic_cross_package / generic | E0 | `margo build (build --check unsupported)` | 4 | 0 | False | 4 | 9 | 146 |
| generic_cross_package / generic | E0 | `moss check <edited file>` | 1 | 1 | True | 1 | 4 | 20 |
| generic_cross_package / generic | E0-revert | `margo build (build --check unsupported) (reverted)` | 4 | 0 | False | 4 | 9 | 146 |
| generic_cross_package / generic | E0-revert | `moss check <reverted file>` | 1 | 1 | True | 1 | 4 | 20 |
| generic_cross_package / generic | E1 | `margo build (build --check unsupported)` | 4 | 1 | False | 4 | 13 | 166 |
| generic_cross_package / generic | E1 | `moss check <edited file>` | 1 | 1 | True | 1 | 4 | 20 |
| generic_cross_package / generic | E1-revert | `margo build (build --check unsupported) (reverted)` | 4 | 1 | False | 4 | 13 | 166 |
| generic_cross_package / generic | E1-revert | `moss check <reverted file>` | 1 | 1 | True | 1 | 4 | 20 |
| generic_cross_package / generic | E2 | `margo build (build --check unsupported)` | 4 | 3 | False | 4 | 13 | 166 |
| generic_cross_package / generic | E2 | `moss check <edited file>` | 1 | 1 | True | 1 | 4 | 20 |
| generic_cross_package / generic | E2-revert | `margo build (build --check unsupported) (reverted)` | 4 | 3 | False | 4 | 13 | 166 |
| generic_cross_package / generic | E2-revert | `moss check <reverted file>` | 1 | 1 | True | 1 | 4 | 20 |
| generic_cross_package / generic | E3 | `margo build (build --check unsupported)` | 4 | 4 | True | 4 | 13 | 166 |
| generic_cross_package / generic | E3 | `moss check <edited file>` | 1 | 1 | True | 1 | 4 | 20 |
| generic_cross_package / generic | E3-revert | `margo build (build --check unsupported) (reverted)` | 4 | 4 | True | 4 | 13 | 166 |
| generic_cross_package / generic | E3-revert | `moss check <reverted file>` | 1 | 1 | True | 1 | 4 | 20 |

Scenario definitions: E0 touches the source without changing bytes; E1 changes an exported function body while preserving its interface; E2 changes an inferred exported result type; E3 changes an explicit exported result type. Core/hub/provider E2 targets include direct dependents; E3 targets the full transitive closure. Top-app E1–E3 targets are one package. Each scenario is edited and reverted independently. `MOSS_M1_ENFORCE_TARGETS=1` turns target mismatches into hard failures; it is intended for after G1 implements incremental checks. The pre-G1 baseline reports mismatches as data.

Functions checked counts source function checker entry occurrences, not unique reachable identities. R3 still requires its diagnostic oracle; any work attribution by reachable function should record identities, not infer them from this aggregate count. Full rows: `benchmarks/phase26_1/results/m1_phase26_main.csv`.
