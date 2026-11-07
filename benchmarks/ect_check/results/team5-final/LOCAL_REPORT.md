# ECT Check Benchmark - Local Report

**Suite:** `moss_ect_check` 0.5.0 (2 fixtures, seed 20261105)  
**Run:** `20261106-165237` (2026-11-06T16:59:21-0800)  
**Compiler:** `/home/owner/daji/code/moss2/build/compiler/moss-opt`  
**Compiler SHA256:** `e9a22cf0a6029d31b617baf863c5c1e866a3641a22c38bb1cd1e78c31bfe4d36`  
**Build flavor:** `opt`  
**Repo:** `phase28-5-bench` @ `533034030869` (dirty tree)  
**Platform:** `Linux-6.18.33.2-microsoft-standard-WSL2-x86_64-with-glibc2.39` (16 CPUs)  
**Reps:** 5 timed runs per fixture/mode (median reported), plus one separate counter run (counters are not collected during timed runs)  
**Warm client:** not configured  

**Noise probe:** loadavg start [0.27197265625, 1.14599609375, 2.64111328125], end [1.2451171875, 1.33642578125, 2.4111328125]; calibration `b2_k04` median wall 548.4 ms at start, 537.5 ms after the timed loop (drift 2.0%).  

## 1. Timing, memory and compiler counters

Wall, CPU and RSS are medians over the timed reps. CPU is user+system of the whole process tree. RSS is the largest single process in the tree (not a sum). Counters are summed over the checker runs one command starts.

### Mode: cold

| Fixture | Kind | Status | Wall ms | CPU ms | RSS KiB | Checker runs | Fns analyzed | Spec evals | Spec hits | Spec keys |
|---|---|---|---:|---:|---:|---:|---:|---:|---:|---:|
| `b3_dag_d20_w4_k02` | package | pass | 456.12 (441.1-473.6) | 455.89 | 24956 | 1 | 81 | 162 | 0 | 162 |
| `b3_fanin_d50_w4_k02` | package | pass | 674.90 (662.6-680.1) | 674.83 | 24948 | 1 | 55 | 116 | 6 | 110 |

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
| (none) | unedited baseline | base | 451.6 | 451.1 | 81 | 81 | 162 | 162 | 1 | 81 | n/a | n/a | n/a | n/a |
| `a` | local | edit | 455.7 | 454.9 | 81 | 81 | 162 | 162 | 1 | 81 | 1 | 2 | 0 | 81.0x |
| `a` | local | revert | 446.5 | 445.3 | 81 | 81 | 162 | 162 | 1 | 81 |  |  |  |  |
| `b` | cone | edit | 435.8 | 435.8 | 81 | 81 | 162 | 162 | 1 | 81 | 21 | 42 | 21 | 3.9x |
| `b` | cone | revert | 430.6 | 430.4 | 81 | 81 | 162 | 162 | 1 | 81 |  |  |  |  |
| `b2` | cone | edit | 438.7 | 438.6 | 81 | 81 | 162 | 162 | 1 | 81 | 21 | 42 | 21 | 3.9x |
| `b2` | cone | revert | 442.1 | 442.0 | 81 | 81 | 162 | 162 | 1 | 81 |  |  |  |  |
| `c` | local | edit | 448.2 | 447.7 | 81 | 81 | 162 | 162 | 1 | 81 | 1 | 2 | 0 | 81.0x |
| `c` | local | revert | 445.8 | 445.4 | 81 | 81 | 162 | 162 | 1 | 81 |  |  |  |  |
| `d` | none | edit | 439.1 | 438.7 | 81 | 81 | 162 | 162 | 1 | 81 | 0 | 0 | 0 | n/a (ideal 0) |
| `d` | none | revert | 444.1 | 444.0 | 81 | 81 | 162 | 162 | 1 | 81 |  |  |  |  |
| `e` | identity | edit | 447.1 | 446.6 | 81 | 81 | 162 | 162 | 1 | 81 | 0 | 0 | 0 | n/a (ideal 0) |
| `e` | identity | revert | 442.3 | 441.9 | 81 | 81 | 162 | 162 | 1 | 81 |  |  |  |  |

### b3_fanin_d50_w4_k02

