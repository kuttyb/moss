#!/usr/bin/env python3
"""Verify checker-published specialization identities and exact ownership coverage."""
from __future__ import annotations

import os
from pathlib import Path
import re
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]
TMP = ROOT / "tmp/phase28_specialized_ownership"
TMP.mkdir(parents=True, exist_ok=True)


def run(command: list[str], **kwargs) -> subprocess.CompletedProcess[str]:
    return subprocess.run(command, cwd=ROOT, text=True, capture_output=True,
                          check=False, **kwargs)


def main() -> int:
    if len(sys.argv) != 2:
        raise SystemExit("usage: check_phase28_specialized_ownership_contract.py COMPILER")
    compiler = sys.argv[1]

    executable = TMP / "typed_local_ownership_contract_test"
    compile_api = run([
        os.environ.get("CXX", "c++"), "-std=c++17", "-Isrc",
        "tests/tooling/typed_local_ownership_contract_test.cpp",
        "-o", str(executable),
    ])
    if compile_api.returncode:
        raise AssertionError(f"typed ownership API test did not compile:\n{compile_api.stderr}")
    api_test = run([str(executable)])
    if api_test.returncode:
        raise AssertionError(f"typed ownership API test failed:\n{api_test.stderr}")

    env = os.environ.copy()
    env["MOSS_EST_FACTS_DUMP"] = "1"
    env["MOSS_TEST_VALIDATE_LOCAL_OWNERSHIP_CONTRACT"] = "1"
    proc = run([compiler, "check",
                "tests/phase28/team_a_ownership/specialized_identity.moss", "--json"],
               env=env)
    if proc.returncode:
        raise AssertionError(f"specialized fixture rejected:\n{proc.stdout}\n{proc.stderr}")

    records = re.findall(
        r'MOSS_EST_OWNERSHIP_RECORD .*? definition="([^"]*alias[^"]*)" '
        r'.*? expr=(\d+) context="([^"]+)" specialization="([^"]*)" '
        r'statement=(\d+)', proc.stderr)
    requirements = re.findall(
        r'MOSS_EST_OWNERSHIP_REQUIREMENT arena=(\d+) expr=(\d+) '
        r'statement=(\d+) context="([^"]+)" specialization="([^"]*)" '
        r'definition="([^"]*alias[^"]*)"', proc.stderr)
    specialized = [row for row in records if row[3]]
    if len(specialized) != 2:
        raise AssertionError(f"expected two specialized alias facts, got {records!r}")
    identities = {row[3] for row in specialized}
    contexts = {row[2] for row in specialized}
    if len(identities) != 2 or len(contexts) != 2:
        raise AssertionError(f"specializations cross-share identity/context: {specialized!r}")

    # Requirements must match their facts on exact EST, statement, context,
    # stable specialization identity and definition identity.
    for definition, expr, context, identity, statement in specialized:
        match = [req for req in requirements
                 if req[1] == expr and req[2] == statement and
                 req[3] == context and req[4] == identity and req[5] == definition]
        if len(match) != 1:
            raise AssertionError(
                f"expected one exact requirement for {(expr, statement, context, identity, definition)!r}; "
                f"got {match!r}")
    if not all(context.startswith("fn:inspect<") for context in contexts):
        raise AssertionError(f"consumer context aliases were not checker-mapped: {contexts!r}")

    print("phase28 specialized ownership contract: PASS (typed exact lookup, two specializations, ambiguity, drop/perturb, unsupported)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
