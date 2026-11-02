#!/usr/bin/env python3
"""Run the committed Phase 27.5.4 workload and enforce stage counter ceilings."""
from __future__ import annotations

import json
import os
from pathlib import Path
import re
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]
DEV = Path(sys.argv[1]).resolve()
WORKLOAD = ROOT / "tests/tooling/fixtures/phase27_5_4_runtime_workload.json"
BASELINE = ROOT / "tests/tooling/fixtures/phase27_5_4_runtime_baseline.json"
OUT = ROOT / "tmp/phase27-5-4-runtime-ratchet"
OUT.mkdir(parents=True, exist_ok=True)
STAGE_RE = re.compile(
    r"MOSS_EST_STAGE stage=(\S+) reparses=(\d+) text_lookups=(\d+) "
    r"cache_lookups=(\d+) tree_rebuilds=(\d+) shadow_crosschecks=(\d+)"
)
METRICS = ("reparses", "text_lookups", "cache_lookups", "tree_rebuilds")


def add_stats(total: dict, output: str, label: str) -> None:
    found = {match.group(1): match.groups()[1:] for match in STAGE_RE.finditer(output)}
    if not found:
        raise SystemExit(f"{label}: compiler omitted MOSS_EST_STAGE counters")
    for stage, values in found.items():
        row = total.setdefault(stage, {metric: 0 for metric in METRICS} |
                               {"shadow_crosschecks": 0})
        for metric, value in zip((*METRICS, "shadow_crosschecks"), values):
            row[metric] += int(value)


def run(total: dict, args: list[str], label: str) -> None:
    env = os.environ.copy()
    env["MOSS_EST_COVERAGE_STATS"] = "1"
    result = subprocess.run(args, cwd=ROOT, env=env, capture_output=True,
                            text=True, timeout=120)
    if result.returncode:
        raise SystemExit(f"{label} failed ({result.returncode}):\n"
                         f"{result.stdout}{result.stderr}")
    add_stats(total, result.stderr, label)


def collect() -> dict:
    workload = json.loads(WORKLOAD.read_text())
    totals: dict = {}
    for index, source in enumerate(workload["sources"]):
        path = ROOT / source
        if not path.is_file():
            raise SystemExit(f"missing workload source: {source}")
        run(totals, [str(DEV), str(path), "-o", str(OUT / f"{index}.rs")],
            f"native emit {source}")
        run(totals, [str(DEV), str(path), "-O", "-o",
                     str(OUT / f"{index}-opt.rs")], f"optimized emit {source}")
    for index, source in enumerate(workload["fastdebug_sources"]):
        run(totals, [str(DEV), "run", "--interp", str(ROOT / source)],
            f"Fast Debug {source}")
    return totals


if __name__ == "__main__":
    current = collect()
    if "--write-baseline" in sys.argv[2:]:
        BASELINE.write_text(json.dumps({"workload": str(WORKLOAD.relative_to(ROOT)),
                                        "counts": current}, indent=2) + "\n")
        print(f"Wrote {BASELINE.relative_to(ROOT)}")
        raise SystemExit(0)
    baseline = json.loads(BASELINE.read_text())["counts"]
    failed = []
    for stage, row in current.items():
        # The Phase 27.5.4 re-scope explicitly removed checker-internal
        # reparses and text-cache lookups from the acceptance target. Keep
        # reporting them for trend data, but gate only consumer/driver stages.
        if stage == "checker":
            continue
        ceiling = baseline.get(stage, {})
        for metric in METRICS:
            value = row.get(metric, 0)
            limit = ceiling.get(metric, 0)
            if value > limit:
                failed.append(f"{stage}.{metric}={value} exceeds baseline {limit}")
            elif value < limit:
                failed.append(f"{stage}.{metric}={value} is below baseline {limit}; "
                              "lower the committed baseline")
    zero_stages = ("fastdebug", "emitter", "optimizer")
    for stage in zero_stages:
        row = current.get(stage, {})
        for metric in ("reparses", "text_lookups", "cache_lookups", "tree_rebuilds"):
            if row.get(metric, 0):
                failed.append(f"{stage}.{metric} must remain zero, got {row[metric]}")
    shadow_mode = os.environ.get("MOSS_EST") == "shadow"
    if shadow_mode:
        for stage, row in current.items():
            if stage not in ("checker", "driver_rewrite") and row["shadow_crosschecks"]:
                failed.append(f"unexpected shadow cross-checks in {stage}: "
                              f"{row['shadow_crosschecks']}")
    for stage, row in sorted(current.items()):
        print("RATCHET", stage, " ".join(f"{metric}={row[metric]}" for metric in METRICS),
              f"shadow_crosschecks={row['shadow_crosschecks']}")
    checker = current.get("checker", {})
    if checker:
        baseline_checker = baseline.get("checker", {})
        print("REPORT-ONLY checker",
              " ".join(f"{metric}={checker.get(metric, 0)}"
                       for metric in METRICS),
              "baseline=" + ",".join(
                  f"{metric}:{baseline_checker.get(metric, 0)}"
                  for metric in METRICS))
    if failed:
        raise SystemExit("runtime ratchet failed:\n- " + "\n- ".join(failed))
    print("PASS phase27.5.4 runtime ratchet")
