# Moss project and build system

Phase 8 extends the Phase 7 project layer with first-class modules while
retaining one semantic compiler pipeline.
Project builds use the same parser, static checks, ownership/effect analysis,
functional IR, backend plan, Rust generator, and `.mossmap` provenance as
direct source compilation. Explicit modules are then projected into separate
Rust crates and compiled in import-DAG order.

Start with the practical [project workflow](PROJECT_WORKFLOW.md) if you are
creating your first project. This document is the build-system reference.

## Requirements and compiler setup

Moss itself requires:

- a C++17 compiler (`c++`, `g++`, Clang, or Apple Clang);
- `make`;
- `rustc` for generated native programs.

Build the compiler from its checkout:

```sh
make
```

That produces `./moss`. Put the checkout on `PATH` or invoke the binary by its
absolute path. Phase 7 does not yet include an installer.

By default Moss resolves `rustc` on `PATH`. `RUSTC` may select another
executable:

```sh
RUSTC=/opt/rust/bin/rustc moss build --release
```

For explicit modules, each module produces a generated Rust unit and ordinary
`lib<module>.rlib`; consumers receive dependency paths through rustc
`--extern`. The root crate links those rlibs and the graph-owned
`libmoss_specializations.rlib`. `.mossi` is the Moss semantic interface:
typed exports can be consumed from `.mossi` plus rlib without source, while
generic exports carry versioned Moss semantic IR. rustc MIR/rmeta is not a
Moss compatibility format.

The selected compiler must execute `--version --verbose` successfully. Its
resolved path and complete response become part of the native-cache identity.

## Conventional layout

```text
project/
  moss.toml
  src/
    main.moss
  tests/
  benches/
```

Only `moss.toml` and at least one `.moss` file below the configured source
root are needed for `moss build`. `tests/` and `benches/` are conventional
optional discovery directories for `moss test` and `moss bench`.

## Manifest reference

A complete manifest is:

```toml
[project]
name = "example"
version = "0.1.0"

[build]
source = "src"
```

### Supported fields

| Field | Required | Default | Current use |
| --- | --- | --- | --- |
| `[project].name` | Yes | None | Project reports and sanitized application artifact name. |
| `[project].version` | Yes | None | Required project version metadata. |
| `[build].source` | No | `src` | Application `.moss` file or recursive source directory relative to the project root. |

`[build]` may therefore be omitted when the entry point is `src/main.moss`:

```toml
[project]
name = "example"
version = "0.1.0"
```

Manifest values must be quoted strings. Blank lines and `#` comments outside
strings are accepted. Unknown sections and unknown keys are rejected rather
than ignored, so misspellings do not silently change a build.

`build.source` must be nonempty, project-relative, and contain no `..` path
component. When it names a directory, Moss recursively discovers all `.moss`
files below it in canonical relative-path order:

```text
source = "src"       -> every <project>/src/**/*.moss
source = "app.moss"  -> <project>/app.moss
```

The source root must contain at least one regular `.moss` file. Files are
parsed as one logical global compilation unit; sorted file order is only a
deterministic loading order, not a declaration-order language rule.

### Project discovery

`moss build`, `moss clean`, `moss test`, and `moss bench` search for the nearest
`moss.toml`, starting in the current working directory and walking toward the
filesystem root. Commands therefore work from the project root or a nested
directory:

```sh
cd project/src
moss build
```

There is no Phase 7 `--project` option. If nested project roots exist, the
nearest manifest wins.

### Current project-model limits

Phase 7 intentionally does not implement:

- dependency or package resolution;
- registries or lockfiles;
- modules/imports between Moss files;
- library targets or multiple application targets;
- custom test/benchmark directory settings;
- arbitrary profile configuration in `moss.toml`.

Until explicit modules/imports exist, Moss uses a temporary uber-module model:

```text
moss build -> all src/**/*.moss
moss test  -> all src/**/*.moss + all tests/**/*.moss
moss bench -> all src/**/*.moss + all benches/**/*.moss
```

Each target is one logical compilation unit with one global namespace. There
are no implicit directory namespaces; duplicate top-level names are ordinary
Moss duplicate-symbol errors. Physical source paths remain attached to
diagnostics, `.mossmap` entries, tests, and benchmarks.

The compiler oracle follows this same target composition: semantic queries and
edits against a `src` file analyze all application sources, while queries against
`tests` or `benches` analyze the corresponding source plus that target directory.
This temporary linkage is not an import/module system.

For example:

```text
my-project/
  moss.toml
  src/math.moss
  src/pricing.moss
  tests/pricing_test.moss
  benches/pricing_bench.moss
```

