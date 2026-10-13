# Phase 24 integration issues

This ledger tracks open acceptance work without erasing historical Team review
records. Package-specific repairs belong on the owning Team branch; Team I
confirms them after merge.

## Open issues

### P24-T-001 — benchmark providers are not release-optimized

- Owner: Team T
- Severity: high (methodology)
- Status: confirmed open
- Reproducer: run `../../margo bench --json` in
  `sdk/text-encoding-acceptance`, then inspect the selected `sdk/text` and
  `sdk/encoding` `.mossbuild` metadata. Providers report `profile = debug` and
  `opt-level = 0` while the benchmark harness is release `-O`.
- Impact: `docs/PHASE_24_TEXT_ENCODING_PERFORMANCE.md` must not describe these
  in-process measurements as release-provider performance.

### P24-T-002 — Moss and Rust Text benchmark workloads differ

- Owner: Team T
- Severity: high (methodology)
- Status: confirmed open
- Reproducer: compare
  `sdk/text-encoding-acceptance/benches/text_encoding.moss` with
  `sdk/text-encoding-acceptance/bench_baseline.rs`. The find inputs are 240
  versus 256 code points, and slice inputs are 222 versus 256 code points.
- Impact: existing Moss/Rust ratios are not comparable and must not be
  published as validated performance results.

### P24-T-003 — UTF-8 tests identify variants but do not assert payload values

- Owner: Team T
- Severity: medium
- Status: confirmed coverage gap
- Reproducer: inspect `error_kind` and `encode_error_kind` in
  `sdk/encoding/tests/encoding.moss`; every payload is multiplied by zero.
- Impact: wrong `index`, `value`, `expected_bytes`, `available_bytes`, or
  `scalar` payloads can pass the current package tests.

### P24-T-004 — Text allocation and repeated-search concerns are unmeasured

- Owner: Team T
- Severity: medium
- Status: unproven concern
- Reproducer: inspect `sdk/text/src/text.moss` together with the static-cost and
  benchmark limitations documented in
  `docs/PHASE_24_TEXT_ENCODING_ARCHITECTURE.md`.
- Impact: API correctness is accepted, but allocation and repeated-search
  efficiency are not established.

### P24-M-001 — public `DateTime` construction bypasses validation

- Owner: Team M
- Severity: high
- Status: confirmed open
- Reproducer: `packages/time/benches/time_bench.moss` directly constructs
  `time.DateTime(...)`; consumers can likewise construct month/day/time fields
  that `time.make_datetime(...)` would reject.
- Impact: invalid public records can reach conversion and formatting APIs
  without `TimeError.InvalidDate`.

### P24-M-002 — extreme calendar conversion arithmetic is not fully bounded

- Owner: Team M
- Severity: high
- Status: unproven boundary concern
- Reproducer: inspect intermediate operations such as `days + 719468`,
  `era * 146097`, and `5 * doy` in `packages/time/src/main.moss`; exercise
  `timestamp_to_datetime` and `datetime_to_timestamp` near Int64 extremes.
- Impact: saturation at the final millisecond calculation does not prove that
  all earlier calendar intermediates avoid wraparound.

### P24-M-003 — `random.uniform` silently accepts inverted bounds

- Owner: Team M
- Severity: medium
- Status: confirmed open
- Reproducer: call `random.uniform(rng, 10.0, 5.0)` against
  `packages/random/src/main.moss`; the function returns `10.0` because
  `low >= high` returns `low`.
- Impact: an invalid range is silently converted to a deterministic value
  instead of using a typed error contract.

### P24-M-004 — rounding signed-zero edge behavior needs confirmation

- Owner: Team M
- Severity: medium
- Status: unproven concern
- Reproducer: add bit/sign-sensitive checks for `math.round(-0.0)` and negative
  values that round to zero around `packages/math/src/main.moss:170`; current
  tests assert numeric equality but not the zero sign.
- Impact: ordinary numerical tests may miss IEEE-754 signed-zero regressions.

### P24-M-005 — numerical adversarial runners are nonportable and mutate source

- Owner: Team M
- Severity: medium (integration reproducibility)
- Status: confirmed open
- Reproducer: inspect the `REPO_ROOT` declarations in
  `tests/oracles/test_{math,random,time}_falsification.py` and their writes to
  `tests/oracles/harness/src/main.moss`. `verify_repairs.py` is path-portable
  but writes the same tracked harness.
- Impact: the corpus cannot be run concurrently or counted as a reproducibly
  clean gate. The portable aggregate runner passed functionally only in an
  isolated disposable worktree.

### P24-M-006 — benchmark bodies permit constant folding or discard results

- Owner: Team M
- Severity: high (methodology)
- Status: confirmed open
- Reproducer: build `packages/{math,random,time}` benchmarks in release mode
  and inspect generated/disassembled bodies. Several Math/Time operations
  collapse to constants; Random next-int/next-float results are not observed.
- Impact: the reported 1–2 ns rows and unobserved RNG measurements are not SDK
  operation timings and are excluded from integration performance claims.

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

## Resolved during integration

### P24-I-001 — Math branch based on an older integration snapshot

- Owner: Team I
- Severity: high during merge
- Status: resolved by merge `d945b252aeaf060e551c4c446c91edd113c86463`
- Reproducer: compare `610f2d63..1bdb548a` before merging; the branch-relative
  diff appears to delete Foundations and feedback assets.
- Resolution: normal three-way merge preserved those assets. Team I verified
  their files and gates after the merge; the only content conflict was the
  additive `.codex/CURRENT_STATUS.md` history.
