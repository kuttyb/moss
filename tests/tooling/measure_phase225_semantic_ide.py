#!/usr/bin/env python3
"""Informational Phase 22.5 editor-query latency measurements.

Every observation starts a new Moss process, matching moss-mode's current
architecture.  There are deliberately no performance thresholds here.
"""

from __future__ import annotations

import argparse
import json
import math
import pathlib
import statistics
import subprocess
import time


def percentile95(values: list[float]) -> float:
    ordered = sorted(values)
    return ordered[max(0, math.ceil(0.95 * len(ordered)) - 1)]


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("compiler", type=pathlib.Path)
    parser.add_argument("--samples", type=int, default=25)
    arguments = parser.parse_args()
    if arguments.samples < 1:
        parser.error("--samples must be positive")

    root = pathlib.Path.cwd().resolve()
    compiler = arguments.compiler.resolve()
    source = (
        root / "tests" / "tooling" / "fixtures" / "phase225_ide" /
        "src" / "main.moss"
    )
    overlay_directory = root / "tmp" / "phase225-latency"
    overlay_directory.mkdir(parents=True, exist_ok=True)
    overlay = overlay_directory / "main-overlay.moss"
    overlay.write_bytes(source.read_bytes())

    queries = {
        "complete": ["complete", "at:8:24"],
        "references": ["references", "fn:leaf"],
        "symbols": ["symbols"],
        "calls": ["calls", "fn:top"],
    }

    def observe(prefix: list[str]) -> float:
        command = [
            str(compiler), *prefix, "--source", str(source),
            "--overlay-source", str(overlay), "--json",
        ]
        started = time.perf_counter_ns()
        process = subprocess.run(
            command, cwd=root, stdout=subprocess.DEVNULL,
            stderr=subprocess.PIPE, check=False,
        )
        elapsed_ms = (time.perf_counter_ns() - started) / 1_000_000
        if process.returncode != 0:
            raise SystemExit(
                f"{' '.join(command)} failed: "
                + process.stderr.decode("utf-8", "replace")
            )
        return elapsed_ms

    results: dict[str, object] = {}
    try:
        for name, query in queries.items():
            first = observe(query)
            observe(query)  # warmup excluded from the subsequent sample
            subsequent = [observe(query) for _ in range(arguments.samples)]
            results[name] = {
                "first_ms": round(first, 3),
                "subsequent_process_samples": arguments.samples,
                "median_ms": round(statistics.median(subsequent), 3),
                "p95_ms": round(percentile95(subsequent), 3),
                "minimum_ms": round(min(subsequent), 3),
                "maximum_ms": round(max(subsequent), 3),
            }
    finally:
        overlay.unlink(missing_ok=True)

    print(json.dumps({
        "phase": "22.5-corrective-hardening",
        "source": str(source.relative_to(root)),
        "overlay": True,
        "new_process_per_invocation": True,
        "warmups_before_subsequent_samples": 1,
        "queries": results,
    }, indent=2, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
