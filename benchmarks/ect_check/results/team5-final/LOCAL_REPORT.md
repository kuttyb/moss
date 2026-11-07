# ECT Check Benchmark - Local Report

**Suite:** `moss_ect_check` 0.5.0 (2 fixtures, seed 20261105)  
**Run:** `20261106-190454` (2026-11-06T19:12:50-0800)  
**Compiler:** `/home/owner/daji/code/moss2/build/compiler/moss-opt`  
**Compiler SHA256:** `e9a22cf0a6029d31b617baf863c5c1e866a3641a22c38bb1cd1e78c31bfe4d36`  
**Build flavor:** `opt`  
**Repo:** `phase28-5-bench` @ `3dd88270d656`  
**Platform:** `Linux-6.18.33.2-microsoft-standard-WSL2-x86_64-with-glibc2.39` (16 CPUs)  
**Reps:** 5 timed runs per fixture/mode (median reported), plus one separate counter run (counters are not collected during timed runs)  
**Warm client:** not configured  

**Noise probe:** loadavg start [0.11572265625, 1.03125, 2.142578125], end [1.173828125, 1.119140625, 1.8818359375]; calibration `b2_k04` median wall 544.9 ms at start, 542.2 ms after the timed loop (drift 0.5%).  

## 1. Timing, memory and compiler counters

Wall, CPU and RSS are medians over the timed reps. CPU is user+system of the whole process tree. RSS is the largest single process in the tree (not a sum). Counters are summed over the checker runs one command starts.

### Mode: cold

| Fixture | Kind | Status | Wall ms | CPU ms | RSS KiB | Checker runs | Fns analyzed | Spec evals | Spec hits | Spec keys |
|---|---|---|---:|---:|---:|---:|---:|---:|---:|---:|
| `b3_dag_d20_w4_k02` | package | pass | 441.32 (425.3-456.0) | 440.77 | 24948 | 1 | 81 | 162 | 0 | 162 |
| `b3_fanin_d50_w4_k02` | package | pass | 672.40 (646.8-813.7) | 672.07 | 24952 | 1 | 55 | 116 | 6 | 110 |

Specialization counters: evals = `specialization_requests`, hits = `reused_specialization_results`, keys = `distinct_specialization_keys` (the compiler's own `MOSS_PROFILE` names; today's memo is in-process, so hits are intra-run reuse only).

## 1b. Generic-DAG families (Phase 28.5 Team 2)

Cold mode only. Counters come from the separate counter run. For package fixtures (B1, fan-in pkg) the provider `lib` is built first (`margo --check build`, its own series below) and the consumer is then measured with an up-to-date provider `.mossi`, so consumer numbers exclude provider build work.

### Shape check: measured vs predicted

Predictions are recomputed here from the fixture SOURCE (callables = top-level `fn` lines except `main`; K = top-level `type` lines), not taken from the generator: functions analyzed = callables; distinct specialization keys = callables x max(K,1). `Meta` flags a disagreement with the generator's manifest metadata. Reqs/hits predictions (fan-in only) come from the manifest. `Local complete` = internal functions of the package's own source whose local summary is `complete` (checker fact `ect_m2_internal_complete`), out of all internal functions: a fact store that trusts only complete summaries can skip no more than this.

| Fixture | Status | Callables (src) | K (src) | Fns pred | Fns meas | Fns delta | Keys pred | Keys meas | Keys delta | Reqs pred/meas | Hits pred/meas | Local complete | Meta |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---|---|---|---|
| `b3_dag_d20_w4_k02` | pass | 81 | 2 | 81 | 81 | +0 | 162 | 162 | +0 | - | - | 0/81 | ok |
| `b3_fanin_d50_w4_k02` | pass | 55 | 2 | 55 | 55 | +0 | 110 | 110 | +0 | - | - | 0/55 | ok |

## B3. Inference Ripple: edits at a leaf (cold process per request)

Each sequence runs `check` (empty build dir: the unedited baseline), applies one frozen leaf patch, `check` (edit), reverts it (byte-identical tree verified), `check` (revert). The package is checked by `margo --check build`; margo itself skips an unchanged package, so every edit here is a whole-package recheck by construction. Counters come from a separate run of the same sequence. 'Ideal' is the callable count a perfect incremental checker would recheck, derived from the SOURCE call graph (reverse reachability from the edited callable) and the edit class, not from generator metadata; 'observed' is the number of callables whose queried facts (`moss inspect`) changed.

### b3_dag_d20_w4_k02

