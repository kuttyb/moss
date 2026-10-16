#!/usr/bin/env python3
"""Structured Type IR parser and solver regression."""
from pathlib import Path
import subprocess

root = Path(__file__).resolve().parents[2]
output = root / "tmp" / "phase25-type-ir-test"
output.parent.mkdir(exist_ok=True)
subprocess.run(["g++", "-std=c++17", "-O0", "-Wall", "-Wextra", "-Werror", "-pedantic",
                str(root / "tests/tooling/check_phase25_type_ir.cpp"), "-o", str(output)], check=True)
subprocess.run([str(output)], check=True)
print("Phase 25 structured Type IR: PASS")
