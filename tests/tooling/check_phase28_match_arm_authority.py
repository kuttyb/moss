#!/usr/bin/env python3
"""Check complete typed ownership facts for every immediate match arm."""

import json
import os
import subprocess
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
COMPILER = str(Path(sys.argv[1]).resolve())
SOURCE = "tests/tooling/fixtures/phase28_match_nested_arm_authority.moss"


def run(extra_env=None):
    env = os.environ.copy()
    for key in tuple(env):
        if key.startswith("MOSS_TEST_"):
            env.pop(key, None)
    env.update({"MOSS_TEST_VALIDATE_MATCH_CONTRACT": "1"})
    env.update(extra_env or {})
    return subprocess.run(
        [COMPILER, "check", SOURCE, "--json"], cwd=ROOT, env=env,
        text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE, check=False
    )


def parsed(result):
    try:
        return json.loads(result.stdout)
    except json.JSONDecodeError as error:
        raise AssertionError(result.stdout + result.stderr) from error


def main():
    baseline = run({"MOSS_TEST_DUMP_MATCH_AUTHORITY_FACTS": "1"})
    response = parsed(baseline)
    assert baseline.returncode == 0 and response.get("ok"), baseline.stdout
    records = [line.removeprefix("MATCH_AUTHORITY_FACT ")
               for line in baseline.stderr.splitlines()
               if line.startswith("MATCH_AUTHORITY_FACT ")]
    by_case = {record.split("case=", 1)[1].split(" ", 1)[0]: record
               for record in records}
    assert len(records) == 2, records
    assert set(by_case) == {"NoMatch", "Matched"}, records
    assert "bindings= identities= types= access=1 transfer=0" in by_case["NoMatch"]
    matched = by_case["Matched"]
    for field in ("bindings=start,end", "types=int,int", "access=1", "transfer=0"):
        assert field in matched, (field, matched)
    identities = matched.split("identities=", 1)[1].split(" types=", 1)[0]
    assert len(identities.split(",")) == 2 and all(identities.split(",")), matched

    corruptions = [
        ({"MOSS_TEST_DROP_MATCH_FACT": "1"},
         "required checked match fact missing"),
        ({"MOSS_TEST_PERTURB_MATCH_ARM_CASE": "Matched"},
         "contradicts requirement"),
        ({"MOSS_TEST_DUPLICATE_MATCH_FACT": "1"},
         "duplicate or contradictory checked match facts"),
        ({"MOSS_TEST_PERTURB_MATCH_ARM_FIELD_FOR_CASE": "Matched:payload_type"},
         "contradicts requirement"),
        ({"MOSS_TEST_PERTURB_MATCH_ARM_FIELD_FOR_CASE": "Matched:payload_binding_name"},
         "contradicts requirement"),
        ({"MOSS_TEST_PERTURB_MATCH_ARM_FIELD_FOR_CASE": "Matched:payload_binding_identity"},
         "contradicts requirement"),
        ({"MOSS_TEST_PERTURB_MATCH_ARM_FIELD_FOR_CASE": "Matched:payload_access"},
         "contradicts requirement"),
        ({"MOSS_TEST_PERTURB_MATCH_ARM_FIELD_FOR_CASE": "Matched:transfers_ownership"},
         "contradicts requirement"),
    ]
    for env, expected in corruptions:
        result = run(env)
        assert result.returncode != 0, (env, result.stdout)
        assert expected in result.stdout + result.stderr, (env, result.stdout, result.stderr)

    print("Phase 28 match-arm ownership authority: 9 scenarios PASS")


if __name__ == "__main__":
    main()
