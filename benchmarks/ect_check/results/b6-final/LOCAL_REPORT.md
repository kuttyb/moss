# ECT Check Benchmark - Local Report

**Suite:** `moss_ect_check` 0.4.1 (2 fixtures, seed 20261105)  
**Run:** `20261106-112733` (2026-11-06T11:49:28-0800)  
**Compiler:** `/home/owner/daji/code/moss2/build/compiler/moss-opt`  
**Compiler SHA256:** `e9a22cf0a6029d31b617baf863c5c1e866a3641a22c38bb1cd1e78c31bfe4d36`  
**Build flavor:** `opt`  
**Repo:** `phase28-5-bench` @ `3fb8e9616f03`  
**Platform:** `Linux-6.18.33.2-microsoft-standard-WSL2-x86_64-with-glibc2.39` (16 CPUs)  
**Reps:** 5 timed runs per fixture/mode (median reported), plus one separate counter run (counters are not collected during timed runs)  
**Warm client:** not configured  

**Noise probe:** loadavg start [0.123046875, 3.14306640625, 4.71484375], end [1.5654296875, 2.1279296875, 3.3642578125]; calibration `b2_k04` median wall 562.8 ms at start, 536.8 ms after the timed loop (drift 4.8%).  

## 1. Timing, memory and compiler counters

Wall, CPU and RSS are medians over the timed reps. CPU is user+system of the whole process tree. RSS is the largest single process in the tree (not a sum). Counters are summed over the checker runs one command starts.

### Mode: cold

| Fixture | Kind | Status | Wall ms | CPU ms | RSS KiB | Checker runs | Fns analyzed | Spec evals | Spec hits | Spec keys |
|---|---|---|---:|---:|---:|---:|---:|---:|---:|---:|
| `b4_floor_file` | file | pass | 1.89 (1.9-1.9) | 1.36 | 9396 | 1 | 0 | 0 | 0 | 0 |
| `b4_floor_pkg` | package | pass | 46.75 (44.9-51.0) | 46.46 | 24952 | 1 | 0 | 0 | 0 | 0 |

Specialization counters: evals = `specialization_requests`, hits = `reused_specialization_results`, keys = `distinct_specialization_keys` (the compiler's own `MOSS_PROFILE` names; today's memo is in-process, so hits are intra-run reuse only).

## B6. Agent-loop replay (last 50 commits touching sdk/packages `.moss`)

