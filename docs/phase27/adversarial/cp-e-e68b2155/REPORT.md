# CP-E full blind review — candidate `e68b2155`

NO-GO

## Scope and verdict

Reviewed candidate source `e68b21557e675cc4697c391a083ead5add7ad76a` against baseline `04be1a1bbbc7a4fd8fe2e4090b4b5bad5dd573e4`. The review checkout is `review/cp-e-e68b2155`; no files under `src/` were modified. Repository bootstrap returned `language_version: moss-0.1`.

The candidate preserves accepted/rejected verdicts over the complete frozen Moss source corpus, but changes the diagnostic code and message for one invalid recovery selector, duplicated in two tracked fixtures. Since this migration must preserve checker behavior, this observable diagnostic change remains a defect and the verdict is NO-GO.

At line 57 of both identical source files, `recover ndarray.NdArrayError.InvalidAxis:` is rejected by baseline as `RECOVERY_PATTERN_REQUIRES_VARIANT` (“recovery patterns must name a qualified enum variant”) and by candidate as `UNKNOWN_ERROR_TYPE` (“unknown error enum 'ndarray.NdArrayError'”). Both modes reject the source; this is diagnostic-only, not a verdict delta. The two files have identical SHA-256 `a00e3da194b0068677fd7840d3b9f86343af2cff1803d464c410f92c5036cfad`:

- `tests/phase27/adversarial/cp-e-phase27-3-22a9709a/source_free_consumer.moss`
- `sdk/advanced-data-acceptance/fixtures/source_free_consumer.moss`

The focused package oracle used the resolved `sdk/advanced-data-acceptance` dependency graph and reproduced the same baseline/candidate diagnostic in both `MOSS_EST=off` and `MOSS_EST=shadow`. The rejection remains a rejection, but diagnostic precedence changed. The candidate EST recovery-pattern path recognizes the qualified pattern before enum identity validation; this is the likely source of the diagnostic shift.

## Reader and comparison audit

Read checker/emitter source, the two Phase 27.3 reader inventory/exemption documents, and associated tests. The inventory baseline contains 635 widened reader call sites overall; candidate linter results show zero remaining reader sites for both required-zero consumers (`checker`, `emitter`). `tree_expression_text` has no checker/emitter reads. The check found zero direct expression-shape tests, zero reversed dynamic comparisons against expression-bearing fields, seven raw dynamic Stmt-field comparisons restricted to binding/callee NAME slots, and 42 raw literal comparisons classified as method, handler, keyword, or builtin NAME values. Exact counts and locations are in `reader-audit.json`.

The bounded field-to-helper trace covered expression inputs passed into inferred-expression, checker, and generator adapters. Expression classifiers and shape reads route through `tree_*` wrappers and EST projections; expression-to-expression equality uses structural EST comparison. Remaining raw literal/dynamic comparisons are name/keyword slots, not expression values. The shadow/off structural comparator and stale-handle controls were exercised by `check_phase27_est.py`.

All three documented function/API exemptions were probed independently. Replacing each API key with a nonexistent API fails as a stale exemption; replacing each function name fails as a missing target. See `exemption-probes.json`.

The scrubbed checkout lacked the historical string-debt baseline document, so the integrated linter was run with a temporary baseline generated from the specified baseline revision through its own `sites()` module. Both required-zero consumers passed; the temporary baseline is not committed.

## Full source corpus

The frozen manifest contains 2,225 candidate-tracked `.moss` paths, including 116 under `tests/phase27/adversarial/`. The manifest stores SHA-256 for each candidate source file. The same candidate source files were installed in the baseline worktree before running the matrix. Eight worker threads ran independent `moss check <path> --json` jobs; each compiler/mode cell completed 2,225 inputs.

| Revision | `MOSS_EST=off` | `MOSS_EST=shadow` |
| --- | ---: | ---: |
| Baseline | 1,423 accepted / 802 rejected | 1,423 accepted / 802 rejected |
| Candidate | 1,423 accepted / 802 rejected | 1,423 accepted / 802 rejected |

There are zero verdict-different paths. After normalizing checkout paths, the only semantic diagnostic differences are the two duplicate source-free fixture paths listed above. Six other raw-output differences were only absolute checkout paths inside domain-cycle diagnostics.

## Package closure and `.mossi`

The frozen package manifest contains all 428 candidate-tracked `Moss.toml` files; `package-manifest.sha256` records their hashes. Every root was checked with `margo build --check --json` in baseline/off, baseline/shadow, candidate/off, and candidate/shadow. Each cell had 351 successful roots and 77 failing roots. After normalizing worktree paths and nondeterministic parallel scheduling metadata, there were zero package verdict or semantic-result differences. The initial candidate pass exposed three scrubbed manifests; all 428 candidate manifests were restored and both candidate mode cells were rerun before recording final counts.

`tests/tooling/check_phase27_est.py` passed malformed-source, malformed-package, structural, raw-place, and stale-handle controls. `tests/tooling/check_phase27_est_mossi.py` passed: generated library/app `.mossi` bytes matched between off/shadow, and imported generic and Section 2 bodies produced EST dumps. `tests/tooling/check_phase27_mossi_baseline.py` passed: baseline/candidate library and app interfaces were equal after normalizing identity-derived fingerprints and execution-body hashes. One tracked `.mossi` fixture exists in the source manifest; the package oracle and imported-body checks above cover generated/interface consumption.

## Commands and artifacts

Startup and builds:

- `make --jobs="$(nproc)"` in candidate and baseline worktrees.
- `./moss agent bootstrap --json` (candidate; `moss-0.1`).
- `python3 tmp/cp_e_lint_runner.py` (generates an ephemeral baseline from baseline revision; runs required-zero checker/emitter checks).
- `python3 tests/tooling/check_phase27_est.py "$PWD/moss"`.
- `python3 tests/tooling/check_phase27_est_mossi.py "$PWD/moss"`.
- `python3 tests/tooling/check_phase27_mossi_baseline.py "$PWD/moss" "$PWD/tmp/phase27-3/cp-e-baseline/moss"`.
- `python3 tmp/run_cp_e_corpus.py` (8 workers; 8,900 total checks).
- `python3 tmp/run_cp_e_packages.py`, followed by the corrected candidate rerun after restoring all tracked package manifests (1,712 final rows).

Evidence is under `tests/phase27/adversarial/cp-e-e68b2155/`: frozen source/package manifests and hashes, raw corpus and Margo results, normalized summaries, reader audit, exemption probes, and package source-free oracle output. `CHECKSUMS.sha256` lists the report and evidence hashes.

Compiler binary SHA-256 values are candidate `e00183916b1063e0acf2124a4dec5f7516a59a6d49d8316e2268a3f5fef3cc5d` and baseline `4a334c71d9b5ea5b2925c311f7a462abd046e479fc4b52df4c0453e612ceb286`.
