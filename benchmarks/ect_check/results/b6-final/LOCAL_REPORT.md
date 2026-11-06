# ECT Check Benchmark - Local Report

**Suite:** `moss_ect_check` 0.4.0 (2 fixtures, seed 20261105)  
**Run:** `20261106-072632` (2026-11-06T07:44:30-0800)  
**Compiler:** `/home/owner/daji/code/moss2/build/compiler/moss-opt`  
**Compiler SHA256:** `e9a22cf0a6029d31b617baf863c5c1e866a3641a22c38bb1cd1e78c31bfe4d36`  
**Build flavor:** `opt`  
**Repo:** `phase28-5-bench` @ `0fb7557bed22`  
**Platform:** `Linux-6.18.33.2-microsoft-standard-WSL2-x86_64-with-glibc2.39` (16 CPUs)  
**Reps:** 5 timed runs per fixture/mode (median reported), plus one separate counter run (counters are not collected during timed runs)  
**Warm client:** not configured  

**Noise probe:** loadavg start [0.24755859375, 1.70654296875, 3.46875], end [1.484375, 2.18017578125, 3.107421875]; calibration `b2_k04` median wall 540.3 ms at start, 533.4 ms after the timed loop (drift 1.3%).  

## 1. Timing, memory and compiler counters

Wall, CPU and RSS are medians over the timed reps. CPU is user+system of the whole process tree. RSS is the largest single process in the tree (not a sum). Counters are summed over the checker runs one command starts.

### Mode: cold

| Fixture | Kind | Status | Wall ms | CPU ms | RSS KiB | Checker runs | Fns analyzed | Spec evals | Spec hits | Spec keys |
|---|---|---|---:|---:|---:|---:|---:|---:|---:|---:|
| `b4_floor_file` | file | pass | 1.90 (1.3-1.9) | 1.34 | 9516 | 1 | 0 | 0 | 0 | 0 |
| `b4_floor_pkg` | package | pass | 47.83 (46.5-54.9) | 47.79 | 24948 | 1 | 0 | 0 | 0 | 0 |

Specialization counters: evals = `specialization_requests`, hits = `reused_specialization_results`, keys = `distinct_specialization_keys` (the compiler's own `MOSS_PROFILE` names; today's memo is in-process, so hits are intra-run reuse only).

## B6. Agent-loop replay (last 50 commits touching sdk/packages `.moss`)

