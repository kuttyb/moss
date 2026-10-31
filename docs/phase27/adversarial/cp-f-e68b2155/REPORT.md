GO

# Phase 27.3 CP-F — fresh full differential

Candidate source freeze: `e68b21557e675cc4697c391a083ead5add7ad76a`  
Baseline: `04be1a1bbbc7a4fd8fe2e4090b4b5bad5dd573e4`  
Candidate dev compiler SHA-256: `e00183916b1063e0acf2124a4dec5f7516a59a6d49d8316e2268a3f5fef3cc5d`  
Baseline dev compiler SHA-256: `4a334c71d9b5ea5b2925c311f7a462abd046e479fc4b52df4c0453e612ceb286`  
Language contract from bootstrap: `moss-0.1`.

The reviewer used clean detached worktrees for both source revisions and the same candidate-frozen tracked `.moss` corpus. No source files were changed. Full parsed checker rows, optimized emission rows, reach traces, example outputs, and reproducible runner scripts are committed under `tests/phase27/adversarial/cp-f-e68b2155/`; `SHA256SUMS` covers the captured evidence.

## Checker verdicts and diagnostics

The full checker matrix contains 2,225 tracked source files in each of four cells: baseline/candidate × `MOSS_EST=off/shadow`. Each cell accepted 1,423 and rejected 802; no timeouts or non-JSON results occurred. The only diagnostic difference is one logical source-free fixture, present at two paths with the same SHA-256 `a00e3da194b0068677fd7840d3b9f86343af2cff1803d464c410f92c5036cfad`:

- `sdk/advanced-data-acceptance/fixtures/source_free_consumer.moss:57`
- `tests/phase27/adversarial/cp-e-phase27-3-22a9709a/source_free_consumer.moss:57`

Baseline rejects with `RECOVERY_PATTERN_REQUIRES_VARIANT: recovery patterns must name a qualified enum variant`. Candidate rejects with `UNKNOWN_ERROR_TYPE: unknown error enum 'ndarray.NdArrayError'`. Both revisions reject in both modes; this is one diagnostic-code/message refinement repeated at two identical source paths, not a verdict change.

**Classification: BENIGN.** To test the L7 package-context condition independently, `sdk/advanced-data-acceptance/check_source_free.py --abi v9 --profile debug` was run with the same candidate-frozen provider sources against baseline and candidate, each in off and shadow. All four actual Margo consumer runs passed: each built 14 source-backed provider interfaces, checked/formatted/compiled the copied consumer, and ran it with the expected output. There are no unadjudicated checker differences.

## Rust default and `-O`

Default Rust emission was checked for all accepted corpus sources in each baseline/candidate × off/shadow cell. For each cell, 1,412 generated Rust hashes matched across revisions. Eleven roots per cell could not emit standalone Rust; baseline and candidate returned the same dependency-context diagnostic for each. These are the same roots in default and optimized emission:

- `examples/swarm/modules/calc_interpreter/repro/natural_full/tests/{calc_test,env_test,eval_test,lexer_test,report_test,token_test}.moss` — `parser_test.moss:15`, `cannot infer assertEqual operand types`.
- `examples/swarm/modules/text_toolkit/toolkit/{src/inventory.moss,src/table.moss,tests/inventory_test.moss,tests/stats_test.moss,tests/table_test.moss}` — `strings.moss:106`, unsupported String ordering operators.

The complete optimized scan ran 5,692 source/emitter combinations (1,423 accepted roots × four compiler/mode cells). It produced 1,412 matching generated Rust hashes per mode and the same 11 standalone emitter failures per mode on both compilers. There were zero hash or diagnostic differences. All 33 top-level examples were also checked in off and shadow. The eight rejected-example cells are the intentional `examples/use_after_transfer.moss` rejection; all other cells emitted default and `-O` Rust, compiled with rustc, and ran successfully. Across 256 native example runs, both generated Rust hashes and runtime output matched baseline for default and `-O`.

## Fast Debug

`moss debug <source>` was compared over all 33 top-level `.moss` examples, baseline/candidate × off/shadow (132 runs). Thirty-one examples completed in every cell with identical status, stdout, and stderr. `examples/functional_pipeline.moss` and `examples/frontend_syntax.moss` timed out on both compilers during the 180-second sweep. Each received the required paired 600-second retry:

- `examples/functional_pipeline.moss`: baseline off and candidate shadow both timed out at 600 seconds; stdout/stderr were empty on both sides.
- `examples/frontend_syntax.moss`: baseline off and candidate shadow both timed out at 600 seconds; stdout/stderr were empty on both sides.

Record both as **BOTH-TIMEOUT** and exclude them from output comparison, per L7. The smaller `moss run --interp` example scan also timed out on those same two examples in all four cells. No other Fast Debug output differences occurred.

## Reach differential

The reach scan replayed all 2,225 tracked sources with scope tracing enabled. Phase 27.2's own off-vs-shadow control has 160 reach-delta rows. Comparing Phase 27.2 off to Phase 27.3 shadow yields the same 160 exact reach/checked-declaration delta rows, with no missing, changed, or additional reach paths. It also has two raw diagnostic rows for the source-free fixture; because the files are byte-identical, this is one logical extra diagnostic row, giving 161 logical rows versus the 160-row control. The new row is the BENIGN source-free diagnostic refinement classified above. No checked-declaration or trace-completeness delta was added. Full set-valued rows are in `reach_matrix.jsonl.gz`; the exact comparison and fixture identity are in `reach_l7_adjudication.json`.

## `.mossi`, lint, and sanitizers

- `check_phase27_est_mossi.py` passed: off/shadow raw `.mossi` hashes match for both generated library and app; the imported ABI-v9 generic body and Section 2 execution-body EST probes passed.
- `check_phase27_mossi_baseline.py` passed: after recomputing identity-derived fields, baseline and candidate `.mossi` bytes match for both library and app.
- The widened source lint passed at the freeze: checker reader sites zero, emitter reader sites zero, reach/Fast Debug reader sites zero; 42 raw NAME comparisons and seven binding comparisons remain under their explicit accounting. `check_phase27_est.py` also passed.
- Full candidate ASan/UBSan shadow scan: 2,225 tracked sources, zero sanitizer, signal, timeout, or non-JSON findings. Raw output: `sanitizer_candidate.json`.

## Verdict

**CP-F verdict: GO.** The sole diagnostic refinement is independently confirmed BENIGN in actual package execution. The 160 inherited reach corrections match exactly; the one logical extra row is that same diagnostic-only refinement. Both long Fast Debug timeout pairs are documented exclusions. No source correction is required.
