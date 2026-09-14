# Moss project workflow

This is the starting point for building, testing, and benchmarking a Moss
project. The project commands invoke the same compiler pipeline as direct
single-file compilation, but manage generated Rust, native binaries, source
maps, and build caching for you.

For complete reference material, see:

- [Build system](BUILD_SYSTEM.md)
- [Unit testing](TESTING.md)
- [Benchmarking](BENCHMARKING.md)
- [Agent and JSON API](AGENT_API.md)
- [Emacs, LLDB, and disassembly tooling](TOOLING.md)

## Prerequisites

Building Moss itself requires a C++17 compiler and `make`. Building a Moss
project also requires `rustc`; Moss invokes it as its native backend.

From a Moss checkout:

```sh
make
export PATH="$PWD:$PATH"
```

The second command is only a convenient shell setup for that checkout. There
is no Phase 7 installer or package manager. You can instead invoke the compiler
by its absolute path, such as `/path/to/moss/moss`.

## A complete minimal project

Create this layout:

```text
my-project/
  moss.toml
  src/
    main.moss
  tests/
    arithmetic.moss
  benches/
    arithmetic.moss
```

`moss.toml`:

```toml
[project]
name = "my-project"
version = "0.1.0"

[build]
source = "src"
```

`src/main.moss`:

```moss
fn add(left: int, right: int) -> int:
  left + right

proc main():
  echo add(20, 22)
```

`tests/arithmetic.moss`:

```moss
fn doubled(value: int) -> int:
  value * 2

test "doubling":
  assert(doubled(21) == 42)
  assertEqual(doubled(-3), -6)
```

`benches/arithmetic.moss`:

```moss
fn mixed(value: int) -> int:
  value * 7 - 3

bench "mixed arithmetic":
  mixed(1234)

bench "bare value":
  1234 * 7 - 3

bench "pipeline sum":
  values = [1, 2, 3, 4]
  values |> map(_ * 2) |> sum
```

The repository contains the verified
[`examples/projects/phase7_demo`](../examples/projects/phase7_demo) project
with the same features.

## Project discovery and manifest rules

Run project commands from the project root or any directory below it. Moss
searches the current directory and then each parent directory for the nearest
`moss.toml`. There is currently no command-line project-path option.

The manifest deliberately supports only these fields:

| Field | Required | Meaning |
| --- | --- | --- |
| `[project].name` | Yes | Nonempty project name used in reports and the application artifact name. |
| `[project].version` | Yes | Nonempty project version metadata. |
| `[build].source` | No | Project-relative source file or directory; defaults to `src`. |

All values are quoted strings. `#` comments are accepted outside strings.
Unknown sections or keys are errors. `build.source` cannot be absolute, empty,
or contain a `..` path component. If it names a directory, Moss builds exactly
`<directory>/main.moss`; it does not concatenate every source file in that
directory. It may instead name one file directly:

```toml
[build]
source = "application.moss"
```

Phase 7 intentionally has no package manager, dependency resolution, module
system, or imports. Files discovered below `tests/` and `benches/` are separate
compilation units, so their helpers must currently be declared in the same
file. A test or benchmark that needs declarations from the application source
can be placed at top level in the configured application file.

## Everyday workflow

From anywhere inside the project:

```sh
moss build
./build/debug/my_project

moss test

moss build --release
./build/release/my_project

moss bench
```

Moss converts non-alphanumeric characters in the project name to underscores
for artifact filenames. Thus `my-project` becomes `my_project`; the displayed
path from `moss build` is authoritative.

### Debug build

```sh
moss build
```

This checks the configured application source, uses Moss's `-O0` reference
lowering, emits a deterministic `.mossmap`, and invokes Rust with debug
information and `opt-level=0`. It is the normal edit/debug profile and the
primary profile for the Phase 5 Moss-source LLDB workflow. It does not run the
resulting program.

Representative output:

```text
Built my-project (debug)
  /work/my-project/build/debug/my_project
```

Run the displayed executable directly.

### Release build

```sh
moss build --release
```

