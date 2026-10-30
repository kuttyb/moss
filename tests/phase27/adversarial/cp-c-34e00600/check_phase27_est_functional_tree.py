#!/usr/bin/env python3
"""Ensure non-pipeline expressions bypass functional EST planning."""
from __future__ import annotations

import json
import os
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]
COMPILER = Path(sys.argv[1]).resolve()
SOURCE = ROOT / "packages/regex/src/main.moss"


def check(compiler: Path, mode: str) -> None:
    env = dict(os.environ)
    env["MOSS_EST"] = mode
    result = subprocess.run(
        [str(compiler), "check", str(SOURCE), "--json"], cwd=ROOT, env=env,
        text=True, capture_output=True, timeout=60)
    payload = json.loads(result.stdout)
    assert result.returncode == 0 and payload.get("ok") is True, (
        compiler, mode, result.returncode, payload, result.stderr)


check(COMPILER, "off")
check(COMPILER, "shadow")
print("PASS non-pipeline apostrophe source checks in EST off and shadow")