Each commit's PARENT tree is materialized (`git archive`), every entry point is checked (`pre`: the baseline, cold), the commit's `.moss` / `Moss.toml` changes are applied and the same entry points are checked again (`post`: the per-edit latency; margo's package fingerprint cache skips unchanged dependencies). A phase's latency is the sum over its entry points of the median over reps. Old trees are checked with TODAY's compiler, so many legitimately fail: failing checks end at the first diagnostic and are NOT representative latencies; the populations below separate them. See README for the entry-point definition and limits.

Reference compiler sha256 `e9a22cf0a602`: the pinned binary the expectations were recorded with.

Measurement guards: 13 mid-run calibration probes (every 5 steps; combined single- and multi-process probe), 3 flagged noisy (the block was re-measured, up to 2 times); max drift 25.9%. Steps re-measured because a phase's rep max/min spread exceeded 1.3: 2 of 50 (kept attempt = lowest worst-phase spread, never chosen by median); still flagged after 3 attempts: 0; blocks left noisy after retries: 0 steps. Median max/min spread over steps and phases: 1.08.

### Latency per edit (cold)

| Population | Commits | post p50 ms | post p95 ms | post min-max ms | pre p50 ms | pre p95 ms |
|---|---:|---:|---:|---:|---:|---:|
| all commits (incl. failing checks) | 50 | 343.0 | 2470.1 | 57.4-2986.4 | 388.6 | 1887.3 |
| post-edit check passes | 14 | 801.8 | 2734.4 | 63.6-2986.4 | 265.8 | 1365.3 |
| post passes, package not new and had sources (pre may fail) | 7 | 806.5 | n/a (n<10) | 63.6-2986.4 | 265.8 | n/a (n<10) |
| clean edits: pre and post pass, not new, a checker ran | 4 | 519.3 | n/a (n<10) | 63.6-835.4 | 330.5 | n/a (n<10) |

Percentiles: linear interpolation between order statistics (`statistics.quantiles` 'inclusive' convention); with this few commits p95 sits close to the maximum.

Distribution of post-edit latency (commits per bucket):

| Bucket | all commits (incl. failing checks) | post-edit check passes | post passes, package not new and had sources (pre may fail) | clean edits: pre and post pass, not new, a checker ran |
|---|---:|---:|---:|---:|
| < 50 ms | 0 | 0 | 0 | 0 |
| 50-100 ms | 8 | 1 | 1 | 1 |
| 100-250 ms | 16 | 1 | 1 | 1 |
| 250-500 ms | 5 | 3 | 1 | 0 |
| 0.5-1 s | 8 | 4 | 2 | 2 |
| 1-2 s | 9 | 2 | 0 | 0 |
| 2-5 s | 4 | 3 | 2 | 0 |
| >= 5 s | 0 | 0 | 0 | 0 |

Compiler counters of the post-edit check (median over the commits that HAVE counters, n shown: a failing step that stops before the checker finishes reports none; per-commit sum over entry points; margo skips unchanged packages, so these count only what was rechecked):

| Population | Fns analyzed p50 (n) | Source fn checks p50 | Spec evals p50 | Spec keys p50 | Checker runs p50 | Fns analyzed (sum) |
|---|---:|---:|---:|---:|---:|---:|
| all commits (incl. failing checks) | 142.0 (n=36 of 50) | 98.0 | 314.0 | 0.0 | 1.5 | 8530 |
| post-edit check passes | 197.0 (n=14 of 14) | 139.5 | 420.5 | 0.0 | 3.0 | 4342 |
| post passes, package not new and had sources (pre may fail) | 173.0 (n=7 of 7) | 111.0 | 409.0 | 0.0 | 2.0 | 1406 |
| clean edits: pre and post pass, not new, a checker ran | 118.0 (n=4 of 4) | 54.5 | 236.5 | 0.0 | 2.0 | 494 |

Passing post-edit checks in which no checker ran (margo reported every package as skipped; the changed files were not package sources or tests): 0 commits (none).

### Per commit (cold)

| # | Commit | Entries | Pre | Post | Pre ms | Post ms | Spread pre/post | Tries | Post fns analyzed | Post checker runs | Subject |
|---:|---|---:|---|---|---:|---:|---|---:|---:|---:|---|
| 1 | `a25a6fe1` | 6 | fail | fail | 1093.0 | 1127.1 | 1.02/1.04 | 1 | 128 | 6 | Phase 24 Team M: Implement L7 closeout repairs and robust be |
| 2 | `026db414` | 1 | pass | pass | 265.8 | 232.1 | 1.05/1.06 | 1 | 15 | 2 | Optimize UTF-8 codec hot paths |
| 3 | `4bae0863` | 3 | fail | fail | 223.3 | 134.4 | 1.04/1.15 | 1 | n/a | 0 | Optimize text search and slicing through native primitives |
| 4 | `eb09e421` | 3 | fail | fail | 220.9 | 133.5 | 1.10/1.09 | 1 | n/a | 0 | Correct preconstructed text performance workloads |
| 5 | `a257917e` | 3 | fail | fail | 216.2 | 130.9 | 1.16/1.07 | 1 | n/a | 0 | Optimize ASCII case and characterize builders |
| 6 | `7e049160` | 2 | fail | fail | 296.2 | 296.2 | 1.11/1.09 | 1 | 15 | 2 | no src; Add independent Text performance validation |
| 7 | `67b7d9b7` | 7 | - | pass | n/a | 2367.7 | n/a/1.03 | 1 | 1205 | 15 | new pkg; Phase 24 Team N: Complete numerical computing SDK with archi |
| 8 | `7ba41b83` | 3 | fail | fail | 224.4 | 131.7 | 1.08/1.08 | 1 | n/a | 0 | Add capacity-aware Text builder |
| 9 | `05834f8b` | 2 | fail | fail | 162.2 | 82.0 | 1.10/1.20 | 1 | n/a | 0 | no src; Correct reusable builder benchmark equivalence |
| 10 | `47d50f49` | 6 | pass | fail | 2219.8 | 1617.8 | 1.18/1.15 | 1 | 587 | 8 | phase24(numerics): resolve L7 review defects, expand differe |
| 11 | `5cc8b17e` | 1 | - | fail | n/a | 58.9 | n/a/1.08 | 1 | 5 | 1 | new pkg; Phase 24 D1: Implement moss.parse cursor primitives and sour |
| 12 | `1d58b910` | 1 | - | fail | n/a | 57.4 | n/a/1.08 | 1 | 5 | 1 | new pkg; Phase 24 D2: Implement moss.fmt formatting and escaping |
| 13 | `c20ef73c` | 1 | - | fail | n/a | 146.4 | n/a/1.08 | 1 | 28 | 3 | new pkg; Phase 24 D3: Implement moss.json columnar tape representatio |
| 14 | `048b46c1` | 2 | fail | fail | 206.0 | 111.7 | 1.13/1.08 | 1 | n/a | 0 | Phase 24 D4: Implement JSON serialization, pretty-printing,  |
| 15 | `ac328671` | 1 | - | fail | n/a | 57.8 | n/a/1.29 | 1 | 5 | 1 | new pkg; Phase 24 D5: Implement moss.csv RFC 4180 reader, writer, val |
| 16 | `234ff9e2` | 5 | fail | fail | 1626.2 | 1098.6 | 1.02/1.02 | 1 | 369 | 5 | phase24-numerics: final L7 closeout repairs and integration  |
| 17 | `6ce098af` | 2 | - | fail | n/a | 154.3 | n/a/1.09 | 1 | 28 | 3 | new pkg; Phase 24 D6: Add cross-package data-acceptance suite, differ |
| 18 | `bff4f72a` | 4 | - | fail | n/a | 213.7 | n/a/1.07 | 1 | 8 | 2 | new pkg; Add Team H path and bounded filesystem SDKs |
| 19 | `f8c14cfc` | 8 | - | fail | n/a | 220.1 | n/a/1.04 | 1 | 8 | 5 | new pkg; Document host I/O service feasibility |
| 20 | `4d04481f` | 1 | pass | pass | 58.3 | 63.6 | 1.11/1.10 | 1 | 6 | 1 | Bound Team H filesystem reads |
| 21 | `31429002` | 1 | - | pass | n/a | 797.1 | n/a/1.06 | 1 | 331 | 4 | new pkg; Implement moss.ndarray core |
| 22 | `2dfdd86c` | 1 | - | pass | n/a | 392.7 | n/a/1.06 | 1 | 111 | 2 | new pkg; Implement Phase 24 priority structures |
| 23 | `dbb3bec5` | 1 | - | fail | n/a | 389.8 | n/a/1.04 | 1 | 52 | 1 | new pkg; feat(tables): add typed columnar table core |
| 24 | `0dfbcd79` | 1 | - | pass | n/a | 588.4 | n/a/1.03 | 1 | 167 | 3 | new pkg; Implement moss.binary primitives |
| 25 | `99fc7fc8` | 1 | pass | pass | 395.3 | 806.5 | 1.06/1.02 | 1 | 221 | 2 | Implement Phase 24 search structures |
| 26 | `0d578424` | 1 | pass | pass | 923.4 | 835.4 | 1.28/1.25 | 1 | 252 | 2 | Implement ndarray broadcasting slicing and reductions |
| 27 | `0fe2e6ef` | 2 | fail | fail | 78.0 | 247.4 | 1.27/1.14 | 1 | n/a | 0 | first-source pkg; feat(hash): implement moss.hash with Adler-32, CRC-32, FNV-1 |
| 28 | `7f6cd553` | 2 | fail | pass | 66.7 | 418.9 | 1.13/1.08 | 1 | 50 | 3 | first-source pkg; feat(compress): implement moss.compress with raw LZ4 blocks  |
| 29 | `b9c6ea40` | 1 | - | pass | n/a | 1510.3 | n/a/1.03 | 1 | 362 | 5 | new pkg; Implement moss.cbor arena and encoder |
| 30 | `7773a300` | 1 | fail | fail | 394.3 | 1378.0 | 1.02/1.07 | 1 | 103 | 1 | feat(tables): add relational operations |
| 31 | `b4271cdc` | 3 | pass | pass | 1525.3 | 1669.1 | 1.13/1.12 | 1 | 710 | 6 | new pkg; Add higher-level ndarray examples |
| 32 | `8d8712a8` | 1 | pass | fail | 1776.4 | 1021.9 | 1.06/1.02 | 1 | 156 | 1 | Add iterative CBOR decoder |
| 33 | `48e36944` | 1 | fail | fail | 1381.9 | 61.7 | 1.03/1.24 | 1 | 5 | 1 | Add typed CSV adapters for tables |
| 34 | `b2f09d61` | 2 | fail | fail | 46.2 | 73.6 | 1.13/1.10 | 1 | n/a | 0 | first-source pkg; feat(regex): implement moss.regex with non-backtracking Thom |
| 35 | `40d4ae20` | 1 | fail | fail | 228.5 | 142.6 | 1.07/1.11 | 1 | n/a | 0 | first-source pkg; feat(data_utils_integration): implement cross-package pipeli |
| 36 | `cdcadb26` | 2 | - | fail | n/a | 1311.5 | n/a/1.14 | 1 | 617 | 9 | new pkg; Add cross-package advanced-data acceptance |
| 37 | `6ef17f1c` | 3 | fail | fail | 264.1 | 175.2 | 1.04/1.09 | 1 | n/a | 0 | fix(json,fmt,diff): address L7 adversarial review findings w |
| 38 | `a50a8342` | 3 | fail | fail | 543.5 | 541.3 | 1.04/1.06 | 1 | 35 | 2 | fix(team-r): resolve L7 corrective review across validation, |
| 39 | `3ea10923` | 2 | fail | fail | 1333.0 | 123.5 | 1.02/1.26 | 1 | n/a | 0 | no src; Repair Team A Architect acceptance gaps |
| 40 | `4068976a` | 2 | fail | fail | 83.2 | 79.8 | 1.08/1.07 | 1 | n/a | 0 | fix(regex): repair NFA split origin propagation asymmetry an |
| 41 | `debf99f0` | 1 | fail | fail | 1140.2 | 884.2 | 1.04/1.04 | 1 | 156 | 1 | Fix CBOR edge ownership validation |
| 42 | `a6ecc79f` | 1 | fail | fail | 1133.9 | 892.0 | 1.03/1.03 | 1 | 156 | 1 | no src; Add CBOR L7 adversarial verification |
| 43 | `76baf873` | 1 | fail | fail | 1155.1 | 884.5 | 1.20/1.18 | 3 | 156 | 1 | no src; Repair CBOR parent-cycle verifier |
| 44 | `58340e63` | 2 | fail | fail | 1321.8 | 123.0 | 1.07/1.11 | 1 | n/a | 0 | no src; Exercise corrected CBOR error through ABI-v9 |
| 45 | `7bf962d2` | 6 | fail | fail | 382.9 | 2553.8 | 1.04/1.05 | 1 | 1224 | 16 | refactor(phase25): route Moss String lowering through MossSt |
| 46 | `f272af9c` | 5 | fail | fail | 1058.0 | 1966.7 | 1.04/1.02 | 1 | 342 | 5 | fix(phase25): reject record ==, Char/String mixing, and juxt |
| 47 | `dd45e307` | 2 | fail | pass | 248.8 | 2598.7 | 1.18/1.20 | 1 | 173 | 4 | Fix SDK Char and String boundaries |
| 48 | `bb4b3409` | 1 | fail | pass | 78.3 | 458.2 | 1.14/1.07 | 1 | 111 | 2 | phase25.1 R4A: repair time package inventory blocker |
| 49 | `c0ff87fd` | 2 | fail | fail | 2383.8 | 154.8 | 1.04/1.06 | 2 | n/a | 0 | no src; phase25.1 R4B: validate advanced-data ABI-v10 |
| 50 | `4601a468` | 1 | fail | pass | 1125.4 | 2986.4 | 1.15/1.19 | 1 | 628 | 5 | no src; Start Phase 26.3 file-scoped test support |

### Checks that fail today (40 of 50 commits have at least one failing entry point; each is the frozen expectation, not a harness error)

| # | Commit | First failing entry points (diagnostic) |
|---:|---|---|
| 1 | `a25a6fe1` | pre pkg:packages/time: MOSS_COMPILE_ERROR\|function 'time__pad_digit' returns 'char' but is annotated 'string'<br>pre aux:packages/time/benches/time_bench.moss: MOSS_COMPILE_ERROR\|function 'time__pad_digit' returns 'char' but is annotated 'string'<br>post pkg:packages/time: MOSS_COMPILE_ERROR\|function 'time__pad_digit' returns 'char' but is annotated 'string'<br>(+1 more) |
| 3 | `4bae0863` | pre pkg:sdk/text: TYPE_MISMATCH\|push argument conflicts with the element type of 'parts'<br>pre pkg:sdk/text-encoding-acceptance: TYPE_MISMATCH\|push argument conflicts with the element type of 'parts'<br>pre aux:sdk/text-encoding-acceptance/benches/text_encoding.moss: MOSS_COMPILE_ERROR\|interface for text is missing or stale; run `margo build`<br>(+3 more) |
| 4 | `eb09e421` | pre pkg:sdk/text: TYPE_MISMATCH\|push argument conflicts with the element type of 'parts'<br>pre pkg:sdk/text-encoding-acceptance: TYPE_MISMATCH\|push argument conflicts with the element type of 'parts'<br>pre aux:sdk/text-encoding-acceptance/benches/text_encoding.moss: MOSS_COMPILE_ERROR\|interface for text is missing or stale; run `margo build`<br>(+3 more) |
| 5 | `a257917e` | pre pkg:sdk/text: TYPE_MISMATCH\|push argument conflicts with the element type of 'parts'<br>pre pkg:sdk/text-encoding-acceptance: TYPE_MISMATCH\|push argument conflicts with the element type of 'parts'<br>post pkg:sdk/text: TYPE_MISMATCH\|push argument conflicts with the element type of 'parts'<br>(+2 more) |
| 6 | `7e049160` | pre pkg:sdk/text: TYPE_MISMATCH\|push argument conflicts with the element type of 'parts'<br>post pkg:sdk/text: TYPE_MISMATCH\|push argument conflicts with the element type of 'parts' |
| 8 | `7ba41b83` | pre pkg:sdk/text: TYPE_MISMATCH\|push argument conflicts with the element type of 'parts'<br>pre pkg:sdk/text-encoding-acceptance: TYPE_MISMATCH\|push argument conflicts with the element type of 'parts'<br>pre aux:sdk/text-encoding-acceptance/benches/case_builder.moss: MOSS_COMPILE_ERROR\|interface for text is missing or stale; run `margo build`<br>(+3 more) |
| 9 | `05834f8b` | pre pkg:sdk/text-encoding-acceptance: TYPE_MISMATCH\|push argument conflicts with the element type of 'parts'<br>pre aux:sdk/text-encoding-acceptance/benches/case_builder.moss: MOSS_COMPILE_ERROR\|interface for text is missing or stale; run `margo build`<br>post pkg:sdk/text-encoding-acceptance: TYPE_MISMATCH\|push argument conflicts with the element type of 'parts'<br>(+1 more) |
| 10 | `47d50f49` | post pkg:packages/linalg: TYPE_MISMATCH\|assignment type mismatch for binding 'ignored'<br>post pkg:packages/sparse: TYPE_MISMATCH\|assignment type mismatch for binding 'ignored' |
| 11 | `5cc8b17e` | post pkg:sdk/parse: TYPE_MISMATCH\|push argument conflicts with the element type of 'parts' |
| 12 | `1d58b910` | post pkg:sdk/fmt: TYPE_MISMATCH\|push argument conflicts with the element type of 'parts' |
| 13 | `c20ef73c` | post pkg:sdk/json: TYPE_MISMATCH\|push argument conflicts with the element type of 'parts' |
| 14 | `048b46c1` | pre pkg:sdk/fmt: TYPE_MISMATCH\|push argument conflicts with the element type of 'parts'<br>pre pkg:sdk/json: TYPE_MISMATCH\|push argument conflicts with the element type of 'parts'<br>post pkg:sdk/fmt: TYPE_MISMATCH\|push argument conflicts with the element type of 'parts'<br>(+1 more) |
| 15 | `ac328671` | post pkg:sdk/csv: TYPE_MISMATCH\|push argument conflicts with the element type of 'parts' |
| 16 | `234ff9e2` | pre pkg:packages/linalg: TYPE_MISMATCH\|assignment type mismatch for binding 'ignored'<br>pre pkg:packages/sparse: TYPE_MISMATCH\|assignment type mismatch for binding 'ignored'<br>post pkg:packages/linalg: TYPE_MISMATCH\|assignment type mismatch for binding 'ignored'<br>(+1 more) |
| 17 | `6ce098af` | post pkg:sdk/data-acceptance: TYPE_MISMATCH\|push argument conflicts with the element type of 'parts'<br>post aux:sdk/data-acceptance/benches/data_benchmarks.moss: MOSS_COMPILE_ERROR\|interface for csv is missing or stale; run `margo build` |
| 18 | `bff4f72a` | post pkg:sdk/path: TYPE_MISMATCH\|push argument conflicts with the element type of 'pieces'<br>post pkg:sdk/path/examples/basic: TYPE_MISMATCH\|push argument conflicts with the element type of 'pieces' |
| 19 | `f8c14cfc` | post file:sdk/host/io/feasibility/env_probe.moss: UNKNOWN_SYMBOL_OR_TYPE\|unknown identifier 'Env'<br>post file:sdk/host/io/feasibility/process_probe.moss: UNKNOWN_SYMBOL_OR_TYPE\|unknown identifier 'Process'<br>post file:sdk/host/io/feasibility/unbounded_range_write.moss: FILEIO_UNBOUNDED_REQUEST\|FileIO write payload must have a statically provable finite bound |
| 23 | `dbb3bec5` | post pkg:sdk/tables: TYPE_MISMATCH\|assignment type mismatch for binding 'ignored' |
| 27 | `0fe2e6ef` | pre pkg:packages/hash: package moss.hash has no Moss sources under <FIXTURE>/packages/hash/src<br>post pkg:packages/hash: TYPE_MISMATCH\|String find expects string<br>post aux:packages/hash/benches/hash_bench.moss: TYPE_MISMATCH\|String find expects string |
| 28 | `7f6cd553` | pre pkg:packages/compress: package moss.compress has no Moss sources under <FIXTURE>/packages/compress/src |
| 30 | `7773a300` | pre pkg:sdk/tables: TYPE_MISMATCH\|assignment type mismatch for binding 'ignored'<br>post pkg:sdk/tables: TYPE_MISMATCH\|assignment type mismatch for binding 'ignored' |
| 32 | `8d8712a8` | post pkg:sdk/cbor: UNKNOWN_SYMBOL_OR_TYPE\|unknown local function 'cbor_test__same_ints'; hint: declared in tests/cbor_test.moss; move it into a test module and |
| 33 | `48e36944` | pre pkg:sdk/tables: TYPE_MISMATCH\|assignment type mismatch for binding 'ignored'<br>post pkg:sdk/tables: TYPE_MISMATCH\|push argument conflicts with the element type of 'parts' |
| 34 | `b2f09d61` | pre pkg:packages/regex: package moss.regex has no Moss sources under <FIXTURE>/packages/regex/src<br>post pkg:packages/regex: TYPE_MISMATCH\|argument 6 to function 'regex__add_tok' requires 'string' for 'esc_ch'<br>post aux:packages/regex/benches/regex_bench.moss: TYPE_MISMATCH\|argument 6 to function 'regex__add_tok' requires 'string' for 'esc_ch' |
| 35 | `40d4ae20` | pre pkg:packages/data_utils_integration: TYPE_MISMATCH\|String find expects string<br>post pkg:packages/data_utils_integration: TYPE_MISMATCH\|String find expects string |
| 36 | `cdcadb26` | post pkg:sdk/advanced-data-acceptance: TYPE_MISMATCH\|push argument conflicts with the element type of 'parts'<br>post aux:sdk/advanced-data-acceptance/fixtures/source_free_consumer.moss: UNKNOWN_SYMBOL_OR_TYPE\|unknown identifier 'ndarray' |
| 37 | `6ef17f1c` | pre pkg:sdk/data-acceptance: TYPE_MISMATCH\|push argument conflicts with the element type of 'parts'<br>pre pkg:sdk/fmt: TYPE_MISMATCH\|push argument conflicts with the element type of 'parts'<br>pre pkg:sdk/json: TYPE_MISMATCH\|push argument conflicts with the element type of 'parts'<br>(+3 more) |
| 38 | `a50a8342` | pre pkg:packages/hash: TYPE_MISMATCH\|String find expects string<br>pre pkg:packages/regex: TYPE_MISMATCH\|argument 6 to function 'regex__add_tok' requires 'string' for 'esc_ch'<br>post pkg:packages/hash: TYPE_MISMATCH\|String find expects string<br>(+1 more) |
| 39 | `3ea10923` | pre pkg:sdk/advanced-data-acceptance: TYPE_MISMATCH\|push argument conflicts with the element type of 'parts'<br>pre aux:sdk/advanced-data-acceptance/fixtures/source_free_consumer.moss: UNKNOWN_SYMBOL_OR_TYPE\|unknown identifier 'ndarray'<br>post pkg:sdk/advanced-data-acceptance: TYPE_MISMATCH\|push argument conflicts with the element type of 'parts'<br>(+1 more) |
| 40 | `4068976a` | pre pkg:packages/regex: TYPE_MISMATCH\|argument 6 to function 'regex__add_tok' requires 'string' for 'esc_ch'<br>pre aux:packages/regex/benches/regex_bench.moss: TYPE_MISMATCH\|argument 6 to function 'regex__add_tok' requires 'string' for 'esc_ch'<br>post pkg:packages/regex: TYPE_MISMATCH\|argument 6 to function 'regex__add_tok' requires 'string' for 'esc_ch'<br>(+1 more) |
| 41 | `debf99f0` | pre pkg:sdk/cbor: UNKNOWN_SYMBOL_OR_TYPE\|unknown local function 'cbor_test__same_ints'; hint: declared in tests/cbor_test.moss; move it into a test module and<br>post pkg:sdk/cbor: UNKNOWN_SYMBOL_OR_TYPE\|unknown local function 'cbor_test__same_ints'; hint: declared in tests/cbor_test.moss; move it into a test module and |
| 42 | `a6ecc79f` | pre pkg:sdk/cbor: UNKNOWN_SYMBOL_OR_TYPE\|unknown local function 'cbor_test__same_ints'; hint: declared in tests/cbor_test.moss; move it into a test module and<br>post pkg:sdk/cbor: UNKNOWN_SYMBOL_OR_TYPE\|unknown local function 'cbor_test__same_ints'; hint: declared in tests/cbor_test.moss; move it into a test module and |
| 43 | `76baf873` | pre pkg:sdk/cbor: UNKNOWN_SYMBOL_OR_TYPE\|unknown local function 'cbor_test__same_ints'; hint: declared in tests/cbor_test.moss; move it into a test module and<br>post pkg:sdk/cbor: UNKNOWN_SYMBOL_OR_TYPE\|unknown local function 'cbor_test__same_ints'; hint: declared in tests/cbor_test.moss; move it into a test module and |
| 44 | `58340e63` | pre pkg:sdk/advanced-data-acceptance: TYPE_MISMATCH\|push argument conflicts with the element type of 'parts'<br>pre aux:sdk/advanced-data-acceptance/fixtures/source_free_consumer.moss: UNKNOWN_ERROR_TYPE\|unknown error enum 'ndarray.NdArrayError'<br>post pkg:sdk/advanced-data-acceptance: TYPE_MISMATCH\|push argument conflicts with the element type of 'parts'<br>(+1 more) |
| 45 | `7bf962d2` | pre pkg:sdk/data-acceptance: TYPE_MISMATCH\|push argument conflicts with the element type of 'parts'<br>pre pkg:sdk/fmt: TYPE_MISMATCH\|push argument conflicts with the element type of 'parts'<br>pre pkg:sdk/json: TYPE_MISMATCH\|push argument conflicts with the element type of 'parts'<br>(+4 more) |
| 46 | `f272af9c` | pre pkg:sdk/path: TYPE_MISMATCH\|Char and String are distinct types and cannot be compared with '==' or '!='; use a Char literal such as 'x', or c.to_string() <br>pre pkg:sdk/tables: TYPE_MISMATCH\|Char and String are distinct types and cannot be compared with '==' or '!='; use a Char literal such as 'x', or c.to_string() <br>pre aux:sdk/text-encoding-acceptance/benches/case_builder.moss: TEST_ASSERTION_CONFIGURATION_ERROR\|assertEqual operands have types 'char' and 'string'<br>(+2 more) |
| 47 | `dd45e307` | pre pkg:packages/hash: TYPE_MISMATCH\|String find expects string<br>pre pkg:packages/regex: TYPE_MISMATCH\|argument 6 to function 'regex__add_tok' requires 'string' for 'esc_ch' |
| 48 | `bb4b3409` | pre pkg:packages/time: MOSS_COMPILE_ERROR\|function 'time__pad_digit' returns 'char' but is annotated 'string' |
| 49 | `c0ff87fd` | pre aux:sdk/advanced-data-acceptance/fixtures/source_free_consumer.moss: UNKNOWN_ERROR_TYPE\|unknown error enum 'ndarray.NdArrayError'<br>post aux:sdk/advanced-data-acceptance/fixtures/source_free_consumer.moss: UNKNOWN_ERROR_TYPE\|unknown error enum 'ndarray.NdArrayError' |
| 50 | `4601a468` | pre pkg:sdk/cbor: UNKNOWN_SYMBOL_OR_TYPE\|unknown local function 'cbor_test__same_ints'; hint: declared in tests/cbor_test.moss; move it into a test module and |

## Summary churn (does an edit move function summaries, and others' summaries?)

Commits: 50 replayed; 21 answered the queries on every touched unit on both sides (29 did not: the tree does not check today). Of the 21: 8 add a package or give a scaffold its first source (every function is added: nothing can change), 4 change nothing under any package's src (tests/benches/fixtures only: vacuous, the churn pass reads package src), 5 only add functions, and **4 change the text of an existing function: the informative edits.**

**Headline (informative edits only):** any function summary changed in 4 of 4 (95% Wilson 51-100%); an edited function's own summary changed in 4 of 4; another, unedited function's summary changed (ripple) in 2 of 4 (95% Wilson 15-85%). 57 functions were edited, 37 of them changed summary, and 4 functions that were NOT edited changed summary. Own-changed functions by commit: {2: 1, 10: 34, 16: 1, 41: 1}.

Secondary figure, over all 13 analysable commits that are not new packages (diluted by the vacuous and addition-only commits):

| Question | Commits | Share | 95% Wilson |
|---|---:|---:|---|
| any function summary changed | 4 / 13 | 31% | 13-58% |
| an edited function's own summary changed | 4 / 13 | 31% | 13-58% |
| some other (not edited) function's summary changed | 2 / 13 | 15% | 4-42% |
| no summary changed at all | 9 / 13 | 69% | |

By category over those commits (commits with at least one changed function in it; any / edited / other):

| Category | any function | edited function | other function (ripple) |
|---|---:|---:|---:|
| types | 1 / 13 | 1 / 13 | 0 / 13 |
| access | 1 / 13 | 1 / 13 | 0 / 13 |
| raises | 4 / 13 | 4 / 13 | 2 / 13 |
| effects | 1 / 13 | 1 / 13 | 0 / 13 |

Commit classes (commit numbers): informative edits [2, 10, 16, 41]; additions only [20, 25, 26, 30, 32]; tests/benches/fixtures only [42, 43, 49, 50]; new package or first source [7, 21, 22, 23, 24, 28, 29, 31].

Coverage: of the 22 commits that modify an existing source file of an existing package, 9 are analysable today. Functions whose summary changed per analysable non-new commit: p50 0.0, p95 16.2, max 36, mean 3.2.

Per commit:

| # | Commit | Fns pre | Fns post | Added | Removed | Edited | Summary changed | Own | Other | Categories |
|---:|---|---:|---:|---:|---:|---:|---:|---:|---:|---|
| 1 | `a25a6fe1` | unavailable | | | | | | | | |
| 2 | `026db414` | 7 | 6 | 0 | 1 | 3 | 1 | 1 | 0 | raises |
| 3 | `4bae0863` | unavailable | | | | | | | | |
| 4 | `eb09e421` | unavailable | | | | | | | | |
| 5 | `a257917e` | unavailable | | | | | | | | |
| 6 | `7e049160` | unavailable | | | | | | | | |
| 7 | `67b7d9b7` | 0 | 81 | 81 | 0 | 0 | 0 | 0 | 0 | - |
| 8 | `7ba41b83` | unavailable | | | | | | | | |
| 9 | `05834f8b` | unavailable | | | | | | | | |
| 10 | `47d50f49` | 78 | 78 | 2 | 2 | 46 | 36 | 34 | 2 | access, effects, raises, types |
| 11 | `5cc8b17e` | unavailable | | | | | | | | |
| 12 | `1d58b910` | unavailable | | | | | | | | |
| 13 | `c20ef73c` | unavailable | | | | | | | | |
| 14 | `048b46c1` | unavailable | | | | | | | | |
| 15 | `ac328671` | unavailable | | | | | | | | |
| 16 | `234ff9e2` | 78 | 79 | 1 | 0 | 6 | 1 | 1 | 0 | raises |
| 17 | `6ce098af` | unavailable | | | | | | | | |
| 18 | `bff4f72a` | unavailable | | | | | | | | |
| 19 | `f8c14cfc` | unavailable | | | | | | | | |
| 20 | `4d04481f` | 5 | 6 | 1 | 0 | 0 | 0 | 0 | 0 | - |
| 21 | `31429002` | 0 | 24 | 24 | 0 | 0 | 0 | 0 | 0 | - |
| 22 | `2dfdd86c` | 0 | 56 | 56 | 0 | 0 | 0 | 0 | 0 | - |
| 23 | `dbb3bec5` | 0 | 52 | 52 | 0 | 0 | 0 | 0 | 0 | - |
| 24 | `0dfbcd79` | 0 | 71 | 71 | 0 | 0 | 0 | 0 | 0 | - |
| 25 | `99fc7fc8` | 56 | 112 | 56 | 0 | 0 | 0 | 0 | 0 | - |
| 26 | `0d578424` | 24 | 47 | 23 | 0 | 0 | 0 | 0 | 0 | - |
| 27 | `0fe2e6ef` | unavailable | | | | | | | | |
| 28 | `7f6cd553` | 0 | 9 | 9 | 0 | 0 | 0 | 0 | 0 | - |
| 29 | `b9c6ea40` | 0 | 77 | 77 | 0 | 0 | 0 | 0 | 0 | - |
| 30 | `7773a300` | 52 | 103 | 51 | 0 | 0 | 0 | 0 | 0 | - |
| 31 | `b4271cdc` | 47 | 49 | 2 | 0 | 0 | 0 | 0 | 0 | - |
| 32 | `8d8712a8` | 77 | 98 | 21 | 0 | 0 | 0 | 0 | 0 | - |
| 33 | `48e36944` | unavailable | | | | | | | | |
| 34 | `b2f09d61` | unavailable | | | | | | | | |
| 35 | `40d4ae20` | unavailable | | | | | | | | |
| 36 | `cdcadb26` | unavailable | | | | | | | | |
| 37 | `6ef17f1c` | unavailable | | | | | | | | |
| 38 | `a50a8342` | unavailable | | | | | | | | |
| 39 | `3ea10923` | unavailable | | | | | | | | |
| 40 | `4068976a` | unavailable | | | | | | | | |
| 41 | `debf99f0` | 98 | 98 | 0 | 0 | 2 | 3 | 1 | 2 | raises |
| 42 | `a6ecc79f` | 98 | 98 | 0 | 0 | 0 | 0 | 0 | 0 | - |
| 43 | `76baf873` | 98 | 98 | 0 | 0 | 0 | 0 | 0 | 0 | - |
| 44 | `58340e63` | unavailable | | | | | | | | |
| 45 | `7bf962d2` | unavailable | | | | | | | | |
| 46 | `f272af9c` | unavailable | | | | | | | | |
| 47 | `dd45e307` | unavailable | | | | | | | | |
| 48 | `bb4b3409` | unavailable | | | | | | | | |
| 49 | `c0ff87fd` | 13 | 13 | 0 | 0 | 0 | 0 | 0 | 0 | - |
| 50 | `4601a468` | 98 | 98 | 0 | 0 | 0 | 0 | 0 | 0 | - |

Unavailable (queries could not run on at least one side):

- `b6_01_a25a6fe1`: pre packages/time/src/main.moss: symbols: MOSS_COMPILE_ERROR: function 'time__pad_digit' returns 'char' but is annotated 'string'; post packages/time/src/main.m
- `b6_03_4bae0863`: pre sdk/text-encoding-acceptance/src/main.moss: symbols: MOSS_COMPILE_ERROR: interface for text is missing or stale; run `margo build`; pre sdk/text/src/text.mo
- `b6_04_eb09e421`: pre sdk/text-encoding-acceptance/src/main.moss: symbols: MOSS_COMPILE_ERROR: interface for text is missing or stale; run `margo build`; pre sdk/text/src/text.mo
- `b6_05_a257917e`: pre sdk/text-encoding-acceptance/src/main.moss: symbols: MOSS_COMPILE_ERROR: interface for text is missing or stale; run `margo build`; pre sdk/text/src/text.mo
- `b6_06_7e049160`: pre sdk/text/src/text.moss: symbols: TYPE_MISMATCH: push argument conflicts with the element type of 'parts'; post sdk/text/src/text.moss: symbols: TYPE_MISMATC
- `b6_08_7ba41b83`: pre sdk/text-encoding-acceptance/src/main.moss: symbols: MOSS_COMPILE_ERROR: interface for text is missing or stale; run `margo build`; pre sdk/text/src/text.mo
- `b6_09_05834f8b`: pre sdk/text-encoding-acceptance/src/main.moss: symbols: MOSS_COMPILE_ERROR: interface for text is missing or stale; run `margo build`; post sdk/text-encoding-a
- `b6_11_5cc8b17e`: post sdk/parse/src/parse.moss: symbols: MOSS_COMPILE_ERROR: interface for text is missing or stale; run `margo build`
- `b6_12_1d58b910`: post sdk/fmt/src/fmt.moss: symbols: MOSS_COMPILE_ERROR: interface for text is missing or stale; run `margo build`
- `b6_13_c20ef73c`: post sdk/json/src/json.moss: symbols: MOSS_COMPILE_ERROR: interface for fmt is missing or stale; run `margo build`
- `b6_14_048b46c1`: pre sdk/fmt/src/fmt.moss: symbols: MOSS_COMPILE_ERROR: interface for text is missing or stale; run `margo build`; pre sdk/json/src/json.moss: symbols: MOSS_COMP
- `b6_15_ac328671`: post sdk/csv/src/csv.moss: symbols: MOSS_COMPILE_ERROR: interface for fmt is missing or stale; run `margo build`
- `b6_17_6ce098af`: post sdk/data-acceptance/src/data_bench_support.moss: symbols: MOSS_COMPILE_ERROR: interface for csv is missing or stale; run `margo build`
- `b6_18_bff4f72a`: post sdk/path/examples/basic/src/main.moss: symbols: MOSS_COMPILE_ERROR: interface for path is missing or stale; run `margo build`; post sdk/path/src/path.moss:
- `b6_19_f8c14cfc`: post sdk/host/io/feasibility/env_probe.moss: symbols: UNKNOWN_SYMBOL_OR_TYPE: unknown identifier 'Env'; post sdk/host/io/feasibility/process_probe.moss: symbols
- `b6_27_0fe2e6ef`: post packages/hash/src/main.moss: symbols: TYPE_MISMATCH: String find expects string
- `b6_33_48e36944`: post sdk/tables/src/csv_adapter.moss: symbols: MOSS_COMPILE_ERROR: interface for csv is missing or stale; run `margo build`; post sdk/tables/src/tables.moss: sy
- `b6_34_b2f09d61`: post packages/regex/src/main.moss: symbols: TYPE_MISMATCH: argument 6 to function 'regex__add_tok' requires 'string' for 'esc_ch'
- `b6_35_40d4ae20`: post packages/data_utils_integration/src/main.moss: symbols: MOSS_COMPILE_ERROR: interface for hash is missing or stale; run `margo build`
- `b6_36_cdcadb26`: post sdk/advanced-data-acceptance/src/main.moss: symbols: MOSS_COMPILE_ERROR: interface for csv is missing or stale; run `margo build`
- `b6_37_6ef17f1c`: pre sdk/data-acceptance/src/data_bench_support.moss: symbols: MOSS_COMPILE_ERROR: interface for csv is missing or stale; run `margo build`; pre sdk/fmt/src/fmt.
- `b6_38_a50a8342`: pre packages/hash/src/main.moss: symbols: TYPE_MISMATCH: String find expects string; pre packages/regex/src/main.moss: symbols: TYPE_MISMATCH: argument 6 to fun
- `b6_39_3ea10923`: pre sdk/advanced-data-acceptance/src/main.moss: symbols: MOSS_COMPILE_ERROR: interface for csv is missing or stale; run `margo build`; post sdk/advanced-data-ac
- `b6_40_4068976a`: pre packages/regex/src/main.moss: symbols: TYPE_MISMATCH: argument 6 to function 'regex__add_tok' requires 'string' for 'esc_ch'; post packages/regex/src/main.m
- `b6_44_58340e63`: pre sdk/advanced-data-acceptance/src/main.moss: symbols: MOSS_COMPILE_ERROR: interface for csv is missing or stale; run `margo build`; post sdk/advanced-data-ac
- `b6_45_7bf962d2`: pre sdk/data-acceptance/src/data_bench_support.moss: symbols: MOSS_COMPILE_ERROR: interface for csv is missing or stale; run `margo build`; pre sdk/fmt/src/fmt.
- `b6_46_f272af9c`: pre sdk/path/src/path.moss: symbols: TYPE_MISMATCH: Char and String are distinct types and cannot be compared with '==' or '!='; use a Char literal such as 'x',
- `b6_47_dd45e307`: pre packages/hash/src/main.moss: symbols: TYPE_MISMATCH: String find expects string; pre packages/regex/src/main.moss: symbols: TYPE_MISMATCH: argument 6 to fun
- `b6_48_bb4b3409`: pre packages/time/src/main.moss: symbols: MOSS_COMPILE_ERROR: function 'time__pad_digit' returns 'char' but is annotated 'string'

## 2. Correctness gate (reference vs candidate)

**Result: PASS** over 2 fixtures. Reference: `/home/owner/daji/code/moss2/build/compiler/moss-opt`. Candidate: `/home/owner/daji/code/moss2/build/compiler/moss-opt` via built-in cold command.

The gate requires candidate verdict == reference verdict and reference == manifest (status, expected exit code, no timeout). A candidate that differs from the manifest is annotated IMPROVED/REGRESSED.

| Fixture | Status | Verdicts identical | Reference matches manifest | Candidate note |
|---|---|---|---|---|
| `b4_floor_file` | pass | yes | yes |  |
| `b4_floor_pkg` | pass | yes | yes |  |

Edit sequences (every check step compared; reference == step expectation). margo's `checked` / `skipped` package lists are volatile here (a client may legitimately answer a repeat from a store) and shown as information only; everything else, including diagnostics and exit codes, is strict:

| Scenario | Steps identical | Reference matches expectation | checked/skipped differ (info) |
|---|---|---|---|
| `b6_01_a25a6fe1` | yes | yes | no |
| `b6_02_026db414` | yes | yes | no |
| `b6_03_4bae0863` | yes | yes | no |
| `b6_04_eb09e421` | yes | yes | no |
| `b6_05_a257917e` | yes | yes | no |
| `b6_06_7e049160` | yes | yes | no |
| `b6_07_67b7d9b7` | yes | yes | no |
| `b6_08_7ba41b83` | yes | yes | no |
| `b6_09_05834f8b` | yes | yes | no |
| `b6_10_47d50f49` | yes | yes | no |
| `b6_11_5cc8b17e` | yes | yes | no |
| `b6_12_1d58b910` | yes | yes | no |
| `b6_13_c20ef73c` | yes | yes | no |
| `b6_14_048b46c1` | yes | yes | no |
| `b6_15_ac328671` | yes | yes | no |
| `b6_16_234ff9e2` | yes | yes | no |
| `b6_17_6ce098af` | yes | yes | no |
| `b6_18_bff4f72a` | yes | yes | no |
| `b6_19_f8c14cfc` | yes | yes | no |
| `b6_20_4d04481f` | yes | yes | no |
| `b6_21_31429002` | yes | yes | no |
| `b6_22_2dfdd86c` | yes | yes | no |
| `b6_23_dbb3bec5` | yes | yes | no |
| `b6_24_0dfbcd79` | yes | yes | no |
| `b6_25_99fc7fc8` | yes | yes | no |
| `b6_26_0d578424` | yes | yes | no |
| `b6_27_0fe2e6ef` | yes | yes | no |
| `b6_28_7f6cd553` | yes | yes | no |
| `b6_29_b9c6ea40` | yes | yes | no |
| `b6_30_7773a300` | yes | yes | no |
| `b6_31_b4271cdc` | yes | yes | no |
| `b6_32_8d8712a8` | yes | yes | no |
| `b6_33_48e36944` | yes | yes | no |
| `b6_34_b2f09d61` | yes | yes | no |
| `b6_35_40d4ae20` | yes | yes | no |
| `b6_36_cdcadb26` | yes | yes | no |
| `b6_37_6ef17f1c` | yes | yes | no |
| `b6_38_a50a8342` | yes | yes | no |
| `b6_39_3ea10923` | yes | yes | no |
| `b6_40_4068976a` | yes | yes | no |
| `b6_41_debf99f0` | yes | yes | no |
| `b6_42_a6ecc79f` | yes | yes | no |
| `b6_43_76baf873` | yes | yes | no |
| `b6_44_58340e63` | yes | yes | no |
| `b6_45_7bf962d2` | yes | yes | no |
| `b6_46_f272af9c` | yes | yes | no |
| `b6_47_dd45e307` | yes | yes | no |
| `b6_48_bb4b3409` | yes | yes | no |
| `b6_49_c0ff87fd` | yes | yes | no |
| `b6_50_4601a468` | yes | yes | no |

B6 reference compiler sha256 `e9a22cf0a602`: the pinned binary the expectations were recorded with.

Reference and candidate are the same compiler today, so this pass only shows the gate is deterministic and wired; it becomes meaningful once the candidate differs.

## 3. Metrics M1 / M2 (sdk/ and packages/)

Not run.

## 4. Caveats

- Cold mode starts a fresh process (package fixtures: `margo --check build` from an empty build directory) per rep; no daemon, no persistent store.
- Wall times for sub-10 ms single-file fixtures are dominated by process startup and are noisy on WSL2; compare medians and ranges, not single digits.
- Counters come from a separate run with `MOSS_PROFILE_COMPILER` enabled, so they never perturb the timed reps.
- Counters are `n/a` for warm mode: a future client is not required to expose them.