| Edit | Class | Step | Wall ms | CPU ms | Fns analyzed | Source fn checks | Spec evals | Spec keys | Checker runs | Callables (src) | Ideal recheck | Ideal spec keys | Observed summary change | Analyzed / ideal |
|---|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| (none) | unedited baseline | base | 440.8 | 440.8 | 81 | 81 | 162 | 162 | 1 | 81 | n/a | n/a | n/a | n/a |
| `a` | local | edit | 460.1 | 459.8 | 81 | 81 | 162 | 162 | 1 | 81 | 1 | 2 | 0 | 81.0x |
| `a` | local | revert | 440.2 | 438.7 | 81 | 81 | 162 | 162 | 1 | 81 |  |  |  |  |
| `b` | cone | edit | 427.7 | 427.7 | 81 | 81 | 162 | 162 | 1 | 81 | 21 | 42 | 21 | 3.9x |
| `b` | cone | revert | 434.0 | 433.6 | 81 | 81 | 162 | 162 | 1 | 81 |  |  |  |  |
| `b2` | cone | edit | 444.5 | 443.0 | 81 | 81 | 162 | 162 | 1 | 81 | 21 | 42 | 21 | 3.9x |
| `b2` | cone | revert | 439.0 | 438.7 | 81 | 81 | 162 | 162 | 1 | 81 |  |  |  |  |
| `c` | local | edit | 436.3 | 435.9 | 81 | 81 | 162 | 162 | 1 | 81 | 1 | 2 | 0 | 81.0x |
| `c` | local | revert | 440.5 | 440.0 | 81 | 81 | 162 | 162 | 1 | 81 |  |  |  |  |
| `d` | none | edit | 437.4 | 437.0 | 81 | 81 | 162 | 162 | 1 | 81 | 0 | 0 | 0 | n/a (ideal 0) |
| `d` | none | revert | 436.4 | 436.2 | 81 | 81 | 162 | 162 | 1 | 81 |  |  |  |  |
| `e` | identity | edit | 424.1 | 423.8 | 81 | 81 | 162 | 162 | 1 | 81 | 0 | 0 | 0 | n/a (ideal 0) |
| `e` | identity | revert | 442.8 | 442.4 | 81 | 81 | 162 | 162 | 1 | 81 |  |  |  |  |

### b3_fanin_d50_w4_k02

| Edit | Class | Step | Wall ms | CPU ms | Fns analyzed | Source fn checks | Spec evals | Spec keys | Checker runs | Callables (src) | Ideal recheck | Ideal spec keys | Observed summary change | Analyzed / ideal |
|---|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| (none) | unedited baseline | base | 668.5 | 669.0 | 55 | 55 | 116 | 110 | 1 | 55 | n/a | n/a | n/a | n/a |
| `a` | local | edit | 671.0 | 670.7 | 55 | 55 | 116 | 110 | 1 | 55 | 1 | 2 | 0 | 55.0x |
| `a` | local | revert | 667.1 | 667.1 | 55 | 55 | 116 | 110 | 1 | 55 |  |  |  |  |
| `b` | cone | edit | 678.7 | 678.4 | 55 | 55 | 116 | 110 | 1 | 55 | 55 | 110 | 55 | 1.0x |
| `b` | cone | revert | 665.6 | 665.6 | 55 | 55 | 116 | 110 | 1 | 55 |  |  |  |  |
| `b2` | cone | edit | 660.9 | 660.8 | 55 | 55 | 116 | 110 | 1 | 55 | 55 | 110 | 55 | 1.0x |
| `b2` | cone | revert | 663.5 | 663.0 | 55 | 55 | 116 | 110 | 1 | 55 |  |  |  |  |
| `c` | local | edit | 642.7 | 642.7 | 55 | 55 | 116 | 110 | 1 | 55 | 1 | 2 | 0 | 55.0x |
| `c` | local | revert | 675.0 | 674.9 | 55 | 55 | 116 | 110 | 1 | 55 |  |  |  |  |
| `d` | none | edit | 658.3 | 657.6 | 55 | 55 | 116 | 110 | 1 | 55 | 0 | 0 | 0 | n/a (ideal 0) |
| `d` | none | revert | 667.2 | 667.1 | 55 | 55 | 116 | 110 | 1 | 55 |  |  |  |  |
| `e` | identity | edit | 668.4 | 668.2 | 55 | 55 | 116 | 110 | 1 | 55 | 0 | 0 | 0 | n/a (ideal 0) |
| `e` | identity | revert | 663.0 | 662.5 | 55 | 55 | 116 | 110 | 1 | 55 |  |  |  |  |

### B3 query evidence (what the compiler says changed)

