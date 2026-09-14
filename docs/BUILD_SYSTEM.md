# Moss project and build system

Phase 7 adds a small project layer around the existing compiler. It does not add
packages, dependency resolution, modules, or a second compilation pipeline. Project
builds use the same parser, static checks, ownership/effect analysis, functional IR,
backend plan, Rust generator, and `.mossmap` provenance as direct source compilation.

## Layout and manifest

A conventional project is:

```text
project/
  moss.toml
  src/
    main.moss
  tests/
  benches/
```

The initial manifest is deliberately small:

```toml
[project]
name = "example"
version = "0.1.0"

[build]
source = "src"
```

`build.source` may name a project-relative `.moss` file or a directory. A directory
means its `main.moss`. The compiler searches the current directory and its parents for
`moss.toml`, so commands work from a project subdirectory. Unknown sections and keys are
rejected instead of being silently ignored. Package and dependency fields are not part
of Phase 7.

## Commands and profiles

```sh
moss build
moss build --release
moss clean
```

Debug builds use the `-O0` Moss reference plan, Rust debug information, and Rust
`opt-level=0`. Release builds use the highest currently implemented Moss optimization
plan and optimized Rust generation. Both compile generated Rust with warnings denied.
Normal project use does not require invoking `rustc` directly.
Moss finds `rustc` on `PATH`; `RUSTC` may name an alternate compiler executable.

Artifacts have deterministic locations:

```text
build/debug/<project>
build/debug/<project>.rs
build/debug/<project>.mossmap
build/release/<project>
build/release/<project>.rs
build/release/<project>.mossmap
build/test/...
build/bench/...
```

If generated Rust and provenance are byte-identical and the native artifact still
exists, Moss reuses it. This is intentionally only a small content-based reuse rule,
not a package-level incremental build system. `moss clean` removes `build/` but retains
saved benchmark baselines under `.moss/benchmarks/`.

Every project command also accepts `--json` and uses the versioned `moss-agent-1`
envelope described in [the agent API](AGENT_API.md). Project failures use stable codes
such as `PROJECT_MANIFEST_ERROR`, `PROJECT_SOURCE_NOT_FOUND`,
`BUILD_PROFILE_ERROR`, `BUILD_TOOL_NOT_FOUND`, and `BUILD_BACKEND_ERROR`.

See [`examples/projects/phase7_demo`](../examples/projects/phase7_demo) for a complete
small project.
