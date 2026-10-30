#!/usr/bin/env python3
"""Two-path reproducer for CP-D's user-named functional stage mismatch."""
import json
import os
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[4]
BASELINE = Path(sys.argv[1]).resolve()
CANDIDATE = Path(sys.argv[2]).resolve()
SOURCE = ROOT / "examples/mini_application.moss"


def run(compiler, mode):
    env = os.environ.copy()
    if mode != "baseline":
        env["MOSS_EST"] = mode
    try:
        result = subprocess.run(
            [str(compiler), "run", "--interp", str(SOURCE)], cwd=ROOT,
            env=env, text=True, capture_output=True, timeout=30)
        return {"exit_code": result.returncode, "stdout": result.stdout,
                "stderr": result.stderr}
    except subprocess.TimeoutExpired:
        return {"exit_code": "TIMEOUT", "stdout": "", "stderr": ""}


def main():
    results = {"baseline": run(BASELINE, "baseline"),
               "candidate_off": run(CANDIDATE, "off"),
               "candidate_shadow": run(CANDIDATE, "shadow")}
    baseline = results["baseline"]
    if baseline["exit_code"] != 1 or \
            "unsupported checked pipeline stage 'schedule_score'" not in baseline["stderr"]:
        raise SystemExit("pinned baseline no longer reproduces the recorded pipeline diagnostic")
    if any(value != baseline for value in results.values()):
        print(json.dumps(results, indent=2))
        raise SystemExit("Fast Debug pipeline diagnostic differs from the pinned baseline")
    print(json.dumps({"source": str(SOURCE.relative_to(ROOT)),
                      "baseline": baseline,
                      "candidate_off": results["candidate_off"],
                      "candidate_shadow": results["candidate_shadow"]}, indent=2))


if __name__ == "__main__":
    main()
