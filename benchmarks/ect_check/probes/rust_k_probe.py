#!/usr/bin/env python3
"""Does the number of user types K change `cargo check` time? (Moss re-walks the DAG per
type; Rust type-checks each generic body once, at its definition.) Same D20 W4 single-crate
DAG as the fixture, K = 2, 8, 32; median wall/CPU of the first check (empty target dir)
over 9 reps after one discarded run. Quiet machine only; prints a table.

  python3 probes/rust_k_probe.py > probes/rust_k_probe.txt
"""
import os, random, shutil, sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import ect_common as C
import rust_cmp as RC
import rust_generators as RG

rust = RC.find_rust()
run_dir = C.TMP_ROOT / "rust_k_probe"
shutil.rmtree(run_dir, ignore_errors=True)
print(f"load {os.getloadavg()}  {rust.versions['cargo']}  {rust.versions['rustc']}")
print("K    wall ms (median, min-max)   cpu ms   rss MiB   source lines of the crate")
for k in (2, 8, 32):
    work = run_dir / f"k{k}" / "w"
    walls, cpus, rss = [], [], []
    for i in range(10):
        shutil.rmtree(work.parent, ignore_errors=True)
        RG.single_crate(work / "crate", "dag_single", RG.dag_nodes(20, 4), 3, k)
        (work / "crate" / "Cargo.lock").write_text(RG.lock_single("dag_single"))
        shutil.rmtree(RC.target_dir(work), ignore_errors=True)
        values = {"cargo": str(rust.cargo), "path": str(work / "crate")}
        res = C.run_measured(C.expand_command(RC.CARGO_CMD, values), cwd=work, env=rust.env(work, run_dir), timeout=120)
        if res["rc"] != 0:
            raise SystemExit(f"K={k}: cargo check failed: {res['stderr'][-300:]}")
        if i:                      # the first run is discarded
            walls.append(res["wall_ms"]); cpus.append(res["cpu_ms"]); rss.append(res["rss_kib"])
    lines = sum(len(p.read_text().splitlines()) for p in (work / "crate" / "src").glob("*.rs"))
    print(f"{k:<4} {C.median(walls):7.1f} ({min(walls):.1f}-{max(walls):.1f})   {C.median(cpus):6.1f}   "
          f"{C.median(rss) / 1024:6.0f}   {lines}")
shutil.rmtree(run_dir, ignore_errors=True)
