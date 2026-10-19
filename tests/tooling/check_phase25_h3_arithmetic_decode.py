#!/usr/bin/env python3
"""Compile and run focused direct tests for the checked-body importer."""

from pathlib import Path
import subprocess


ROOT = Path(__file__).resolve().parents[2]
TMP = ROOT / "tmp"
TMP.mkdir(exist_ok=True)
source = ROOT / "tests" / "tooling" / "check_phase25_h3_arithmetic_decode.cpp"
binary = TMP / "phase25-h3-arithmetic-decode"
subprocess.run([
    "g++", "-std=c++17", "-O0", "-Wall", "-Wextra", "-Werror",
    "-Wno-missing-field-initializers", "-pedantic",
    str(source), "-o", str(binary),
], check=True)
subprocess.run([str(binary)], check=True)