| Scenario | Edit class | Evidence |
|---|---|---|
| `b3_dag_a` | local | body: n019_0; summary facts: none; provenance only: none; @line identities: none |
| `b3_dag_b` | cone | body: n019_0; summary facts: n019_0 .. root (21 callables); provenance only: none; @line identities: none |
| `b3_dag_b2` | cone | body: n019_0 .. root (21 callables); summary facts: n019_0 .. root (21 callables); provenance only: none; @line identities: none |
| `b3_dag_c` | local | body: n019_0; summary facts: none; provenance only: n019_0; @line identities: n019_0 |
| `b3_dag_d` | none | body: none; summary facts: none; provenance only: none; @line identities: none |
| `b3_dag_e` | identity | body: none; summary facts: none; provenance only: n019_0 .. root (81 callables); @line identities: n019_0 .. root (81 callables) |
| `b3_fanin_a` | local | body: c049; summary facts: none; provenance only: none; @line identities: none |
| `b3_fanin_b` | cone | body: c049; summary facts: c049 .. root (55 callables); provenance only: none; @line identities: none |
| `b3_fanin_b2` | cone | body: c049 .. root (55 callables); summary facts: c049 .. root (55 callables); provenance only: none; @line identities: none |
| `b3_fanin_c` | local | body: c049; summary facts: none; provenance only: c049; @line identities: c049 |
| `b3_fanin_d` | none | body: none; summary facts: none; provenance only: none; @line identities: none |
| `b3_fanin_e` | identity | body: none; summary facts: none; provenance only: c049 .. root (55 callables); @line identities: c049 .. root (55 callables) |

## B4. Cold vs warm

Sequence per project: `first` (empty build dir, fresh process), `second` (identical tree, fresh process, build dir left by `first`), apply a one-line edit, `edit`, revert. Warm client: **not configured**.

| Project | Step | Wall ms | CPU ms | RSS KiB | Checker runs | Fns analyzed | Spec keys |
|---|---|---:|---:|---:|---:|---:|---:|
| `b3_dag_d20_w4_k02` | first | 442.3 | 441.4 | 24940 | 1 | 81 | 162 |
| `b3_dag_d20_w4_k02` | second | 43.9 | 43.7 | 24936 | 0 | n/a | n/a |
| `b3_dag_d20_w4_k02` | edit | 442.9 | 442.9 | 24956 | 1 | 81 | 162 |

Startup decomposition (median of 11 bare starts, untimed-state independent):

| Command | Wall ms | CPU ms |
|---|---:|---:|
| python3 -c pass | 9.4 | 8.7 |
| margo --help (Python start + imports + argparse) | 37.6 | 37.2 |
| moss check floor file (no margo) | 1.8 | 1.3 |
| margo --check build floor package (empty build dir) | 47.6 | 47.1 |

margo is a Python script: the package rows of every table include this interpreter and driver overhead, which a resident client also removes.

## 5. Rust comparison and the B3 error path (Team 5)

**Not apples-to-apples**: a different language, checker and feature set (see README). Rust figures are `cargo check` on a std-only crate with the same generic DAG shape; Moss figures are `margo --check build` of the B3 package from the same run. Cells are wall ms / CPU ms / peak RSS MiB of the WHOLE process tree (sampled every ~3 ms in separate untimed passes, median of 3; for both languages: margo + python + moss, cargo + rustc). `/usr/bin/time %M` (largest single process) is in the all-steps table and results.csv; for Moss it probably reads margo's Python rather than the compiler (not established). Wall and CPU are medians over the timed reps.

Toolchain: `cargo 1.98.1 (797e8a9bc 2026-08-05)`, `rustc 1.98.1 (48a229cea 2026-09-01)`. CARGO_INCREMENTAL unset (default); CARGO_HOME and CARGO_TARGET_DIR under tmp/; --offline --locked; std only. `CARGO_INCREMENTAL` is the default for `cargo check`.

### DAG D20 W4 K2: step after each edit (Moss vs Rust)

| Edit | Moss `margo --check build` | Rust `cargo check` (single) | Rust `cargo check` (workspace) | Rust units rechecked / reused (single / workspace) |
|---|---|---|---|---|
| `a` | 460.1 / 459.8 / 41 | 22.4 / 18.7 / 98 | 31.2 / 25.9 / 92 | 1 / 0 / 2 / 0 |
| `b` | 427.7 / 427.7 / 40 | 23.5 / 20.4 / 106 | 33.9 / 32.2 / 100 | 1 / 0 / 2 / 0 |
| `c` | 436.3 / 435.9 / 41 | 21.1 / 17.8 / 93 | 30.7 / 25.3 / 93 | 1 / 0 / 2 / 0 |
| `d` | 437.4 / 437.0 / 40 | 20.7 / 17.2 / 105 | 33.3 / 27.1 / 92 | 1 / 0 / 2 / 0 |
| `e` | 424.1 / 423.8 / 40 | 21.6 / 18.9 / 102 | 34.9 / 29.2 / 97 | 1 / 0 / 2 / 0 |
| `f` (fails) | 326.4 / 325.9 / 38 | 25.7 / 22.3 / 115 | 26.2 / 23.7 / 114 | 1 / 0 / 1 / 0 |

