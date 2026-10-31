GO

# CP-E independent review: Phase 27.3

Candidate source: `e35b193c4f8ffca14fd820f6c938f53e1c1a0744`  
Baseline source: `04be1a1bbbc7a4fd8fe2e4090b4b5bad5dd573e4` (`phase27-2-m1`)  
Compiler contract: bootstrap reported `language_version=moss-0.1`.

## Verdict basis

The checker and emitter reader lints pass with `--require-zero`. Their raw `Stmt` field reads that compare values are fully classified: 42 literal NAME/keyword comparisons and seven assignment-binding comparisons. No expression-bearing value is classified by those comparisons. There are no direct expression-shape string tests, no checker/emitter `tree_expression_text(...)` calls, and no remaining expression-shape exemption. The three canonical type-argument exemptions are keyed by function and API; synthetic disappearance and duplicate-site probes confirmed both fail-closed conditions for each key.

The bounded taint review followed expression-bearing `Stmt.a`, `.b`, `.text`, and EST projections into their local copies and helper arguments. Semantic consumers route through EST-aware helpers (`tree_parse_*`, `tree_identifier_*`, `inferred_expr_type`, `generated_expr_type`, `constrain_call`, `instantiate_summary`, and `static_mark_possible_mutation`). `trim` is used only before these EST queries. `tree_split_top_level` consumers split canonical type applications or builtin signature text, not expression-bearing statement fields. The raw site-by-site inventory is `raw-stmt-reader-inventory.tsv`; the exemption mutation probes are `exemption_fail_closed.py` and `exemption-fail-closed.txt`.

## Differential results and adjudications

The tracked standalone corpus contained 2,225 `.moss` inputs. The matrix compared baseline-off, candidate-off, and candidate-shadow verdicts and complete diagnostics (including the top-level `error` object). It found two source-file diagnostic differences, both instances of the same source-free fixture checked without its provider context:

- `sdk/advanced-data-acceptance/fixtures/source_free_consumer.moss:57`
- `tests/phase27/adversarial/cp-e-phase27-3-22a9709a/source_free_consumer.moss:57`

Both sides reject the context-free input. Baseline reports `RECOVERY_PATTERN_REQUIRES_VARIANT`; candidate reports `UNKNOWN_ERROR_TYPE` for `ndarray.NdArrayError`. Classification: **LEGACY MISPARSE**. Independent oracle: the source uses a syntactically qualified recovery variant (`ndarray.NdArrayError.InvalidAxis`), and the actual source-free package check resolves and accepts that same clause when the declared `ndarray` provider interface is supplied. The baseline therefore rejected the recovery pattern before provider resolution despite its valid qualified form. The two-path reproducer is `corpus-matrix.py` on the two paths above with baseline `MOSS_EST=off` and candidate `MOSS_EST=off`; the package-context counterpart is `sdk/advanced-data-acceptance/check_source_free.py`.

No other verdict or diagnostic differences were found in the 2,225-input matrix. The 27.1 rejected-source controls in `check_phase27_est.py` pass against both compilers. Candidate-off and candidate-shadow also pass the full ABI-9 source-free package gate; baseline-off passes the same gate.

`check_phase27_mossi_baseline.py` reports normalized `.mossi` bytes equal for both the library and application. `check_phase27_est_mossi.py` passes for baseline and candidate and confirms imported generic and Section 2 execution bodies build EST dumps. The independent accepted-declaration check verifies an unreachable valid declaration remains accepted and an unreachable unknown identifier remains rejected, identically for baseline/candidate and off/shadow.

The supplemental C1 trace scan found 160 edge-name rows where legacy and candidate names differed; normalizing only the module qualification (`module__leaf` versus `leaf`) makes every row equal (160/160), with no accepted-entry verdict or checked-declaration delta. Classification: **BENIGN**, name-spelling representation only; full raw rows are in `checked-closure.json`. It also found four legacy-only checked IDs, all on standalone package files rejected by both compilers at `INVALID_BARE_IDENTIFIER_STATEMENT` for unsupported `break` syntax; package-mode builds reject at the same site. Classification: **BENIGN**, deeper closure is not reached for these rejected inputs. The independent accepted-declaration closure probe and accepted subset of the trace scan both show zero missing or extra declarations.

## Evidence and commands

- `python3 tests/tooling/check_phase27_string_debt.py --require-zero checker` — PASS
- `python3 tests/tooling/check_phase27_string_debt.py --require-zero emitter` — PASS
- `python3 tests/tooling/check_phase27_est.py ./moss` and baseline binary — PASS; rejected-source controls included
- `python3 tests/tooling/check_phase27_est_mossi.py` with candidate and baseline binaries — PASS
- `python3 tests/tooling/check_phase27_mossi_baseline.py ./moss <baseline-binary>` — PASS; normalized library/application bytes equal
- `python3 sdk/advanced-data-acceptance/check_source_free.py --abi v9` — PASS for candidate off, candidate shadow, and baseline off
- `python3 tests/phase27/adversarial/cp-e-e35b193c/corpus_matrix.py` — 2,225 inputs; two adjudicated legacy-misparse diagnostic differences, no verdict differences
- `python3 tests/phase27/adversarial/cp-e-e35b193c/accepted_declaration_closure.py` — PASS
- `python3 tests/phase27/adversarial/cp-e-e35b193c/exemption_fail_closed.py` — PASS for all three exemptions, function disappearance and duplicate site

Raw evidence: `corpus-matrix.json`, `corpus-summary.json`, `checked-closure.json`, `accepted-declaration-closure.json`, `exemption-fail-closed.txt`, and `raw-stmt-reader-inventory.tsv` under `tests/phase27/adversarial/cp-e-e35b193c/`.
