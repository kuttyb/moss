# Independent review — Phase 28 R7-010 Team A

- **Reviewed SHA:** `735a36c3ac2b1a6501721ca746cb7a6293ce6cd1`
- **Review checkout:** detached at the exact SHA above. No candidate source was edited.
- **Verdict:** **NO-GO**

## Required issue dispositions

### Isolated Phase 26 `-Werror` validation — CLOSED for the C3 parse-cache gate

Command:

```sh
python3 tests/tooling/check_phase26_0_1_c3_parse_cache.py tmp/review-r7-010-team-a-build/moss-dev
```

Result: PASS. The helper compile uses `g++ -std=c++17 -O0 -Wall -Wextra -Werror -Wno-missing-field-initializers -pedantic`; the negative-parse cache probes and all three profile fixtures pass.

### Overloaded-method identity defect — OPEN; blocks GO

The original saved repro was not recoverable. A minimal replacement is at `tmp/review-overloaded-none-run.moss`. It defines `Choice.next(Int) -> option[Int]` and `Choice.next(String) -> option[String]`, with both overloads returning `None`.

- `moss check --json` accepts the source.
- With `MOSS_TEST_DUMP_PHASE28_AUTHORITY_FACTS=1` and `MOSS_TEST_VALIDATE_PHASE28_AUTHORITY_FACTS=1`, both ReturnValue facts report `type=option[int]`, empty `specialization_identity`, and `context=method:Choice.next`. Their EST contexts differ only by source line (`:3` versus `:5`). The String overload must have type `option[string]`.
- `moss run --interp` fails closed with `MOSS_INTERNAL_INVARIANT return-value EST type fact missing outside exact imported owner`.

`tests/tooling/check_phase28_return_none_authority.py` passes, but it does not assert overload-specific type or identity separation.

## Phase 26 source-free X1

Command:

```sh
python3 tests/tooling/check_phase26_1_1_x1.py tmp/review-r7-010-team-a-build/moss-dev tmp/review-r7-010-team-a-x1
```

Result: FAIL. Check-only Fast Debug emits `4\n7\n`, then errors with `MOSS_INTERNAL_INVARIANT checked return-value authority unavailable` before the handler result. The X1 gate is therefore not green on this candidate.

## Artifacts and integrity

The compiler was built independently from the exact review checkout into `tmp/review-r7-010-team-a-build/`. Reproducer, output, and error artifacts are in the review checkout's ignored `tmp/`. Bootstrap reports `moss-0.1`. This report is review evidence only; it contains no candidate implementation change.
