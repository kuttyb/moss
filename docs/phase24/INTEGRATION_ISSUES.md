# Phase 24 integration issues

This ledger tracks open acceptance work without erasing historical Team review
records. Package-specific repairs belong on the owning Team branch; Team I
confirms them after merge.

## Current issues and dispositions

### P24-T-001 — benchmark providers are not release-optimized

- Owner: Team T
- Severity: high (methodology)
- Status: resolved by Team T performance merge `f4c4ad3d77ddea8e2fa2060a9368c31ad45f7ed4`
- Reproducer: run `../../margo bench --json` in
  `sdk/text-encoding-acceptance`, then inspect the selected `sdk/text` and
  `sdk/encoding` `.mossbuild` metadata. Providers report `profile = debug` and
  `opt-level = 0` while the benchmark harness is release `-O`.
- Impact: `docs/PHASE_24_TEXT_ENCODING_PERFORMANCE.md` must not describe these
  in-process measurements as release-provider performance.
- Evidence: `python3 sdk/text-encoding-acceptance/validate_release_benchmarks.py`
  passed on the combined tree, reporting `profile = release`, `-O`, and exact
  SHA-256 equality for all six benchmark-linked provider artifacts.

### P24-T-002 — Moss and Rust Text benchmark workloads differ

- Owner: Team T
- Severity: high (methodology)
- Status: resolved by Team T performance merge `f4c4ad3d77ddea8e2fa2060a9368c31ad45f7ed4`
- Reproducer: compare
  `sdk/text-encoding-acceptance/benches/text_encoding.moss` with
  `sdk/text-encoding-acceptance/bench_baseline.rs`. The find inputs are 240
  versus 256 code points, and slice inputs are 222 versus 256 code points.
- Impact: existing Moss/Rust ratios are not comparable and must not be
  published as validated performance results.
- Evidence: the release validator now checks matched operation inputs and timed
  boundaries; the corrected case-builder and text benchmark suites pass.

### P24-T-003 — UTF-8 tests identify variants but do not assert payload values

- Owner: Team T
- Severity: medium
- Status: resolved by Team T performance merge `f4c4ad3d77ddea8e2fa2060a9368c31ad45f7ed4`
- Reproducer: inspect `error_kind` and `encode_error_kind` in
  `sdk/encoding/tests/encoding.moss`; every payload is multiplied by zero.
- Impact: wrong `index`, `value`, `expected_bytes`, `available_bytes`, or
  `scalar` payloads can pass the current package tests.
- Evidence: Text 12/12 and Encoding 5/5 package tests, the cross-package suite,
  and the 66-valid/15-invalid three-mode UTF-8 corpus pass with payload cases.

### P24-T-004 — Text allocation and repeated-search concerns are unmeasured

- Owner: Team T
- Severity: medium
- Status: accepted documented limitation
- Reproducer: inspect `sdk/text/src/text.moss` together with the static-cost and
  benchmark limitations documented in
  `docs/PHASE_24_TEXT_ENCODING_ARCHITECTURE.md`.
- Impact: API correctness is accepted, but allocation and repeated-search
  efficiency are not established.
- Evidence: repeated-search and casing workloads now have release benchmarks and
  observable outputs; Moss allocation counts remain unmeasured, so no allocation
  comparison is claimed. See `docs/PHASE_24_TEXT_ENCODING_STRING_BUILDER_ARCHITECT.md`.

### P24-M-001 — public `DateTime` construction bypasses validation

- Owner: Team M
- Severity: high
- Status: resolved by Team M final delivery, verified in integration
- Reproducer: `packages/time/benches/time_bench.moss` directly constructs
  `time.DateTime(...)`; consumers can likewise construct month/day/time fields
  that `time.make_datetime(...)` would reject.
- Impact: invalid public records can reach conversion and formatting APIs
  without `TimeError.InvalidDate`.
- Evidence: `packages/time/tests/time_test.moss` direct-construction tests and
  `python3 tests/oracles/verify_repairs.py` pass.

