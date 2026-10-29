#!/usr/bin/env python3
"""Regression test for checker occurrence invalidation in deferred pipelines."""
from __future__ import annotations

import json
import os
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]
COMPILER = Path(sys.argv[1]).resolve()
SOURCE = ROOT / (
    "examples/swarm/expression_surface_2026/parser_evaluator/failed_attempts/"
    "02_untyped_pipeline_return.moss")


def main() -> int:
    for mode in ("off", "shadow"):
        env = os.environ.copy()
        env["MOSS_EST"] = mode
        result = subprocess.run(
            [str(COMPILER), "check", str(SOURCE), "--json"],
            cwd=ROOT, env=env, text=True, capture_output=True, timeout=60)
        try:
            response = json.loads(result.stdout)
        except json.JSONDecodeError as error:
            raise SystemExit(
                f"{mode} compiler did not return JSON ({result.returncode}): "
                f"{result.stdout}{result.stderr}") from error
        if result.returncode != 0 or response.get("ok") is not True:
            raise SystemExit(
                f"{mode} rejected deferred untyped pipeline result: "
                f"{result.stdout}{result.stderr}")
    print("PASS untyped pipeline return checks in EST off and shadow")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
