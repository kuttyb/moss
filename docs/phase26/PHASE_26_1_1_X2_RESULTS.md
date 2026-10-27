# Phase 26.1.1 X2 — entry-scoped Fast Debug checkpoint

## Implementation

Fast Debug now scopes project checking from the selected `main` or test entries.
A filtered test checks only that test's reachable source bodies; `moss test
--interp` without a file includes all package tests. Root-package source files
remain available for name resolution, while unreachable function bodies are
removed before checking. The runtime reports an internal
`FAST_DEBUG_UNCHECKED_EXECUTION` error if execution attempts an unresolved
callable or method after scoped checking.

Margo `debug` and `test --interp` refresh dependency packages with check-only
builds, then pass their `.mossi` directories through `MOSS_MODULE_PATH`. The
root package is not package-checked first; it is checked from the selected
Fast Debug entries. The source-root environment path and its editor handoff
were removed. Dependency imports resolve through the current `.mossi`; stale
interfaces fail with the existing `margo build` message.

X0's Fast Debug identity targets now count source functions checked in the
consumer. Non-generic dependency bodies are already checked inputs from their
`.mossi` Section 2, so dependency package checks and regenerated artifacts are
reported separately.

## Evidence at this checkpoint

- `tests/tooling/check_phase26_1_1_x2.py`: passed. Covers selected `main`, a
  filtered test despite an invalid unselected test, no-file package test
  discovery, Margo dependency snapshot creation and refresh after a body edit,
  no-rustc execution, and ignoring `MOSS_FAST_DEBUG_SOURCE_ROOTS` when a
  dependency interface is stale.
- `tests/tooling/check_phase15_8_cross_package_fast_debug.py`: 10/10 passed
  after updating its source-provider expectations to `.mossi` execution.
- `tests/tooling/check_phase25_conformance.py`: G2 11/11 passed. Gate 11 now
  expects Margo Fast Debug to execute the dependency generic from its checked
  interface.
- `MOSS_M1_ENFORCE_TARGETS=1 make package-perf`: passed, 86/86 scenario rows.
  For the partial-reach rows, Fast Debug checked one reached consumer function
  and zero dependency functions in the consumer; E1 regenerated only the
  provider artifact, and E2/E3 followed the expected package propagation.
- The initial full `make check` at X1 SHA `424818e6` had 143/146 jobs pass.
  The two Fast Debug failures expected the former source-required behavior and
  are covered by the passing updated suites above. The runtime
  `48-tmax-physical-thread-slots` failure passed on isolated rerun (54/54 Phase
  20 executor tests); the full suite must be rerun before final gates.

X2 remains in progress. The full corpus Fast Debug parity, R12 corpus guard,
corpus differential, complete `make check`, `make c1-corpus-gates`, compiler
performance guard, and Architect review of the X2 implementation SHA remain
pending.
