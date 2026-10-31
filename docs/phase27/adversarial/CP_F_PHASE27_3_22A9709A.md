## L7 adjudication follow-up — CP-F verdict: GO

L7 resolved the original coverage notes as follows. The single paired 600-second Fast Debug check for `examples/functional_pipeline.moss` timed out on both the exact pinned phase27-1 compiler and candidate, each with empty stdout/stderr/trace. Record this as **BOTH-TIMEOUT** and exclude the fixture from output comparison; it is not a blocker. Machine-readable result: `tests/phase27/adversarial/cp-f-phase27-3-22a9709a/fastdebug_pipeline_600s_both_timeout.json`.

Reach acceptance compares Phase 27.3 against Phase 27.2's own off-vs-shadow result: 160 existing failure rows versus 161 candidate rows. All pre-existing rows match after removing the one new standalone source-free diagnostic-code delta; no reach edges, checked declarations, or trace-completeness fields changed. CP-E already adjudicated that standalone-only diagnostic delta BENIGN, and both package-context checks pass. Evidence and checksums: `tests/phase27/adversarial/cp-f-phase27-3-22a9709a/reach_l7_adjudication.json`.

No source change was required. Per L7's ruling, the adjudicated **CP-F verdict: GO**. The original reviewer NO-GO reflected the two now-resolved coverage notes above.

---

# CP-F review: Phase 27.3 candidate `22a9709a`

## Verdict

**CP-F verdict: NO-GO**

No candidate-specific semantic regression was established in the completed slices. The pinned reach differential gate failed because the required historical compiler hash could not be reproduced, and the required Fast Debug parity set has a coverage gap. Neither result is waived.

## Revisions and method

- Candidate: `22a9709a76539df7dc35c0d20e22582fc82f03a6`.
- Baseline: peeled `phase27-2-m1` commit `04be1a1bbbc7a4fd8fe2e4090b4b5bad5dd573e4`; annotated tag object `d4446b7250f651e60e978740661e6fb20ff6ef10`.
- Isolated candidate and baseline worktrees were used. No compiler-performance run was started.
- The first corpus invocation omitted the `check` subcommand and generated untracked `.rs`/`.mossmap` files beside sources. That run was invalidated. The cleanup verified all 2,664 paths came from its captured command output, were contained in the isolated candidate worktree and untracked, and removed them. The corrected harness used `check`; the failed invocation and cleanup evidence are retained in this directory.
- Adversarial-named path components were filtered before corpus traversal.

## Differential findings

### Standalone diagnostics and Rust emission

The corrected standalone differential checked all 2,058 candidate-tree `.moss` sources against baseline and candidate diagnostics in off/shadow modes. Diagnostic signatures matched for 2,057 sources. The only mismatch was `sdk/advanced-data-acceptance/fixtures/source_free_consumer.moss`, run outside its provider-interface context: baseline reports `RECOVERY_PATTERN_REQUIRES_VARIANT`; candidate reports `UNKNOWN_ERROR_TYPE` for `ndarray.NdArrayError`. This is **BENIGN counted**: the actual compiled-only Margo source-free package gate passed on both revisions in both off and shadow modes (ABI-9 debug, five Team A modules, fourteen provider interfaces). The contextless standalone result is not treated as package parity evidence.

For each of default, `-O0`, and optimized native emission, 1,332 sources emitted successfully in both revisions. All 1,332 baseline/candidate raw Rust hashes and all 1,332 normalized Rust hashes matched for each profile. The remaining eleven common emitter failures are **BENIGN counted**, with no candidate delta:

- Six `examples/swarm/modules/calc_interpreter/repro/natural_full/tests/*.moss` test sources (`calc_test`, `env_test`, `eval_test`, `lexer_test`, `report_test`, `token_test`) fail identically with cannot infer `assertEqual` operand types at `parser_test.moss:15`; the package test reports a missing `report` provider.
- Five `examples/swarm/modules/text_toolkit/toolkit/{src,tests}/*.moss` sources (`src/inventory.moss`, `src/table.moss`, `tests/inventory_test.moss`, `tests/stats_test.moss`, `tests/table_test.moss`) fail identically with `UNSUPPORTED_STRING_ORDERING` at `src/strings.moss:106`; the package test reports the same error.

