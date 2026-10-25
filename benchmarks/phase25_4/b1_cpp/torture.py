#!/usr/bin/env python3
"""Deterministic Phase 25.4 B1 C++ semantic-only specialization torture."""
import argparse
import csv
import hashlib
import json
import os
from pathlib import Path
import platform
import re
import signal
import statistics
import subprocess
import tempfile
import time

TIMERS = re.compile(r"^MOSS_PROFILE\|([^|]+)\|([0-9]+)$", re.M)

def generate(n, distinct):
    text = ["# Generated C++ Moss semantic-only benchmark."]
    for i in range(n):
        text += [f"type Sensor{i:03d}:", "  value: Int", "",
                 "  fn score() -> Int:", f"    return value + {i+1}", ""]
    text += ["fn project(value):", "  return value.score()", "",
             "fn combine(a, b):", "  return project(a) + project(b)", "",
             "fn main():"]
    for i in range(n):
        for j in range(n):
            a, b = (i, j) if distinct else (i % 2, j % 2)
            text.append(
                f"  echo combine(Sensor{a:03d}(value = 1), Sensor{b:03d}(value = 2))"
            )
    return "\n".join(text) + "\n"

def fixtures(root, sizes):
    root.mkdir(parents=True, exist_ok=True)
    cases = []
    for n in sizes:
        for label, distinct in [("W2", True), ("W3", False)]:
            source = generate(n, distinct)
            path = root / f"{label.lower()}_n{n:03d}.moss"
            path.write_text(source)
            cases.append({
                "case": label, "n": n, "path": str(path.resolve()),
                "source_bytes": len(source.encode()),
                "sha256": hashlib.sha256(source.encode()).hexdigest(),
                "call_sites": n*n,
                "expected_combine_signatures": n*n if distinct else 4,
            })
    (root/"manifest.json").write_text(json.dumps(cases, indent=2) + "\n")
    return cases

def trial(moss, case, iteration, cap):
    env = dict(os.environ)
    env["MOSS_PROFILE_COMPILER"] = "1"
    with tempfile.TemporaryDirectory() as temp:
        stats = Path(temp)/"gnu_time.txt"
        command = ["/usr/bin/time", "-f", "%U|%S|%M", "-o", str(stats),
                   str(moss), "check", case["path"], "--json"]
        p = subprocess.Popen(command, env=env, stdout=subprocess.PIPE,
                             stderr=subprocess.PIPE, text=True,
                             start_new_session=True)
        start = time.perf_counter_ns()
        timeout = False
        try:
            stdout, stderr = p.communicate(timeout=cap)
        except subprocess.TimeoutExpired:
            timeout = True
            os.killpg(p.pid, signal.SIGKILL)
            stdout, stderr = p.communicate()
        wall_ms = (time.perf_counter_ns()-start)/1e6
        resource = stats.read_text().strip().split("|") if stats.exists() else []
    user = sys = rss = None
    if len(resource) == 3:
        try:
            user, sys, rss = round(float(resource[0])*1000, 2), round(float(resource[1])*1000, 2), int(resource[2])
        except ValueError:
            pass
    stages = {}
    for stage, ns in TIMERS.findall(stderr):
        stages[stage] = stages.get(stage, 0.0) + int(ns)/1e6
    try:
        accepted = json.loads(stdout).get("ok") is True
    except (ValueError, TypeError):
        accepted = False
    success = (not timeout) and p.returncode == 0 and accepted
    return {**case, "iteration": iteration, "wall_ms": round(wall_ms, 3),
            "cpu_user_ms": user, "cpu_sys_ms": sys, "peak_rss_kib": rss,
            "actual_checked_specializations": None,
            "success": success, "exit_code": p.returncode,
            "stage_ms_json": json.dumps(stages, sort_keys=True),
            "error": ("TIMEOUT" if timeout else (stderr or stdout)[-900:]) if not success else ""}