`moss build` links `math.moss` and `pricing.moss`; `moss test` additionally
links `pricing_test.moss`; and `moss bench` additionally links
`pricing_bench.moss`. No import is needed between these files.

## Build commands

```sh
moss build
moss build --release
moss clean
```

All three commands also accept `--json`. Other build/clean arguments are
rejected with `BUILD_PROFILE_ERROR`.

### `moss build`: debug/reference profile

`moss build`:

1. loads the nearest project manifest;
2. discovers all application `.moss` files under `build.source`;
3. parses and statically checks them as one unit;
4. uses Moss's `-O0` reference plan;
5. emits generated Rust and a `.mossmap` provenance map;
6. compiles a native executable with Rust debug information.

The current effective Rust flags are:

```text
--edition=2021 -D warnings -g -C opt-level=0
```

This is the preferred profile for source-level LLDB/DAP work. It preserves
stable generated symbols and debug information and favors correspondence with
Moss source over runtime performance. See [Tooling](TOOLING.md).

The command builds but does not run the executable:

```text
Built phase7-demo (debug)
  /work/phase7-demo/build/debug/phase7_demo
```

### `moss build --release`: optimized profile

The release profile enables the highest currently implemented Moss
optimization plan before Rust generation. That includes proven functional
semantic rewrites/fusion and the existing optimized domain-backend planning;
it does not change Moss source semantics. Rust is then invoked with:

```text
--edition=2021 -D warnings -O -C debuginfo=1
```

Release is the production/performance profile. `moss bench` always uses this
optimized path without requiring `--release`.

No additional claim is implied: Phase 7 does not enable a configurable LTO
mode, native-CPU targeting, SIMD, threading, or other optimization merely by
calling the profile “release.”

### `moss clean`

`moss clean` recursively removes `<project>/build`:

```text
Cleaned phase7-demo.
```

This removes application, test, and benchmark generated files and native
caches. The next command recompiles them. Saved baselines live under
`.moss/benchmarks/` and are deliberately retained.

Project artifact writes are not currently guarded by a cross-process lock.
Run project commands sequentially within one project rather than launching
concurrent builds into the same artifact directories.

## Artifact layout

For `[project].name = "phase7-demo"`, application builds produce:

```text
build/
  debug/
    phase7_demo
    phase7_demo.rs
    phase7_demo.mossmap
    phase7_demo.mossbuild
  release/
    phase7_demo
    phase7_demo.rs
    phase7_demo.mossmap
    phase7_demo.mossbuild
  test/
    phase7_demo_tests
    phase7_demo_tests.rs
    phase7_demo_tests.mossmap
    phase7_demo_tests.mossbuild
  bench/
    phase7_demo_benches
    phase7_demo_benches.rs
    phase7_demo_benches.mossmap
    phase7_demo_benches.mossbuild
```

Application filenames use a sanitized project name: characters other than
letters, digits, and `_` become `_`, trailing underscores are removed, and an
otherwise empty name becomes `anonymous`. Test and benchmark targets use one
combined artifact per target; the exact sanitized names are reported by
`--json`.

| Artifact | Purpose | Usually user-facing? |
| --- | --- | --- |
| Extensionless file | Native executable. | Yes. |
| `.rs` | Compiler-generated Rust backend input. | No; do not edit it. |
| `.mossmap` | Shared Moss-to-Rust/native source and symbol provenance for Phase 5 tools. | Only for debugging/tooling. |
| `.mossbuild` | Backend identity sidecar used for native cache validation. | No; do not edit it. |

Release builds also emit `.mossmap`, but the primary high-fidelity debugging
contract is the debug/`-O0` profile. Tests and benchmarks have their own native
harness artifacts; declarations from those harnesses do not enter the normal
application executable.

## Native artifact caching

Caching is small and content-based. Before backend compilation, Moss compares:

- the complete ordered project source set (relative paths and contents);
- the complete generated Rust file;
- the complete generated `.mossmap`;
- existence of the native executable;
- exact `.mossbuild` contents for the selected backend.

`.mossbuild` contains a deterministic fingerprint derived from at least:

- the absolute resolved `rustc` executable path;
- the complete output of `rustc --version --verbose`;
- the effective profile (`debug`, `test-debug`, or `release`);
- every effective Rust compiler flag.

Moss reuses the executable only when the generated files are byte-identical,
the executable exists, and this sidecar exactly matches. Otherwise it removes
the prior executable and invokes Rust again. Missing or edited sidecars are a
cache miss, not a reason to trust an old binary.

