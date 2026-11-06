#!/usr/bin/env python3
"""Check checker authority for None returned from an Option method."""
from __future__ import annotations

import json
import os
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]
FIXTURE = ROOT / "tests/phase28/r7_010/team_a/return_none_fact.moss"


def run(command: list[str], env: dict[str, str]):
    return subprocess.run(command, cwd=ROOT, env=env, text=True,
                          stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                          check=False)


def main() -> int:
    if len(sys.argv) != 2:
        raise SystemExit("usage: check_phase28_return_none_authority.py COMPILER")
    compiler = str(Path(sys.argv[1]).resolve())
    base_env = os.environ.copy()
    env = {**base_env, "MOSS_TEST_DUMP_PHASE28_AUTHORITY_FACTS": "1",
           "MOSS_TEST_VALIDATE_PHASE28_AUTHORITY_FACTS": "1"}
    checked = run([compiler, "check", "--json", str(FIXTURE)], env)
    response = json.loads(checked.stdout)
    if checked.returncode or not response.get("ok"):
        raise AssertionError(f"None return checker rejected fixture: {response!r}\n"
                             f"{checked.stderr}")
    if "PHASE28_AUTHORITY_FACTS" not in checked.stderr or "return=2" not in checked.stderr:
        raise AssertionError(f"expected both checked return facts, got {checked.stderr!r}")

    for hook in ("MOSS_TEST_DROP_RETURN_VALUE_FACT",
                 "MOSS_TEST_PERTURB_RETURN_VALUE_FACT",
                 "MOSS_TEST_PERTURB_RETURN_VALUE_TYPE_ORIGIN"):
        failed = run([compiler, "check", "--json", str(FIXTURE)],
                     {**env, hook: "1"})
        result = json.loads(failed.stdout)
        if result.get("ok") or result.get("error", {}).get("code") != \
                "MOSS_INTERNAL_OR_IO_ERROR":
            raise AssertionError(f"{hook} did not fail closed: {result!r}\n"
                                 f"{failed.stderr}")
    print("Phase 28 None return-value authority: PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