| Edit | Class | Step | Wall ms | CPU ms | Fns analyzed | Source fn checks | Spec evals | Spec keys | Checker runs | Callables (src) | Ideal recheck | Ideal spec keys | Observed summary change | Analyzed / ideal |
|---|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| (none) | unedited baseline | base | 670.9 | 670.6 | 55 | 55 | 116 | 110 | 1 | 55 | n/a | n/a | n/a | n/a |
| `a` | local | edit | 698.1 | 697.5 | 55 | 55 | 116 | 110 | 1 | 55 | 1 | 2 | 0 | 55.0x |
| `a` | local | revert | 691.9 | 691.6 | 55 | 55 | 116 | 110 | 1 | 55 |  |  |  |  |
| `b` | cone | edit | 871.3 | 871.3 | 55 | 55 | 116 | 110 | 1 | 55 | 55 | 110 | 55 | 1.0x |
| `b` | cone | revert | 852.0 | 851.6 | 55 | 55 | 116 | 110 | 1 | 55 |  |  |  |  |
| `b2` | cone | edit | 691.5 | 691.0 | 55 | 55 | 116 | 110 | 1 | 55 | 55 | 110 | 55 | 1.0x |
| `b2` | cone | revert | 687.8 | 687.4 | 55 | 55 | 116 | 110 | 1 | 55 |  |  |  |  |
| `c` | local | edit | 700.0 | 699.8 | 55 | 55 | 116 | 110 | 1 | 55 | 1 | 2 | 0 | 55.0x |
| `c` | local | revert | 679.5 | 679.2 | 55 | 55 | 116 | 110 | 1 | 55 |  |  |  |  |
| `d` | none | edit | 656.8 | 656.8 | 55 | 55 | 116 | 110 | 1 | 55 | 0 | 0 | 0 | n/a (ideal 0) |
| `d` | none | revert | 658.6 | 658.5 | 55 | 55 | 116 | 110 | 1 | 55 |  |  |  |  |
| `e` | identity | edit | 655.8 | 655.4 | 55 | 55 | 116 | 110 | 1 | 55 | 0 | 0 | 0 | n/a (ideal 0) |
| `e` | identity | revert | 666.8 | 666.4 | 55 | 55 | 116 | 110 | 1 | 55 |  |  |  |  |

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
| `b3_dag_d20_w4_k02` | first | 433.7 | 432.0 | 24964 | 1 | 81 | 162 |
| `b3_dag_d20_w4_k02` | second | 46.0 | 45.6 | 24948 | 0 | n/a | n/a |
| `b3_dag_d20_w4_k02` | edit | 437.4 | 437.0 | 24960 | 1 | 81 | 162 |

Startup decomposition (median of 11 bare starts, untimed-state independent):

| Command | Wall ms | CPU ms |
|---|---:|---:|
| python3 -c pass | 9.3 | 8.8 |
| margo --help (Python start + imports + argparse) | 38.7 | 38.6 |
| moss check floor file (no margo) | 1.9 | 1.3 |
| margo --check build floor package (empty build dir) | 47.5 | 47.2 |

margo is a Python script: the package rows of every table include this interpreter and driver overhead, which a resident client also removes.

## 5. Rust comparison and the B3 error path (Team 5)

