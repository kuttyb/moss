# Fast Debug execution

Fast Debug is the first Phase 10 execution backend. It runs the already
checked Moss representation directly, so ordinary edit/check/run cycles do not
need generated Rust or `rustc`.

```text
moss run --interp program.moss
moss run --interp --trace program.moss
moss test --interp tests/fast_debug.moss
```

The interpreter shares Moss's parser, type/effect/ownership checks, call
resolution, and other front-end validation. It is not a second type system and
does not provide dynamic dispatch or an `Any` escape hatch. Integer arithmetic
uses Moss's wrapping semantics, and function calls, locals, arithmetic,
conditionals, while loops, structs, fields, methods, and assertions are
supported in this initial 10.1A slice.

`--trace` writes newline-delimited structured JSON events to standard error.
Events retain the Moss function and source line, including function entry and
exit, local writes, branch choices, loop iterations, and assertion failures.
The trace is intentionally machine-readable so later debugger and agent tools
can consume it without parsing display text.

The initial interpreter is deliberately limited: domain instances, `message`,
`await`, and `reply` still require the compiled backend. Fast Debug reports a
clear interpreter diagnostic rather than silently falling back to Rust. The
compiled Rust/LLVM path remains the production and concurrency implementation.