This enables the highest currently implemented Moss optimization plan and
uses Rust's optimized compilation mode. The current backend flags also retain
limited debug information (`debuginfo=1`). Release is intended for deployment
and performance measurement; `moss bench` selects this path automatically.

Moss performs its own proven functional/dataflow and domain-backend planning
before generating straightforward Rust. The release command does not promise
LTO, a particular CPU instruction set, or any optimization not implemented by
the current compiler.

### Clean and rebuild

```sh
moss clean
moss build
```

`moss clean` removes the project's entire `build/` directory, including debug,
release, test, and benchmark native caches. It deliberately keeps saved
benchmark baselines under `.moss/benchmarks/`. It does not rebuild anything.

Project commands currently do not coordinate concurrent writers. Do not run
two build/test/benchmark commands against the same project directory at the
same time; they can target the same generated files. Sequential commands are
the supported workflow.

## Running unit tests

Tests are top-level Moss declarations:

```moss
test "addition":
  assert(add(2, 3) == 5)
  assertEqual(add(-2, 5), 3)
```

Run every discovered test:

```sh
moss test
```

Or pass one case-sensitive substring filter. Moss matches it against both the
test name and stable test ID:

```sh
moss test addition
```

Representative output:

```text
PASS src/main.moss:addition
PASS tests/arithmetic.moss:external arithmetic

2 passed
0 failed
```

An `assert(...)` failure identifies the original expression. An
`assertEqual(actual, expected)` failure additionally prints the evaluated
values. Each assertion failure is caught at its generated test boundary, so
later tests in that compilation unit still run:

```text
PASS src/main.moss:continues after a failure
FAIL src/main.moss:reports actual and expected
  /work/my-project/src/main.moss:2: Moss assertion failed at line 2: assertEqual(2 + 2, 5); actual=4, expected=5

1 passed
1 failed
```

All tests passing returns exit status 0. An assertion failure, compile error,
discovery error, or test-runtime error returns nonzero. See
[Unit testing](TESTING.md) for assertion types, discovery, filtering, JSON, and
isolation limitations.

## Running benchmarks

Run all benchmarks or select a case-sensitive substring:

```sh
moss bench
moss bench "pipeline sum"
```

Each benchmark uses the release/high-optimization profile. Moss performs 5
untimed warmup rounds, then collects 31 measured samples. A round or sample
invokes the benchmark body 1,000 times. It reports an estimated
per-invocation median and the p25/p75 spread in integer nanoseconds:

```text
pipeline sum

median: 8 ns
p25: 8 ns
p75: 9 ns
samples: 31
```

The numbers above are illustrative; timing varies by machine and system load.
Median is the headline result because it is less sensitive to an isolated
slow sample than a mean. p25 and p75 show the middle half of measured sample
estimates. These are lightweight comparative measurements, not a guarantee of
nanosecond precision.

Discarded results of calls, ordinary value expressions, and functional
pipelines are consumed through a backend black-box boundary so the compiler
cannot trivially delete the benchmarked value. Setup written in the benchmark
body is part of the timed work.

See [Benchmarking](BENCHMARKING.md) for the exact methodology and advice on
constructing useful benchmarks.

## Save, compare, and enforce a baseline

Save the current release measurements:

```sh
moss bench --save main
```

This writes `.moss/benchmarks/main.json`. Compare another run:

```sh
moss bench --compare main
```

Representative comparison:

```text
pipeline sum

median: 8 ns
p25: 8 ns
p75: 9 ns
samples: 31

pipeline sum
baseline: 9 ns
current: 8 ns
change: -11.1%
```

Negative change is faster; positive change is slower. To fail the command when
any matched median is more than 5 percent slower than a compatible baseline:

```sh
moss bench --compare main --fail-over 5%
```

`5` and `5%` are equivalent. The threshold must be nonnegative and requires
`--compare`. The comparison is strictly greater-than: a change of exactly 5
percent does not fail a 5 percent threshold.

Moss compares only matching stable benchmark IDs and only when the baseline's
Moss compiler, release profile, platform, resolved Rust compiler, complete
`rustc --version --verbose` output, and effective backend flags are compatible.
An incompatible baseline produces `BASELINE_INCOMPATIBLE`, skips numeric
comparison, and does not apply `--fail-over`. There is currently no force
option for cross-toolchain comparison.

