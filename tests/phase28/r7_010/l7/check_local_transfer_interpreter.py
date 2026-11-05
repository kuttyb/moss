#!/usr/bin/env python3
"""Check interpreter transfer behavior and fail-closed fact consumption."""
from pathlib import Path
import os
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[4]
FIXTURE = ROOT / "tests/phase28/r7_010/l7/local_transfer_interpreter.moss"
MOSS = Path(sys.argv[1]).resolve()


def run(env=None, expect=0):
    proc = subprocess.run(
        [MOSS, "run", "--interp", FIXTURE], cwd=ROOT,
        env=env, text=True, capture_output=True, check=False, timeout=60)
    if proc.returncode != expect:
        raise RuntimeError(
            f"interpreter returned {proc.returncode}, expected {expect}\n"
            f"stdout:\n{proc.stdout}\nstderr:\n{proc.stderr}")
    return proc.stdout.strip(), proc.stderr


assert run()[0] == "1"
for corruption in (
        {"MOSS_TEST_CONSUMER_DROP_LOCAL_TRANSFER_FACT": "1"},
        {"MOSS_TEST_DROP_LOCAL_TRANSFER_FACT": "1"},
        {"MOSS_TEST_PERTURB_LOCAL_TRANSFER_FACT": "liveness"},
        {"MOSS_TEST_DUPLICATE_LOCAL_TRANSFER_FACT": "1"},
):
    env = dict(os.environ)
    env.update(corruption)
    stdout, stderr = run(env, expect=1)
    assert "MOSS_INTERNAL_INVARIANT" in stdout + stderr, corruption

print("Fast Debug local transfer and four fail-closed corruptions PASS")
