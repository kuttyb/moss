# First formulation: `for range` simulation

The checker accepted `src/main.moss` without diagnostics:

```json
{"command":"check","ok":true,"result":{"diagnostics":[]}}
```

Native package build then failed in generated code, reported through Margo as
structured `BUILD_BACKEND_ERROR`: Rust could not find the source loop variable
`day` at each of the three indexed reads in `simulate`, and the generated loop
closure also returned `i64` where `()` was expected. This is a native-lowering
failure after a successful Moss check, not a source rejection.

Fast Debug failed separately and explicitly:

```text
moss:47: interpreter error: Fast Debug does not support for iteration yet
```

The source formulation is preserved in `01_for_loop_main.moss`; the complete
command output was observed before the `while` rewrite. The Fast Debug result
matches its published current limitation. The native result needs a minimal
reproducer before any broader compiler conclusion.

## Minimal native reproducer

`01_for_native_reproducer/` contains only an indexed `for index in range(...)`
sum. Its `moss check --json` also succeeds. Its Margo build fails with the same
structured `BUILD_BACKEND_ERROR`: `index` is absent in generated native code and
the loop closure's `i64` result conflicts with expected `()`. This establishes a
native-lowering bug for indexed range traversal, rather than an application bug.
