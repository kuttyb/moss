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
NON_PIPELINE_SOURCE = ROOT / "packages/regex/src/main.moss"
PIPELINE_SOURCE = ROOT / "benchmarks/agent/baselines/post-22.1/AB016/final/main.moss"


def check(compiler: Path, mode: str, source: Path) -> None:
    env = dict(os.environ)
    env["MOSS_EST"] = mode
    result = subprocess.run(
        [str(compiler), "check", str(source), "--json"], cwd=ROOT, env=env,
        text=True, capture_output=True, timeout=60)
    payload = json.loads(result.stdout)
    assert result.returncode == 0 and payload.get("ok") is True, (
        compiler, mode, result.returncode, payload, result.stderr)


def run_pipeline(compiler: Path, mode: str) -> None:
    env = dict(os.environ)
    env["MOSS_EST"] = mode
    result = subprocess.run(
        [str(compiler), "run", "--interp", str(PIPELINE_SOURCE)], cwd=ROOT,
        env=env, text=True, capture_output=True, timeout=60)
    assert result.returncode == 0 and result.stdout == "14\n", (
        compiler, mode, result.returncode, result.stdout, result.stderr)


for mode in ("off", "shadow"):
    check(COMPILER, mode, NON_PIPELINE_SOURCE)
    check(COMPILER, mode, PIPELINE_SOURCE)
    run_pipeline(COMPILER, mode)
print("PASS pipeline Fast Debug output and regex apostrophe checks in EST off/shadow")
