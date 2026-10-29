#!/usr/bin/env python3
"""Reproduce the Phase 27 Fast Debug timeout comparison.

Usage: python3 timeout_compare.py BASELINE_MOSS FROZEN_MOSS
The timeout matches the original 15-second subprocess threshold.
"""
import subprocess
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[4]
SOURCES = ("examples/frontend_syntax.moss", "examples/functional_pipeline.moss")
TIMEOUT_SECONDS = 15


def run(label, compiler):
    compiler = Path(compiler).resolve()
    for relative in SOURCES:
        source = (ROOT / relative).resolve()
        started = time.monotonic()
        try:
            result = subprocess.run(
                [str(compiler), "run", "--interp", str(source)], cwd=ROOT,
                capture_output=True, text=True, timeout=TIMEOUT_SECONDS,
                check=False,
            )
        except subprocess.TimeoutExpired:
            print(f"{label} {relative}: TIMEOUT ({TIMEOUT_SECONDS}s)")
            continue
        elapsed = time.monotonic() - started
        print(f"{label} {relative}: exit={result.returncode} elapsed={elapsed:.3f}s")


if __name__ == "__main__":
    if len(sys.argv) != 3:
        raise SystemExit("expected baseline and frozen compiler paths")
    run("baseline-4d6d0c8", sys.argv[1])
    run("frozen-0f116ada", sys.argv[2])
