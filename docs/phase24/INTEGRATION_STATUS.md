# Phase 24 SDK integration status

## Checkpoint

- State: **AMBER**
- Integrated implementation SHA: `8ca9c529bc9ebd0a4f41eef8d8938220265cf03d`
- Branch: `phase24-integration`
- Integration date: 2026-10-12
- Whole-system Architect review: deferred to the final Phase 24 acceptance milestone by coordinator direction

This is the consolidated Phase 24 SDK checkpoint. Accepted Team D structured
data, Team T performance, and Team N numerical deliveries are integrated; the
remaining limitations and pending Teams are explicitly recorded in
[INTEGRATION_ISSUES.md](INTEGRATION_ISSUES.md).

## Integrated sources

| Component | Source branch | Source SHA | Delivered scope | Integration state |
| --- | --- | --- | --- | --- |
| Foundations | `phase24-core` | `14a4b835e0d493562d0dfaa9d16768b1b0ad38b5` | `moss.core`, `moss.bytes`, `moss.collections`, `moss.algorithms` | Already present; preserved |
| Feedback infrastructure | `phase24-agent-feedback-bootstrap` | `b647586b463758c0dc7cc43db4f1e6280d274ef5` | Policy, schema, validator, workflow hooks | Already present; preserved |
| Text and Encoding | `phase24-text` | `4a9b9be12fd230dcbd0b176d539937c29248d2f4` | `moss.text`, `moss.encoding`, acceptance and benchmark assets | Final updates merged by `ee4973f5556b3cd07204e877157e35fd49ad1874` |
| Math, Random, and Time | `phase24-math` | `dd24df3bc653cd9d5fa93af9f5f25a9d9c3ff9bf` | `moss.math`, `moss.random`, `moss.time`, Team M integration/oracles | Final updates merged by `a2864a55414fd3f21619a500fcda4c37f91c4467` |
| Text performance | `phase24-text-perf` | `0d8d1b6cf75ecf72f90be5bab9fc821c3219f584` (reviewed implementation `63a7089bdad2f7739c9b234669657dc1a43ae981`) | String search/slice/case performance, capacity builders, ownership regressions, release validators | Merged by `f4c4ad3d77ddea8e2fa2060a9368c31ad45f7ed4` |
| Numerics | `phase24-numerics` | `234ff9e2e3a1e03de64be4276f2aef64ba8c6d73` | `moss.linalg`, `moss.sparse`, `moss.statistics`, integration, oracles, release benchmarks | Merged by `bea649a42bd39c87035ba3a1baca603311ca8e05` after unconditional Architect GO |

| Structured Data | `phase24-data` | `8b4b2cff7ce062992cdbfdfbf57ab407093d904e` (Architect GO at `e8543918c249fd90b20f6eabbc9769d969b8e8ae`) | `moss.parse`, `moss.fmt`, `moss.json`, `moss.csv`, acceptance/differential suites | Merged by `04b7683b6b6c0900e3079be60116514ba3d41564`; known D-001/D-002 limitations retained |

The normal merge commits are `a2864a55414fd3f21619a500fcda4c37f91c4467` (Team M),
`ee4973f5556b3cd07204e877157e35fd49ad1874` (Team T),
`f4c4ad3d77ddea8e2fa2060a9368c31ad45f7ed4` (Team T performance), and
`bea649a42bd39c87035ba3a1baca603311ca8e05` (Team N), and
`04b7683b6b6c0900e3079be60116514ba3d41564` (Team D). No merge into `main`
was performed.

## Merge and compiler reconciliation

The original Text and Math merges are preserved. Team T performance had one
`.codex/CURRENT_STATUS.md` conflict, resolved by retaining the integration
history; `src/moss.cpp` merged automatically with the existing numeric lowering,
imported-generic support, and Team T String ownership changes intact. Team N then
merged cleanly; its compiler delta adds `moss test --release` support without
replacing the combined compiler. Team D merged conflict-free and made no
compiler or numerical edits; its Architect GO and two documented limitations
are retained in the issue ledger.

The integrated compiler retains the Foundations traversal/lowering support for
imported generic calls, nested expressions, record-field projections, and indexed
vector literals; Team M mixed Float/Int lowering; Team T String search/slice/case,
capacity, ownership, and nested-alias checks; and Team N release-test support.
Focused source-backed, Fast Debug, native, and source-free ABI-v9 gates passed.

## Executed validation

All commands below were executed against the final merged tree at `8ca9c52`.

