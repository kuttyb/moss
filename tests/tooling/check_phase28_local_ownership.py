#!/usr/bin/env python3
"""Exercise the Phase 28 checker-owned EST local ownership slice."""
from __future__ import annotations

import json
import os
from pathlib import Path
import re
import subprocess
import sys


ROOT = Path(__file__).resolve().parents[2]
FIXTURES = ROOT / "tests/phase28/team_a_ownership"


def check(compiler: str, fixture: str, expected_effect: int | None = None,
          expected_error: str | None = None, hooks: tuple[str, ...] = ()) -> None:
    env = os.environ.copy()
    env["MOSS_EST_FACTS_DUMP"] = "1"
    env.pop("MOSS_TEST_DROP_OWNERSHIP_FACT", None)
    env.pop("MOSS_TEST_PERTURB_OWNERSHIP_FACT", None)
    for hook in hooks:
        env[hook] = "1"
    proc = subprocess.run(
        [compiler, "check", str(FIXTURES / fixture), "--json"],
        cwd=ROOT, text=True, capture_output=True, env=env, check=False,
    )
    try:
        result = json.loads(proc.stdout)
    except json.JSONDecodeError as error:
        raise AssertionError(
            f"{fixture}: invalid structured checker output: {proc.stdout!r}\n"
            f"stderr: {proc.stderr}") from error
    error = result.get("error", {}).get("code")
    if expected_error:
        if result.get("ok") or error != expected_error:
            raise AssertionError(
                f"{fixture}: expected {expected_error}, got {result!r}")
        return
    if not result.get("ok"):
        raise AssertionError(f"{fixture}: expected accepted, got {result!r}")
    if expected_effect is not None:
        facts = re.findall(
            r"MOSS_EST_OWNERSHIP_FACT .*? expr=(\d+) .*? access=(\d+) "
            r"availability=(\d+)", proc.stderr)
        expected = (expected_effect, 0)  # 0 == Present
        if not any((int(access), int(availability)) == expected
                   for _, access, availability in facts):
            raise AssertionError(
                f"{fixture}: expected EST ownership fact {expected}, "
                f"found {facts!r}; stderr: {proc.stderr}")


def main() -> int:
    if len(sys.argv) != 2:
        raise SystemExit("usage: check_phase28_local_ownership.py COMPILER")
    compiler = sys.argv[1]
    check(compiler, "read_alias.moss", expected_effect=0)
    check(compiler, "write_alias.moss", expected_effect=1)
    check(compiler, "consume_transfer.moss", expected_effect=2)
    check(compiler, "use_after_move.moss",
          expected_error="OWNERSHIP_USE_AFTER_CONSUME")
    check(compiler, "surviving_field_demand.moss",
          expected_error="MOSS_COMPILE_ERROR")
    check(compiler, "read_alias.moss",
          expected_error="MOSS_INTERNAL_INVARIANT",
          hooks=("MOSS_TEST_DROP_OWNERSHIP_FACT",))
    check(compiler, "read_alias.moss",
          expected_error="MOSS_INTERNAL_INVARIANT",
          hooks=("MOSS_TEST_PERTURB_OWNERSHIP_FACT",))
    print("phase28 local ownership facts: PASS (3 effects, 2 negatives, drop/perturb)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
