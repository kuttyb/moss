#!/usr/bin/env python3
"""Compare frozen baseline and candidate on every common-source verdict change."""
import json
import os
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[4]
SOURCES = (
    "benchmarks/agent/phase22_3_ab/tasks/P223017_simple_arithmetic/starter/main.moss",
    "sdk/advanced-data-acceptance/fixtures/source_free_consumer.moss",
    "tests/phase27/est/repro/incomplete_expression.moss",
)

def verdict(binary: Path, mode: str, source: str):
    env = os.environ.copy()
    env["MOSS_EST"] = mode
    result = subprocess.run([str(binary), "check", source, "--json"],
                            cwd=ROOT, env=env, capture_output=True, text=True)
    try:
        payload = json.loads(result.stdout)
        error = payload.get("error") or {}
        return f"{payload.get('ok')}:{error.get('code') or '-'}:{error.get('line') or '-'}"
    except json.JSONDecodeError:
        return f"NOJSON:{result.returncode}:-"

baseline = Path(sys.argv[1]).resolve()
candidate = Path(sys.argv[2]).resolve()
print("source\tbaseline_off\tcandidate_off\tcandidate_shadow")
for source in SOURCES:
    print("\t".join((source, verdict(baseline, "off", source),
                      verdict(candidate, "off", source),
                      verdict(candidate, "shadow", source))))
