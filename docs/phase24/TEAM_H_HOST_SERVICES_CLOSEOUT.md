# Phase 24 Team H — Host Services closeout

**Branch:** `phase24-host`  
**Base:** `2845390534272e330a0ecc7c9e970d1815eb0588`  
**Scope:** source-only SDK work; no compiler, runtime, Executor, or Phase 30 changes.

## Delivery classification

### Complete on this backplane

- `moss.path` is a pure lexical POSIX-style (`/`) String package. It provides
  component extraction, lexical normalization, join, basename/dirname,
  extension/stem, absolute/relative checks, and component-boundary
  prefix/suffix checks. It does not inspect the host filesystem, resolve
  symlinks, or promise Windows-native semantics.
- `moss.fs` provides owned-byte regular-file reads with fixed 4096-byte
  requests (`read_prefix`, `read_all`, `read_limited`) and a deliberately
  narrow synced `copy_prefix`. `read_limited` checks the next borrowed chunk
  before copying it, so the owned result never exceeds `max_bytes`; it raises
  `FsError.LimitExceeded` on overflow and `FsError.InvalidLimit` for a negative
  limit. Negative offsets raise `FsError.InvalidOffset`; FileIO errors retain
  the Phase 21 typed error contract. Borrowed `Range` values never escape a
  live FileIO owner.

## Limited or deferred

- The `moss.io` workstream is delivered as feasibility probes and a permanent
  capability report rather than a misleading public package. Same-unit bounded
  reads, EOF collection, checksums, and explicit-offset writes work. An
  exported FileIO capability currently fails package linking because provider
  and consumer lower to distinct Rust FileIO types (`moss_hostcap::FileIO` vs
  `moss_fileio::FileIO`), reproduced as `BUILD_BACKEND_ERROR`/E0308.
- Metadata/stat, existence, directory operations, rename/atomic replacement,
  truncation, file size, permissions, environment variables, current working
  directory, arguments, process creation, waiting, and exit status are not
  published. Bootstrap and minimized probes show that these require approved
  host-runtime primitives; no shell, unsafe FFI, Executor substitute, or
  fake-success stub was introduced.
- Fast Debug intentionally rejects FileIO. Pure `moss.path` remains usable in
  interpreter/Fast Debug contexts; native debug/release are the FileIO oracles.

## Benchmark status

No comparative throughput or allocation benchmark is claimed for the host
packages. The current public boundary is dominated by regular-file open/close
and a cross-package FileIO capability ABI failure; a tiny five-byte correctness
probe would not be a meaningful Rust comparison. The feasibility report records
the required future methodology: identical files and offsets, separate
open/close from pre-opened operations, release builds, observable copied-byte
counts, and distributional timings. Benchmarking is deferred until the host
runtime contracts and package ABI are approved.

## Evidence and validation

The integrated branch passed:

```text
python3 sdk/path/tests/check_differential.py
python3 sdk/path/tests/check_source_free.py
python3 sdk/fs/tests/check_native.py
python3 sdk/fs/tests/check_negative.py
python3 sdk/fs/tests/check_source_free.py
python3 sdk/fs/tests/check_adversarial.py
python3 sdk/host/io/check_feasibility.py
python3 tests/tooling/check_agent_feedback.py
python3 tests/tooling/check_agent_skills.py
TMPDIR="$PWD/tmp" make check
make examples
git diff --check
```

The independent design freeze and final implementation review are attributable
to the Team H L6 Architect. Team I owns any later merge into
`phase24-integration`; this branch is not merged into `main`.