### P24-M-002 — extreme calendar conversion arithmetic is not fully bounded

- Owner: Team M
- Severity: high
- Status: resolved by Team M final delivery, verified in integration
- Reproducer: inspect intermediate operations such as `days + 719468`,
  `era * 146097`, and `5 * doy` in `packages/time/src/main.moss`; exercise
  `timestamp_to_datetime` and `datetime_to_timestamp` near Int64 extremes.
- Impact: saturation at the final millisecond calculation does not prove that
  all earlier calendar intermediates avoid wraparound.
- Evidence: extreme-year and saturation tests plus the portable adversarial oracle
  pass without falsifications.

### P24-M-003 — `random.uniform` silently accepts inverted bounds

- Owner: Team M
- Severity: medium
- Status: resolved by Team M final delivery, verified in integration
- Reproducer: call `random.uniform(rng, 10.0, 5.0)` against
  `packages/random/src/main.moss`; the function returns `10.0` because
  `low >= high` returns `low`.
- Impact: an invalid range is silently converted to a deterministic value
  instead of using a typed error contract.
- Evidence: Random 8/8 tests and the portable adversarial oracle verify
  `RandomError.InvalidRange` for inverted bounds.

### P24-M-004 — rounding signed-zero edge behavior needs confirmation

- Owner: Team M
- Severity: medium
- Status: resolved by Team M final delivery, verified in integration
- Reproducer: add bit/sign-sensitive checks for `math.round(-0.0)` and negative
  values that round to zero around `packages/math/src/main.moss:170`; current
  tests assert numeric equality but not the zero sign.
- Impact: ordinary numerical tests may miss IEEE-754 signed-zero regressions.
- Evidence: Math 18/18 tests and the portable adversarial oracle verify signed-zero
  rounding behavior.

### P24-M-005 — numerical adversarial runners are nonportable and mutate source

- Owner: Team M
- Severity: medium (integration reproducibility)
- Status: resolved by Team M final delivery, verified in integration
- Reproducer: inspect the `REPO_ROOT` declarations in
  `tests/oracles/test_{math,random,time}_falsification.py` and their writes to
  `tests/oracles/harness/src/main.moss`. `verify_repairs.py` is path-portable
  but writes the same tracked harness.
- Impact: the corpus cannot be run concurrently or counted as a reproducibly
  clean gate. The portable aggregate runner passed functionally only in an
  isolated disposable worktree.
- Evidence: all harness writes now target `tmp/adversarial_oracle_harness`; the
  runner passed with zero falsifications and no tracked-tree mutation.

### P24-M-006 — benchmark bodies permit constant folding or discard results

- Owner: Team M
- Severity: high (methodology)
- Status: resolved by Team M final delivery and Team N benchmark verification
- Reproducer: build `packages/{math,random,time}` benchmarks in release mode
  and inspect generated/disassembled bodies. Several Math/Time operations
  collapse to constants; Random next-int/next-float results are not observed.
- Impact: the reported 1–2 ns rows and unobserved RNG measurements are not SDK
  operation timings and are excluded from integration performance claims.
- Evidence: corrected 1,000-iteration release benchmark bodies observe complete
  outputs; `margo bench --release --json` runs for linalg, sparse, and statistics,
  and the tracked Rust baseline runs with `rustc -O` and full-result `black_box`.

### P24-D-001 — Unicode scalar-to-String runtime primitive missing for JSON \uXXXX synthesis

- Owner: Team D / Team I
- Severity: high (language/runtime capability)
- Status: submitted to Team I
- Reproducer: inspect `docs/phase24/TEAM_I_ESCALATION_UNICODE_SCALAR.md`. Moss v0.1 has no `string_from_scalar(Int)` or `string_from_utf8(Vector[Int])` primitive.
- Impact: JSON `\uXXXX` and surrogate pairs are validated and decoded to integer scalars and ASCII strings; full non-ASCII string synthesis is deferred pending Team I runtime hook, with no false compliance claimed.

