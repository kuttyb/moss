#!/usr/bin/env python3
"""Focused checker-level coverage for generic arithmetic requirement capture."""

import json
from pathlib import Path
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]
MOSS = Path(sys.argv[1]).resolve() if len(sys.argv) > 1 else ROOT / "moss"
TMP = ROOT / "tmp"
TMP.mkdir(exist_ok=True)

cpp_test = ROOT / "tests/tooling/check_phase25_generic_arithmetic_requirements.cpp"
cpp_binary = TMP / "phase25-generic-arithmetic-requirements-cpp"
subprocess.run([
    "g++", "-std=c++17", "-O0", "-Wall", "-Wextra", "-Werror",
    "-Wno-missing-field-initializers", "-pedantic", str(cpp_test),
    "-o", str(cpp_binary),
], check=True)
subprocess.run([str(cpp_binary)], check=True)

with tempfile.TemporaryDirectory(prefix="phase25-generic-arithmetic-",
                                 dir=TMP) as scratch:
    source = Path(scratch) / "compute.moss"
    source.write_text("""export fn compute(x):
  return x * 2 + 20

fn main():
  echo compute(5)
""")
    run = subprocess.run([str(MOSS), "run", "--interp", str(source)],
                         text=True, capture_output=True)
    assert run.returncode == 0, (run.stdout, run.stderr)
    assert run.stdout == "30\n", run.stdout
    inspected = subprocess.run([str(MOSS), "inspect", "fn:compute",
        "--source", str(source), "--json"], text=True, capture_output=True)
    assert inspected.returncode == 0, (inspected.stdout, inspected.stderr)
    projection = json.loads(inspected.stdout)["result"]["typed_semantic_v1"]
    compute_gap = [entry for entry in projection["unavailable_open_summaries"]
                   if entry["callable_id"].endswith(":fn:compute")]
    assert len(compute_gap) == 1, projection
    assert compute_gap[0]["gap"] == "missing_typed_facts", compute_gap

    nonnumeric = Path(scratch) / "nonnumeric.moss"
    nonnumeric.write_text("""fn compute(x):
  return x * 2 + 20

fn main():
  echo compute("bad")
""")
    rejected = subprocess.run([str(MOSS), "check", str(nonnumeric), "--json"],
                              text=True, capture_output=True)
    assert rejected.returncode != 0, (rejected.stdout, rejected.stderr)
    diagnostic = json.loads(rejected.stdout)
    assert not diagnostic.get("ok", True), diagnostic
    print("PASS source-backed compute(5) returns 30; nonnumeric specialization remains rejected")
