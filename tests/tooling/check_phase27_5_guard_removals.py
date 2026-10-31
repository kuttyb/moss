#!/usr/bin/env python3
"""Removal test for the retained G2 checked statement-type guard."""
import os
from pathlib import Path
import subprocess
import sys
import time

ROOT = Path(__file__).resolve().parents[2]
COMPILER = Path(sys.argv[1]).resolve()
SOURCE = ROOT / "tests/tooling/fixtures/phase27_5_guard_pipeline.moss"


def invoke(output, fact=None):
    env = os.environ.copy()
    env.pop("MOSS_TEST_DROP_FACT", None)
    if fact and env.get("MOSS_TEST_DISABLE_GUARD_HOOKS") != "1":
        env["MOSS_TEST_DROP_FACT"] = fact
    return subprocess.run([str(COMPILER), str(SOURCE), "-o", str(output)],
                          cwd=ROOT, env=env, text=True,
                          stdout=subprocess.PIPE, stderr=subprocess.PIPE)


def main():
    work = ROOT / "tmp" / "phase27-5-g2-removal" / str(time.time_ns())
    work.mkdir(parents=True)
    ordinary = invoke(work / "ordinary.rs")
    if ordinary.returncode:
        raise SystemExit(f"ordinary emitter path failed: {ordinary.stdout}{ordinary.stderr}")
    dropped = invoke(work / "dropped.rs", "g2")
    if dropped.returncode == 0 or "MOSS_INTERNAL_INVARIANT missing checked statement type" not in dropped.stderr:
        raise SystemExit(f"G2 dropped fact did not fail closed: {dropped.stdout}{dropped.stderr}")
    print("Phase 27.5 G2 removal test passed")


if __name__ == "__main__":
    main()
