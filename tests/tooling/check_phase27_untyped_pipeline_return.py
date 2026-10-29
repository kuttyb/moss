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
SOURCES = (
    ("examples/swarm/expression_surface_2026/parser_evaluator/failed_attempts/"
     "02_untyped_pipeline_return.moss", True),
    ("examples/swarm/expression_surface_2026/validation_engine/failed_attempts/"
     "04_pipeline_collection_inference.moss", None),
    ("examples/swarm/modules/domain_services/repro/generic_pipeline/"
     "single/src/main.moss", None),
    ("examples/swarm/modules/domain_services/repro/generic_pipeline/"
     "split/src/main.moss", None),
    ("examples/swarm/modules/domain_services/repro/generic_pipeline/"
     "split/src/stats.moss", None),
    ("examples/swarm/modules/lib_and_app/repro/"
     "generic_pipeline_method_placeholder/single/src/main.moss", None),
)


def main() -> int:
    for mode in ("off", "shadow"):
        cases = SOURCES if mode == "shadow" else SOURCES[:1]
        for source, expected_ok in cases:
            env = os.environ.copy()
            env["MOSS_EST"] = mode
            env["MOSS_PHASE26_C1_SCOPE_TRACE"] = "1"
            result = subprocess.run(
                [str(COMPILER), "check", str(ROOT / source), "--json"],
                cwd=ROOT, env=env, text=True, capture_output=True, timeout=60)
            try:
                response = json.loads(result.stdout)
            except json.JSONDecodeError as error:
                raise SystemExit(
                    f"{mode} compiler crashed on {source} ({result.returncode}): "
                    f"{result.stdout}{result.stderr}") from error
            if expected_ok is not None and (
                    result.returncode != 0 or response.get("ok") is not expected_ok):
                raise SystemExit(
                    f"{mode} changed the accepted result for {source}: "
                    f"{result.stdout}{result.stderr}")
    print("PASS deferred pipeline crash regressions in EST off and shadow")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