| Command or suite | Result |
| --- | --- |
| `python3 sdk/text-encoding-acceptance/run_all.py --full` | PASS: Text 7/7, Encoding 4/4, cross-package 3/3; native debug/release, Fast Debug, compiled-only providers, and Python corpus (66 valid, 15 invalid) |
| Team T performance package/ownership gates | PASS: Text 12/12, Encoding 5/5, cross-package 3/3; String concat, source-free nested alias, and release-provider checks |
| Foundations package tests, native and Fast Debug | PASS: 15/15 in each mode |
| `python3 sdk/foundations/acceptance/run_all.py --full` | PASS: O0/release/Fast Debug, negative contracts, 44 sort/search plus 8 byte oracle cases, source-free ABI-v9 |
| Team M package tests | PASS: Math 18/18, Random 8/8, Time 11/11, integration 2/2 |
| `python3 tests/tooling/check_phase24_imported_generic_expressions.py ./moss` | PASS: source-backed and source-free |
| `python3 tests/tooling/check_phase24_adversarial_architect.py` | PASS: inherited Foundations compiler regression gate |
| `TMPDIR="$PWD/tmp" make check` | PASS: all Moss v0.1 tests, including 54/54 Executor tests |
| `make examples` | PASS: all positive examples; intentional negative example skipped by the target |
| `../../margo test` in `packages/math`, `sdk/text`, and `sdk/encoding` | PASS: Math 18/18, Text 7/7, Encoding 4/4 |
| `../../margo test` in `packages/random`, `packages/time`, and `packages/integration` | PASS: Random 8/8, Time 11/11, integration 2/2 |
| Team N package tests | PASS: linalg 9/9, sparse 9/9, statistics 9/9, numerics integration 3/3 in native debug, Fast Debug, and release |
| Team D package tests | PASS: parse 5/5, fmt 5/5, json 9/9, csv 6/6, each native and Fast Debug; data acceptance 11/11 in both modes |
| `python3 sdk/data-acceptance/tests/check_source_free.py` | PASS: compiled-only `moss.json`/`moss.csv` ABI-v9 provider check |
| `python3 tests/differential/run_differential_tests.py` | PASS: 46/46 JSON/CSV differential cases against Python stdlib |
| `python3 tests/oracles/test_numerics_falsification.py` | PASS: zero differential falsifications |
| `python3 tests/tooling/check_numerics_abi_v9.py` | PASS: compiled-only ABI-v9 consumer, 100% parity |
| `python3 tests/oracles/verify_repairs.py` | PASS: zero falsifications; harness confined to `tmp/adversarial_oracle_harness` |
| `python3 sdk/text-encoding-acceptance/validate_release_benchmarks.py` | PASS: release profile, `-O`, six provider hashes, matched workloads |
| `margo bench --release --json` in linalg/sparse/statistics | PASS: 31 samples, five warmups, observable outputs; no cross-language ratio claimed here |
| `rustc --edition=2021 -O tests/benchmarks/bench_numerics_baseline.rs` and run | PASS under documented `-O` methodology; emits a non-blocking default dead-code warning for unused Csc fields |
| `g++ -std=c++17 -O2 -Wall -Wextra -Werror -pedantic -fsyntax-only src/moss.cpp` | PASS |
| `../../margo run`, `../../margo run --release`, `../../margo debug` in `packages/integration` | PASS with identical Math/Random/Time output |
| `python3 tests/oracles/verify_repairs.py` in an isolated disposable worktree | Functional PASS with zero falsifications; not counted as a reproducibly clean gate because it overwrote the tracked oracle harness |
| Team T and Team N benchmark comparisons | Team T release-provider/workload methodology validated; Team N corrected Rust baselines and observed outputs validated. Reported ratios remain workload-specific, not a universal performance claim. |

Historical hardcoded Team M oracle scripts remain documented in the issue ledger;
the current aggregate runner and Team N oracle runners use disposable harnesses
under `tmp/` and do not mutate tracked source.

## Review and issue disposition

Team T's two historical performance Architect NO-GOs and subsequent GO, Team M's
historical findings, and Team N's conditional review followed by unconditional
GO are preserved. This cycle inspected merge conflicts, shared compiler changes,
package suites, source-free checks, differential oracles, and benchmark
methodology. No integration-blocking compiler, ABI, or cross-package defect was
found.

The independent whole-system Phase 24 Architect review is intentionally pending
until final acceptance. Team-specific follow-ups remain owned by their source
Teams; Team I owns confirmation on future integration commits.

## Remaining known limitations

- Moss allocation counts for the Team T builder workloads remain unmeasured; only
  Rust allocation instrumentation and matched timing workloads are documented.
- Team N's Rust baseline emits a default `dead_code` warning for unused Csc fields
  under plain `rustc -O`; the documented benchmark gate does not use `-D warnings`.
- Team M's real-clock `moss.time` `.now()` functionality remains explicitly
  deferred pending intrinsic approval.
- Layer B String/UTF-8 interoperability remains deferred because Moss v0.1 has no
  supported String/UTF-8 byte conversion or scalar String constructor.
- Team D P24-D-001 remains open: non-ASCII JSON `\\uXXXX` synthesis requires a
  scalar-to-String runtime primitive; validation reports the typed limitation.
- Team D P24-D-002 remains open as a tooling limitation: raising functions cannot
  currently be invoked inside in-tree `bench` blocks, so timing uses standalone
  release harnesses.
- The independent whole-system Architect review remains pending for final Phase 24
  acceptance; these package and repository gates do not substitute for that review.

## Next actions

1. Keep Team T allocation evidence explicitly scoped; do not publish Moss
   allocation ratios without direct measurement.
2. Keep Team N benchmark comparisons tied to their corrected workloads and Rust
   `-O` baseline; do not generalize across packages.
3. Team I fixed the data acceptance runners' hard-coded checkout path; retain
   the path-relative form for reproducible clones.
4. Conduct the independent whole-system Architect review on the final Phase 24
   acceptance SHA.
