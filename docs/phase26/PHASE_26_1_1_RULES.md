# Phase 26.1.1 rules

## Command scope

| Mode | Commands | Rule |
|---|---|---|
| Emits Rust | `moss build`, native `moss test`, `moss bench`, `margo build`, `margo run`, `margo test` | Unchanged package rules: check the whole package; load other packages through `.mossi` only. |
| Edit-compile-test | `moss check <file>`, `moss run --interp`, `moss test --interp`, `moss debug`, `margo test --interp`, `margo debug` | Check only the selected entry points and the functions, handlers, and domains they reach. Dependency bodies come from checked package snapshots, never dependency `.moss` source. |

## Invariants

- **R8 — no dependency source:** edit-compile-test commands do not parse or
  check dependency package `.moss` files. Dependency bodies are read only from
  that package's checked snapshot.
- **R9 — snapshots current first:** Margo makes dependency snapshots current
  before Fast Debug execution, as `margo build --check` does. Missing or stale
  snapshots are rebuilt or produce an error directing the user to
  `margo build --check`; execution never falls back to source.
- **R10 — behavior parity:** for reachable code, Fast Debug preserves stdout,
  exit status, and trace events.
- **R11 — selected entries define reachability:**
  - `run --interp <file>` and `debug <file>` start from that file's `main`.
  - `test --interp <file> [filter]` starts from the selected tests in that
    file; with no file argument, it starts from every test in the package.
  - Reachability includes C1's edges and execution edges for functions passed
    as values (all candidates when a name is ambiguous), `message` handlers,
    and domains built from the entries.
- **R12 — checked execution guard:** the interpreter must not execute a
  function that was neither checked in the current run nor loaded from a
  current dependency snapshot. Attempting this is a loud internal error.
- **R13 — dependency generics:** when an app instantiates a dependency generic,
  check that concrete instantiation using the generic body from the dependency
  snapshot. Importing the package alone does not check the generic body.
- **Source-root override removal:** remove `MOSS_FAST_DEBUG_SOURCE_ROOTS` and
  its allowlist behavior once R8 is enforced.

## Snapshot and source-location artifacts

- `margo build --check` writes one checked-program snapshot per package next
  to its `.mossi`. The snapshot and interface use the same package fingerprint
  so their checked facts cannot drift apart.
- The snapshot must carry generic bodies in a representation the consumer can
  instantiate and check. ABI v10 checked bodies already present in `.mossi`
  must be reused rather than duplicated. If R13 cannot be supported without
  duplication, stop and report the ABI decision; do not fall back to source.
- `margo build --check` also writes declaration source locations (file and
  line) to a sidecar next to `.mossi`. The sidecar is outside the interface
  digest and package fingerprint. Queries/editor navigation use it without
  parsing or checking dependency source. A source-location-only edit updates
  the sidecar without causing downstream package rechecks.

## Work order and acceptance gates

1. **X0:** add count-only Fast Debug E0–E3 package/function counters to
   `make package-perf`, plus a two-package partial-reach fixture that names the
   expected checked functions by identity. Cover an edited dependency function
   reached by a test, an unrelated test, app-only edits, E1 dependency snapshot
   regeneration, and G1 E2/E3 propagation.
2. **X1:** implement the paired per-package snapshot and prove R13 feasibility
   before continuing.
3. **X2:** implement entry-scoped root Fast Debug checking, R12's execution
   guard, Margo snapshot refresh for `debug` and `test --interp`, and remove the
   source-root override.
4. **X3:** add the declaration-location sidecar and restore cross-package
   Emacs xref source locations. Verify a dependency blank-line edit changes
   the sidecar without downstream rechecking.
5. **X4:** cover edits between runs, dependency domains, compatible and
   incompatible dependency generic instantiations, cross-package callbacks,
   ambiguous callbacks, empty filters, and no-file test execution.

Final gates run locally on the work machine at one exact commit: corpus Fast
Debug parity for every test file and `main`; R12 guard absent across the
corpus; corpus differential against `origin/main` with only the documented 12
C1 and 3 T1 rows and unchanged Rust-emitting command output; `make check`;
`make c1-corpus-gates`; enforced `package-perf` including Fast Debug count
rows; compiler-perf against `0872d642` with no slowdown beyond the noise floor;
and Architect GO on that exact commit. No hosted regression jobs or PRs.

## Exit and merge boundary

Push the completed branch and report all outcomes, Fast Debug timings on
`regex` and one other SDK consumer, E-scenario times, and the Fast Debug count
table in `PHASE_26_1_EXIT.md`. Do not merge to `main` until Bank explicitly
authorizes it. After that authorization, merge with `--no-ff`, run certified
compiler-perf on clean `main`, add the certification report, and push.

