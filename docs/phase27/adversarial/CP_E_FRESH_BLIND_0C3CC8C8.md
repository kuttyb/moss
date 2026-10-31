# Fresh blind CP-E review: `0c3cc8c8`

CP-E verdict: NO-GO

## Freeze

- Candidate source: `0c3cc8c89f5e5f704813bda22486089c7b0a6f30`.
- Diagnostic baseline: `04be1a1bbbc7a4fd8fe2e4090b4b5bad5dd573e4` (`phase27-2-m1`).
- Review branch: `review/cp-e-blind-0c3cc8c8-run2`.
- Candidate source was checked in an isolated worktree. All review writes are under `docs/phase27/adversarial/` and `tests/phase27/adversarial/cp-e-freshblind-0c3cc8c8/`; no `src/` files were changed.
- Required startup was completed: both local Moss skills loaded, `./moss agent bootstrap --json` returned `language_version: moss-0.1`, and both optimized and full compiler builds succeeded for baseline and candidate.

## Defects

1. **MIGRATION BUG — malformed call argument slots are accepted.** Fresh probes `malformed_leading_arg.moss` (`add(, 1)`) and `incomplete_call.moss` (`add(1, )`) are rejected by baseline off with `TYPE_MISMATCH` at line 4:1 (`argument 1/2 ... has type '', expected 'int'`). Candidate off and candidate shadow both return success for both sources. The candidate therefore accepts calls with a missing required argument. The four diagnostic records are in `probes.json`.

2. **MIGRATION BUG — malformed collection separator diverges and crashes shadow.** Fresh probe `malformed_double_comma.moss` uses `[1,, 2]`. Baseline off rejects it with `ECHO_DISPLAY_UNSUPPORTED`. Candidate off accepts it. Candidate shadow aborts at `src/semantic_checker.cpp:11089` on `Assertion 'moss::parse_vector_literal(text) == recovered' failed`. This is both an invalid-source acceptance and a shadow-mode crash; the exact stderr is retained in `probes.json`.

The two call probes and vector probe are three distinct reproductions of two defect classes. These semantic acceptance changes fail the required phase27-2-m1 diagnostic-parity oracle, so the frozen candidate is NO-GO.

Other fresh controls matched baseline off and candidate off/shadow: `incomplete_binary.moss` (`MOSS_COMPILE_ERROR`), `unresolved_call.moss` (`UNKNOWN_SYMBOL_OR_TYPE`), `unresolved_collection.moss` (accepted), and `unresolved_index.moss` (`TYPE_AMBIGUOUS_OPEN_RESULT`).

## Reader audit

- Independent scan covered nine checker/emitter source files and recorded 868 direct `.a`/`.b`/`.text` slot references: 228 direct tree/fact consumers and 640 statement-kind name, presence, flow, and forwarding accesses. The per-site inventory and bounded-alias table are in the evidence directory.
- `tree_expression_text(...)` has zero checker/emitter call sites.
- `check_phase27_string_debt.py --require-zero checker` and `--require-zero emitter` both pass. The gates report 40 raw Stmt NAME comparisons and 9 binding-name comparisons, matching the permitted inventory.
- All three exact type-syntax exemptions (`phase20_is_nontransferable_capability`, `builtin_equality_type`, `contains_float_type` × `split_top_level`) were verified. For each one, removing its reader made the exemption stale and duplicating it failed the single-site check (3/3 removal checks and 3/3 duplicate checks rejected).
- Ten bounded aliases were followed and recorded in `bounded-alias-audit.json`, including expression aliases that feed EST/tree/type projections and the `call_text` reconstruction that feeds FileIO lifecycle analysis.

## Differential and package results

- The full shared non-adversarial corpus contains 2,004 `.moss` files from `tests/`, `examples/`, `benchmarks/`, and `sdk/`. The raw JSONL records baseline off, candidate off, and candidate shadow verdicts plus diagnostic code/message/span.
- Six raw diagnostic-message differences were only absolute baseline/candidate worktree paths in domain-cycle diagnostics. After path normalization, 2,003 comparable files have 0 baseline-off/candidate-off differences and 0 candidate-off/candidate-shadow differences. The one excluded source is `sdk/advanced-data-acceptance/fixtures/source_free_consumer.moss`, which is a package fixture and was checked once outside its package import context; its proper package-aware source-free oracle passes on both versions. The 2,003 comparable files include 678 rejected inputs, with no verdict changes.
- Clean-source checked-declaration closure: `margo build --check --json` checked all 15 packages in the advanced-data dependency graph on both baseline and candidate; none were skipped.
- `sdk/tables` native package builds pass on both. `margo test --json` fails identically on both at `tests/tables_test.moss:151`, `TYPE_MISMATCH` for binding `ignored`; this is a baseline failure, not a candidate-only regression.
- The repository's compiled-only package oracle passes on both: `python3 sdk/advanced-data-acceptance/check_source_free.py --abi v9 --profile debug` reports 5 Team A modules and 14 current source-backed provider interfaces.

## Review-scope note

An accidental `rg` search across `docs/` (excluding `docs/phase27/`) emitted clipped one-line snippets from Phase 21/review documents. I stopped immediately and did not open those files. I did not read `docs/phase27/27_3_REPORT.md`, any contents under `docs/phase27/adversarial/`, or any contents under `tests/phase27/adversarial/` before creating this review's evidence.

## Evidence

Raw evidence and reproduction sources are committed under `tests/phase27/adversarial/cp-e-freshblind-0c3cc8c8/`. It includes the corpus records and normalized comparison, fresh probes and outputs, direct slot inventory, alias audit, fail-closed exemption checks, package oracles, and clean checked-declaration closure output.
