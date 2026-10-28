# Moss Dev Cheat Sheet

Oct 28, 2026 · @Bank kus

For fast iteration use `moss check` and the interpreter, which never run rustc. Anything that emits Rust for a package goes through Margo.

## ECT loop (no rustc)

Run `margo build` once so every dependency has a fresh `.mossi`. After that, iterate with these.

| Goal | Command | What it checks | Dependencies |
| --- | --- | --- | --- |
| Check one file | `moss check <file>` | Only what the file transitively reaches | Reads dependency `.mossi` only. Missing or stale: error, run `margo build` |
| Run without a native build | `moss run --interp <file>` (add `--trace`) | `moss check` on the entry's reach, then interprets in memory | Dependency bodies run from `.mossi` Section 2. Never rechecks dependency packages |
| Debug a package | `margo debug` (add `--trace`) | Same as above, from the package root | Same as above |
| Interpreted tests | `margo test --interp [filter]` (add `--trace`) | The tests' reach | Same as above. No `--release` |
| Re-run only affected tests | `moss test --affected` | Tests the edit can reach, chosen conservatively | Single project |

When a dependency changes, run `margo build` again. It rebuilds only packages whose fingerprint changed.

## Native unit tests

Use `margo test`. It builds the package graph through Cargo and runs the tests natively.

| Goal | Command | Notes |
| --- | --- | --- |
| All tests, debug profile | `margo test` | Package rules. Dependency tests run only when that package is the root |
| Filtered tests | `margo test <filter>` |  |
| Tests on the release profile | `margo test --release` |  |
| Cap parallel jobs | `margo test -j N` | Default is the core count |
| Machine-readable output | add `--json` |  |

`margo test` runs the package's tests fully native. Test-file helpers are scoped
to their declaring file, and shared test helpers belong in an explicitly
imported module under `tests/`. The same rules apply to `margo test --interp`.

## Release builds

| Goal | Command | Notes |
| --- | --- | --- |
| Build the package graph, release | `margo build --release` | One Cargo workspace build for the whole graph. Unchanged packages are skipped |
| Build and run, release | `margo run --release` | Runs the root package's executable |
| Benchmarks | `margo bench [filter]` |  |
| Whole-package check, no native build | `margo build --check` | Fingerprint skipping applies |
| Cap parallel jobs | add `-j N` | One cap covers both the `moss` passes and Cargo |
| Start clean | `margo clean` |  |

Generated crates build with `-D warnings`, so a rustc upgrade can fail a build with no Moss change.

## Rules that hold everywhere

- **`.mossi` is the boundary.** Once a dependency's `.mossi` is present and fresh, nothing reads that package's source. Rebuilding dependencies is Margo's job only.
- **Package builds go through Margo.** `moss build` and `moss build --check` on a package fail with `MOSS_PACKAGE_BUILD_REQUIRES_MARGO`. Use `margo build` or `margo build --check`.
- **Reach rules are for ECT only.** `moss check` and the interpreter check just what the entry reaches. Anything that emits Rust follows package rules.
- **`moss check` never looks at rlibs.** A swapped provider rlib is caught by `margo build`, not by check.
