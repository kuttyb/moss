#!/usr/bin/env python3
"""Paired test-build emitter counters over tracked tests/examples Moss sources."""
from __future__ import annotations

import json
import os
from pathlib import Path
import re
import subprocess
import sys
import time

ROOT = Path(__file__).resolve().parents[2]
COUNTER_PATTERNS = {
    "text_node_lookups": re.compile(r"^MOSS_EST_TEXT_NODE_LOOKUPS=(\d+)$", re.M),
    "native_expression_view_reparses": re.compile(
        r"^MOSS_EST_REPARSES native_expression_view=(\d+)", re.M),
    "native_functional_pipeline_reparses": re.compile(
        r"^MOSS_EST_REPARSES .*native_functional_pipeline=(\d+)", re.M),
    "consumer_expression_parses": re.compile(
        r"^MOSS_EST_EXPRESSION_TEXT_PARSES .*consumer=(\d+)", re.M),
    "consumer_type_inference": re.compile(
        r"^MOSS_EST_CONSUMER_TYPE_INFERENCE=(\d+)$", re.M),
}
TYPE_COVERAGE = re.compile(
    r"^MOSS_EST_TYPE_COVERAGE consumer=([^ ]+) kind=([^ ]+) "
    r"PRESENT=(\d+) ABSENT=(\d+)$", re.M)


def measure(compiler: Path, profile: str, sources: list[Path], out: Path) -> dict:
    totals = {name: 0 for name in COUNTER_PATTERNS}
    by_mode = {mode: {name: 0 for name in COUNTER_PATTERNS}
               for mode in ("default", "optimized")}
    coverage: dict[str, dict[str, int]] = {}
    failures = 0
    failures_by_mode = {mode: 0 for mode in by_mode}
    env = os.environ.copy()
    env.pop("MOSS_EST", None)
    env["MOSS_EST_COVERAGE_STATS"] = "1"
    output_dir = out / profile
    output_dir.mkdir(parents=True)
    for index, source in enumerate(sources):
        modes = (("default", []), ("optimized", ["-O"]))
        for mode, flags in modes:
            rust = output_dir / f"{index:04d}-{mode}.rs"
            result = subprocess.run(
                [str(compiler), str(source), *flags, "-o", str(rust)],
                cwd=ROOT, env=env, text=True, capture_output=True, timeout=90)
            if result.returncode:
                failures += 1
                failures_by_mode[mode] += 1
            for name, pattern in COUNTER_PATTERNS.items():
                match = pattern.search(result.stderr)
                if match:
                    amount = int(match.group(1))
                    totals[name] += amount
                    by_mode[mode][name] += amount
            for match in TYPE_COVERAGE.finditer(result.stderr):
                consumer, kind, present, absent = match.groups()
                counts = coverage.setdefault(consumer, {"present": 0,
                                                        "absent": 0})
                counts["present"] += int(present)
                counts["absent"] += int(absent)
    return {"compiler": str(compiler), "sources": len(sources),
            "commands": len(sources) * 2, "failures": failures,
            "failures_by_mode": failures_by_mode, "counts": totals,
            "counts_by_mode": by_mode, "type_coverage": coverage}


def main() -> None:
    if len(sys.argv) != 3:
        raise SystemExit(f"usage: {sys.argv[0]} <phase27-5-3-c1 moss-dev> <candidate moss-dev>")
    baseline, candidate = (Path(arg).resolve() for arg in sys.argv[1:])
    tracked = subprocess.check_output(["git", "ls-files", "-z"], cwd=ROOT)
    sources = sorted(
        ROOT / entry.decode() for entry in tracked.split(b"\0") if entry and
        (entry.startswith(b"tests/") or entry.startswith(b"examples/")) and
        entry.endswith(b".moss"))
    manifests = sum(
        1 for entry in tracked.split(b"\0") if entry and
        (entry.startswith(b"tests/") or entry.startswith(b"examples/")) and
        entry.endswith(b"/Moss.toml"))
    out = ROOT / "tmp/phase2754-emitter-counts" / str(time.time_ns())
    out.mkdir(parents=True)
    result = {
        "workload": {"tracked_sources": len(sources),
                     "tracked_package_manifests": manifests,
                     "commands_per_source": ["default emission", "-O emission"]},
        "baseline": measure(baseline, "baseline", sources, out),
        "candidate": measure(candidate, "candidate", sources, out),
    }
    path = out / "summary.json"
    path.write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(result, indent=2))
    print(f"summary: {path.relative_to(ROOT)}")


if __name__ == "__main__":
    main()