The compact adjudication is in `tests/phase27/adversarial/cp-f-phase27-3-22a9709a/corpus_adjudication.json`; full per-source outcomes and emitted-Rust hashes are in `corpus_differential.json`. The standalone corpus gives Rust-emission parity only for the 1,332 successful sources; it does not imply successful emission for all 2,058.

### Package tests, interfaces, and probes

- `check_phase27_accepts_rustc.py` filtered gate passed both revisions: 236 accepted standalone roots, native Margo test passed, six active seeds.
- `check_source_free.py --abi v9 --profile debug` passed baseline and candidate in both off and shadow modes: five Team A modules and fourteen current source-backed provider interfaces. The `.mossi` EST off/shadow checks passed; normalized library/app interface bytes matched after the expected package fingerprint normalization.
- The four malformed-expression migrations (`unsupported_exponent`, `unsupported_operators`, `membership_operator`, `symbolic_boolean`) retained the same diagnostics on baseline/candidate and off/shadow. The package `2 ** 3` probe also retained its `invalid arithmetic expression` diagnostic. No migration bug found.
- The integer-overflow regression compiled and ran in baseline/candidate `-O0` and optimized native modes, and generated Rust with `rustc -O` plus overflow checks. All nine output lines matched wrapping expectations; generated-Rust and stdout checksums are retained.
- String reader zero gate passed for checker, emitter, reach walker, and Fast Debug reader sites.

### Fast Debug

The accepted `tests/phase27/conformance/three_engine.moss` Fast Debug probe passed on baseline and candidate with identical output (`999999` three times, then `9`). The actual invocation for `examples/functional_pipeline.moss` was `moss debug examples/functional_pipeline.moss --trace`; both revisions ran CPU-bound for 60 seconds and timed out with empty stdout/stderr/trace. The same source without `--trace` also timed out on both. Native/Rust execution on both revisions produces `81`, providing an independent oracle. This paired timeout is **BENIGN counted** as a pre-existing Fast Debug limitation, not a semantic difference. Fast Debug parity coverage for this source is unavailable, so the requested Fast Debug parity set remains incomplete.

## Sanitizers

Separate Clang 18.1.3 ASan/UBSan compiler builds scanned the baseline corpus (2,046 sources), candidate corpus (2,058), and candidate corpus using the baseline compiler (2,058). All three scans completed with zero findings. The machine-readable results record sanitizer names, revision, compiler hash, source count, and empty findings.

## Reach differential gate: FAIL

The exact gate is `tests/tooling/check_phase27_reach_differential.py`; it validates the SHA-256 of the passed legacy compiler binary. The accepted historical baseline pin is `4a9d7d7509f9883194f2d3f147c4ff66c39e9c832e758ddffdd7572d57667ffd`. Rebuilding peeled `phase27-1-m1` with the repository `make` default (same artifact as named `moss-dev`) under system Clang 18.1.3, x86_64, `-O0` produced SHA-256 `66750217b93bc777ed0d0a0dab4d8fc183e73ce16e7a55c1d3f19a9b936e2a82`; the `moss` path was this built compiler, not the launcher. The optimized artifact hash was `a1762b7e43d30f3fb0ec93ca0f3d30828a3b8baed7da18fc3852a52029618550`. The gate rejects the rebuilt default binary at its historical hash check. No expected-hash override was used; reach differential evidence is therefore unavailable and the pinned gate remains FAIL.

## Classification summary

- **MIGRATION BUG:** none found.
- **LEGACY MISPARSE:** none found.
- **SEMANTIC:** none adjudicated.
- **BENIGN counted:** paired package/emitter failures, the contextless source-free diagnostic difference (resolved by the actual package oracle), and paired Fast Debug timeout with native/Rust oracle.
- **UNEXPLAINED:** none among completed differentials.
- **Blocking evidence gaps:** pinned reach gate FAIL; Fast Debug parity coverage gap for `functional_pipeline.moss`.

The reach blocker and Fast Debug coverage gap make this a **NO-GO** despite no established candidate semantic regression.

## Evidence files and checksums

Raw probes, gate logs, corpus manifests, per-source results, timeout data, and sanitizer findings are retained under `tests/phase27/adversarial/cp-f-phase27-3-22a9709a/`. `SHA256SUMS` covers every raw file in that directory except itself. The invalid first corpus attempt is explicitly marked as method-correction evidence and excluded from results.
