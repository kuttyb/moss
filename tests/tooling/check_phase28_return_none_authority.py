#!/usr/bin/env python3
"""Check checker authority for None returned from an Option method."""
from __future__ import annotations

import json
import os
from pathlib import Path
import re
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]
FIXTURE = ROOT / "tests/phase28/r7_010/team_a/return_none_fact.moss"
SHADOWED_FIXTURE = ROOT / "tests/phase28/r7_010/team_a/return_shadowed_none.moss"


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

    shadowed = run([compiler, "check", "--json", str(SHADOWED_FIXTURE)], env)
    shadow_response = json.loads(shadowed.stdout)
    if shadowed.returncode or not shadow_response.get("ok"):
        raise AssertionError(f"shadowed None binding was rejected: {shadow_response!r}\n"
                             f"{shadowed.stderr}")
    shadow_rows = re.findall(r"PHASE28_RETURN_FACT ([^\n]+)", shadowed.stderr)
    if not any(re.search(r"binding=\S+", row) and "type_origin=0" in row
               for row in shadow_rows):
        raise AssertionError(f"shadowed None did not use checked binding authority: "
                             f"{shadow_rows!r}")
    interpreted_shadow = run([compiler, "run", "--interp", str(SHADOWED_FIXTURE)],
                             base_env)
    if interpreted_shadow.returncode or interpreted_shadow.stdout != "7\n":
        raise AssertionError("shadowed None runtime mismatch: "
                             f"{interpreted_shadow.returncode}, "
                             f"{interpreted_shadow.stdout!r}, "
                             f"{interpreted_shadow.stderr!r}")
    print("Phase 28 None return-value authority: PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