## Artifacts at a glance

For the project above, application artifacts are:

```text
build/
  debug/
    my_project              # native debug executable
    my_project.rs           # generated Rust (do not edit)
    my_project.mossmap      # Moss/Rust/native provenance
    my_project.mossbuild    # native cache/toolchain identity
  release/
    my_project
    my_project.rs
    my_project.mossmap
    my_project.mossbuild
  test/                     # per-source generated test artifacts
  bench/                    # per-source generated benchmark artifacts

.moss/
  benchmarks/
    main.json               # saved benchmark baseline
```

Most users need only the native executable. `.mossmap` is consumed by the
Phase 5 debugger/disassembly tools. Generated `.rs`, `.mossbuild`, and the
contents of `build/test` and `build/bench` are compiler-managed implementation
artifacts and should not be edited.

Use structured build output to discover exact application paths and whether a
native artifact was reused:

```sh
moss build --json
```

## Build caching

Moss rewrites generated Rust and `.mossmap` files only when their content
changes. It reuses the native executable only if all of these remain true:

- the generated Rust is byte-identical;
- the generated `.mossmap` is byte-identical;
- the native executable still exists;
- `.mossbuild` exactly matches the current backend identity.

The backend identity includes the resolved `rustc` executable, its full
`rustc --version --verbose` response, profile, and effective flags. Practical
examples:

```text
same generated program + same rustc + same profile/flags
  -> native executable may be reused

different RUSTC, Rust version, profile, or effective flags
  -> native executable is rebuilt

moss clean
  -> build cache is removed; the next command rebuilds
```

Set `RUSTC` to select another compiler executable. If it cannot be resolved or
identified, Moss reports a build-tool/backend error rather than trusting an
old executable.

## Structured output for CI and agents

`build`, `clean`, `test`, and `bench` all accept `--json` and use the same
versioned `moss-agent-1` envelope:

```json
{
  "protocol_version": 1,
  "schema_version": "moss-agent-1",
  "compiler_version": "0.2.0",
  "command": "test",
  "ok": true,
  "result": {}
}
```

The detailed testing and benchmarking documents show the command-specific
result fields. Paths, compiler versions, fingerprints, timings, and source
lines naturally vary by project and environment. Start automated tooling with:

```sh
moss agent bootstrap --json
```

## Common problems

### `moss` is not found

Build the compiler with `make`, then invoke `./moss` from the compiler checkout
or put that directory on `PATH`.

### No manifest is found

`PROJECT_MANIFEST_ERROR` means there is no `moss.toml` in the current directory
or any parent, or the manifest is invalid. Run the command inside the intended
project and check that required values are quoted strings.

### The application source is missing

`PROJECT_SOURCE_NOT_FOUND` reports the exact expected path. With
`source = "src"`, that path is `src/main.moss`.

### Rust cannot be found or identified

Install a working Rust compiler on `PATH`, or set `RUSTC` to an executable.
`BUILD_TOOL_NOT_FOUND` means Moss could not resolve it;
`BUILD_BACKEND_ERROR` means version discovery or native compilation failed.

### No test or benchmark matches

Filters are case-sensitive substrings. An unmatched test filter reports
`TEST_DISCOVERY_ERROR`; an unmatched benchmark filter reports
`BENCHMARK_CONFIGURATION_ERROR`. A filter selects declarations to execute but
does not bypass parsing and static checking of discovered source files.

### A benchmark baseline is incompatible

Use a baseline recorded with the same Moss compiler, release profile, platform,
Rust compiler identity, and backend flags. Moss intentionally refuses to apply
regression thresholds across incompatible environments. Save a new baseline
only after deciding that the new environment is the comparison point you want.

### A build appears stale

The cache is content- and backend-aware, so an unchanged executable normally
means the generated program and toolchain identity were unchanged. To force a
fresh native build, run `moss clean` and build again. Never edit generated Rust
or `.mossbuild` to work around a cache issue.
