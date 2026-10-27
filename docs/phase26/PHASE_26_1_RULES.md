# Phase 26.1 Package Build and Check Rules

These rules are the contract for Phase 26.1. Implementation work must preserve
the existing language and compiler architecture while making package build and
check work only where needed.

## Interface loading

- **R1:** A present, current `.mossi` is sufficient for every other package.
  `moss check` never parses or semantically checks another package's source.
  It may read source bytes solely to compute the package hash required by R2.
- **R2:** A missing or stale `.mossi` is an error: `interface for X is missing or
  stale; run \`margo build\``. Staleness is detected using the package source
  hash recorded in `.mossi`. Packages always ship with source; binary-only
  packages are unsupported.
- **R6 (checkstop):** If a rebuilt package's `.mossi` hash is unchanged, its
  dependents are not re-checked.

## Check scope

- **R3:** `moss check <file>` checks everything transitively reachable from
  that file, function by function within its package. For method calls, every
  method with that name counts as reachable. Other packages are loaded only
  through `.mossi`. It never emits Rust or calls rustc.
- **R4:** Rules requiring a whole package run only under `margo build` and
  `margo build --check`: duplicate names, trait and implementation coherence,
  conflicting domain routes, and analyses spanning several handlers.
- The per-file diagnostic oracle requires diagnostics to equal package-mode
  diagnostics restricted to the same reachable set: no extra or missing
  errors.

## Margo build behavior

- **R5:** `margo build` skips a package whose fingerprint is unchanged. The
  fingerprint covers its Moss sources, compiler and toolchain identity, build
  flags, and the `.mossi` hashes of its dependencies.
- **R7:** `margo build --check` checks each package and writes `.mossi`, without
  invoking rustc.
- Output remains deterministic.

## Measurement and rollout

- M1 establishes pre-change measurements for application/library, dependency
  chain, fan-out, and cross-package generic fixtures, and edit/revert scenarios
  E0–E3. It records packages checked, modules and functions checked, and
  functions re-inferred.
- The required scenario outcomes are: E0 checks zero packages; E1 checks only
  the edited package; E2 checks the edited package and its direct dependents;
  E3 propagates checks as interfaces change. Demonstrate checkstop.
- Each work item gets its own change and Architect review. Required gates are
  corpus differential (verdicts, diagnostic codes, byte-identical generated
  Rust), Fast Debug parity, `make check`, and `compiler-perf` against main with
  no slowdowns beyond the noise floor.
- The exit report publishes the M1 table and a fresh certified
  `compiler-perf` run as the Phase 26.2 baseline.

## Scope exclusions

Parallel builds, rustc/Cargo invocation design, one crate per package, retiring
source-free provider machinery, incremental builds, and changes to Fast Debug
beyond preserving its behavior are out of scope.
