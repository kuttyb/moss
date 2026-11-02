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
          expected_error: str | None = None, hooks: tuple[str, ...] = (),
          expected_records: tuple[int, int, int] | None = None,
          expected_unsupported: tuple[int, int] | None = None,
          expected_join_predecessors: int = 0) -> None:
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
        expected = (expected_effect, 1)  # 1 == Present (0 == NotRequired)
        if not any((int(access), int(availability)) == expected
                   for _, access, availability in facts):
            raise AssertionError(
                f"{fixture}: expected EST ownership fact {expected}, "
                f"found {facts!r}; stderr: {proc.stderr}")
    records = re.findall(
        r"MOSS_EST_OWNERSHIP_RECORD source_kind=(\d+) .*? access=(\d+) "
        r"inference_status=(\d+) unsupported_reason=(\d+) availability=(\d+)",
        proc.stderr)
    if expected_records:
        source_kind, effect, count = expected_records
        matches = [row for row in records
                   if tuple(map(int, row))[0:3] ==
                   (source_kind, effect, 0) and int(row[4]) == 1]
        if len(matches) < count:
            raise AssertionError(
                f"{fixture}: expected {count} inferred source-kind {source_kind} "
                f"records with effect {effect}, found {records!r}")
    if expected_unsupported:
        source_kind, reason = expected_unsupported
        if not any(int(row[0]) == source_kind and int(row[2]) == 1 and
                   int(row[3]) == reason and int(row[4]) == 0
                   for row in records):
            raise AssertionError(
                f"{fixture}: expected unsupported source-kind {source_kind} "
                f"reason {reason}, found {records!r}")
    if expected_join_predecessors:
        join_records = re.findall(
            r"MOSS_EST_OWNERSHIP_RECORD source_kind=0 definition=.*? "
            r"join=\"([^\"]+)\" .*? inference_status=0 .*? availability=1",
            proc.stderr)
        if max((join_records.count(identity) for identity in set(join_records)),
               default=0) < expected_join_predecessors:
            raise AssertionError(
                f"{fixture}: expected {expected_join_predecessors} inferred "
                f"predecessor facts sharing a join identity, got {join_records!r}")


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
    check(compiler, "if_else_live_join.moss",
          expected_records=(0, 0, 2), expected_join_predecessors=2)
    check(compiler, "if_else_live_write.moss",
          expected_records=(0, 1, 2), expected_join_predecessors=2)
    check(compiler, "if_else_live_consume.moss",
          expected_records=(0, 2, 2), expected_join_predecessors=2)
    check(compiler, "if_else_consume_after_join.moss",
          expected_error="OWNERSHIP_USE_AFTER_CONSUME")
    check(compiler, "control_flow_unsupported.moss",
          expected_unsupported=(0, 1))
    check(compiler, "vector_read_iteration.moss",
          expected_records=(1, 0, 1))
    check(compiler, "vector_write_iteration.moss",
          expected_records=(1, 1, 1))
    check(compiler, "vector_consume_iteration.moss",
          expected_error="OWNERSHIP_VECTOR_ELEMENT_CONSUME")
    check(compiler, "implicit_method_argument_effect.moss")
    check(compiler, "read_alias.moss",
          expected_error="MOSS_INTERNAL_INVARIANT",
          hooks=("MOSS_TEST_DROP_OWNERSHIP_FACT",))
    check(compiler, "read_alias.moss",
          expected_error="MOSS_INTERNAL_INVARIANT",
          hooks=("MOSS_TEST_PERTURB_OWNERSHIP_FACT",))
    print("phase28 ownership facts: PASS (local effects, if/else joins, Vector iteration, negatives, drop/perturb)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
