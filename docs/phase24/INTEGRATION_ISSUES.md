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
- Status: resolved by Team D corrective delivery `6ef17f1c2edc1022f00f77a35bec051b1b9cdab3` (published tip `db5f6663e94f3711db3b55a611d13f1508f97368`), merged at `8b54293`
- Reproducer: on the earlier tree, parsing `[[1],2]` and
  `{"a":{"x":1},"b":2}` resulted in interleaved descendant corruption: the
  first outer array element reported child `1` for both descendants and
  serialized as `[[1],1]`; nested object lookup lost `b` and ended with
  interpreter vector bounds failure.
- Historical evidence: `tmp/json-nested-probe/src/main.moss`, `../../margo debug`, output
  `2, 1, 1, 1, [[1],1], 2, a, x, 1` followed by bounds error.
- Verification & Resolution: Team D re-architected the tape AST child indexing
  to use singly-linked entry nodes (`first_child`, `next_sibling`) preserving
  exact parent boundaries across interleaving. Verified on combined tree:
  `[[1],2]` parses and stringifies to `[[1],2]`; `{"a":{"x":1},"b":2}` preserves
  both keys and lookups; nested sibling containers `[[1,2],[3,4],5]` preserve
  order and identity; compact and pretty round-trips match; 16/16 unit tests
  pass in native and Fast Debug; 103/103 differential tests pass.

### P24-D-004 — Linked-entry JSON serialization repeatedly traverses children from head (O(n²))

- Owner: Team D
- Severity: medium (performance follow-up)
- Status: open / assigned to Team D
- Observation: In `sdk/json/src/json.moss`, `array_get` and `object_key`/`object_val`
  traverse the singly linked sibling list starting from `first_child` on each
  indexed lookup. Serializing or iterating an array or object with $n$ elements
  executes $\sum_{i=0}^{n-1} i = O(n^2)$ step traversals.
- Impact: Wide JSON arrays and objects exhibit quadratic serialization time.
- Assigned repair: Team D should implement direct iterative cursor/entry
  traversal or tail-cached traversal helpers for $O(n)$ linear serialization.

### P24-D-005 — Public JSON builder APIs permit construction of cyclic node graphs

- Owner: Team D
- Severity: medium (robustness / invariant follow-up)
- Status: open / assigned to Team D
- Observation: Public builder APIs (`array_push`, `object_set`) allow arbitrary
  `node_id` references to be linked as children without cycle detection.
  Constructing a cycle causes recursive serialization (`stringify`) or traversal
  loops.
- Impact: Unbounded recursion or infinite loops on cyclically linked builder nodes.
- Assigned repair: Team D should define and enforce an acyclic graph invariant
  or bounded cycle/depth rejection rule in builder mutation APIs.

### P24-D-006 — Differential oracle textual AST representation uses unescaped delimiters

- Owner: Team D
- Severity: low (test harness robustness follow-up)
- Status: open / assigned to Team D
- Observation: In `tests/differential/run_differential_tests.py`, the textual AST
  serialization uses unescaped semicolons, colons, and commas (e.g. `parts.push(key + "=" + ...)`).
  Strings containing these delimiter characters could cause AST parsing ambiguities
  in the differential harness. Furthermore, the test harness currently executes
  probes via `margo debug` (Fast Debug) rather than dual-engine (native and Fast Debug).
- Impact: Fragility for arbitrary string corpora containing delimiter characters;
  differential pass counts reflect Fast Debug execution only.
- Assigned repair: Team D should make the oracle AST protocol robust with
  properly escaped payload tokens, and extend the differential harness to execute
  and report independent native and Fast Debug differential passes.

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

### P24-A-001 — Team A test count arithmetic discrepancy

- Owner: Team A / Team I
- Severity: low (documentation / arithmetic discrepancy)
- Status: resolved by Team I editorial reconciliation
- Observation: Team A's final delivery reports undercounted declared tests due to
  an arithmetic transcription error, reporting 103 instead of the actual 111 unique
  tests across 5 packages, 333 across 3 modes.
- Resolution: Verified in-tree test declarations (`moss.ndarray`: 21, `moss.datastructures`: 23,
  `moss.tables`: 24, `moss.binary`: 9, `moss.cbor`: 34; sum = 111) and fresh runner output
  (111 passed per mode, 333 total). Reconciled in `docs/phase24/TEAM_A_ADVANCED_DATA_DELIVERY.md`,
  `docs/phase24/agent-patterns/team-a-advanced-data.md`, `.codex/CURRENT_STATUS.md`, and
  `docs/phase24/INTEGRATION_STATUS.md`. Preserved historical 101-test baseline and 10 CBOR additions.

### P24-R-001 — Team R Architect report implementation SHA transcription discrepancy

- Owner: Team R / Team I
- Severity: medium (provenance integrity)
- Status: resolved by Team R Section 7 erratum in integration
- Observation: Team R's authored report transcribed implementation SHA
  `4068976264f3312c5bfad3e680a6b12fcf45ff3b`, which does not exist in git history.
- Resolution: Verified actual Git implementation commit implementing the NFA split-origin
  correction and runtime-scaling verification is `4068976a9a4d1bb046b69fbc35708d8b51f831e4`,
  an exact ancestor of delivery HEAD `9794122db5dc2aade42cc259879dffb16200f54b`. Recorded
  formal Section 7 erratum in `docs/team_r/ARCHITECT_FINAL_ACCEPTANCE.md` and updated
  `docs/phase24/agent-patterns/team-r-data-utils.md` without altering the Architect's authored text.
