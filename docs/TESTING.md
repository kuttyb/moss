# Moss unit testing

Phase 7 unit tests are Moss declarations, not macros or generated-Rust test functions
that users must manage:

```moss
fn add(left: int, right: int) -> int:
  left + right

test "addition":
  assert(add(2, 3) == 5)
  assertEqual(add(-2, 5), 3)
```

`assert` requires one `bool`. `assertEqual` requires two values with the same resolved
scalar, string, or Vector type. A failure reports the test identity, Moss file and line,
failed source expression, and actual/expected values when available. It is presented as
`TEST_ASSERTION_FAILED`, not as a generated Rust panic trace.
The two assertion names are reserved builtins so a user function cannot silently shadow
the test contract.

Run all tests or filter by test name/stable identity:

```sh
moss test
moss test addition
moss test --json
```

Moss discovers declarations in the configured application source and recursively in
`tests/*.moss`. Test IDs have the deterministic form
`test:<project-relative-source>:<name>`. Files under `tests/` are independently compiled
test units in this initial implementation; because Moss does not yet have a module or
import system, helpers used by such a file must currently be declared in that file.
Tests that need application-local declarations can live beside them in the configured
source.

Tests execute sequentially. An assertion panic is caught at each generated test
boundary so later tests are still reported. Process-level aborts and failures in other
threads may still terminate that test executable; Phase 7 deliberately does not add a
process-isolation framework.

Human output is concise:

```text
PASS src/main.moss:addition
FAIL tests/parser.moss:malformed

23 passed
1 failed
```

All-pass exits zero. Assertion failure, discovery failure, or compilation failure exits
nonzero. `moss test --json` reuses the Phase 6A protocol and includes stable IDs,
locations, status, structured diagnostics, and totals. Tests are omitted from ordinary
application artifacts.

The intentionally failing example in
[`examples/projects/phase7_failing_test`](../examples/projects/phase7_failing_test)
demonstrates actual/expected reporting and continuation after a failed assertion.
