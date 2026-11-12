# Phase 28.5 ECT check benchmarks: final report

Branch `phase28-5-bench`, suite 0.5.0, final tag `bench-t5-done-r2` (superseded tags: `bench-t2-done`, `bench-t3-done`,
`bench-t4-done`, `bench-t5-done`; the last was missing its Rust sources). Only compiler change: +67 read-only counter lines in
`src/semantic_checker.cpp`; `benchmarks/compiler_torture` untouched. A full baseline run on a quiet machine is left to the
owner: `python3 benchmarks/ect_check/run.py bench --reps 5 --out tmp/ect_check/baseline` from a clean checkout (README, "How to run").
Numbers below are local medians (moss-opt, 16-CPU WSL2) from the committed runs; see `results/` and the README tables.

## Baseline (today's compiler)
- **M1** (exported generics reaching `scheme_complete`): 4/4. **M2** (internal callees with a `complete` summary, eligible functions): 0/2.
- **B1 Mossi Leak:** consumer check grows linearly with K and with D*W (K=32: 183 ms at D5 W1, 4293 ms at D20 W4, 22646 ms at D50 W4);
  provider cost is independent of K. The app defines no DAG functions yet analyzes D*W+1 of them. B2 (one package) 118 ms/type vs 133 ms/type across the boundary.
- **B3 Inference Ripple:** wall time is flat across every edit (~420 ms DAG, ~640 ms fan-in); the whole package is rechecked (81 / 55 functions) where a leaf edit needs 1.
- **B4:** startup floor 1.9 ms (file), ~46 ms (margo package, mostly margo's Python); margo skips an unchanged package, a one-line edit costs a full check.
- **B5:** nothing is shared; CPU, RSS and work scale 16x at N=16 (DAG: 12.0 s CPU, 640 MiB; package set: 10.4 s, 2.1 GiB).
- **B6 (50 replayed commits):** post-edit check p50 343 ms, p95 2470 ms; 4 clean edits 64-835 ms.
- **Churn:** 4 of 4 informative edits changed a summary, 2 of 4 rippled to unedited functions. Thin sample (29 of 50 commits do not check today).
- **Rust (same-shape std-only crates):** `cargo check` after an edit is about 12-33x faster than `margo --check build`, depending on shape and layout;
  about 17-29x against bare `moss check` (375 / 584 ms). Not apples-to-apples; README says what may and may not be concluded.

## Compiler defects found (not fixed)
Nested inferred-generic call `bump(bump(x))` fails; chains deeper than ~250 segfault at 8 MiB stack; some call shapes grow exponentially;
leaf-node breaks give no answer within 20 s (cause not established); almost no callables get a `complete` summary (~4/81).
Nothing needed a language change; in Rust only edit b2 and a one-site inferred-return edit cannot be expressed without explicit signature edits.

## Process
Five sequential teams, each with a builder and an adversarial reviewer, two rounds each. At most two LLM threads at once; no subagents spawned by agents.
Caveat: a separate user-run Codex session shared the machine; noise probes, spread guards and reruns were used, noisy runs discarded.
`make check`: only the 3 baseline failures (one flaky memory-limit check once failed under 16-job load, passes alone).
Review catches fixed: Rust sources were gitignored (found from a fresh worktree), weak gate selftests, an overstated speed-up figure.
