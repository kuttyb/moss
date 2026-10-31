#!/usr/bin/env python3
"""D1/D2 Fast Debug and native regression coverage for Phase 27.5."""
import os
import json
from pathlib import Path
import re
import subprocess
import sys
import time

ROOT = Path(__file__).resolve().parents[2]
COMPILER = Path(sys.argv[1]).resolve()
FOR_FIXTURE = ROOT / "tests/tooling/fixtures/phase27_5_for.moss"


def run(args, *, env=None):
    return subprocess.run(args, cwd=ROOT, env=env, text=True,
                          stdout=subprocess.PIPE, stderr=subprocess.PIPE)


def require_ok(result, label):
    if result.returncode:
        raise SystemExit(f"{label} failed:\n{result.stdout}{result.stderr}")


def parity(source, work):
    checked = run([str(COMPILER), "check", str(source), "--json"])
    require_ok(checked, f"check {source.name}")
    if not json.loads(checked.stdout).get("ok"):
        raise SystemExit(f"checker rejected {source.name}: {checked.stdout}")
    fast = run([str(COMPILER), "run", "--interp", str(source)])
    require_ok(fast, f"Fast Debug {source.name}")
    trace_out = run([str(COMPILER), "run", "--trace", "--interp", str(source)])
    require_ok(trace_out, f"Fast Debug trace {source.name}")
    rust = work / f"{source.stem}.rs"
    emitted = run([str(COMPILER), str(source), "-o", str(rust)])
    require_ok(emitted, f"emit {source.name}")
    binary = work / source.stem
    compiled = run(["rustc", "--edition=2021", str(rust), "-o", str(binary)])
    require_ok(compiled, f"rustc {source.name}")
    native = run([str(binary)])
    require_ok(native, f"native {source.name}")
    if fast.stdout != native.stdout or trace_out.stdout != native.stdout:
        raise SystemExit(f"Fast Debug/native output mismatch for {source.name}: "
                         f"fast={fast.stdout!r}, native={native.stdout!r}")
    return rust.read_text(encoding="utf-8"), trace_out.stderr


def main():
    work = ROOT / "tmp" / "phase27-5-for-match" / str(time.time_ns())
    work.mkdir(parents=True)
    env = os.environ.copy()
    _, trace = parity(FOR_FIXTURE, work)
    if not re.search(r'"event":"LocalWrite"[^\n]*"detail":"item"', trace):
        raise SystemExit("For trace is missing a LocalWrite for its loop variable")
    print("Phase 27.5 D1 targeted fixture passed")


if __name__ == "__main__":
    main()