Each commit's PARENT tree is materialized (`git archive`), every entry point is checked (`pre`: the baseline, cold), the commit's `.moss` / `Moss.toml` changes are applied and the same entry points are checked again (`post`: the per-edit latency; margo's package fingerprint cache skips unchanged dependencies). A phase's latency is the sum over its entry points of the median over reps. Old trees are checked with TODAY's compiler, so many legitimately fail: failing checks end at the first diagnostic and are NOT representative latencies; the populations below separate them. See README for the entry-point definition and limits.

### Latency per edit (cold)

| Population | Commits | post p50 ms | post p95 ms | post max ms | pre p50 ms | pre p95 ms |
|---|---:|---:|---:|---:|---:|---:|
| all commits (incl. failing checks) | 50 | 308.1 | 2209.6 | 2934.7 | 424.0 | 1952.9 |
| post-edit check passes | 15 | 697.3 | 2436.9 | 2934.7 | 355.9 | 1910.1 |
| post passes, package existed (pre may fail) | 9 | 382.3 | 2637.8 | 2934.7 | 258.9 | 1765.1 |
| clean edits: pre and post pass, package existed | 5 | 231.2 | 893.4 | 942.4 | 452.9 | 1907.7 |

Percentiles: linear interpolation between order statistics (`statistics.quantiles` 'inclusive' convention); with this few commits p95 sits close to the maximum.

Distribution of post-edit latency (commits per bucket):

| Bucket | all commits (incl. failing checks) | post-edit check passes | post passes, package existed (pre may fail) | clean edits: pre and post pass, package existed |
|---|---:|---:|---:|---:|
| < 50 ms | 0 | 0 | 0 | 0 |
| 50-100 ms | 7 | 1 | 1 | 1 |
| 100-250 ms | 15 | 2 | 2 | 2 |
| 250-500 ms | 7 | 3 | 2 | 0 |
| 0.5-1 s | 9 | 4 | 2 | 2 |
| 1-2 s | 8 | 2 | 0 | 0 |
| 2-5 s | 4 | 3 | 2 | 0 |
| >= 5 s | 0 | 0 | 0 | 0 |

Compiler counters of the post-edit check (median over commits of the per-commit sum over entry points; margo skips unchanged packages, so these count only what was rechecked):

| Population | Fns analyzed p50 | Source fn checks p50 | Spec evals p50 | Spec keys p50 | Checker runs p50 | Fns analyzed (sum) |
|---|---:|---:|---:|---:|---:|---:|
| all commits (incl. failing checks) | 134 | 98 | 314 | 0 | 2 | 7902 |
| post-edit check passes | 197 | 140 | 420 | 0 | 2 | 4142 |
| post passes, package existed (pre may fail) | 142 | 102 | 339 | 0 | 2 | 1441 |
| clean edits: pre and post pass, package existed | 118 | 54 | 236 | 0 | 2 | 494 |

Passing post-edit checks in which no checker ran (margo reported every package as skipped; the changed files were not package sources or tests): 1 commits (#49).

### Per commit (cold)

| # | Commit | Entries | Pre | Post | Pre ms | Post ms | Post fns analyzed | Post checker runs | Subject |
|---:|---|---:|---|---|---:|---:|---:|---:|---|
| 1 | `a25a6fe1` | 3 | fail | fail | 901.3 | 936.6 | 108 | 4 | Phase 24 Team M: Implement L7 closeout repairs and robust be |
| 2 | `026db414` | 1 | pass | pass | 258.9 | 231.2 | 15 | 2 | Optimize UTF-8 codec hot paths |
| 3 | `4bae0863` | 2 | fail | fail | 211.2 | 124.2 | n/a | 0 | Optimize text search and slicing through native primitives |
| 4 | `eb09e421` | 2 | fail | fail | 207.6 | 125.8 | n/a | 0 | Correct preconstructed text performance workloads |
| 5 | `a257917e` | 2 | fail | fail | 209.0 | 127.4 | n/a | 0 | Optimize ASCII case and characterize builders |
| 6 | `7e049160` | 2 | fail | fail | 281.9 | 287.9 | 15 | 2 | no src; Add independent Text performance validation |
| 7 | `67b7d9b7` | 4 | - | pass | n/a | 2223.6 | 1020 | 12 | new pkg; Phase 24 Team N: Complete numerical computing SDK with archi |
| 8 | `7ba41b83` | 2 | fail | fail | 219.7 | 132.5 | n/a | 0 | Add capacity-aware Text builder |
| 9 | `05834f8b` | 1 | fail | fail | 271.1 | 101.5 | n/a | 0 | no src; Correct reusable builder benchmark equivalence |
| 10 | `47d50f49` | 3 | pass | fail | 2284.9 | 1626.5 | 395 | 5 | phase24(numerics): resolve L7 review defects, expand differe |
| 11 | `5cc8b17e` | 1 | - | fail | n/a | 68.8 | 5 | 1 | new pkg; Phase 24 D1: Implement moss.parse cursor primitives and sour |
| 12 | `1d58b910` | 1 | - | fail | n/a | 70.0 | 5 | 1 | new pkg; Phase 24 D2: Implement moss.fmt formatting and escaping |
| 13 | `c20ef73c` | 1 | - | fail | n/a | 182.1 | 28 | 3 | new pkg; Phase 24 D3: Implement moss.json columnar tape representatio |
| 14 | `048b46c1` | 2 | fail | fail | 251.9 | 131.3 | n/a | 0 | Phase 24 D4: Implement JSON serialization, pretty-printing,  |
| 15 | `ac328671` | 1 | - | fail | n/a | 72.2 | 5 | 1 | new pkg; Phase 24 D5: Implement moss.csv RFC 4180 reader, writer, val |
| 16 | `234ff9e2` | 3 | fail | fail | 1874.2 | 1261.2 | 257 | 3 | phase24-numerics: final L7 closeout repairs and integration  |
| 17 | `6ce098af` | 1 | - | fail | n/a | 184.9 | 28 | 3 | new pkg; Phase 24 D6: Add cross-package data-acceptance suite, differ |
| 18 | `bff4f72a` | 4 | - | fail | n/a | 250.7 | 8 | 2 | new pkg; Add Team H path and bounded filesystem SDKs |
| 19 | `f8c14cfc` | 8 | - | fail | n/a | 264.0 | 8 | 5 | new pkg; Document host I/O service feasibility |
| 20 | `4d04481f` | 1 | pass | pass | 70.7 | 76.2 | 6 | 1 | Bound Team H filesystem reads |
| 21 | `31429002` | 1 | - | pass | n/a | 987.8 | 331 | 4 | new pkg; Implement moss.ndarray core |
| 22 | `2dfdd86c` | 1 | - | pass | n/a | 449.7 | 111 | 2 | new pkg; Implement Phase 24 priority structures |
| 23 | `dbb3bec5` | 1 | - | fail | n/a | 384.5 | 52 | 1 | new pkg; feat(tables): add typed columnar table core |
| 24 | `0dfbcd79` | 1 | - | pass | n/a | 660.9 | 167 | 3 | new pkg; Implement moss.binary primitives |
| 25 | `99fc7fc8` | 1 | pass | pass | 452.9 | 942.4 | 221 | 2 | Implement Phase 24 search structures |
| 26 | `0d578424` | 1 | pass | pass | 783.8 | 697.3 | 252 | 2 | Implement ndarray broadcasting slicing and reductions |
| 27 | `0fe2e6ef` | 1 | fail | fail | 65.4 | 130.3 | n/a | 0 | feat(hash): implement moss.hash with Adler-32, CRC-32, FNV-1 |
| 28 | `7f6cd553` | 1 | fail | pass | 65.1 | 328.4 | 35 | 2 | feat(compress): implement moss.compress with raw LZ4 blocks  |
| 29 | `b9c6ea40` | 1 | - | pass | n/a | 1494.7 | 362 | 5 | new pkg; Implement moss.cbor arena and encoder |
| 30 | `7773a300` | 1 | fail | fail | 515.3 | 1964.6 | 103 | 1 | feat(tables): add relational operations |
| 31 | `b4271cdc` | 3 | pass | pass | 1569.6 | 1765.2 | 710 | 6 | new pkg; Add higher-level ndarray examples |
| 32 | `8d8712a8` | 1 | pass | fail | 1837.2 | 1091.2 | 156 | 1 | Add iterative CBOR decoder |
| 33 | `48e36944` | 1 | fail | fail | 1714.0 | 77.1 | 5 | 1 | Add typed CSV adapters for tables |
| 34 | `b2f09d61` | 1 | fail | fail | 56.9 | 69.6 | n/a | 0 | feat(regex): implement moss.regex with non-backtracking Thom |
| 35 | `40d4ae20` | 1 | fail | fail | 262.2 | 172.5 | n/a | 0 | feat(data_utils_integration): implement cross-package pipeli |
| 36 | `cdcadb26` | 1 | - | fail | n/a | 1292.4 | 617 | 9 | new pkg; Add cross-package advanced-data acceptance |
| 37 | `6ef17f1c` | 3 | fail | fail | 311.4 | 215.9 | n/a | 0 | fix(json,fmt,diff): address L7 adversarial review findings w |
| 38 | `a50a8342` | 3 | fail | fail | 542.6 | 549.2 | 35 | 2 | fix(team-r): resolve L7 corrective review across validation, |
| 39 | `3ea10923` | 1 | fail | fail | 1471.4 | 134.1 | n/a | 0 | no src; Repair Team A Architect acceptance gaps |
| 40 | `4068976a` | 1 | fail | fail | 74.9 | 74.8 | n/a | 0 | fix(regex): repair NFA split origin propagation asymmetry an |
| 41 | `debf99f0` | 1 | fail | fail | 1125.1 | 881.8 | 156 | 1 | Fix CBOR edge ownership validation |
| 42 | `a6ecc79f` | 1 | fail | fail | 1139.2 | 887.0 | 156 | 1 | no src; Add CBOR L7 adversarial verification |
| 43 | `76baf873` | 1 | fail | fail | 1145.2 | 885.9 | 156 | 1 | no src; Repair CBOR parent-cycle verifier |
| 44 | `58340e63` | 1 | fail | fail | 1304.6 | 116.1 | n/a | 0 | no src; Exercise corrected CBOR error through ABI-v9 |
| 45 | `7bf962d2` | 6 | fail | fail | 395.2 | 2498.2 | 1224 | 16 | refactor(phase25): route Moss String lowering through MossSt |
| 46 | `f272af9c` | 3 | fail | fail | 977.1 | 1899.4 | 238 | 3 | fix(phase25): reject record ==, Char/String mixing, and juxt |
| 47 | `dd45e307` | 2 | fail | pass | 213.0 | 2192.5 | 173 | 4 | Fix SDK Char and String boundaries |
| 48 | `bb4b3409` | 1 | fail | pass | 63.2 | 382.3 | 111 | 2 | phase25.1 R4A: repair time package inventory blocker |
| 49 | `c0ff87fd` | 1 | pass | pass | 2188.7 | 130.1 | n/a | 0 | no src; phase25.1 R4B: validate advanced-data ABI-v10 |
| 50 | `4601a468` | 1 | fail | pass | 1129.7 | 2934.7 | 628 | 5 | no src; Start Phase 26.3 file-scoped test support |

### Checks that fail today (39 of 50 commits have at least one failing entry point; each is the frozen expectation, not a harness error)

| # | Commit | First failing entry points (diagnostic) |
|---:|---|---|
| 1 | `a25a6fe1` | pre pkg:packages/time: MOSS_COMPILE_ERROR\|function 'time__pad_digit' returns 'char' but is annotated 'string'<br>post pkg:packages/time: MOSS_COMPILE_ERROR\|function 'time__pad_digit' returns 'char' but is annotated 'string' |
| 3 | `4bae0863` | pre pkg:sdk/text: TYPE_MISMATCH\|push argument conflicts with the element type of 'parts'<br>pre pkg:sdk/text-encoding-acceptance: TYPE_MISMATCH\|push argument conflicts with the element type of 'parts'<br>post pkg:sdk/text: TYPE_MISMATCH\|push argument conflicts with the element type of 'parts'<br>(+1 more) |
| 4 | `eb09e421` | pre pkg:sdk/text: TYPE_MISMATCH\|push argument conflicts with the element type of 'parts'<br>pre pkg:sdk/text-encoding-acceptance: TYPE_MISMATCH\|push argument conflicts with the element type of 'parts'<br>post pkg:sdk/text: TYPE_MISMATCH\|push argument conflicts with the element type of 'parts'<br>(+1 more) |
| 5 | `a257917e` | pre pkg:sdk/text: TYPE_MISMATCH\|push argument conflicts with the element type of 'parts'<br>pre pkg:sdk/text-encoding-acceptance: TYPE_MISMATCH\|push argument conflicts with the element type of 'parts'<br>post pkg:sdk/text: TYPE_MISMATCH\|push argument conflicts with the element type of 'parts'<br>(+1 more) |
| 6 | `7e049160` | pre pkg:sdk/text: TYPE_MISMATCH\|push argument conflicts with the element type of 'parts'<br>post pkg:sdk/text: TYPE_MISMATCH\|push argument conflicts with the element type of 'parts' |
| 8 | `7ba41b83` | pre pkg:sdk/text: TYPE_MISMATCH\|push argument conflicts with the element type of 'parts'<br>pre pkg:sdk/text-encoding-acceptance: TYPE_MISMATCH\|push argument conflicts with the element type of 'parts'<br>post pkg:sdk/text: TYPE_MISMATCH\|push argument conflicts with the element type of 'parts'<br>(+1 more) |
| 9 | `05834f8b` | pre pkg:sdk/text-encoding-acceptance: TYPE_MISMATCH\|push argument conflicts with the element type of 'parts'<br>post pkg:sdk/text-encoding-acceptance: TYPE_MISMATCH\|push argument conflicts with the element type of 'parts' |
| 10 | `47d50f49` | post pkg:packages/linalg: TYPE_MISMATCH\|assignment type mismatch for binding 'ignored'<br>post pkg:packages/sparse: TYPE_MISMATCH\|assignment type mismatch for binding 'ignored' |
| 11 | `5cc8b17e` | post pkg:sdk/parse: TYPE_MISMATCH\|push argument conflicts with the element type of 'parts' |
| 12 | `1d58b910` | post pkg:sdk/fmt: TYPE_MISMATCH\|push argument conflicts with the element type of 'parts' |
| 13 | `c20ef73c` | post pkg:sdk/json: TYPE_MISMATCH\|push argument conflicts with the element type of 'parts' |
| 14 | `048b46c1` | pre pkg:sdk/fmt: TYPE_MISMATCH\|push argument conflicts with the element type of 'parts'<br>pre pkg:sdk/json: TYPE_MISMATCH\|push argument conflicts with the element type of 'parts'<br>post pkg:sdk/fmt: TYPE_MISMATCH\|push argument conflicts with the element type of 'parts'<br>(+1 more) |
| 15 | `ac328671` | post pkg:sdk/csv: TYPE_MISMATCH\|push argument conflicts with the element type of 'parts' |
| 16 | `234ff9e2` | pre pkg:packages/linalg: TYPE_MISMATCH\|assignment type mismatch for binding 'ignored'<br>pre pkg:packages/sparse: TYPE_MISMATCH\|assignment type mismatch for binding 'ignored'<br>post pkg:packages/linalg: TYPE_MISMATCH\|assignment type mismatch for binding 'ignored'<br>(+1 more) |
| 17 | `6ce098af` | post pkg:sdk/data-acceptance: TYPE_MISMATCH\|push argument conflicts with the element type of 'parts' |
| 18 | `bff4f72a` | post pkg:sdk/path: TYPE_MISMATCH\|push argument conflicts with the element type of 'pieces'<br>post pkg:sdk/path/examples/basic: TYPE_MISMATCH\|push argument conflicts with the element type of 'pieces' |
| 19 | `f8c14cfc` | post file:sdk/host/io/feasibility/env_probe.moss: UNKNOWN_SYMBOL_OR_TYPE\|unknown identifier 'Env'<br>post file:sdk/host/io/feasibility/process_probe.moss: UNKNOWN_SYMBOL_OR_TYPE\|unknown identifier 'Process'<br>post file:sdk/host/io/feasibility/unbounded_range_write.moss: FILEIO_UNBOUNDED_REQUEST\|FileIO write payload must have a statically provable finite bound |
| 23 | `dbb3bec5` | post pkg:sdk/tables: TYPE_MISMATCH\|assignment type mismatch for binding 'ignored' |
| 27 | `0fe2e6ef` | pre pkg:packages/hash: package moss.hash has no Moss sources under <FIXTURE>/packages/hash/src<br>post pkg:packages/hash: TYPE_MISMATCH\|String find expects string |
| 28 | `7f6cd553` | pre pkg:packages/compress: package moss.compress has no Moss sources under <FIXTURE>/packages/compress/src |
| 30 | `7773a300` | pre pkg:sdk/tables: TYPE_MISMATCH\|assignment type mismatch for binding 'ignored'<br>post pkg:sdk/tables: TYPE_MISMATCH\|assignment type mismatch for binding 'ignored' |
| 32 | `8d8712a8` | post pkg:sdk/cbor: UNKNOWN_SYMBOL_OR_TYPE\|unknown local function 'cbor_test__same_ints'; hint: declared in tests/cbor_test.moss; move it into a test module and |
| 33 | `48e36944` | pre pkg:sdk/tables: TYPE_MISMATCH\|assignment type mismatch for binding 'ignored'<br>post pkg:sdk/tables: TYPE_MISMATCH\|push argument conflicts with the element type of 'parts' |
| 34 | `b2f09d61` | pre pkg:packages/regex: package moss.regex has no Moss sources under <FIXTURE>/packages/regex/src<br>post pkg:packages/regex: TYPE_MISMATCH\|argument 6 to function 'regex__add_tok' requires 'string' for 'esc_ch' |
| 35 | `40d4ae20` | pre pkg:packages/data_utils_integration: TYPE_MISMATCH\|String find expects string<br>post pkg:packages/data_utils_integration: TYPE_MISMATCH\|String find expects string |
| 36 | `cdcadb26` | post pkg:sdk/advanced-data-acceptance: TYPE_MISMATCH\|push argument conflicts with the element type of 'parts' |
| 37 | `6ef17f1c` | pre pkg:sdk/data-acceptance: TYPE_MISMATCH\|push argument conflicts with the element type of 'parts'<br>pre pkg:sdk/fmt: TYPE_MISMATCH\|push argument conflicts with the element type of 'parts'<br>pre pkg:sdk/json: TYPE_MISMATCH\|push argument conflicts with the element type of 'parts'<br>(+3 more) |
| 38 | `a50a8342` | pre pkg:packages/hash: TYPE_MISMATCH\|String find expects string<br>pre pkg:packages/regex: TYPE_MISMATCH\|argument 6 to function 'regex__add_tok' requires 'string' for 'esc_ch'<br>post pkg:packages/hash: TYPE_MISMATCH\|String find expects string<br>(+1 more) |
| 39 | `3ea10923` | pre pkg:sdk/advanced-data-acceptance: TYPE_MISMATCH\|push argument conflicts with the element type of 'parts'<br>post pkg:sdk/advanced-data-acceptance: TYPE_MISMATCH\|push argument conflicts with the element type of 'parts' |
| 40 | `4068976a` | pre pkg:packages/regex: TYPE_MISMATCH\|argument 6 to function 'regex__add_tok' requires 'string' for 'esc_ch'<br>post pkg:packages/regex: TYPE_MISMATCH\|argument 6 to function 'regex__add_tok' requires 'string' for 'esc_ch' |
| 41 | `debf99f0` | pre pkg:sdk/cbor: UNKNOWN_SYMBOL_OR_TYPE\|unknown local function 'cbor_test__same_ints'; hint: declared in tests/cbor_test.moss; move it into a test module and<br>post pkg:sdk/cbor: UNKNOWN_SYMBOL_OR_TYPE\|unknown local function 'cbor_test__same_ints'; hint: declared in tests/cbor_test.moss; move it into a test module and |
| 42 | `a6ecc79f` | pre pkg:sdk/cbor: UNKNOWN_SYMBOL_OR_TYPE\|unknown local function 'cbor_test__same_ints'; hint: declared in tests/cbor_test.moss; move it into a test module and<br>post pkg:sdk/cbor: UNKNOWN_SYMBOL_OR_TYPE\|unknown local function 'cbor_test__same_ints'; hint: declared in tests/cbor_test.moss; move it into a test module and |
| 43 | `76baf873` | pre pkg:sdk/cbor: UNKNOWN_SYMBOL_OR_TYPE\|unknown local function 'cbor_test__same_ints'; hint: declared in tests/cbor_test.moss; move it into a test module and<br>post pkg:sdk/cbor: UNKNOWN_SYMBOL_OR_TYPE\|unknown local function 'cbor_test__same_ints'; hint: declared in tests/cbor_test.moss; move it into a test module and |
| 44 | `58340e63` | pre pkg:sdk/advanced-data-acceptance: TYPE_MISMATCH\|push argument conflicts with the element type of 'parts'<br>post pkg:sdk/advanced-data-acceptance: TYPE_MISMATCH\|push argument conflicts with the element type of 'parts' |
| 45 | `7bf962d2` | pre pkg:sdk/data-acceptance: TYPE_MISMATCH\|push argument conflicts with the element type of 'parts'<br>pre pkg:sdk/fmt: TYPE_MISMATCH\|push argument conflicts with the element type of 'parts'<br>pre pkg:sdk/json: TYPE_MISMATCH\|push argument conflicts with the element type of 'parts'<br>(+4 more) |
| 46 | `f272af9c` | pre pkg:sdk/path: TYPE_MISMATCH\|Char and String are distinct types and cannot be compared with '==' or '!='; use a Char literal such as 'x', or c.to_string() <br>pre pkg:sdk/tables: TYPE_MISMATCH\|Char and String are distinct types and cannot be compared with '==' or '!='; use a Char literal such as 'x', or c.to_string() <br>post pkg:sdk/tables: TYPE_MISMATCH\|assignment type mismatch for binding 'ignored' |
| 47 | `dd45e307` | pre pkg:packages/hash: TYPE_MISMATCH\|String find expects string<br>pre pkg:packages/regex: TYPE_MISMATCH\|argument 6 to function 'regex__add_tok' requires 'string' for 'esc_ch' |
| 48 | `bb4b3409` | pre pkg:packages/time: MOSS_COMPILE_ERROR\|function 'time__pad_digit' returns 'char' but is annotated 'string' |
| 50 | `4601a468` | pre pkg:sdk/cbor: UNKNOWN_SYMBOL_OR_TYPE\|unknown local function 'cbor_test__same_ints'; hint: declared in tests/cbor_test.moss; move it into a test module and |

## Summary churn (does an edit move function summaries, and others' summaries?)

Commits: 50 replayed; 21 answered the queries on every touched unit on both sides (29 did not: the tree does not check today); of those, 7 add a package (pure membership change, no before state) and **14 are edits of existing packages: the denominator below.**

| Question | Commits | Share of edits | 95% Wilson interval |
|---|---:|---:|---|
| any function summary changed (types, access, raises or effects) | 4 / 14 | 29% | 12-55% |
| an EDITED function's own summary changed | 4 / 14 | 29% | 12-55% |
| some OTHER (not edited) function's summary changed: ripple | 2 / 14 | 14% | 4-40% |
| no summary changed at all | 10 / 14 | 71% | |
| ... of which only functions were added/removed | 6 / 14 | | |

By category (commits in which at least one function's summary changed in that category):

| Category | any function | edited function | other function (ripple) |
|---|---:|---:|---:|
| types | 1 / 14 | 1 / 14 | 0 / 14 |
| access | 1 / 14 | 1 / 14 | 0 / 14 |
| raises | 4 / 14 | 4 / 14 | 2 / 14 |
| effects | 1 / 14 | 1 / 14 | 0 / 14 |

Restricted to edit commits that changed the text of at least one EXISTING function (4 of 14; the other 10 only added functions, or changed files/regions no existing function's text covers): any summary changed 4 / 4, an edited function's own summary changed 4 / 4, another function's summary changed (ripple) 2 / 4 (95% Wilson interval 15-85%). Function level over those commits: 57 functions edited, 37 of them changed summary; 4 functions that were NOT edited changed summary.


Functions whose summary changed per edit commit: p50 0.0, p95 14.5, max 36, mean 2.9.

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

Reference and candidate are the same compiler today, so this pass only shows the gate is deterministic and wired; it becomes meaningful once the candidate differs.

## 3. Metrics M1 / M2 (sdk/ and packages/)

Not run.

## 4. Caveats

- Cold mode starts a fresh process (package fixtures: `margo --check build` from an empty build directory) per rep; no daemon, no persistent store.
- Wall times for sub-10 ms single-file fixtures are dominated by process startup and are noisy on WSL2; compare medians and ranges, not single digits.
- Counters come from a separate run with `MOSS_PROFILE_COMPILER` enabled, so they never perturb the timed reps.
- Counters are `n/a` for warm mode: a future client is not required to expose them.
