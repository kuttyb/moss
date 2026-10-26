#!/usr/bin/env python3
"""Require same-machine W1 gains on the two Phase 26 parse-cache targets."""

import json
from pathlib import Path
import statistics
import subprocess
import sys
import time


ROOT = Path(__file__).resolve().parents[2]
WORKLOADS = {
    "w3_spec_repeated_n032": ROOT / "benchmarks/compiler_torture/fixtures/specialization/w3_spec_repeated_n032.moss",
    "w5_infer_wide_w100": ROOT / "benchmarks/compiler_torture/fixtures/inference/w5_infer_wide_w100.moss",
}


def trial(binary: Path, source: Path) -> tuple[float, bytes]:
    start = time.perf_counter()
    result = subprocess.run(
        [str(binary), "check", str(source), "--json"],
        capture_output=True, check=False,
    )
    elapsed = time.perf_counter() - start
    if result.returncode != 0 or json.loads(result.stdout).get("ok") is not True:
        raise AssertionError(f"{binary} rejected {source}: {result.stdout!r} {result.stderr!r}")
    return elapsed, result.stdout


def main() -> int:
    if len(sys.argv) != 3:
        raise SystemExit("usage: check_phase26_w1_performance.py BASELINE_COMPILER W1_COMPILER")
    baseline, candidate = (Path(argument).resolve() for argument in sys.argv[1:])
    samples = {name: {"main": [], "w1": []} for name in WORKLOADS}
    for index in range(3):
        order = ("main", "w1") if index % 2 == 0 else ("w1", "main")
        for name, source in WORKLOADS.items():
            outputs = {}
            for label in order:
                elapsed, output = trial(baseline if label == "main" else candidate, source)
                samples[name][label].append(elapsed)
                outputs[label] = output
            if outputs["main"] != outputs["w1"]:
                raise AssertionError(f"check output differs for {name}")

    failed = False
    for name, values in samples.items():
        before = statistics.median(values["main"])
        after = statistics.median(values["w1"])
        change = (1.0 - after / before) * 100.0
        print(f"{name}: main={before:.4f}s W1={after:.4f}s change={change:.1f}%")
        failed |= after >= before
    if failed:
        raise AssertionError("W1 did not reduce check time on every target workload")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