def report(rows, out, meta):
    lines = ["# Phase 25.4 B1 — C++ torture baseline", "",
             "Compiler SHA: " + meta["git_sha"],
             "Binary SHA256: " + meta["binary_sha256"],
             "OS: " + meta["platform"],
             "Scope: C++ Moss check (semantic-only), no Rust codegen or rustc.",
             "Process mode: fresh C++ process for each trial; NOT a warmed Java daemon.",
             "", "| N | W2 median (ms) | W3 median (ms) | Ratio | Success |",
             "|---:|---:|---:|---:|---|"]
    for n in sorted({r["n"] for r in rows}):
        samples = {label:[r for r in rows if r["n"]==n and r["case"]==label] for label in ("W2","W3")}
        t = {k:[float(r["wall_ms"]) for r in v if r["success"]] for k,v in samples.items()}
        w2 = statistics.median(t["W2"]) if t["W2"] else None
        w3 = statistics.median(t["W3"]) if t["W3"] else None
        fmt = lambda v:"n/a" if v is None else f"{v:.2f}"
        lines.append(f"| {n} | {fmt(w2)} | {fmt(w3)} | {fmt(w2/w3) if w2 is not None and w3 else 'n/a'} | {len(t['W2'])}/{len(samples['W2'])} W2; {len(t['W3'])}/{len(samples['W3'])} W3 |")
    lines += ["", "## Existing C++ stage timers", "",
              "MOSS_PROFILE_COMPILER may time only selected checker sub-stages. Missing time is unattributed, not necessarily parse time.",
              "| Case | N | Median measured stages (ms) |", "|---|---:|---|"]
    for label in ("W2","W3"):
        for n in sorted({r["n"] for r in rows}):
            s = [json.loads(r["stage_ms_json"]) for r in rows if r["n"]==n and r["case"]==label and r["success"]]
            keys = sorted({key for obj in s for key in obj})
            stages = "; ".join(f"{k}={statistics.median(v[k] for v in s if k in v):.2f}" for k in keys) or "No timers emitted"
            lines.append(f"| {label} | {n} | {stages} |")
    lines += ["", "## Limitations / interpretation", "",
              "- The generator knows how many distinct combine signatures it requested, but compiler-observed checked-specialization counts are NOT yet instrumented.",
              "- Parsing volume and call-site count match between W2/W3; concrete signature diversity differs.",
              "- All timing samples, including failures, are in results.csv.",
              "- Stage timers can be nested or incomplete; do not add them to invent percentages.",
              "- No inference about parallel, batch, or incremental speedup from this clean-check baseline.",
              "- Next: if specialization cost is material, add a bounded CPU sample or checker counter; then run deep/wide and module-DAG workloads."]
    (out/"REPORT.md").write_text("\n".join(lines)+"\n")

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("command", choices=["generate","run"])
    parser.add_argument("--sizes", default="8,16,32")
    parser.add_argument("--moss", default="./moss")
    parser.add_argument("--reps", type=int, default=7)
    parser.add_argument("--cap-seconds", type=int, default=40)
    parser.add_argument("--root", type=Path, default=Path(__file__).resolve().parent)
    args=parser.parse_args()
    sizes=[int(x) for x in args.sizes.split(",")]
    if any(n<2 or n>128 for n in sizes):
        parser.error("size must be between 2 and 128")
    cases=fixtures(args.root/"fixtures",sizes)
    if args.command=="generate":
        print(json.dumps({"cases":len(cases),"paths":[c["path"] for c in cases]}))
        return
    moss=Path(args.moss).resolve()
    if not moss.is_file():
        parser.error(f"C++ Moss binary missing: {moss}")
    out=args.root/"results"
    out.mkdir(parents=True,exist_ok=True)
    sha=subprocess.run(["git","rev-parse","HEAD"],capture_output=True,text=True).stdout.strip()
    meta={"git_sha":sha,"binary_sha256":hashlib.sha256(moss.read_bytes()).hexdigest(),
          "platform":platform.platform(),"cpus":os.cpu_count(),
          "moss":str(moss),"stage_timers":"MOSS_PROFILE_COMPILER=1",
          "backend":"moss check <file> --json: semantic-only, no rustc"}
    (out/"metadata.json").write_text(json.dumps(meta,indent=2)+"\n")
    rows=[]
    for iteration in range(1,args.reps+1):
        for n in sizes:
            pair=[c for c in cases if c["n"]==n]
            if iteration%2==0: pair.reverse()
            for case in pair:
                row=trial(moss,case,iteration,args.cap_seconds)
                rows.append(row)
                print("RESULT",json.dumps({k:row[k] for k in ("case","n","iteration","wall_ms","success","error")}),flush=True)
                with (out/"results.csv").open("w",newline="") as f:
                    writer=csv.DictWriter(f,fieldnames=list(rows[0]))
                    writer.writeheader()
                    writer.writerows(rows)
                report(rows,out,meta)
    if any(not r["success"] for r in rows):
        raise SystemExit("Some fixtures failed or timed out. Timings are incomplete.")
    print("REPORT",out/"REPORT.md")

if __name__=="__main__":
    main()