**Not apples-to-apples**: a different language, checker and feature set (see README). Rust figures are `cargo check` on a std-only crate with the same generic DAG shape; Moss figures are `margo --check build` of the B3 package from the same run. Cells are wall ms / CPU ms / peak RSS MiB (`/usr/bin/time %M`, the largest single process of the tree; Moss rows read 24 MiB everywhere, consistent with margo's Python being the largest process, not established); medians over the timed reps.

Toolchain: `cargo 1.98.1 (797e8a9bc 2026-08-05)`, `rustc 1.98.1 (48a229cea 2026-09-01)`. CARGO_INCREMENTAL unset (default); CARGO_HOME and CARGO_TARGET_DIR under tmp/; --offline --locked; std only. `CARGO_INCREMENTAL` is the default for `cargo check`.

### DAG D20 W4 K2: step after each edit (Moss vs Rust)

| Edit | Moss `margo --check build` | Rust `cargo check` (single) | Rust `cargo check` (workspace) | Rust units rechecked / reused (single / workspace) |
|---|---|---|---|---|
| `a` | 455.7 / 454.9 / 24 | 21.5 / 18.2 / 79 | 32.5 / 26.3 / 77 | 1 / 0 / 2 / 0 |
| `b` | 435.8 / 435.8 / 24 | 22.9 / 20.4 / 85 | 35.4 / 29.4 / 81 | 1 / 0 / 2 / 0 |
| `c` | 448.2 / 447.7 / 24 | 21.6 / 19.1 / 79 | 31.6 / 27.4 / 77 | 1 / 0 / 2 / 0 |
| `d` | 439.1 / 438.7 / 24 | 21.9 / 18.6 / 80 | 32.4 / 27.4 / 77 | 1 / 0 / 2 / 0 |
| `e` | 447.1 / 446.6 / 24 | 22.5 / 19.3 / 78 | 33.7 / 27.6 / 81 | 1 / 0 / 2 / 0 |
| `f` (fails) | 321.3 / 320.9 / 24 | 26.2 / 24.5 / 96 | 26.4 / 23.9 / 96 | 1 / 0 / 1 / 0 |

### Fan-in D50 W4 K2: step after each edit (Moss vs Rust)

| Edit | Moss `margo --check build` | Rust `cargo check` (single) | Rust units rechecked / reused (single) |
|---|---|---|---|
| `a` | 698.1 / 697.5 / 24 | 20.6 / 17.4 / 79 | 1 / 0 |
| `b` | 871.3 / 871.3 / 24 | 25.9 / 22.7 / 85 | 1 / 0 |
| `c` | 700.0 / 699.8 / 24 | 21.8 / 18.1 / 79 | 1 / 0 |
| `d` | 656.8 / 656.8 / 24 | 20.5 / 17.7 / 80 | 1 / 0 |
| `e` | 655.8 / 655.4 / 24 | 21.4 / 17.8 / 77 | 1 / 0 |
| `f` (fails) | 568.6 / 568.3 / 24 | 26.2 / 23.0 / 96 | 1 / 0 |

### First check (empty target / build dir), no-change second check, and revert

Rust `first` = empty target dir; `second` = identical tree (cargo's freshness check); `revert` = after the byte-identical revert. Moss `first`/`revert` from the B3 `a` sequence; Moss `second` from the B4 sequence of the same package (margo skips an unchanged package).

| Project | Step | Moss | Rust (single crate) | Rust (workspace) | Rust units rechecked / reused (single / ws) |
|---|---|---|---|---|---|
| DAG D20 W4 K2 | first | 451.6 / 451.1 / 24 | 39.4 / 34.1 / 82 | 50.7 / 42.2 / 80 | 1 / 0 ; 2 / 0 |
| DAG D20 W4 K2 | second | 46.0 / 45.6 / 24 | 7.7 / 7.1 / 23 | 7.8 / 7.0 / 24 | 0 / 1 ; 0 / 2 |
| DAG D20 W4 K2 | revert | 446.5 / 445.3 / 24 | 21.2 / 18.1 / 79 | 32.3 / 27.4 / 77 | 1 / 0 ; 2 / 0 |
| Fan-in D50 W4 K2 | first | 670.9 / 670.6 / 24 | 38.2 / 33.4 / 82 | n/a | 1 / 0 |
| Fan-in D50 W4 K2 | second | n/a | 7.5 / 6.9 / 23 | n/a | 0 / 1 |
| Fan-in D50 W4 K2 | revert | 691.9 / 691.6 / 24 | 20.5 / 17.0 / 79 | n/a | 1 / 0 |

Startup floor (a `main` that only prints a literal): Rust `cargo check` first 31.8 / 27.5 / 71, second 7.6 / 7.0 / 24, after a one-line edit 17.9 / 14.8 / 73; Moss `margo --check build` floor is in the B4 table above.

### Peak RSS of the whole process tree (sampled in separate untimed passes, MiB)

| Sequence | first | second | edit | revert |
|---|---:|---:|---:|---:|
| `rust_dag_single_a` | 105 | 16 | 94 | 89 |
| `rust_dag_single_f` | 105 | 16 | 117 | 87 |
| `rust_dag_workspace_a` | 104 | 17 | 91 | 88 |
| `rust_dag_workspace_f` | 103 | 16 | 109 | 87 |
| `rust_fanin_single_a` | 106 | 17 | 92 | 93 |
| `rust_fanin_single_f` | 107 | 16 | 111 | 87 |
| `rust_floor_edit` | 74 | 16 | 86 | 83 |

Measurement quality (Rust): per-scenario spread guard limit 1.3 (max/min wall over reps), at most 3 attempts; steps flagged after the last attempt: 0; rows measured more than once: 8; rows in a block still noisy after the retries: 0; rows whose cargo bookkeeping differed between reps: 0.

### All Rust steps

| Sequence | Step | Status (exit) | Wall ms (min-max) | CPU ms | RSS MiB | Units rechecked / reused | Spread | Attempts |
|---|---|---|---:|---:|---:|---|---:|---:|
| `rust_dag_single_a` | first | pass (0) | 39.4 (38.4-39.6) | 34.1 | 82 | 1 / 0 | 1.03 | 1 |
| `rust_dag_single_a` | second | pass (0) | 7.7 (6.9-8.4) | 7.1 | 23 | 0 / 1 | 1.21 | 1 |
| `rust_dag_single_a` | edit | pass (0) | 21.5 (20.5-21.7) | 18.2 | 79 | 1 / 0 | 1.06 | 1 |
| `rust_dag_single_a` | revert | pass (0) | 21.2 (20.5-23.2) | 18.1 | 79 | 1 / 0 | 1.13 | 1 |
| `rust_dag_single_b` | first | pass (0) | 39.8 (38.2-41.4) | 35.3 | 82 | 1 / 0 | 1.08 | 1 |
| `rust_dag_single_b` | second | pass (0) | 7.5 (7.1-8.3) | 7.0 | 24 | 0 / 1 | 1.16 | 1 |
| `rust_dag_single_b` | edit | pass (0) | 22.9 (22.9-23.5) | 20.4 | 85 | 1 / 0 | 1.03 | 1 |
| `rust_dag_single_b` | revert | pass (0) | 24.1 (22.9-24.7) | 20.6 | 85 | 1 / 0 | 1.08 | 1 |
| `rust_dag_single_c` | first | pass (0) | 39.2 (38.7-39.5) | 33.9 | 82 | 1 / 0 | 1.02 | 1 |
| `rust_dag_single_c` | second | pass (0) | 7.1 (6.9-7.5) | 6.8 | 23 | 0 / 1 | 1.08 | 1 |
| `rust_dag_single_c` | edit | pass (0) | 21.6 (20.1-22.5) | 19.1 | 79 | 1 / 0 | 1.12 | 1 |
| `rust_dag_single_c` | revert | pass (0) | 22.3 (20.9-22.7) | 18.2 | 79 | 1 / 0 | 1.08 | 1 |
| `rust_dag_single_d` | first | pass (0) | 39.3 (37.6-44.3) | 35.4 | 82 | 1 / 0 | 1.18 | 1 |
| `rust_dag_single_d` | second | pass (0) | 7.5 (7.1-7.9) | 7.0 | 23 | 0 / 1 | 1.12 | 1 |
| `rust_dag_single_d` | edit | pass (0) | 21.9 (20.8-23.6) | 18.6 | 80 | 1 / 0 | 1.13 | 1 |
| `rust_dag_single_d` | revert | pass (0) | 21.7 (21.1-23.5) | 19.8 | 80 | 1 / 0 | 1.11 | 1 |
| `rust_dag_single_e` | first | pass (0) | 39.8 (39.6-40.3) | 34.4 | 82 | 1 / 0 | 1.02 | 1 |
| `rust_dag_single_e` | second | pass (0) | 7.8 (7.1-8.6) | 7.4 | 23 | 0 / 1 | 1.20 | 1 |
| `rust_dag_single_e` | edit | pass (0) | 22.5 (22.2-23.1) | 19.3 | 78 | 1 / 0 | 1.04 | 1 |
| `rust_dag_single_e` | revert | pass (0) | 23.2 (22.0-24.4) | 19.6 | 78 | 1 / 0 | 1.11 | 1 |
| `rust_dag_single_f` | first | pass (0) | 40.1 (39.3-42.9) | 36.6 | 82 | 1 / 0 | 1.09 | 1 |
| `rust_dag_single_f` | second | pass (0) | 7.6 (6.9-7.7) | 6.9 | 23 | 0 / 1 | 1.11 | 1 |
| `rust_dag_single_f` | edit | fail (101) | 26.2 (24.6-26.7) | 24.5 | 96 | 1 / 0 | 1.08 | 1 |
| `rust_dag_single_f` | revert | pass (0) | 20.5 (18.8-21.0) | 17.4 | 67 | 1 / 0 | 1.12 | 1 |
| `rust_dag_workspace_a` | first | pass (0) | 50.7 (49.5-51.1) | 42.2 | 80 | 2 / 0 | 1.03 | 1 |
| `rust_dag_workspace_a` | second | pass (0) | 7.8 (7.5-8.4) | 7.0 | 24 | 0 / 2 | 1.11 | 1 |
| `rust_dag_workspace_a` | edit | pass (0) | 32.5 (30.1-32.9) | 26.3 | 77 | 2 / 0 | 1.09 | 1 |
| `rust_dag_workspace_a` | revert | pass (0) | 32.3 (30.7-33.4) | 27.4 | 77 | 2 / 0 | 1.09 | 1 |
| `rust_dag_workspace_b` | first | pass (0) | 49.7 (48.3-50.9) | 42.2 | 80 | 2 / 0 | 1.05 | 1 |
| `rust_dag_workspace_b` | second | pass (0) | 7.8 (7.5-8.4) | 7.4 | 24 | 0 / 2 | 1.12 | 1 |
| `rust_dag_workspace_b` | edit | pass (0) | 35.4 (35.1-36.5) | 29.4 | 81 | 2 / 0 | 1.04 | 1 |
| `rust_dag_workspace_b` | revert | pass (0) | 35.4 (34.3-36.1) | 28.2 | 81 | 2 / 0 | 1.05 | 1 |
| `rust_dag_workspace_c` | first | pass (0) | 49.0 (48.3-51.1) | 41.2 | 80 | 2 / 0 | 1.06 | 1 |
| `rust_dag_workspace_c` | second | pass (0) | 7.5 (7.5-8.2) | 6.9 | 24 | 0 / 2 | 1.09 | 1 |
| `rust_dag_workspace_c` | edit | pass (0) | 31.6 (30.7-32.9) | 27.4 | 77 | 2 / 0 | 1.07 | 1 |
| `rust_dag_workspace_c` | revert | pass (0) | 30.8 (30.4-34.0) | 26.2 | 77 | 2 / 0 | 1.12 | 1 |
| `rust_dag_workspace_d` | first | pass (0) | 49.1 (47.8-50.1) | 43.1 | 80 | 2 / 0 | 1.05 | 1 |
| `rust_dag_workspace_d` | second | pass (0) | 7.7 (7.5-8.5) | 7.1 | 24 | 0 / 2 | 1.13 | 1 |
| `rust_dag_workspace_d` | edit | pass (0) | 32.4 (30.2-37.6) | 27.4 | 77 | 2 / 0 | 1.25 | 1 |
| `rust_dag_workspace_d` | revert | pass (0) | 31.5 (30.7-33.9) | 26.4 | 77 | 2 / 0 | 1.10 | 1 |
| `rust_dag_workspace_e` | first | pass (0) | 51.8 (49.5-54.3) | 44.8 | 80 | 2 / 0 | 1.10 | 1 |
| `rust_dag_workspace_e` | second | pass (0) | 7.6 (7.5-8.1) | 7.1 | 24 | 0 / 2 | 1.08 | 1 |
| `rust_dag_workspace_e` | edit | pass (0) | 33.7 (32.1-34.8) | 27.6 | 81 | 2 / 0 | 1.08 | 1 |
| `rust_dag_workspace_e` | revert | pass (0) | 34.2 (33.8-37.9) | 28.8 | 81 | 2 / 0 | 1.12 | 1 |
| `rust_dag_workspace_f` | first | pass (0) | 52.9 (48.4-57.3) | 48.0 | 80 | 2 / 0 | 1.18 | 1 |
| `rust_dag_workspace_f` | second | pass (0) | 7.7 (7.6-8.1) | 7.1 | 24 | 0 / 2 | 1.06 | 1 |
| `rust_dag_workspace_f` | edit | fail (101) | 26.4 (26.0-26.7) | 23.9 | 96 | 1 / 0 | 1.03 | 1 |
| `rust_dag_workspace_f` | revert | pass (0) | 30.6 (30.1-31.0) | 26.3 | 65 | 2 / 0 | 1.03 | 1 |
| `rust_fanin_single_a` | first | pass (0) | 38.2 (37.4-43.2) | 33.4 | 82 | 1 / 0 | 1.16 | 2 |
| `rust_fanin_single_a` | second | pass (0) | 7.5 (7.1-8.1) | 6.9 | 23 | 0 / 1 | 1.15 | 2 |
| `rust_fanin_single_a` | edit | pass (0) | 20.6 (20.0-21.5) | 17.4 | 79 | 1 / 0 | 1.07 | 2 |
| `rust_fanin_single_a` | revert | pass (0) | 20.5 (20.1-21.9) | 17.0 | 79 | 1 / 0 | 1.09 | 2 |
| `rust_fanin_single_b` | first | pass (0) | 38.8 (38.4-40.0) | 34.5 | 82 | 1 / 0 | 1.04 | 1 |
| `rust_fanin_single_b` | second | pass (0) | 7.6 (7.1-7.7) | 6.9 | 23 | 0 / 1 | 1.09 | 1 |
| `rust_fanin_single_b` | edit | pass (0) | 25.9 (24.7-26.8) | 22.7 | 85 | 1 / 0 | 1.08 | 1 |
| `rust_fanin_single_b` | revert | pass (0) | 24.7 (24.0-25.4) | 21.7 | 84 | 1 / 0 | 1.06 | 1 |
| `rust_fanin_single_c` | first | pass (0) | 37.6 (35.9-39.0) | 33.1 | 82 | 1 / 0 | 1.09 | 1 |
| `rust_fanin_single_c` | second | pass (0) | 7.7 (7.0-8.2) | 6.9 | 23 | 0 / 1 | 1.18 | 1 |
| `rust_fanin_single_c` | edit | pass (0) | 21.8 (20.1-22.8) | 18.1 | 79 | 1 / 0 | 1.14 | 1 |
| `rust_fanin_single_c` | revert | pass (0) | 20.6 (20.0-21.6) | 18.3 | 79 | 1 / 0 | 1.08 | 1 |
| `rust_fanin_single_d` | first | pass (0) | 38.4 (35.3-40.4) | 34.7 | 82 | 1 / 0 | 1.14 | 1 |
| `rust_fanin_single_d` | second | pass (0) | 7.5 (6.9-7.8) | 6.8 | 24 | 0 / 1 | 1.12 | 1 |
| `rust_fanin_single_d` | edit | pass (0) | 20.5 (19.9-21.3) | 17.7 | 80 | 1 / 0 | 1.07 | 1 |
| `rust_fanin_single_d` | revert | pass (0) | 20.7 (20.2-21.3) | 18.9 | 80 | 1 / 0 | 1.05 | 1 |
| `rust_fanin_single_e` | first | pass (0) | 36.9 (35.9-42.3) | 33.1 | 82 | 1 / 0 | 1.18 | 1 |
| `rust_fanin_single_e` | second | pass (0) | 7.1 (7.0-7.9) | 7.0 | 23 | 0 / 1 | 1.12 | 1 |
| `rust_fanin_single_e` | edit | pass (0) | 21.4 (20.0-21.9) | 17.8 | 77 | 1 / 0 | 1.10 | 1 |
| `rust_fanin_single_e` | revert | pass (0) | 21.8 (20.8-22.9) | 18.5 | 77 | 1 / 0 | 1.10 | 1 |
| `rust_fanin_single_f` | first | pass (0) | 38.6 (36.5-40.5) | 33.7 | 82 | 1 / 0 | 1.11 | 1 |
| `rust_fanin_single_f` | second | pass (0) | 8.0 (7.0-8.2) | 7.0 | 23 | 0 / 1 | 1.18 | 1 |
| `rust_fanin_single_f` | edit | fail (101) | 26.2 (24.4-26.5) | 23.0 | 96 | 1 / 0 | 1.08 | 1 |
| `rust_fanin_single_f` | revert | pass (0) | 20.2 (18.8-20.8) | 15.5 | 66 | 1 / 0 | 1.11 | 1 |
| `rust_floor_edit` | first | pass (0) | 31.8 (29.9-32.3) | 27.5 | 71 | 1 / 0 | 1.08 | 3 |
| `rust_floor_edit` | second | pass (0) | 7.6 (7.1-8.4) | 7.0 | 24 | 0 / 1 | 1.18 | 3 |
| `rust_floor_edit` | edit | pass (0) | 17.9 (17.2-18.8) | 14.8 | 73 | 1 / 0 | 1.09 | 3 |
| `rust_floor_edit` | revert | pass (0) | 17.7 (16.5-19.0) | 14.4 | 73 | 1 / 0 | 1.15 | 3 |

### The error path: edit `f` (a type error in the top node of the edited chain)

Moss: `+ "one"` in `n000_0` (DAG) / `e0` (fan-in); the same text and position in the Rust crates (`i64 + &str`). Probed alternatives (leaf breaks) did not terminate within 20 s on Moss: `probes/f_edit_probe.txt`. Expected: `edit` fails (Moss margo exit 1, cargo exit 101), `revert` passes. Counters for a failing Moss check are whatever the compiler reports before the failure (functions analyzed shown).

| Sequence | Step | Status (exit) | Wall ms | CPU ms | RSS MiB | Fns analyzed (Moss) / units rechecked (Rust) | Diagnostic |
|---|---|---|---:|---:|---:|---|---|
| `b3_dag_f` | base | pass (0) | 431.5 | 431.4 | 24 | 81 |  |
| `b3_dag_f` | edit | fail (1) | 321.3 | 320.9 | 24 | n/a | TYPE_AMBIGUOUS_OPEN_RESULT: static specialization of function 'main__root' has an unresolved result type |
| `b3_dag_f` | revert | pass (0) | 47.9 | 47.6 | 24 | n/a |  |
| `b3_fanin_f` | base | pass (0) | 658.3 | 657.9 | 24 | 55 |  |
| `b3_fanin_f` | edit | fail (1) | 568.6 | 568.3 | 24 | n/a | TYPE_AMBIGUOUS_OPEN_RESULT: static specialization of function 'main__root' has an unresolved result type |
| `b3_fanin_f` | revert | pass (0) | 48.4 | 48.0 | 24 | n/a |  |
| `rust_dag_single_f` | first | pass (0) | 40.1 | 36.6 | 82 | 1 rechecked / 0 reused |  |
| `rust_dag_single_f` | edit | fail (101) | 26.2 | 24.5 | 96 | 1 rechecked / 0 reused | E0277: cannot add `&str` to `i64` |
| `rust_dag_single_f` | revert | pass (0) | 20.5 | 17.4 | 67 | 1 rechecked / 0 reused |  |
| `rust_dag_workspace_f` | first | pass (0) | 52.9 | 48.0 | 80 | 2 rechecked / 0 reused |  |
| `rust_dag_workspace_f` | edit | fail (101) | 26.4 | 23.9 | 96 | 1 rechecked / 0 reused | E0277: cannot add `&str` to `i64` |
| `rust_dag_workspace_f` | revert | pass (0) | 30.6 | 26.3 | 65 | 2 rechecked / 0 reused |  |
| `rust_fanin_single_f` | first | pass (0) | 38.6 | 33.7 | 82 | 1 rechecked / 0 reused |  |
| `rust_fanin_single_f` | edit | fail (101) | 26.2 | 23.0 | 96 | 1 rechecked / 0 reused | E0277: cannot add `&str` to `i64` |
| `rust_fanin_single_f` | revert | pass (0) | 20.2 | 15.5 | 66 | 1 rechecked / 0 reused |  |

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
