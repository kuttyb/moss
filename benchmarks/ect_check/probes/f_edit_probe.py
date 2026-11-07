#!/usr/bin/env python3
"""Probe for B3 edit `f` (error path): which leaf breaks make `margo --check build`
of a B3 shape terminate with a diagnostic? Each candidate is applied to a scratch
copy of the frozen fixture (leaf n019_0 / c049) and run with a timeout; on timeout
the whole process group is killed (an orphaned moss-opt would keep spinning).

Usage: f_edit_probe.py [timeout_s] [leaf|inner|both] [fixture:candidate]     (needs build/compiler/moss-opt)
Recorded result: probes/f_edit_probe.txt
"""
import json, os, shutil, signal, subprocess, sys, time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
FIXTURES = ROOT / "benchmarks" / "ect_check" / "fixtures" / "b3_edits"
MOSS = ROOT / "build" / "compiler" / "moss-opt"
TIMEOUT = float(sys.argv[1]) if len(sys.argv) > 1 else 30.0
SHAPES = {"b3_dag_d20_w4_k02": "n019_0", "b3_fanin_d50_w4_k02": "c049"}
LEAF = "fn {n}(x):\n  # leaf body\n  k = 1\n  r = x.score()\n  return r + k\n"
CANDIDATES = {
    "string_constant": lambda n: LEAF.format(n=n).replace("k = 1", 'k = "one"'),
    "undefined_name": lambda n: LEAF.format(n=n).replace("r + k", "r + kk"),
    "unknown_function": lambda n: LEAF.format(n=n).replace("x.score()", "missing_fn(x)"),
    "missing_method": lambda n: LEAF.format(n=n).replace("x.score()", "x.nothere()"),
    "wrong_return_annotation": lambda n: LEAF.format(n=n).replace(f"fn {n}(x):", f"fn {n}(x) -> Bool:"),
    "return_string": lambda n: LEAF.format(n=n).replace("return r + k", 'return "s"'),
}


# Phase 2: the break is in a NON-leaf node (`return child(x) + 1` -> `+ "one"`).
INNER = {
    "b3_dag_d20_w4_k02": [(f"n{l:03d}_0", f"fn n{l:03d}_0(x):\n  return n{l + 1:03d}_0(x) + 1\n") for l in (0, 1, 5, 10, 15, 18)],
    "b3_fanin_d50_w4_k02": [("e0", "fn e0(x):\n  return c000(x) + 0\n")] +
                           [(f"c{l:03d}", f"fn c{l:03d}(x):\n  return c{l + 1:03d}(x) + 1\n") for l in (0, 1, 10, 25, 40, 48)],
}


def run_inner(fid, name, old):
    return run(fid, name, f"inner_{name}", lambda n: old.replace("+ 1", '+ "one"').replace("+ 0", '+ "one"'),
               old_text=old)


def run(fid, leaf, name, make, old_text=None):
    work = ROOT / "tmp" / "ect_check" / "f_probe" / f"{fid}-{name}"
    shutil.rmtree(work, ignore_errors=True)
    shutil.copytree(FIXTURES / fid, work, ignore=shutil.ignore_patterns("build"))
    src = work / "pkg" / "src" / "main.moss"
    text = src.read_text()
    old = old_text or LEAF.format(n=leaf)
    assert text.count(old) == 1
    src.write_text(text.replace(old, make(leaf)))
    t = time.perf_counter()
    p = subprocess.Popen([str(ROOT / "margo"), "--root", str(work / "pkg"), "--check", "--json", "build"],
                         stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, start_new_session=True,
                         env={**os.environ, "MOSS": str(MOSS)})
    try:
        out, err = p.communicate(timeout=TIMEOUT)
        res = f"rc={p.returncode} {time.perf_counter() - t:.2f}s"
        try:
            doc = json.loads(out)
            e = doc.get("error")
            i = e.find("{") if isinstance(e, str) else -1
            inner = json.loads(e[i:]) if i >= 0 else {}
            err_obj = inner.get("error") or inner
            res += f" ok={doc.get('ok')} code={err_obj.get('code')} msg={str(err_obj.get('message', ''))[:110]!r}"
        except Exception:  # noqa: BLE001
            res += f" (unparsed: {out[:80]!r})"
    except subprocess.TimeoutExpired:
        os.killpg(p.pid, signal.SIGKILL)
        p.wait()
        res = f"TIMEOUT {TIMEOUT:.0f}s (process group killed)"
    shutil.rmtree(work, ignore_errors=True)
    return res


phase = sys.argv[2] if len(sys.argv) > 2 else "both"
if len(sys.argv) > 3:       # one leaf variant only: <fixture>:<candidate>, e.g. b3_dag_d20_w4_k02:string_constant
    fid, cand = sys.argv[3].split(":")
    print(f"{fid} leaf:{cand}: {run(fid, SHAPES[fid], cand, CANDIDATES[cand])}", flush=True)
    sys.exit(0)
for fid, leaf in SHAPES.items():
    if phase in ("leaf", "both"):
        for name, make in CANDIDATES.items():
            print(f"{fid} leaf:{name}: {run(fid, leaf, name, make)}", flush=True)
    if phase in ("inner", "both"):
        for name, old in INNER[fid]:
            print(f"{fid} inner:{name}: {run_inner(fid, name, old)}", flush=True)
