# Phase 27.5 Fast Debug fixes and fail-closed guards — in progress

## D1 — duplicate Fast Debug `For`

- Removed the first duplicate `For` branch and retained one handler with block scoping, per-iteration `LocalWrite`, and native-style range termination. Added runtime support for checked custom Iterator sources using their checked `iter()`/`next()` methods.
- Targeted fixture `tests/tooling/fixtures/phase27_5_for.moss` covers range, Vector, Map key snapshots, and a custom Iterator. `moss check` accepts the fixture. Native Rust compiles and runs. Fast Debug output and traced Fast Debug output match native output (`1`, `2`, `4`, `5`, `1`); trace includes `LocalWrite` for `item`.
- Direct Queue iteration is not accepted by the checker at this base (the probe reports `TYPE_INFERENCE_FAILED`); no new Queue iteration semantics were added. The remaining interpreter handler's Queue branch is unreachable for checked programs and is retained pending broader compatibility checks.
- `python3 tests/tooling/check_phase27_5_for_match.py ./moss` passed. Full suites remain pending.

## D2 — match payload Copy classification

- Both interpreter and emitter now use `copy_type_name()` for non-consuming payloads. The fixture covers Char, `option[Int]`, `option[Char]`, and String control payloads. Fast Debug and native output match; generated Rust compiles; the three Copy cases lower as dereferences and String is not cloned.
- `python3 tests/tooling/check_phase27_5_match_copy.py ./moss` passed. Registered separately in `tests/run_parallel.py` for off/shadow `make check`. D2 commit/push is pending.

## Guards G1–G4

- Not started. No full suite or combined guard measurement has been run.
