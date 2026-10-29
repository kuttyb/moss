#!/usr/bin/env python3
"""Focused EST expression dump and off-allocation regression checks."""
import os
import pathlib
import re
import subprocess
import sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
COMPILER = pathlib.Path(sys.argv[1]).resolve()
CASES = {
    "message_expression.moss": ("message#", '"Run"', 'literal#'),
    "bitwise_shift.moss": ('binary#', '"<<"', '"&"'),
    "numeric_member.moss": ('member#', '"to_string"', 'literal#'),
}

def run(path, mode, **extra):
    env = os.environ.copy()
    env.update(extra)
    env["MOSS_EST"] = mode
    return subprocess.run([str(COMPILER), "check", str(path), "--json"], cwd=ROOT,
                          env=env, text=True, stdout=subprocess.PIPE,
                          stderr=subprocess.PIPE, check=False)

def main():
    for name, pieces in CASES.items():
        path = ROOT / "tests/phase27/est" / name
        result = run(path, "shadow", MOSS_EST_DUMP="1")
        if result.returncode != 0:
            raise SystemExit(f"shadow check failed for {name}: {result.stdout}{result.stderr}")
        dumps = [line for line in result.stderr.splitlines() if line.startswith("MOSS_EST_DUMP ")]
        if not dumps or not all(piece in "\n".join(dumps) for piece in pieces):
            raise SystemExit(f"missing expected tree fields for {name}: {dumps}")
        for dump in dumps:
            ids = [int(x) for x in re.findall(r"#(\d+)", dump)]
            if len(ids) != len(set(ids)):
                raise SystemExit(f"ExprId reused within one occurrence in {name}: {ids}")
    probe = ROOT / "tests/phase27/est/bitwise_shift.moss"
    result = run(probe, "off", MOSS_EST_STATS="1")
    if result.returncode != 0 or "MOSS_EST_STATS arena_size=0 handle_count=0" not in result.stderr:
        raise SystemExit(f"off allocated EST storage: {result.stdout}{result.stderr}")
    print("phase27 EST focused checks passed (3 dumps; off arena size 0)")

if __name__ == "__main__":
    main()
