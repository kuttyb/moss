#!/usr/bin/env python3
"""Assert direct-call and statement-sort occurrence roles in checker state."""

from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[2]
TMP = ROOT / "tmp"
TMP.mkdir(exist_ok=True)

binary = TMP / "phase25-statement-call-occurrence-roles-cpp"
subprocess.run([
    "g++", "-std=c++17", "-O0", "-Wall", "-Wextra", "-Werror",
    "-Wno-missing-field-initializers", "-pedantic",
    str(ROOT / "tests/tooling/check_phase25_statement_call_occurrence_roles.cpp"),
    "-o", str(binary),
], check=True)
subprocess.run([str(binary)], check=True)
