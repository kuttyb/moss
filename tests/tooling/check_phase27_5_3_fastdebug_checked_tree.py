#!/usr/bin/env python3
"""Check Fast Debug executes checked EST nodes without consumer reparsing."""
from __future__ import annotations

import os
from pathlib import Path
import re
import subprocess
import sys
import time

ROOT = Path(__file__).resolve().parents[2]
COMPILER = Path(sys.argv[1]).resolve()
FIXTURES = [
    ROOT / "examples/functional_basics.moss",
    ROOT / "tests/phase27/est/specialized_call_facts.moss",
    ROOT / "tests/swarm_001_parenthesized_pipeline.moss",
]


def run(args: list[str], env: dict[str, str]) -> subprocess.CompletedProcess[str]:
    return subprocess.run(args, cwd=ROOT, env=env, text=True,
                          capture_output=True, timeout=120)


source = (ROOT / "src/interpreter.hpp").read_text(encoding="utf-8")
for forbidden in ("est::parse_expression(", "est::parse_view(",
                  "est::parse_functional_pipeline("):
    if forbidden in source:
        raise SystemExit(f"Fast Debug still calls a parser API: {forbidden}")

work = ROOT / "tmp/phase27-5-3-fastdebug" / str(time.time_ns())
work.mkdir(parents=True)
for fixture in FIXTURES:
    env = os.environ.copy()
    env.pop("MOSS_EST", None)
    env["MOSS_EST_COVERAGE_STATS"] = "1"
    checked = run([str(COMPILER), "check", str(fixture), "--json"], env)
    if checked.returncode:
        raise SystemExit(f"checker rejected {fixture}: {checked.stderr}")
    fast = run([str(COMPILER), "run", "--interp", str(fixture)], env)
    if fast.returncode:
        raise SystemExit(f"Fast Debug failed for {fixture}: {fast.stderr}")
    counts = re.search(
        r"MOSS_EST_REPARSES .*fastdebug_expression_tree=(\d+) "
        r"fastdebug_functional_pipeline=(\d+)", fast.stderr)
    if not counts or tuple(map(int, counts.groups())) != (0, 0):
        raise SystemExit(
            f"Fast Debug reparsed checked expressions for {fixture}: {fast.stderr}")

    rust = work / f"{fixture.stem}.rs"
    emitted = run([str(COMPILER), str(fixture), "-o", str(rust)], env)
    if emitted.returncode:
        raise SystemExit(f"emitter failed for {fixture}: {emitted.stderr}")
    outputs = [fast.stdout]
    for label, flags in (("native", []), ("optimized", ["-O"])):
        binary = work / f"{fixture.stem}-{label}"
        compiled = subprocess.run(
            ["rustc", "--edition=2021", *flags, str(rust), "-o", str(binary)],
            cwd=ROOT, text=True, capture_output=True, timeout=120)
        if compiled.returncode:
            raise SystemExit(f"rustc failed for {fixture} ({label}): {compiled.stderr}")
        executed = subprocess.run([str(binary)], cwd=ROOT, text=True,
                                  capture_output=True, timeout=120)
        if executed.returncode:
            raise SystemExit(f"native failed for {fixture} ({label}): {executed.stderr}")
        outputs.append(executed.stdout)
    if outputs[0] != outputs[1] or outputs[0] != outputs[2]:
        raise SystemExit(f"three-engine output mismatch for {fixture}: {outputs!r}")

print("PASS Fast Debug checked-tree execution: zero reparses and three-engine parity")
