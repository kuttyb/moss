#!/usr/bin/env python3
"""Ensure checker call-target facts are retained per static specialization."""
from __future__ import annotations

import os
from pathlib import Path
import re
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]
COMPILER = Path(sys.argv[1]).resolve()
SOURCE = ROOT / "tests/phase27/est/specialized_call_facts.moss"

for mode in ("default", "shadow"):
    env = os.environ.copy()
    env["MOSS_EST_FACTS_DUMP"] = "1"
    if mode == "default":
        env.pop("MOSS_EST", None)
    else:
        env["MOSS_EST"] = mode
    result = subprocess.run(
        [str(COMPILER), "check", str(SOURCE), "--json"], cwd=ROOT,
        env=env, text=True, capture_output=True, timeout=60)
    if result.returncode:
        raise SystemExit(f"{mode} check failed: {result.stdout}{result.stderr}")
    specialized_inner = [line for line in result.stderr.splitlines()
                         if line.startswith("MOSS_EST_FACT ")
                         and 'context="specialization:' in line
                         and "fn:leaf\"" in line]
    if len(specialized_inner) < 2:
        raise SystemExit(
            f"{mode} did not retain the nested call target in each specialization: "
            f"{specialized_inner}\n{result.stderr}")
    contexts = {re.search(r'context="([^"]+)"', line).group(1)
                for line in specialized_inner}
    if len(contexts) < 2:
        raise SystemExit(f"expected distinct specialization fact contexts: {contexts}")

print("PASS checker facts retained for nested calls in two specialization contexts")
