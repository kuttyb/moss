# Moss unit testing

Moss tests are ordinary top-level language declarations checked by the same
static compiler pipeline as application code. `moss test` discovers them,
builds native test harnesses, and reports failures in Moss terms and at Moss
source locations.

For a first project, begin with [Project workflow](PROJECT_WORKFLOW.md). This
document is the testing reference.

## Test declarations

A test has a quoted, nonempty name and a nonempty two-space-indented body:

```moss
fn add(left: int, right: int) -> int:
  left + right

test "addition":
  assert(add(2, 3) == 5)
  assertEqual(add(-2, 5), 3)
```

The declaration must be at top level. The colon, quoted name, and parentheses
on `assert(...)`/`assertEqual(...)` are part of the accepted syntax. Duplicate
test names in one compilation unit are rejected.

Tests can appear in:

- the configured application source (`src/main.moss` by convention); or
- any `.moss` file recursively below the project's `tests/` directory.

The configured application source is always considered during test discovery.
The `tests/` directory itself is optional.

### Compilation-unit boundary

Every external test file is compiled independently. Moss does not yet have a
module or import system, so `tests/arithmetic.moss` cannot call a function
defined only in `src/main.moss`. Put helpers in the same external file:

```moss
fn doubled(value: int) -> int:
  value * 2

test "external arithmetic":
  assertEqual(doubled(21), 42)
```

If a test needs application-local declarations, place that test in the
configured application source beside those declarations. This is a current
project-model limitation, not a requirement to duplicate generated Rust.

Ordinary `moss build` and `moss build --release` do not generate or execute the
test harness. Test declarations are included only when `moss test` builds its
per-source artifacts under `build/test/`.

## Assertions

Moss reserves `assert` and `assertEqual` for test assertions. User functions
cannot shadow these names.

### `assert(condition)`

`assert` accepts exactly one statically resolved `bool`:

```moss
test "positive sum":
  total = 2 + 3
  assert(total > 0)
```

A false condition reports the original Moss expression and assertion line.
Representative output for `assert(2 + 2 == 5)` is:

```text
FAIL tests/arithmetic.moss:boolean check
  /work/my-project/tests/arithmetic.moss:2: Moss assertion failed at line 2: assert(2 + 2 == 5)
```

The message comes from the Moss test harness; a generated Rust panic trace is
not the primary user interface.

### `assertEqual(actual, expected)`

`assertEqual` accepts exactly two operands whose statically resolved types are
the same. Current printable comparison support covers `int`, `float`, `bool`,
`string`, and `Vector` values. Compare a field explicitly for other
user-defined values.

```moss
test "exact values":
  assertEqual(2 + 3, 5)
  assertEqual("moss", "moss")
  assertEqual([1, 2, 3], [1, 2, 3])
```

On failure, both operands are evaluated once and reported as `actual` and
`expected`:

```text
FAIL src/main.moss:reports actual and expected
  /work/my-project/src/main.moss:2: Moss assertion failed at line 2: assertEqual(2 + 2, 5); actual=4, expected=5
```

Wrong assertion arity, a non-boolean `assert` condition, mismatched
`assertEqual` types, and unsupported printable types are compile-time Moss
diagnostics (`TEST_ASSERTION_CONFIGURATION_ERROR` in project/JSON reporting),
not runtime assertion failures.

## Discovery and stable IDs

Run all tests from the project root or a subdirectory:

```sh
moss test
```

Moss collects the configured application source and every regular `.moss` file
recursively below `tests/`, sorts/deduplicates source paths, checks each source,
and builds one native test unit per source containing selected declarations.

Each project test has an ID:

```text
test:<project-relative-source>:<declared-name>
```

Examples:

```text
test:src/main.moss:addition
test:tests/arithmetic.moss:external arithmetic
```

This ID is deterministic for the same project-relative path and declaration
name and is suitable for reports and automation. Renaming the declaration or
moving the source file changes it.

## Filtering

Pass at most one filter argument:

```sh
moss test addition
moss test 'test:tests/arithmetic.moss:external'
```

Filtering is a case-sensitive substring match against either the declared test
name or the full stable ID. It is not a regular expression, glob, or exact-only
match. Thus `addition` matches a test named `integer addition`, while
`Addition` does not unless the capitalization also matches.

The filter selects test declarations to generate and run. It does not skip
parsing and static checking of the discovered source units. A compile error in
a discovered file can therefore fail a filtered command even when the broken
declaration's name does not match.

