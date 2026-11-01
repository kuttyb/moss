#!/usr/bin/env python3
"""Validate that EST coverage counters are test-build-only and observational."""
from __future__ import annotations

import os
from pathlib import Path
import re
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]
DEV = Path(sys.argv[1]).resolve()
OPT = Path(sys.argv[2]).resolve()
SOURCE = ROOT / "tests/phase27/est/specialized_call_facts.moss"
OUT = ROOT / "tmp/phase27-est-coverage"
OUT.mkdir(parents=True, exist_ok=True)


def emit(binary: Path, output: Path, enabled: bool):
    env = os.environ.copy()
    if enabled:
        env["MOSS_EST_COVERAGE_STATS"] = "1"
    else:
        env.pop("MOSS_EST_COVERAGE_STATS", None)
    result = subprocess.run(
        [str(binary), str(SOURCE), "-o", str(output)], cwd=ROOT, env=env,
        text=True, capture_output=True, timeout=60)
    if result.returncode:
        raise SystemExit(f"{binary.name} emit failed: {result.stdout}{result.stderr}")
    return result.stderr


dev_with = OUT / "dev-with.rs"
dev_without = OUT / "dev-without.rs"
dev_stats = emit(DEV, dev_with, True)
if "MOSS_EST_REPARSES " not in dev_stats:
    raise SystemExit("test compiler did not report consumer reparse counters")
if "MOSS_EST_TEXT_NODE_LOOKUPS=" not in dev_stats:
    raise SystemExit("test compiler did not report expression-text node lookups")
if "MOSS_EST_TREE_TEXT_REBUILDS=" not in dev_stats:
    raise SystemExit("test compiler did not report expression-tree rebuilds")
if "MOSS_EST_EXPRESSION_TEXT_PARSES " not in dev_stats:
    raise SystemExit("test compiler did not report expression-text parser calls")
facts = re.findall(
    r"MOSS_EST_GENERATED_TYPE_FACT kind=\S+ PRESENT=(\d+) ABSENT=(\d+)",
    dev_stats)
if not facts or not any(int(present) + int(absent) for present, absent in facts):
    raise SystemExit("test compiler did not report generated_expr_type fact coverage")
dev_plain_stats = emit(DEV, dev_without, False)
if ("MOSS_EST_REPARSES " in dev_plain_stats or
        "MOSS_EST_GENERATED_TYPE_FACT" in dev_plain_stats or
        "MOSS_EST_TEXT_NODE_LOOKUPS=" in dev_plain_stats or
        "MOSS_EST_TREE_TEXT_REBUILDS=" in dev_plain_stats or
        "MOSS_EST_EXPRESSION_TEXT_PARSES " in dev_plain_stats):
    raise SystemExit("coverage counters printed without the opt-in report environment")
if dev_with.read_bytes() != dev_without.read_bytes():
    raise SystemExit("coverage instrumentation changed generated Rust")

opt_output = OUT / "opt.rs"
opt_stats = emit(OPT, opt_output, True)
if ("MOSS_EST_REPARSES " in opt_stats or
        "MOSS_EST_GENERATED_TYPE_FACT" in opt_stats or
        "MOSS_EST_TEXT_NODE_LOOKUPS=" in opt_stats or
        "MOSS_EST_TREE_TEXT_REBUILDS=" in opt_stats or
        "MOSS_EST_EXPRESSION_TEXT_PARSES " in opt_stats):
    raise SystemExit("optimized/release compiler contains test-only coverage counters")

print("PASS EST counters are opt-in, test-build-only, and output-neutral")
