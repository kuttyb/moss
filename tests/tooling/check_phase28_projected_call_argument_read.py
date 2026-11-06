#!/usr/bin/env python3
"""Check exact checker authority for projected collection call arguments."""
from __future__ import annotations

import json
import os
from pathlib import Path
import re
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]
FIXTURE = ROOT / "tests/phase28/team_a_ownership/projected_call_argument_read.moss"
PACKAGE_FIXTURE = ROOT / "tests/phase28/team_a_ownership/projected_call_package"
TMP = ROOT / "tmp/team-a/projected-call-argument-read"
TMP.mkdir(parents=True, exist_ok=True)


def run(command: list[str], env: dict[str, str]) -> subprocess.CompletedProcess[str]:
    return subprocess.run(command, cwd=ROOT, env=env, text=True,
                          stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                          check=False)


def main() -> int:
    if len(sys.argv) != 2:
        raise SystemExit("usage: check_phase28_projected_call_argument_read.py COMPILER")
    compiler = sys.argv[1]
    compiler_path = Path(compiler)
    if not compiler_path.is_absolute():
        compiler_path = (ROOT / compiler_path).resolve()
    api_executable = TMP / "typed_projected_call_argument_read_test"
    api_compile = run([
        os.environ.get("CXX", "c++"), "-std=c++17", "-Isrc",
        "tests/tooling/typed_projected_call_argument_read_test.cpp",
        "-o", str(api_executable),
    ], os.environ.copy())
    if api_compile.returncode:
        raise AssertionError(f"typed projected-read API test failed to compile:\n{api_compile.stderr}")
    api_test = run([str(api_executable)], os.environ.copy())
    if api_test.returncode:
        raise AssertionError(f"typed projected-read API corruption test failed:\n{api_test.stderr}")
    env = os.environ.copy()
    env["MOSS_EST_FACTS_DUMP"] = "1"
    env["MOSS_TEST_DUMP_PHASE28_AUTHORITY_FACTS"] = "1"
    env["MOSS_TEST_VALIDATE_PHASE28_AUTHORITY_FACTS"] = "1"
    checked = run([compiler, "check", str(FIXTURE), "--json"], env)
    result = json.loads(checked.stdout)
    if not result.get("ok"):
        raise AssertionError(
            f"projected call argument fixture rejected: {result!r}\n{checked.stderr}")
    row_pattern = re.compile(
        r'PHASE28_PROJECTED_CALL_READ availability=0 arena=(\d+) '
        r'argument=(\d+) receiver=(\d+) index_node=(\d+) '
        r'index=(\d+) member=(\d+) statement=(\d+) argument_index=0 '
        r'parameter_index=0 target="builtin:assertEqual-v1" target_spec="" '
        r'target_resolution=0 '
        r'parameter_access=0 receiver_binding="([^"]+)" '
        r'receiver_definition="([^"]+)" receiver_type="vector\[MatchSpan\]" '
        r'index_type="int" element_type="MatchSpan" member_type="string" '
        r'copy=0 nonescaping=1 context="([^"]+)" est_context="([^"]+)"')
    rows = row_pattern.findall(checked.stderr)
    if len(rows) != 1:
        raise AssertionError(
            f"expected one exact nonescaping READ/no-copy fact, got {rows!r}\n"
            f"{checked.stderr}")
    row = rows[0]
    if row[1] != row[5] or len(set(row[2:5])) != 3 or not all(row[7:]):
        raise AssertionError(f"projected call fact omitted or aliased identity fields: {row!r}")

    for hook in (
        "MOSS_TEST_DROP_PROJECTED_CALL_ARGUMENT_READ_FACT",
        "MOSS_TEST_DUPLICATE_PROJECTED_CALL_ARGUMENT_READ_FACT",
        "MOSS_TEST_PERTURB_PROJECTED_CALL_ARGUMENT_READ_FACT",
        "MOSS_TEST_DROP_PROJECTED_CALL_ARGUMENT_READ_REQUIREMENT",
        "MOSS_TEST_DUPLICATE_PROJECTED_CALL_ARGUMENT_READ_REQUIREMENT",
        "MOSS_TEST_PERTURB_PROJECTED_CALL_ARGUMENT_READ_REQUIREMENT",
        "MOSS_TEST_PERTURB_PROJECTED_CALL_READ_MODULE",
        "MOSS_TEST_CLEAR_PROJECTED_CALL_READ_MODULE_BOTH",
    ):
        corrupted = run([compiler, "check", str(FIXTURE), "--json"],
                        {**env, hook: "1"})
        response = json.loads(corrupted.stdout)
        if response.get("ok") or response.get("error", {}).get("code") != \
                "MOSS_INTERNAL_OR_IO_ERROR":
            raise AssertionError(
                f"{hook} did not fail closed: {response!r}\n{corrupted.stderr}")
    package_check = subprocess.run(
        [str(compiler_path), "test", "--check", "--json"], cwd=PACKAGE_FIXTURE,
        env={**os.environ, "MOSS_TEST_DUMP_PHASE28_AUTHORITY_FACTS": "1"},
        text=True, stdout=subprocess.PIPE,
        stderr=subprocess.PIPE, check=False)
    package_result = json.loads(package_check.stdout)
    if not package_result.get("ok"):
        raise AssertionError(
            "package test projected-call provenance check failed: "
            f"{package_result!r}\n{package_check.stderr}")
    package_rows = re.findall(
        r'PHASE28_PROJECTED_CALL_READ availability=0 .*?'
        r'source_module="([^"]+)"', package_check.stderr)
    expected_module = (
        "module-v1:moss.projected-call-read-test@0.1.0::projected_read_test")
    expected_no_import_module = (
        "module-v1:moss.projected-call-read-test@0.1.0::projected_read_no_import")
    if sorted(package_rows) != sorted(
            [expected_module, expected_no_import_module]):
        raise AssertionError(
            "package tests projected-call facts did not retain their exact source "
            f"module identity: {package_rows!r}\n{package_check.stderr}")
    print("Phase 28 projected call-argument read authority: PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
