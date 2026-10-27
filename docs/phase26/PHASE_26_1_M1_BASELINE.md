# Phase 26.1 M1 package work counters

- Main SHA: `d109b8f17276f70b591432fd345d57e727f17501`
- The harness checks whether `margo build --check` is supported. On the pre-feature baseline it records the rejection and measures `margo build` as a full-work fallback.
- Counter source: one `MOSS_PROFILE_COMPILER=1` compiler invocation per checked package; module counts are source modules in checked packages. Function and reinference counters sum all checker-instance profile records.
- The Margo rows are package-graph totals. The file-check rows profile the edited file with the built dependency interfaces on `MOSS_MODULE_PATH`.

## Package graph fixtures

| Fixture | Packages | Baseline packages checked | Modules | Functions checked | Re-inferred functions |
|---|---:|---:|---:|---:|---:|
| `application_library` | 2 | 2 | 2 | 4 | 20 |
| `dependency_chain` | 4 | 4 | 4 | 30 | 164 |
| `fan_in_eight` | 10 | 10 | 10 | 52 | 292 |
| `generic_cross_package` | 4 | 4 | 4 | 16 | 88 |

## Edit and revert scenarios

| Edit | Command | Packages checked | Modules checked | Functions checked | Functions re-inferred |
|---|---|---:|---:|---:|---:|
| E0 | `margo build (build --check unsupported)` | 4 | 4 | 15 | 82 |
| E0 | `moss check <edited core file>` | 1 | 1 | 4 | 20 |
| E0-revert | `margo build (reverted; --check unsupported)` | 4 | 4 | 15 | 82 |
| E0-revert | `moss check <reverted core file>` | 1 | 1 | 4 | 20 |
| E1 | `margo build (build --check unsupported)` | 4 | 4 | 19 | 102 |
| E1 | `moss check <edited core file>` | 1 | 1 | 4 | 20 |
| E1-revert | `margo build (reverted; --check unsupported)` | 4 | 4 | 19 | 102 |
| E1-revert | `moss check <reverted core file>` | 1 | 1 | 4 | 20 |
| E2 | `margo build (build --check unsupported)` | 4 | 4 | 19 | 102 |
| E2 | `moss check <edited core file>` | 1 | 1 | 4 | 20 |
| E2-revert | `margo build (reverted; --check unsupported)` | 4 | 4 | 19 | 102 |
| E2-revert | `moss check <reverted core file>` | 1 | 1 | 4 | 20 |
| E3 | `margo build (build --check unsupported)` | 4 | 4 | 19 | 102 |
| E3 | `moss check <edited core file>` | 1 | 1 | 4 | 20 |
| E3-revert | `margo build (reverted; --check unsupported)` | 4 | 4 | 19 | 102 |
| E3-revert | `moss check <reverted core file>` | 1 | 1 | 4 | 20 |

Scenario definitions: E0 touches the source without changing bytes; E1 changes one function body while preserving its interface; E2 changes another function body so its inferred result type changes; E3 changes an explicit result signature whose type is forwarded through the dependency chain. Each starts from a fresh chain, measures both commands after the edit, restores the original source, and measures both commands after revert.

Full rows: `m1_work_counters.csv`.