Practical behavior:

```text
unchanged generated source/map + same rustc/profile/flags
  -> reuse is allowed

changed generated source or source provenance
  -> rebuild

changed RUSTC path, Rust version, profile, or flags
  -> rebuild

missing executable or .mossbuild
  -> rebuild

moss clean
  -> all build/ cache entries removed
```

This is not a package-level incremental compiler: it does not cache individual
Moss declarations or dependency cones. A source edit can still yield a cache
hit only if both generated Rust and provenance happen to remain byte-for-byte
unchanged.

## Structured build and clean output

`moss build --json` uses the Phase 6A protocol and reports exact paths,
backend identity, and cache reuse. Values below are representative; absolute
paths, compiler identity, and fingerprint depend on the system:

```json
{
  "protocol_version": 1,
  "schema_version": "moss-agent-1",
  "compiler_version": "0.2.0",
  "command": "build",
  "ok": true,
  "result": {
    "project": "phase7-demo",
    "profile": "debug",
    "source": "/work/phase7-demo/src/main.moss",
    "artifacts": {
      "executable": "/work/phase7-demo/build/debug/phase7_demo",
      "generated_rust": "/work/phase7-demo/build/debug/phase7_demo.rs",
      "debug_map": "/work/phase7-demo/build/debug/phase7_demo.mossmap",
      "cache_metadata": "/work/phase7-demo/build/debug/phase7_demo.mossbuild"
    },
    "backend_toolchain": {
      "fingerprint": "0123456789abcdef",
      "resolved_rustc": "/usr/bin/rustc",
      "version_verbose": "rustc <installed version and verbose identity>",
      "profile": "debug",
      "compile_flags": [
        "--edition=2021", "-D", "warnings", "-g", "-C", "opt-level=0"
      ]
    },
    "reused": true
  }
}
```

The illustrative fingerprint is not a fixed Moss value. Automation should
treat it as an opaque identity and use `reused` rather than comparing to the
example.

`moss clean --json` returns the removed directory:

```json
{
  "protocol_version": 1,
  "schema_version": "moss-agent-1",
  "compiler_version": "0.2.0",
  "command": "clean",
  "ok": true,
  "result": {
    "project": "phase7-demo",
    "removed": "/work/phase7-demo/build"
  }
}
```

See [Agent API](AGENT_API.md) for the shared envelope and structured error
shape.

### Message payload lowering

Incoming domain payloads are immutable Moss value snapshots, including
primitive values and domain handles; handlers cannot reassign or reply with
the original binding. Mailbox-backed targets retain an owned copy in the queued
message. When the planner proves a
synchronous DirectMutex, DirectRwLock, DirectAtomic, or cluster-local call,
nontrivial READ-only payloads are passed as temporary immutable references in
generated Rust instead of cloned at that boundary. Primitive Copy values stay
by value. The logical copy boundary and all observable behavior are unchanged.

## Failures and troubleshooting

Human diagnostics have this general shape:

```text
/work/project/moss.toml:4: error[PROJECT_MANIFEST_ERROR]: unknown manifest key 'project.source'
```

JSON mode places the same stable code and Moss/project location in the shared
error envelope.

| Code | Meaning and first check |
| --- | --- |
| `PROJECT_MANIFEST_ERROR` | No nearest manifest, missing required field, unquoted value, unknown field, or unsafe source path. |
| `PROJECT_SOURCE_NOT_FOUND` | The configured file, or `main.moss` below the configured directory, does not exist. |
| `BUILD_PROFILE_ERROR` | Unsupported argument to `build` or `clean`. |
| `BUILD_TOOL_NOT_FOUND` | `rustc`/`RUSTC` did not resolve to an executable. |
| `BUILD_BACKEND_ERROR` | Rust identity discovery or native compilation failed; the diagnostic includes backend output for compilation failures. |
| `MOSS_INTERNAL_OR_IO_ERROR` | Artifact creation, process launch, cache replacement, or another filesystem/process operation failed. |

If a native artifact is unexpectedly missing or damaged, `moss clean` followed
by the desired build is the supported recovery. Do not repair the generated
`.rs` or `.mossbuild` manually.
## Phase 8 modules

Explicit modules are checked as logical units and composed in import-DAG order.
The Rust backend remains the materialized-code boundary; Moss does not persist
rustc MIR. Builds emit versioned `.mossi` semantic interfaces containing public
signatures, ownership/effect contracts, legacy await metadata where present, and
generic semantic artifacts.
Optimization and the Rust toolchain fingerprint affect native artifact caches,
not semantic module identity.
