#!/usr/bin/env python3
"""Focused checker tests for generic collection operation facts."""

from pathlib import Path
import subprocess
import sys

from compiler_test_support import compiler_support_sources

ROOT = Path(__file__).resolve().parents[2]
TMP = ROOT / "tmp"
TMP.mkdir(exist_ok=True)

binary = TMP / "phase25-generic-collection-facts-cpp"
subprocess.run([
    "g++", "-std=c++17", "-O0", "-Wall", "-Wextra", "-Werror",
    "-Wno-missing-field-initializers", "-pedantic",
    str(ROOT / "tests/tooling/check_phase25_generic_collection_facts.cpp"),
    *compiler_support_sources(),
    "-o", str(binary),
], check=True)
subprocess.run([str(binary)], check=True)
