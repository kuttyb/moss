# Phase 26.1 M1 package work counters

- Main SHA: `d109b8f17276f70b591432fd345d57e727f17501`
- Measurement revision SHA: `a0ee71e1cc85f7640f181641194dbcf1b70e0aec`
- The harness checks whether `margo build --check` is supported. On the pre-feature baseline it records the rejection and measures `margo build` as a full-work fallback.
- Counter source: one `MOSS_PROFILE_COMPILER=1` compiler invocation per checked package; modules count source files in checked packages (these fixtures use one source module per file). `functions_checked` counts source function checker entries, summed across Checker instances. `inference_function_visits` is total inference visits, not unique functions.
- The Margo rows are package-graph totals. The file-check rows profile the edited file with the built dependency interfaces on `MOSS_MODULE_PATH`.

## Package graph fixtures

| Fixture | Packages | Baseline packages checked | Modules | Functions checked | Inference visits |
|---|---:|---:|---:|---:|---:|
| `application_library` | 2 | 2 | 2 | 2 | 20 |
| `dependency_chain` | 4 | 4 | 4 | 12 | 164 |
| `fan_in_eight` | 10 | 10 | 10 | 18 | 292 |
| `generic_cross_package` | 4 | 4 | 4 | 6 | 88 |

## Edit and revert scenarios

| Edit | Command | Packages checked | Modules checked | Functions checked | Inference visits |
|---|---|---:|---:|---:|---:|
| E0 | `margo build (build --check unsupported)` | 4 | 4 | 6 | 82 |
| E0 | `moss check <edited core file>` | 1 | 1 | 4 | 20 |
| E0-revert | `margo build (reverted; --check unsupported)` | 4 | 4 | 6 | 82 |
| E0-revert | `moss check <reverted core file>` | 1 | 1 | 4 | 20 |
| E1 | `margo build (build --check unsupported)` | 4 | 4 | 10 | 102 |
| E1 | `moss check <edited core file>` | 1 | 1 | 4 | 20 |
| E1-revert | `margo build (reverted; --check unsupported)` | 4 | 4 | 10 | 102 |
| E1-revert | `moss check <reverted core file>` | 1 | 1 | 4 | 20 |
| E2 | `margo build (build --check unsupported)` | 4 | 4 | 10 | 102 |
| E2 | `moss check <edited core file>` | 1 | 1 | 4 | 20 |
| E2-revert | `margo build (reverted; --check unsupported)` | 4 | 4 | 10 | 102 |
| E2-revert | `moss check <reverted core file>` | 1 | 1 | 4 | 20 |
| E3 | `margo build (build --check unsupported)` | 4 | 4 | 10 | 102 |
| E3 | `moss check <edited core file>` | 1 | 1 | 4 | 20 |
| E3-revert | `margo build (reverted; --check unsupported)` | 4 | 4 | 10 | 102 |
| E3-revert | `moss check <reverted core file>` | 1 | 1 | 4 | 20 |

Scenario definitions: E0 touches the source without changing bytes; E1 changes one function body while preserving its interface; E2 changes another function body so its inferred result type changes; E3 changes an explicit result signature whose type is forwarded through the dependency chain. Each starts from a fresh chain, measures both commands after the edit, restores the original source, and measures both commands after revert.

Functions checked counts source function checker entries, not call-graph reachability. R3 acceptance requires an additional counter for actual reached body checks. Full rows: `benchmarks/phase26_1/results/m1_phase26_main.csv`.
