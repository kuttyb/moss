#!/usr/bin/env python3
"""Like-for-like bare compiler number for the Rust comparison (Team 5 review): `moss check <file> --json`
(no margo, no Python) on the single source file of the B3 DAG and fan-in packages, cold process, 9 timed
reps after one discarded run, with the loadavg and a calibration fixture (b2_k04) before and after so a
noisy run is visible. Compare with `cargo check` (single crate) which is also one process tree.
  python3 benchmarks/ect_check/probes/moss_check_bare_probe.py | tee benchmarks/ect_check/probes/moss_check_bare_probe.txt
"""
import os, shutil, sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import ect_common as C

moss = C.DEFAULT_MOSS
tc = C.Toolchain(moss, C.DEFAULT_MARGO)
manifest = C.load_manifest()
work = C.TMP_ROOT / "bare_probe"


def measure(fx_id, file_rel, reps=9, sample=False):
    fx = next(f for f in manifest["fixtures"] if f["id"] == fx_id)
    walls, cpus, rss, ok = [], [], [], True
    shutil.rmtree(work, ignore_errors=True)
    C.stage_fixture(fx, work)
    argv = [str(moss), "check", str(work / file_rel), "--json"]
    for i in range(reps + 1):
        r = C.run_measured(argv, cwd=work, env=tc.env(), timeout=120, sample_tree_rss=sample)
        ok &= r["rc"] == 0
        if i:
            walls.append(r["wall_ms"]); cpus.append(r["cpu_ms"])
            rss.append(r.get("tree_rss_peak_kib") if sample else r["rss_kib"])
    return walls, cpus, rss, ok


def calib():
    return measure("b2_k04", "b2_k04.moss" if False else next(
        f for f in manifest["fixtures"] if f["id"] == "b2_k04")["entry"], reps=5)[0]


print(f"load before {os.getloadavg()}")
c0 = C.median(calib())
print(f"calibration b2_k04 before: {c0:.1f} ms")
for fid in ("b3_dag_d20_w4_k02", "b3_fanin_d50_w4_k02"):
    walls, cpus, rss, ok = measure(fid, "pkg/src/main.moss")
    _, _, tree, _ = measure(fid, "pkg/src/main.moss", reps=3, sample=True)
    print(f"{fid}: moss check <file> --json  wall {C.median(walls):.1f} ms (min {min(walls):.1f}, max {max(walls):.1f}, "
          f"spread {max(walls) / min(walls):.2f})  cpu {C.median(cpus):.1f} ms  rss(time %M) {C.median(rss) / 1024:.0f} MiB  "
          f"tree peak {C.median(tree) / 1024:.0f} MiB  exit ok={ok}")
c1 = C.median(calib())
print(f"calibration b2_k04 after: {c1:.1f} ms (drift {abs(c1 - c0) / min(c0, c1) * 100:.1f}%); load after {os.getloadavg()}")
shutil.rmtree(work, ignore_errors=True)
