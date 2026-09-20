# Fast Debug execution

Fast Debug is the first Phase 10 execution backend. It runs the already
checked Moss representation directly, so ordinary edit/check/run cycles do not
need generated Rust or `rustc`.

```text
moss run --interp program.moss
moss run --interp --trace program.moss
moss test --interp tests/fast_debug.moss
moss debug app
```

For a project, `moss debug` accepts the project name (`app`, the manifest
name, or `.`), a project directory, or an entry `.moss` file. It uses the
same checked project analysis as native builds. Explicit-module projects
execute the transitive source-module closure of the entry module; legacy
projects use their complete `src/**/*.moss` uber-module. Every selected Moss
module runs in the interpreter for that invocation. A compiled `.mossi` module
or other native Moss dependency is not mixed into an interpreted run; include
the reachable Moss source or use the native build instead.

The interpreter shares Moss's parser, type/effect/ownership checks, call
resolution, and other front-end validation. It is not a second type system and
does not provide dynamic dispatch or an `Any` escape hatch. Integer arithmetic
uses Moss's wrapping semantics, and function calls, locals, arithmetic,
conditionals, while loops, structs, fields, methods, and assertions are
supported in this initial 10.1A slice.

`--trace` writes newline-delimited structured JSON events to standard error.
Events retain the Moss function, physical source file, semantic identity, and
source line, including function entry and exit, local reads/writes, returns,
branch choices, loop iterations, and assertion failures. The event names and
fields describe settled ordinary execution only; lock, synchronization, and
domain-rank events are reserved until those designs are settled.
The trace is intentionally machine-readable so later debugger and agent tools
can consume it without parsing display text.

The initial interpreter is deliberately limited: domain instances, `message`,
`await`, and `reply` still require the compiled backend. Fast Debug reports a
clear interpreter diagnostic rather than silently falling back to Rust. The
compiled Rust/LLVM path remains the production and concurrency implementation.
