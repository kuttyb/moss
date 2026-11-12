#!/usr/bin/env python3
"""`moss check` parity: every .moss under tests/, examples/, benchmarks/.

usage: check_parity.py <base-moss> <candidate-moss> [--shadow] [--jobs N] [--files LIST]
  --shadow  run the candidate with MOSS_SUMT_SHADOW=/dev/null (shadow on)
  --files   check only the .moss paths listed in LIST (one per line)
Prints the number of files whose exit code or output differ, then each one.
"""
import argparse, concurrent.futures as cf, os, pathlib, subprocess

parser = argparse.ArgumentParser()
parser.add_argument("base"); parser.add_argument("candidate")
parser.add_argument("--shadow", action="store_true")
parser.add_argument("--jobs", type=int, default=os.cpu_count() or 4)
parser.add_argument("--files")
args = parser.parse_args()
if args.files:
    files = sorted(pathlib.Path(l.strip()) for l in open(args.files) if l.strip())
else:
    files = sorted(p for d in ("tests", "examples", "benchmarks")
                   for p in pathlib.Path(d).rglob("*.moss") if "build" not in p.parts)

def run(binary, path, shadow):
    env = dict(os.environ); env.pop("MOSS_SUMT_SHADOW", None); env.pop("MOSS_SUMT_VALIDATE", None)
    if shadow: env["MOSS_SUMT_SHADOW"] = "/dev/null"
    try:
        r = subprocess.run([binary, "check", str(path)], capture_output=True, text=True,
                           timeout=120, env=env)
        return r.returncode, r.stdout + r.stderr
    except subprocess.TimeoutExpired:
        return "timeout", ""

def one(path):
    return path, run(args.base, path, False), run(args.candidate, path, args.shadow)

diff = []
with cf.ThreadPoolExecutor(args.jobs) as pool:
    for path, a, b in pool.map(one, files):
        if a != b: diff.append((str(path), a, b))
print("files", len(files), "differ", len(diff))
for path, a, b in diff:
    print("DIFF", path, "\n  base:", a[0], a[1].strip()[-160:], "\n  cand:", b[0], b[1].strip()[-160:])