If no test matches, the command exits nonzero:

```text
moss: error[TEST_DISCOVERY_ERROR]: no Moss tests matched filter 'missing'
```

If the project contains no tests at all, the same code reports `no Moss tests
were found`.

## Human-readable results and exit status

Results are sorted by stable ID and use project-relative source paths:

```text
PASS src/main.moss:addition
PASS tests/arithmetic.moss:external arithmetic

2 passed
0 failed
```

A mixed result looks like:

```text
PASS src/main.moss:continues after a failure
FAIL src/main.moss:reports actual and expected
  /work/my-project/src/main.moss:2: Moss assertion failed at line 2: assertEqual(2 + 2, 5); actual=4, expected=5

1 passed
1 failed
```

Exit status is:

| Situation | Status |
| --- | --- |
| Every selected test passes | 0 |
| One or more assertion failures | Nonzero (currently 1) |
| Discovery, compilation, configuration, or runtime failure | Nonzero (currently 1) |

Within one compiled source unit, tests run sequentially. Moss catches an
assertion panic at each test boundary, records it, and continues to later tests
in that unit. Tests are not process-isolated from one another: process aborts,
stack overflows, or failures in other threads can terminate the unit and yield
`TEST_RUNTIME_ERROR`. There is currently no parallel runner, per-test process
isolation, setup/teardown API, or retry mechanism.

Different source files are separate native test processes, but a compile or
runtime infrastructure error stops the overall command rather than promising
to report every later source unit.

## JSON output

Use the shared Phase 6A protocol for CI and agents:

```sh
moss test --json
moss test addition --json
```

A passing filtered result has this shape (absolute paths vary):

```json
{
  "protocol_version": 1,
  "schema_version": "moss-agent-1",
  "compiler_version": "0.2.0",
  "command": "test",
  "ok": true,
  "result": {
    "project": "phase7-demo",
    "filter": "addition",
    "tests": [
      {
        "id": "test:src/main.moss:addition",
        "name": "addition",
        "status": "pass",
        "source_file": "/work/phase7-demo/src/main.moss",
        "line": 11,
        "diagnostic": null
      }
    ],
    "summary": {
      "passed": 1,
      "failed": 0,
      "total": 1
    }
  }
}
```

An assertion failure remains a normal test result (with `ok: false`) and has a
structured diagnostic:

```json
{
  "id": "test:src/main.moss:reports actual and expected",
  "name": "reports actual and expected",
  "status": "fail",
  "source_file": "/work/my-project/src/main.moss",
  "line": 2,
  "diagnostic": {
    "code": "TEST_ASSERTION_FAILED",
    "severity": "error",
    "message": "Moss assertion failed at line 2: assertEqual(2 + 2, 5); actual=4, expected=5",
    "details": {
      "expression": "assertEqual(2 + 2, 5)",
      "actual": "4",
      "expected": "5"
    }
  }
}
```

For a boolean `assert`, `expression` is populated and `actual`/`expected` are
`null`. A discovery or compilation failure uses the protocol's top-level
`error` object instead of `result`. Consumers should use `ok`, stable codes,
and IDs rather than parse human output. See [Agent API](AGENT_API.md).

## Troubleshooting

| Code | Meaning |
| --- | --- |
| `TEST_DISCOVERY_ERROR` | No declarations exist/match, too many filter arguments were supplied, or duplicate names were found. |
| `TEST_ASSERTION_CONFIGURATION_ERROR` | Assertion arity/type/support is invalid at compile time. |
| `TEST_ASSERTION_FAILED` | A selected test executed and an assertion was false. |
| `TEST_RUNTIME_ERROR` | The native harness exited or reported malformed/incomplete results. |
| `BUILD_TOOL_NOT_FOUND` / `BUILD_BACKEND_ERROR` | Rust could not be found, identified, or used to compile the test unit. |

Common checks:

- Confirm that the declaration is top-level, quoted, ends in `:`, and has a
  nonempty indented body.
- Remember that filters are case-sensitive substrings.
- Keep external-test helpers in the same file until Moss has modules/imports.
- Run project commands sequentially; Phase 7 does not lock shared artifact
  directories against concurrent invocations.
- Use `moss clean` to discard generated harness caches if an artifact was
  interrupted or externally damaged.

The intentionally failing
[`examples/projects/phase7_failing_test`](../examples/projects/phase7_failing_test)
is a runnable reference for actual/expected output and continued reporting.
