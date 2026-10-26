#!/usr/bin/env python3
"""Require a same-runner W5 inference gain over the Phase 26 W1 compiler."""

import json
from pathlib import Path
import statistics
import subprocess
import sys
import time


ROOT = Path(__file__).resolve().parents[2]
W5 = ROOT / "benchmarks/compiler_torture/fixtures/inference/w5_infer_wide_w100.moss"


def trial(binary: Path) -> tuple[float, bytes]:
    start = time.perf_counter()
    result = subprocess.run(
        [str(binary), "check", str(W5), "--json"],
        cwd=ROOT, capture_output=True, check=False,
    )
    elapsed = time.perf_counter() - start
    if result.returncode != 0 or json.loads(result.stdout).get("ok") is not True:
        raise AssertionError(f"{binary} rejected W5: {result.stdout!r} {result.stderr!r}")
    return elapsed, result.stdout


def main() -> int:
    if len(sys.argv) != 3:
        raise SystemExit("usage: check_phase26_w2_performance.py W1_COMPILER W2_COMPILER")
    baseline, candidate = (Path(argument).resolve() for argument in sys.argv[1:])
    samples = {"w1": [], "w2": []}
    for index in range(3):
        order = ("w1", "w2") if index % 2 == 0 else ("w2", "w1")
        outputs = {}
        for label in order:
            elapsed, output = trial(baseline if label == "w1" else candidate)
            samples[label].append(elapsed)
            outputs[label] = output
        if outputs["w1"] != outputs["w2"]:
            raise AssertionError("W1/W2 W5 check output differs")

    before = statistics.median(samples["w1"])
    after = statistics.median(samples["w2"])
    change = (1.0 - after / before) * 100.0
    print(f"w5_infer_wide_w100: W1={before:.4f}s W2={after:.4f}s change={change:.1f}%")
    if after >= before:
        raise AssertionError("W2 did not reduce same-runner W5 check time")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
