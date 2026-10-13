# Phase 24 host I/O, environment, and process feasibility

**Branch:** `phase24-host-io`

**Base:** `2845390534272e330a0ecc7c9e970d1815eb0588` (`phase24-host`)

**Scope:** Feasibility evidence only; no compiler, runtime, Executor, shell, or
unsafe-FFI changes.

## Result

Moss v0.1 can express bounded FileIO helpers in a native compilation unit. It
can also import a provider that opens a file internally and exposes ordinary
values. It cannot pass a FileIO capability through a package API because the
provider and consumer lower FileIO to different Rust types. Environment and
process APIs are not in the live language surface. Accordingly, this branch
does not publish `moss.io`, `moss.env`, or `moss.process` packages.

The permanent gate is:

```sh
python3 sdk/host/io/check_feasibility.py
```

It builds and runs the positive native cases and asserts the structured codes
and backend details for every negative case.

## Confirmed FileIO surface

`sdk/host/io/feasibility/same_package.moss` demonstrates all of the following
without compiler changes:

- a fixed 4096-byte read copied from borrowed `Range` into `Vector[Int]`;
- iteration over a borrowed `Range` to calculate a checksum;
- collection to EOF with a hard 65536-byte limit and a one-byte overflow probe;
- an explicit-offset write whose payload is the statically bounded Range from
  `read(0, 4096)`.

On the five-byte fixture `Moss\n`, native debug and optimized native execution
both print:

```text
5
428
5
5
```

The path-only provider in `feasibility/provider` also builds, imports, links,
and returns byte value `77` (`M`) from its consumer. This proves that FileIO may
remain private to a provider implementation; it does not prove that FileIO is
a cross-package capability type.

A generic wrapper accepting an unconstrained `Range` write payload is correctly
rejected with `FILEIO_UNBOUNDED_REQUEST`. The compiler cannot prove a finite
bound from that public parameter. The supported helper therefore writes the
Range returned by a locally visible, constant-bound read.

Fast Debug is inapplicable to these helpers by documented design: the
interpreter reports that FileIO is unavailable. Native release can compile the
path-only provider, but source-free ABI-v9 execution cannot establish a FileIO
capability contract because capability-bearing source-backed compilation fails
before linking.

## Package boundary blocker

The minimized provider exports `read_first(file: FileIO)`. Its consumer opens a
FileIO and calls that function. Margo reaches the Rust backend, which returns
`BUILD_BACKEND_ERROR` / Rust `E0308`:

```text
expected `moss_hostcap::FileIO`, found `moss_fileio::FileIO`
`moss_fileio::FileIO` and `moss_hostcap::FileIO` ... are actually distinct types
```

This is not a Moss ownership error or an inferred-effect error. FileIO is
currently generated separately into each crate, so its native representation
has no canonical cross-crate identity. Range has a related public-surface
constraint: an arbitrary imported Range cannot satisfy the write bound merely
because the caller obtained it from a bounded read.

This result is consistent with FileIO's Phase 20 pinned, non-transferable
capability model and Phase 21's categorical prohibition on carrying FileIO or
Range through exceptional capture boundaries
(`docs/MOSS_PHASE_20_FILE_IO_AND_EXECUTORS.md` and
`docs/PHASE_21_SHARED_ABI.md`). Ordinary imported helper parameters are not
rejected at Moss checking, however; they reach rustc and fail as E0308. If such
parameters remain unsupported, the smaller compiler improvement is an early,
structured Moss diagnostic instead of a backend type error.

Supporting capability-bearing APIs requires an explicit compiler/runtime ABI
decision that gives FileIO one canonical opaque cross-crate borrowed handle and
makes provider and consumer lower to that same type. The interface must preserve
the existing pinned ownership/in-use guarantees, forbid transfer/retention, and
must not expose raw file descriptors. Range writes additionally need
boundedness represented in the imported contract, or package APIs must keep
both read and write in one provider call.

After that decision, a conservative first SDK could expose fixed-bound,
path-oriented operations such as `read_chunk_4096`, `collect_to_eof_65536`,
`checksum_chunk_4096`, and `copy_chunk_4096` with explicit source and destination
offsets. API naming and error enums should be finalized only after the ABI
contract exists; this feasibility branch intentionally does not create stubs.

## Environment and process blockers

The bootstrap capability surface and Phase 20/21 documentation contain no
environment or process primitive. Minimal checked probes report
`UNKNOWN_SYMBOL_OR_TYPE` for both `Env` and `Process`. No guessed spelling,
shell escape, or unsafe FFI is used as a substitute.

The minimal environment primitive should be a direct, non-shell host operation
with a typed contract equivalent to:

```text
get(name: String) -> String raises Missing | InvalidName | InvalidEncoding
```

It needs a declared host-read effect, UTF-8 behavior, diagnostics that never
print secret values, and an explicit deterministic Fast Debug model (an
injected map or a clear unsupported diagnostic).

The minimal process primitive should invoke an executable directly with an argv
vector, never through a shell, and bound captured stdout/stderr. Its result must
include exit status and byte-vector output; errors must distinguish not found,
permission, invalid input, output-limit, and I/O failures. Working directory and
environment inheritance require explicit policy. This is an authority-bearing
runtime feature and needs L7 approval before source syntax or public SDK shape
is fixed.

## Performance and delivery disposition

No comparative benchmark is reported. A same-unit five-byte correctness probe
is not a meaningful I/O benchmark, and benchmarking the minimized path-only
provider would prematurely privilege an unapproved API shape. Once the runtime
contracts are approved, benchmarks should separate open/close latency from
pre-opened sequential reads/writes, use identical files and offsets, include
short and limit-sized workloads, and report release distributions plus copied
byte counts.

Team H requests L7 disposition on the canonical FileIO capability ABI and on
the environment/process authority model. Until then, the honest deliverable is
the executable feasibility evidence rather than packages that imply unsupported
contracts.
