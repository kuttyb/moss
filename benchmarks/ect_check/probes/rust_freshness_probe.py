#!/usr/bin/env python3
"""How does cargo decide a crate is fresh? Probe on the single-crate Rust DAG fixture.
Each case changes the staged tree in one way and reports cargo's own verdict
(`Checking` line on stderr = rustc ran; none = fresh). Writes nothing outside tmp/.

  python3 probes/rust_freshness_probe.py  > probes/rust_freshness_probe.txt
"""
import os, shutil, subprocess, sys, time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import ect_common as C
import rust_cmp as RC

rust = RC.find_rust()
fx = RC.fixture_by_id(C.load_manifest(), "rust_dag_single_d20_w4_k02")
run_dir = C.TMP_ROOT / "rust_freshness_probe"
work = run_dir / "work" / "w"
shutil.rmtree(run_dir, ignore_errors=True)
values = RC.stage(fx, work)
src = work / "crate" / "src" / "dag.rs"


def check(label):
    res = C.run_measured(RC.argv_for(fx, rust, values), cwd=work, env=rust.env(work, run_dir), timeout=120)
    v = RC.extract_verdict(res, RC.replacements(work, rust, run_dir))
    print(f"{label:<58} rustc ran on {v['bookkeeping']['units_checked']} unit(s), "
          f"{v['bookkeeping']['units_fresh']} reused, exit {v['exit_code']}")


print(f"cargo: {rust.versions['cargo']}; rustc: {rust.versions['rustc']}")
check("1. first check (empty target dir)")
check("2. no change")
st = src.stat()
text = src.read_text()
time.sleep(0.05)
os.utime(src, None)
check("3. touch (mtime changes, content identical)")
src.write_text(text)
check("4. rewrite identical bytes (mtime changes)")
src.write_text(text.replace("let k = 1;", "let k = 2;", 1))
check("5. content change (k = 1 -> 2)")
src.write_text(text)
check("6. content restored (new mtime)")
src.write_text(text.replace("let k = 1;", "let k = 7;", 1))
os.utime(src, ns=(st.st_atime_ns, st.st_mtime_ns))
check("7. content change, mtime reset to the old value")
