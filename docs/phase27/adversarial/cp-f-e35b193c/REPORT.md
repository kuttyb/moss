GO

# Phase 27.3 CP-F — fresh review at e35b193c

Candidate freeze: `e35b193c4f8ffca14fd820f6c938f53e1c1a0744`  
Baseline: `04be1a1bbbc7a4fd8fe2e4090b4b5bad5dd573e4`  
Candidate dev compiler SHA-256: `7e12d81c73b66fe245f8a9ea13fb539db135d8d28ca89b9c21fc3ec5c4267319`  
Baseline dev compiler SHA-256: `4a334c71d9b5ea5b2925c311f7a462abd046e479fc4b52df4c0453e612ceb286`  
Candidate optimized compiler SHA-256: `c0b0e2c4109405a8bc37c575d8cfa496e2793cea2223141244d6150ea3431695`  
Baseline optimized compiler SHA-256: `a7014f21f024ffba26801d377474d346eeecfc4c536e604d7e0816d4c7b211f0`  
Bootstrap language contract: `moss-0.1`.

The review used clean detached worktrees at both exact source revisions and candidate-frozen tracked `.moss` inputs. No source files were changed. Because the candidate binary hash differs from the earlier freeze, the long Fast Debug pairs were repeated at this freeze. Complete parsed rows, output hashes, timeout records, and probes are under `tests/phase27/adversarial/cp-f-e35b193c/`; `SHA256SUMS` covers the captured artifacts.

## Verdicts and diagnostics

The checker matrix has 2,225 tracked source files in each of four cells (baseline/candidate × `MOSS_EST=off/shadow`), for 8,900 total checks. Each cell accepted 1,423 and rejected 802; there were no timeout or non-JSON rows, and zero accept/reject differences.

The only diagnostic difference is one logical source-free fixture copied at two paths with identical SHA-256 `a00e3da194b0068677fd7840d3b9f86343af2cff1803d464c410f92c5036cfad`:

- `sdk/advanced-data-acceptance/fixtures/source_free_consumer.moss:57`
- `tests/phase27/adversarial/cp-e-phase27-3-22a9709a/source_free_consumer.moss:57`

Baseline emits `RECOVERY_PATTERN_REQUIRES_VARIANT: recovery patterns must name a qualified enum variant`; candidate emits `UNKNOWN_ERROR_TYPE: unknown error enum 'ndarray.NdArrayError'`. Both paths reject in off and shadow. This is one standalone diagnostic refinement repeated at two identical source paths, not a verdict change.

**Classification: BENIGN.** The package-context consumer was rechecked independently using `sdk/advanced-data-acceptance/check_source_free.py --abi v9 --profile debug` against the same candidate-frozen provider sources. Baseline and candidate each passed in off and shadow. Each run built 14 provider interfaces, checked/formatted/compiled the copied consumer, and executed it with the expected output. No unadjudicated checker differences remain.

## Rust default and optimized output

Default Rust emission was checked for all 1,423 accepted roots in each of the four compiler/mode cells. In each cell, 1,412 emitted Rust hashes match across compilers. Eleven standalone roots per cell fail emission identically on both compilers because their dependency context rejects a referenced module source:

- `examples/swarm/modules/calc_interpreter/repro/natural_full/tests/{calc_test,env_test,eval_test,lexer_test,report_test,token_test}.moss` — `parser_test.moss:15`, `cannot infer assertEqual operand types`.
- `examples/swarm/modules/text_toolkit/toolkit/{src/inventory.moss,src/table.moss,tests/inventory_test.moss,tests/stats_test.moss,tests/table_test.moss}` — `strings.moss:106`, unsupported String ordering operators.

The optimized `-O` matrix contains 5,692 source/compiler/mode runs. It produced 1,412 matching Rust hashes per compiler/mode cell and the same 11 emitter failures per cell on both compilers. There are zero unmatched output hashes or failure diagnostics.

Across all 33 top-level examples, the eight rejected cells are the intentional `examples/use_after_transfer.moss` rejection. All other cells generated, compiled, and ran default and `-O` Rust. The 256 native executions had matching generated hashes and runtime output across baseline/candidate and off/shadow.

## Fast Debug

The `moss debug <source>` sweep covered all 33 top-level examples in baseline/candidate × off/shadow (132 runs). Thirty-one examples completed in every cell with identical status and output. `examples/functional_pipeline.moss` and `examples/frontend_syntax.moss` timed out in both compilers and both modes at the 180-second probe.

Each fixture then received a fresh paired 600-second run at this source freeze:

- `examples/functional_pipeline.moss`: baseline off and candidate shadow both timed out at about 600.5 seconds, with empty stdout and stderr. Command included `--trace`.
- `examples/frontend_syntax.moss`: baseline off and candidate shadow both timed out at about 600.5 seconds, with empty stdout and stderr.

Record both fixtures as **BOTH-TIMEOUT** and exclude them from output comparison under L7's ruling. The other 31 examples had no Fast Debug output differences.

## Reach differential

The reach scan completed 8,900 trace checks over the 2,225 tracked inputs. Phase 27.2's own off-vs-shadow control contains 160 reach-delta rows. Phase 27.2 off vs Phase 27.3 shadow contains those same 160 exact edge/checked-declaration deltas, with no missing, changed, or additional reach paths. The raw path comparison adds two source-free diagnostic rows; their files have identical hashes, so this is one logical diagnostic row and 161 logical rows total. That row is the BENIGN diagnostic refinement above. No new checked-declaration or trace-completeness delta was added.

Full reach sets and the exact comparison are in `reach_matrix.jsonl.gz` and `reach_l7_adjudication.json`.

## `.mossi`, lint, and sanitizers

- `check_phase27_est_mossi.py` passed: candidate off/shadow `.mossi` bytes match for library and app; imported ABI-v9 generic and Section 2 execution-body EST probes passed.
- `check_phase27_mossi_baseline.py` passed: after recomputing identity-derived fields, baseline and candidate `.mossi` bytes match for library and app.
- Widened string-debt lint passed with checker, emitter, reach-walk, and Fast Debug reader counts at zero (42 NAME comparisons and seven binding comparisons remain under their explicit accounting). The Phase 27 EST focused harness passed.
- Full candidate ASan/UBSan shadow scan: 2,225 sources and zero sanitizer, signal, timeout, or non-JSON findings. Report: `sanitizer_candidate.json`.

## Verdict

**CP-F verdict: GO.** The sole diagnostic refinement is independently confirmed BENIGN in actual package-context execution. All 160 inherited reach rows match exactly; the one logical additional row is that diagnostic-only refinement. The two Fast Debug BOTH-TIMEOUT fixtures are documented exclusions. No source correction is needed.
