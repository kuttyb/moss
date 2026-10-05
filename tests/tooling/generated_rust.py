"""Shared helpers for scanning compiler-generated Rust."""

EXECUTOR_RUNTIME_BEGIN = "// Phase 20 Executor Runtime  (moss executor_runtime_rust)"
EXECUTOR_RUNTIME_END = "// End Phase 20 Executor Runtime  (moss executor_runtime_rust)"


def without_executor_runtime(rust):
    """Return generated Rust minus the single delimited Phase 20 executor
    runtime block that every root emits. Its type-erased Root/Branch work
    (`Box<dyn FnOnce() + Send>`), FFI `unsafe`, and atomics are runtime
    plumbing, so negative scans of compiler lowering skip exactly that block
    and still check every other line."""
    if rust.count(EXECUTOR_RUNTIME_BEGIN) != 1 or rust.count(EXECUTOR_RUNTIME_END) != 1:
        raise AssertionError("expected exactly one delimited Phase 20 executor runtime block")
    begin = rust.index(EXECUTOR_RUNTIME_BEGIN)
    end = rust.index(EXECUTOR_RUNTIME_END, begin) + len(EXECUTOR_RUNTIME_END)
    return rust[:begin] + rust[end:]
