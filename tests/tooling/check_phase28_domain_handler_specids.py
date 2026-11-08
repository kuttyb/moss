#!/usr/bin/env python3
"""Verify instance-specific checker authority for specialized domain handlers."""

from pathlib import Path
import subprocess

from compiler_test_support import compiler_support_sources

ROOT = Path(__file__).resolve().parents[2]
TMP = ROOT / "tmp"
TMP.mkdir(exist_ok=True)

binary = TMP / "phase28-domain-handler-specids-cpp"
subprocess.run([
    "g++", "-std=c++17", "-O0", "-Wall", "-Wextra", "-Werror",
    "-Wno-missing-field-initializers", "-pedantic", "-DMOSS_TEST_BUILD=1",
    str(ROOT / "tests/tooling/check_phase28_domain_handler_specids.cpp"),
    *compiler_support_sources(), "-o", str(binary),
], check=True)
subprocess.run([str(binary)], check=True)