### P24-D-002 — Compiler internal error when bench block lowers functions with Phase 21 typed raise effects

- Owner: Team D / Team I
- Severity: medium (tooling limitation)
- Status: submitted to Team I
- Reproducer: invoking any function with a declared/inferred `raise` effect (such as `json.parse` or `csv.read_all`) inside a `bench "..."` block causes the compiler to throw `internal error: checked raising function call lacks a Phase 21 outcome context` in `src/moss.cpp:17050` (`benchmark_body_` path lacks `phase21_result_context_`).
- Impact: Microbenchmarks for raising functions are measured via executable standalone release timing harnesses (`measure_benchmarks.py`) rather than in-tree `margo bench`. Per AGENTS.md rule, compiler internals (`src/moss.cpp`) were not modified.

### P24-D-003 — JSON nested-container child ranges interleave incorrectly

- Owner: Team D
- Severity: high (correctness)
- Status: confirmed open; `moss.json` RED for final acceptance
- Reproducer: on the combined tree, parse `[[1],2]` and
  `{"a":{"x":1},"b":2}` and inspect child IDs, object keys, lookups, and
  compact serialization. The first outer array element reports child `1` for
  both descendants and serializes as `[[1],1]`; the nested object lookup loses
  `b` and the interpreter reports `vector index out of bounds`.
- Evidence: `tmp/json-nested-probe/src/main.moss`, `../../margo debug`, output
  `2, 1, 1, 1, [[1],1], 2, a, x, 1` followed by the bounds error.
- Impact: parsing can succeed while semantic child traversal, lookup, and
  round-trip serialization return incorrect values or fail. Existing validity
  and differential tests do not cover interleaved descendant ranges.
- Required disposition: Team D must repair the child-range invariant, add
  semantic nested-container tests, and submit a new exact-SHA independent
  Architect review. Team I will not patch the JSON implementation locally.

### P24-H-001 — Host runtime packages remain primitive-gated

- Owner: Team H / runtime maintainers
- Severity: accepted documented limitation
- Status: pending runtime capability work
- Scope: Team H intentionally delivers lexical `moss.path` and bounded regular
  file `moss.fs`; `moss.io`, `moss.env`, `moss.process`, metadata/mutation, and
  general byte writes remain deferred.
- Evidence: `python3 sdk/host/io/check_feasibility.py` passes the supported
  path-only provider and reproduces the expected cross-package FileIO capability
  boundary blocker.

## Resolved during integration

### P24-I-003 — Team D acceptance runners used a stale absolute checkout path

- Owner: Team I
- Severity: low (reproducibility)
- Status: resolved by path-relative runner fix
- Observation: `sdk/data-acceptance/run_all.py` and its compiled-only helper
  hard-coded `/home/owner/daji/code/moss3`, so a fresh clone could not execute
  the documented acceptance command.
- Resolution: derive the repository root from each script's location; the
  combined-tree package, source-free, and differential gates now run in place.

### P24-I-002 — shared Margo build artifacts require serial package validation

- Owner: Team I
- Severity: low (workflow)
- Status: resolved by serial rerun
- Observation: parallel package test commands sharing the `math` dependency
  occasionally raced on Margo/Rust object files and produced missing-object
  linker errors.
- Resolution: rerunning package commands serially passed every affected suite;
  no source or compiler defect was reproduced.

### P24-I-001 — Math branch based on an older integration snapshot

- Owner: Team I
- Severity: high during merge
- Status: resolved by merge `d945b252aeaf060e551c4c446c91edd113c86463`
- Reproducer: compare `610f2d63..1bdb548a` before merging; the branch-relative
  diff appears to delete Foundations and feedback assets.
- Resolution: normal three-way merge preserved those assets. Team I verified
  their files and gates after the merge; the only content conflict was the
  additive `.codex/CURRENT_STATUS.md` history.
