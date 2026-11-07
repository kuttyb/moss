# Reviewer-controlled ORACLE-15 holdouts — version 2

Frozen before any compiler invocation for v2 against combined compiler source `83e42eece65663b3de46e09a14227f9e17633513`.

1. `holdout_read.moss`: `binding = holder.entries` has only READ uses (passed to `count_entries`, whose only body operation counts values), then the owning field is read again. Expected: checker accepts and Fast Debug, Native Debug, and Native Optimized each print exactly `4`.
2. `holdout_consume.moss`: `binding = ledger_value.records` is passed to a callee that returns its `Vector[Int]` parameter, making it a consuming use. Expected: checker, Fast Debug, Native Debug, and Native Optimized all reject with `OWNERSHIP_ILLEGAL_CONSUME`; no Rust artifact should be emitted. Explicit `take` is required.

Holdout v1 was abandoned after native Rust compilation showed that the test variable `crate` is reserved in emitted Rust (`r#crate` is not allowed). V1 was not used as evidence. V2 changes only that local identifier and was frozen before rerunning any compiler command.
