#!/usr/bin/env python3
"""Check exact checker authority for bare Vector[index] call arguments."""
from __future__ import annotations

import json
import os
from pathlib import Path
import re
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]
FIXTURE = ROOT / "tests/phase28/r7_010/team_a/indexed_call_argument_read.moss"
TMP = ROOT / "tmp/phase28-indexed-call-argument-read"
TMP.mkdir(parents=True, exist_ok=True)


def run(command: list[str], env: dict[str, str], cwd: Path = ROOT):
    return subprocess.run(command, cwd=cwd, env=env, text=True,
                          stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                          check=False)


def checked_fixture(compiler: Path, env: dict[str, str]):
    result = run([str(compiler), "check", str(FIXTURE), "--json"], env)
    response = json.loads(result.stdout)
    if not response.get("ok"):
        raise AssertionError(f"direct index fixture rejected: {response!r}\n{result.stderr}")
    rows = re.findall(
        r'PHASE28_INDEXED_CALL_READ availability=0 arena=(\d+) '
        r'argument=(\d+) index_node=(\d+) receiver=(\d+) selector=(\d+) '
        r'statement=(\d+) argument_index=0 parameter_index=0 '
        r'target="builtin:assertEqual-v1" target_spec="" target_resolution=0 '
        r'parameter_access=0 receiver_binding="([^"]+)" '
        r'receiver_definition="([^"]+)" receiver_type="vector\[string\]" '
        r'index_type="int" arg_span_line=(\d+) arg_span_col=(\d+) '
        r'arg_span_start=(\d+) arg_span_end=(\d+) index_span_line=(\d+) '
        r'index_span_col=(\d+) index_span_start=(\d+) index_span_end=(\d+) '
        r'element_type="string" copy=0 nonescaping=1 context="([^"]+)" '
        r'est_context="([^"]+)" source_module="([^"]+)"', result.stderr)
    if len(rows) != 1:
        raise AssertionError(f"expected one exact direct-index fact: {rows!r}\n{result.stderr}")
    row = rows[0]
    if row[1] != row[2] or len(set(row[3:5])) != 2 or \
            row[5] == str(2**32 - 1) or not all(row[6:]):
        raise AssertionError(f"direct-index identities/context incomplete: {row!r}")
    if int(row[8]) != int(row[12]) or int(row[10]) >= int(row[11]) or \
            int(row[14]) >= int(row[15]):
        raise AssertionError(f"direct-index source spans invalid: {row!r}")


def main() -> int:
    if len(sys.argv) != 2:
        raise SystemExit("usage: check_phase28_indexed_call_argument_read.py COMPILER")
    compiler = Path(sys.argv[1]).resolve()
    api = TMP / "typed_indexed_call_argument_read_test"
    compiled = run([
        os.environ.get("CXX", "c++"), "-std=c++17", "-Isrc",
        "tests/tooling/typed_indexed_call_argument_read_test.cpp",
        "-o", str(api),
    ], os.environ.copy())
    if compiled.returncode:
        raise AssertionError(f"indexed-read API test compile failed:\n{compiled.stderr}")
    api_result = run([str(api)], os.environ.copy())
    if api_result.returncode:
        raise AssertionError(f"indexed-read API corruptions failed:\n{api_result.stderr}")

    env = {**os.environ, "MOSS_TEST_DUMP_PHASE28_AUTHORITY_FACTS": "1",
           "MOSS_TEST_VALIDATE_PHASE28_AUTHORITY_FACTS": "1"}
    checked_fixture(compiler, env)
    for hook in (
        "MOSS_TEST_DROP_INDEXED_CALL_ARGUMENT_READ_FACT",
        "MOSS_TEST_DUPLICATE_INDEXED_CALL_ARGUMENT_READ_FACT",
        "MOSS_TEST_PERTURB_INDEXED_CALL_ARGUMENT_READ_FACT",
        "MOSS_TEST_DROP_INDEXED_CALL_ARGUMENT_READ_REQUIREMENT",
        "MOSS_TEST_DUPLICATE_INDEXED_CALL_ARGUMENT_READ_REQUIREMENT",
        "MOSS_TEST_PERTURB_INDEXED_CALL_ARGUMENT_READ_REQUIREMENT",
        "MOSS_TEST_PERTURB_INDEXED_CALL_READ_MODULE",
        "MOSS_TEST_CLEAR_INDEXED_CALL_READ_MODULE_BOTH",
    ):
        result = run([str(compiler), "check", str(FIXTURE), "--json"],
                     {**env, hook: "1"})
        response = json.loads(result.stdout)
        if response.get("ok") or response.get("error", {}).get("code") != \
                "MOSS_INTERNAL_OR_IO_ERROR":
            raise AssertionError(f"{hook} did not fail closed: {response!r}\n{result.stderr}")

    regex_source = ROOT / "packages/regex/tests/regex_test.moss"
    regex = run([str(compiler), "check", str(regex_source), "--json"], env)
    regex_result = json.loads(regex.stdout)
    if not regex_result.get("ok"):
        raise AssertionError(f"Regex test checker rejected source: {regex_result!r}")
    regex_rows = [line for line in regex.stderr.splitlines()
                  if line.startswith("PHASE28_INDEXED_CALL_READ ") and
                  'context="test:regex split"' in line]
    if len(regex_rows) != 4 or not any(
            'index_span_line=109 ' in line and
            'receiver_type="vector[string]"' in line and
            'element_type="string"' in line and
            'source_module="module-v1:moss.regex@0.1.0::regex_test"' in line
            for line in regex_rows):
        raise AssertionError(f"Regex split direct-index facts incomplete: {regex_rows!r}")
    print("Phase 28 indexed call-argument read authority: PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