### Fan-in D50 W4 K2: step after each edit (Moss vs Rust)

| Edit | Moss `margo --check build` | Rust `cargo check` (single) | Rust units rechecked / reused (single) |
|---|---|---|---|
| `a` | 671.0 / 670.7 / 41 | 20.2 / 17.1 / 91 | 1 / 0 |
| `b` | 678.7 / 678.4 / 41 | 27.9 / 24.1 / 103 **(spread flagged)** | 1 / 0 |
| `c` | 642.7 / 642.7 / 41 | 21.1 / 16.9 / 102 | 1 / 0 |
| `d` | 658.3 / 657.6 / 41 | 21.4 / 18.9 / 93 | 1 / 0 |
| `e` | 668.4 / 668.2 / 41 | 21.0 / 17.9 / 91 | 1 / 0 |
| `f` (fails) | 587.9 / 587.5 / 38 | 25.2 / 22.9 / 119 | 1 / 0 |

### First check (empty target / build dir), no-change second check, and revert

Rust `first` = empty target dir; `second` = identical tree (cargo's freshness check); `revert` = after the byte-identical revert. Moss `first`/`revert` from the B3 `a` sequence; Moss `second` from the B4 sequence of the same package (margo skips an unchanged package).

| Project | Step | Moss | Rust (single crate) | Rust (workspace) | Rust units rechecked / reused (single / ws) |
|---|---|---|---|---|---|
| DAG D20 W4 K2 | first | 440.8 / 440.8 / 40 | 39.5 / 35.6 / 105 | 51.6 / 44.6 / 100 | 1 / 0 ; 2 / 0 |
| DAG D20 W4 K2 | second | 43.9 / 43.7 / 26 | 7.6 / 6.8 / 17 | 7.6 / 6.9 / 16 | 0 / 1 ; 0 / 2 |
| DAG D20 W4 K2 | revert | 440.2 / 438.7 / 41 | 21.8 / 19.0 / 87 | 32.5 / 27.7 / 90 | 1 / 0 ; 2 / 0 |
| Fan-in D50 W4 K2 | first | 668.5 / 669.0 / 41 | 37.9 / 33.0 / 105 | n/a | 1 / 0 |
| Fan-in D50 W4 K2 | second | n/a | 7.5 / 6.8 / 17 | n/a | 0 / 1 |
| Fan-in D50 W4 K2 | revert | 667.1 / 667.1 / 41 | 20.1 / 19.1 / 87 | n/a | 1 / 0 |

Startup floor (a `main` that only prints a literal): Rust `cargo check` first 32.5 / 28.5 / 76, second 7.6 / 6.8 / 16, after a one-line edit 18.5 / 14.9 / 81; Moss `margo --check build` floor is in the B4 table above.

### Peak RSS of the whole process tree (sampled in separate untimed passes, MiB)

| Sequence | first | second | edit | revert |
|---|---:|---:|---:|---:|
| `rust_dag_single_a` | 105 | 17 | 98 | 87 |
| `rust_dag_single_f` | 105 | 17 | 115 | 86 |
| `rust_dag_workspace_a` | 100 | 16 | 92 | 90 |
| `rust_dag_workspace_f` | 100 | 16 | 114 | 83 |
| `rust_fanin_single_a` | 105 | 17 | 91 | 87 |
| `rust_fanin_single_f` | 107 | 17 | 119 | 86 |
| `rust_floor_edit` | 76 | 16 | 81 | 84 |

Measurement quality (Rust): per-scenario spread guard limit 1.3 (max/min wall over reps), at most 3 attempts; steps flagged after the last attempt: 4; rows measured more than once: 24; rows in a block still noisy after the retries: 0; rows whose cargo bookkeeping differed between reps: 0.

Flagged (spread above the limit after 3 attempts): `rust_fanin_single_b:first` (1.20), `rust_fanin_single_b:second` (1.32), `rust_fanin_single_b:edit` (1.23), `rust_fanin_single_b:revert` (1.17)

### All Rust steps

| Sequence | Step | Status (exit) | Wall ms (min-max) | CPU ms | RSS MiB | Units rechecked / reused | Spread | Attempts |
|---|---|---|---:|---:|---:|---|---:|---:|
| `rust_dag_single_a` | first | pass (0) | 39.5 (38.4-40.5) | 35.6 | 82 | 1 / 0 | 1.05 | 2 |
| `rust_dag_single_a` | second | pass (0) | 7.6 (7.0-7.6) | 6.8 | 23 | 0 / 1 | 1.09 | 2 |
| `rust_dag_single_a` | edit | pass (0) | 22.4 (20.2-24.3) | 18.7 | 79 | 1 / 0 | 1.20 | 2 |
| `rust_dag_single_a` | revert | pass (0) | 21.8 (20.7-24.3) | 19.0 | 79 | 1 / 0 | 1.17 | 2 |
| `rust_dag_single_b` | first | pass (0) | 38.9 (38.1-40.0) | 35.3 | 82 | 1 / 0 | 1.05 | 1 |
| `rust_dag_single_b` | second | pass (0) | 7.7 (7.5-8.1) | 6.9 | 23 | 0 / 1 | 1.07 | 1 |
| `rust_dag_single_b` | edit | pass (0) | 23.5 (22.7-24.3) | 20.4 | 85 | 1 / 0 | 1.07 | 1 |
| `rust_dag_single_b` | revert | pass (0) | 23.7 (23.2-27.7) | 21.1 | 85 | 1 / 0 | 1.19 | 1 |
| `rust_dag_single_c` | first | pass (0) | 40.2 (37.7-41.9) | 35.7 | 82 | 1 / 0 | 1.11 | 1 |
| `rust_dag_single_c` | second | pass (0) | 7.0 (6.9-8.8) | 6.7 | 24 | 0 / 1 | 1.27 | 1 |
| `rust_dag_single_c` | edit | pass (0) | 21.1 (20.5-22.3) | 17.8 | 79 | 1 / 0 | 1.09 | 1 |
| `rust_dag_single_c` | revert | pass (0) | 21.4 (20.5-22.8) | 19.0 | 79 | 1 / 0 | 1.11 | 1 |
| `rust_dag_single_d` | first | pass (0) | 39.4 (38.3-40.9) | 35.2 | 82 | 1 / 0 | 1.07 | 1 |
| `rust_dag_single_d` | second | pass (0) | 7.6 (6.9-8.4) | 6.9 | 23 | 0 / 1 | 1.21 | 1 |
| `rust_dag_single_d` | edit | pass (0) | 20.7 (20.1-22.6) | 17.2 | 80 | 1 / 0 | 1.13 | 1 |
| `rust_dag_single_d` | revert | pass (0) | 21.2 (20.7-22.5) | 19.5 | 80 | 1 / 0 | 1.09 | 1 |
| `rust_dag_single_e` | first | pass (0) | 38.8 (38.1-39.2) | 35.0 | 82 | 1 / 0 | 1.03 | 1 |
| `rust_dag_single_e` | second | pass (0) | 7.5 (7.0-7.7) | 6.9 | 23 | 0 / 1 | 1.09 | 1 |
| `rust_dag_single_e` | edit | pass (0) | 21.6 (20.5-23.1) | 18.9 | 78 | 1 / 0 | 1.13 | 1 |
| `rust_dag_single_e` | revert | pass (0) | 22.3 (21.1-25.8) | 19.2 | 78 | 1 / 0 | 1.22 | 1 |
| `rust_dag_single_f` | first | pass (0) | 38.9 (37.7-40.3) | 35.1 | 82 | 1 / 0 | 1.07 | 2 |
| `rust_dag_single_f` | second | pass (0) | 7.5 (7.0-8.2) | 6.9 | 23 | 0 / 1 | 1.17 | 2 |
| `rust_dag_single_f` | edit | fail (101) | 25.7 (25.1-26.3) | 22.3 | 97 | 1 / 0 | 1.05 | 2 |
| `rust_dag_single_f` | revert | pass (0) | 19.9 (18.3-20.3) | 17.3 | 67 | 1 / 0 | 1.11 | 2 |
| `rust_dag_workspace_a` | first | pass (0) | 51.6 (49.9-54.1) | 44.6 | 81 | 2 / 0 | 1.09 | 1 |
| `rust_dag_workspace_a` | second | pass (0) | 7.6 (7.3-9.0) | 6.9 | 24 | 0 / 2 | 1.23 | 1 |
| `rust_dag_workspace_a` | edit | pass (0) | 31.2 (30.8-32.0) | 25.9 | 77 | 2 / 0 | 1.04 | 1 |
| `rust_dag_workspace_a` | revert | pass (0) | 32.5 (31.3-36.5) | 27.7 | 77 | 2 / 0 | 1.17 | 1 |
| `rust_dag_workspace_b` | first | pass (0) | 51.5 (47.7-54.4) | 44.1 | 80 | 2 / 0 | 1.14 | 1 |
| `rust_dag_workspace_b` | second | pass (0) | 7.7 (7.2-8.3) | 7.1 | 24 | 0 / 2 | 1.16 | 1 |
| `rust_dag_workspace_b` | edit | pass (0) | 33.9 (33.7-37.6) | 32.2 | 81 | 2 / 0 | 1.12 | 1 |
| `rust_dag_workspace_b` | revert | pass (0) | 34.3 (33.7-38.0) | 28.2 | 81 | 2 / 0 | 1.13 | 1 |
| `rust_dag_workspace_c` | first | pass (0) | 50.7 (49.2-52.9) | 44.8 | 80 | 2 / 0 | 1.07 | 3 |
| `rust_dag_workspace_c` | second | pass (0) | 7.5 (6.9-7.7) | 7.0 | 24 | 0 / 2 | 1.11 | 3 |
| `rust_dag_workspace_c` | edit | pass (0) | 30.7 (30.1-34.6) | 25.3 | 77 | 2 / 0 | 1.15 | 3 |
| `rust_dag_workspace_c` | revert | pass (0) | 32.2 (31.3-35.2) | 25.2 | 77 | 2 / 0 | 1.12 | 3 |
| `rust_dag_workspace_d` | first | pass (0) | 52.1 (51.7-57.0) | 46.2 | 80 | 2 / 0 | 1.10 | 1 |
| `rust_dag_workspace_d` | second | pass (0) | 7.5 (7.5-9.6) | 7.2 | 24 | 0 / 2 | 1.28 | 1 |
| `rust_dag_workspace_d` | edit | pass (0) | 33.3 (32.2-37.4) | 27.1 | 77 | 2 / 0 | 1.16 | 1 |
| `rust_dag_workspace_d` | revert | pass (0) | 34.6 (31.4-37.3) | 30.8 | 77 | 2 / 0 | 1.19 | 1 |
| `rust_dag_workspace_e` | first | pass (0) | 51.4 (50.9-52.9) | 44.8 | 81 | 2 / 0 | 1.04 | 1 |
| `rust_dag_workspace_e` | second | pass (0) | 7.6 (6.9-8.2) | 7.0 | 24 | 0 / 2 | 1.18 | 1 |
| `rust_dag_workspace_e` | edit | pass (0) | 34.9 (33.9-35.7) | 29.2 | 81 | 2 / 0 | 1.06 | 1 |
| `rust_dag_workspace_e` | revert | pass (0) | 34.9 (33.6-35.7) | 30.2 | 81 | 2 / 0 | 1.06 | 1 |
| `rust_dag_workspace_f` | first | pass (0) | 51.6 (49.0-53.2) | 44.8 | 80 | 2 / 0 | 1.09 | 1 |
| `rust_dag_workspace_f` | second | pass (0) | 8.0 (7.5-8.2) | 7.2 | 24 | 0 / 2 | 1.09 | 1 |
| `rust_dag_workspace_f` | edit | fail (101) | 26.2 (25.1-28.0) | 23.7 | 96 | 1 / 0 | 1.11 | 1 |
| `rust_dag_workspace_f` | revert | pass (0) | 30.3 (28.1-33.5) | 24.9 | 65 | 2 / 0 | 1.19 | 1 |
| `rust_fanin_single_a` | first | pass (0) | 37.9 (35.9-41.2) | 33.0 | 82 | 1 / 0 | 1.15 | 3 |
| `rust_fanin_single_a` | second | pass (0) | 7.5 (6.9-7.6) | 6.8 | 23 | 0 / 1 | 1.10 | 3 |
| `rust_fanin_single_a` | edit | pass (0) | 20.2 (19.4-21.8) | 17.1 | 79 | 1 / 0 | 1.12 | 3 |
| `rust_fanin_single_a` | revert | pass (0) | 20.1 (20.0-22.6) | 19.1 | 79 | 1 / 0 | 1.13 | 3 |
| `rust_fanin_single_b` | first | pass (0) | 42.2 (40.6-48.7) | 38.6 | 82 | 1 / 0 | 1.20 | 3 | **(spread flagged)**
| `rust_fanin_single_b` | second | pass (0) | 9.3 (7.6-10.0) | 8.6 | 23 | 0 / 1 | 1.32 | 3 | **(spread flagged)**
| `rust_fanin_single_b` | edit | pass (0) | 27.9 (24.1-29.6) | 24.1 | 85 | 1 / 0 | 1.23 | 3 | **(spread flagged)**
| `rust_fanin_single_b` | revert | pass (0) | 27.8 (25.9-30.2) | 25.4 | 84 | 1 / 0 | 1.17 | 3 | **(spread flagged)**
| `rust_fanin_single_c` | first | pass (0) | 38.2 (36.6-39.3) | 33.9 | 82 | 1 / 0 | 1.07 | 1 |
| `rust_fanin_single_c` | second | pass (0) | 7.6 (6.9-8.2) | 6.8 | 23 | 0 / 1 | 1.18 | 1 |
| `rust_fanin_single_c` | edit | pass (0) | 21.1 (19.5-22.2) | 16.9 | 79 | 1 / 0 | 1.14 | 1 |
| `rust_fanin_single_c` | revert | pass (0) | 20.7 (20.0-23.9) | 18.5 | 79 | 1 / 0 | 1.20 | 1 |
| `rust_fanin_single_d` | first | pass (0) | 38.8 (36.1-40.9) | 33.4 | 82 | 1 / 0 | 1.13 | 1 |
| `rust_fanin_single_d` | second | pass (0) | 7.0 (6.9-7.6) | 6.6 | 24 | 0 / 1 | 1.10 | 1 |
| `rust_fanin_single_d` | edit | pass (0) | 21.4 (20.6-21.8) | 18.9 | 80 | 1 / 0 | 1.06 | 1 |
| `rust_fanin_single_d` | revert | pass (0) | 20.7 (20.1-24.0) | 17.7 | 80 | 1 / 0 | 1.19 | 1 |
| `rust_fanin_single_e` | first | pass (0) | 39.5 (36.6-40.0) | 34.2 | 82 | 1 / 0 | 1.09 | 1 |
| `rust_fanin_single_e` | second | pass (0) | 7.5 (7.0-7.8) | 6.9 | 23 | 0 / 1 | 1.13 | 1 |
| `rust_fanin_single_e` | edit | pass (0) | 21.0 (20.9-22.4) | 17.9 | 77 | 1 / 0 | 1.07 | 1 |
| `rust_fanin_single_e` | revert | pass (0) | 21.2 (20.3-21.9) | 18.0 | 77 | 1 / 0 | 1.08 | 1 |
| `rust_fanin_single_f` | first | pass (0) | 38.4 (36.5-38.8) | 34.3 | 82 | 1 / 0 | 1.06 | 2 |
| `rust_fanin_single_f` | second | pass (0) | 7.1 (6.9-7.7) | 6.7 | 23 | 0 / 1 | 1.11 | 2 |
| `rust_fanin_single_f` | edit | fail (101) | 25.2 (24.8-27.1) | 22.9 | 96 | 1 / 0 | 1.10 | 2 |
| `rust_fanin_single_f` | revert | pass (0) | 18.9 (18.2-21.6) | 16.3 | 66 | 1 / 0 | 1.18 | 2 |
| `rust_floor_edit` | first | pass (0) | 32.5 (31.2-35.3) | 28.5 | 71 | 1 / 0 | 1.13 | 1 |
| `rust_floor_edit` | second | pass (0) | 7.6 (6.9-8.2) | 6.8 | 23 | 0 / 1 | 1.19 | 1 |
| `rust_floor_edit` | edit | pass (0) | 18.5 (17.4-19.5) | 14.9 | 73 | 1 / 0 | 1.12 | 1 |
| `rust_floor_edit` | revert | pass (0) | 18.4 (17.7-21.3) | 15.3 | 73 | 1 / 0 | 1.20 | 1 |

### The error path: edit `f` (a type error in the top node of the edited chain)

Moss: `+ "one"` in `n000_0` (DAG) / `e0` (fan-in); the same text and position in the Rust crates (`i64 + &str`). Probed alternatives (leaf breaks) gave no answer within 20 s on Moss: `probes/f_edit_probe.txt`. Expected: `edit` fails (Moss margo exit 1, cargo exit 101), `revert` passes. Counters for a failing Moss check are whatever the compiler reports before the failure (functions analyzed shown).

| Sequence | Step | Status (exit) | Wall ms | CPU ms | RSS MiB | Fns analyzed (Moss) / units rechecked (Rust) | Diagnostic |
|---|---|---|---:|---:|---:|---|---|
| `b3_dag_f` | base | pass (0) | 441.3 | 441.3 | 24 | 81 |  |
| `b3_dag_f` | edit | fail (1) | 326.4 | 325.9 | 24 | n/a | TYPE_AMBIGUOUS_OPEN_RESULT: static specialization of function 'main__root' has an unresolved result type |
| `b3_dag_f` | revert | pass (0) | 45.8 | 45.2 | 24 | n/a |  |
| `b3_fanin_f` | base | pass (0) | 670.4 | 670.1 | 24 | 55 |  |
| `b3_fanin_f` | edit | fail (1) | 587.9 | 587.5 | 24 | n/a | TYPE_AMBIGUOUS_OPEN_RESULT: static specialization of function 'main__root' has an unresolved result type |
| `b3_fanin_f` | revert | pass (0) | 45.5 | 45.1 | 24 | n/a |  |
| `rust_dag_single_f` | first | pass (0) | 38.9 | 35.1 | 82 | 1 rechecked / 0 reused |  |
| `rust_dag_single_f` | edit | fail (101) | 25.7 | 22.3 | 97 | 1 rechecked / 0 reused | E0277: cannot add `&str` to `i64` |
| `rust_dag_single_f` | revert | pass (0) | 19.9 | 17.3 | 67 | 1 rechecked / 0 reused |  |
| `rust_dag_workspace_f` | first | pass (0) | 51.6 | 44.8 | 80 | 2 rechecked / 0 reused |  |
| `rust_dag_workspace_f` | edit | fail (101) | 26.2 | 23.7 | 96 | 1 rechecked / 0 reused | E0277: cannot add `&str` to `i64` |
| `rust_dag_workspace_f` | revert | pass (0) | 30.3 | 24.9 | 65 | 2 rechecked / 0 reused |  |
| `rust_fanin_single_f` | first | pass (0) | 38.4 | 34.3 | 82 | 1 rechecked / 0 reused |  |
| `rust_fanin_single_f` | edit | fail (101) | 25.2 | 22.9 | 96 | 1 rechecked / 0 reused | E0277: cannot add `&str` to `i64` |
| `rust_fanin_single_f` | revert | pass (0) | 18.9 | 16.3 | 66 | 1 rechecked / 0 reused |  |

### Rust correctness gate (reference vs candidate cargo)

**Result: PASS** over 19 sequences. Compared per check step: exit status, `build-finished` success and every diagnostic (level, code, message, primary-span file/line/column; paths normalized, `rendered` text and cargo's fresh/dirty counts excluded).

| Sequence | Steps identical | Reference matches expectation |
|---|---|---|
| `rust_dag_single_a` | yes | yes |
| `rust_dag_single_b` | yes | yes |
| `rust_dag_single_c` | yes | yes |
| `rust_dag_single_d` | yes | yes |
| `rust_dag_single_e` | yes | yes |
| `rust_dag_single_f` | yes | yes |
| `rust_dag_workspace_a` | yes | yes |
| `rust_dag_workspace_b` | yes | yes |
| `rust_dag_workspace_c` | yes | yes |
| `rust_dag_workspace_d` | yes | yes |
| `rust_dag_workspace_e` | yes | yes |
| `rust_dag_workspace_f` | yes | yes |
| `rust_fanin_single_a` | yes | yes |
| `rust_fanin_single_b` | yes | yes |
| `rust_fanin_single_c` | yes | yes |
| `rust_fanin_single_d` | yes | yes |
| `rust_fanin_single_e` | yes | yes |
| `rust_fanin_single_f` | yes | yes |
| `rust_floor_edit` | yes | yes |

## 2. Correctness gate (reference vs candidate)

**Result: PASS** over 2 fixtures. Reference: `/home/owner/daji/code/moss2/build/compiler/moss-opt`. Candidate: `/home/owner/daji/code/moss2/build/compiler/moss-opt` via built-in cold command.

The gate requires candidate verdict == reference verdict and reference == manifest (status, expected exit code, no timeout). A candidate that differs from the manifest is annotated IMPROVED/REGRESSED.

| Fixture | Status | Verdicts identical | Reference matches manifest | Candidate note |
|---|---|---|---|---|
| `b3_dag_d20_w4_k02` | pass | yes | yes |  |
| `b3_fanin_d50_w4_k02` | pass | yes | yes |  |

Edit sequences (every check step compared; reference == step expectation). margo's `checked` / `skipped` package lists are volatile here (a client may legitimately answer a repeat from a store) and shown as information only; everything else, including diagnostics and exit codes, is strict:

| Scenario | Steps identical | Reference matches expectation | checked/skipped differ (info) |
|---|---|---|---|
| `b3_dag_a` | yes | yes | no |
| `b3_dag_b` | yes | yes | no |
| `b3_dag_b2` | yes | yes | no |
| `b3_dag_c` | yes | yes | no |
| `b3_dag_d` | yes | yes | no |
| `b3_dag_e` | yes | yes | no |
| `b3_fanin_a` | yes | yes | no |
| `b3_fanin_b` | yes | yes | no |
| `b3_fanin_b2` | yes | yes | no |
| `b3_fanin_c` | yes | yes | no |
| `b3_fanin_d` | yes | yes | no |
| `b3_fanin_e` | yes | yes | no |
| `b4_seq_dag_d20_w4_k02` | yes | yes | no |
| `b3_dag_f` | yes | yes | no |
| `b3_fanin_f` | yes | yes | no |

Reference and candidate are the same compiler today, so this pass only shows the gate is deterministic and wired; it becomes meaningful once the candidate differs.

## 3. Metrics M1 / M2 (sdk/ and packages/)

Not run.

## 4. Caveats

- Cold mode starts a fresh process (package fixtures: `margo --check build` from an empty build directory) per rep; no daemon, no persistent store.
- Wall times for sub-10 ms single-file fixtures are dominated by process startup and are noisy on WSL2; compare medians and ranges, not single digits.
- Counters come from a separate run with `MOSS_PROFILE_COMPILER` enabled, so they never perturb the timed reps.
- Counters are `n/a` for warm mode: a future client is not required to expose them.
