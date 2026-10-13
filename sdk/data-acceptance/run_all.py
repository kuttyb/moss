#!/usr/bin/env python3
"""Run Team D data SDK acceptance suite across all engines and validation gates."""

import os
from pathlib import Path
import subprocess
import sys

PROJECT = Path(__file__).resolve().parent
REPO = Path("/home/owner/daji/code/moss3")
MARGO = REPO / "margo"
PARSE = REPO / "sdk" / "parse"
FMT = REPO / "sdk" / "fmt"
JSON_PKG = REPO / "sdk" / "json"
CSV_PKG = REPO / "sdk" / "csv"

env = os.environ.copy()
env.update(
    MOSS=str(REPO / "moss"),
    MARGO_HOME=str(REPO / "tmp" / "margo-data-acceptance"),
    TMPDIR=str(REPO / "tmp"),
)
(REPO / "tmp").mkdir(exist_ok=True)


def run(cmd, cwd):
    print(f"Running {' '.join(cmd)} in {cwd.relative_to(REPO)}")
    subprocess.run(cmd, cwd=cwd, env=env, check=True)


def main():
    print("=== 1. Package-level native and Fast Debug tests ===")
    for pkg in (PARSE, FMT, JSON_PKG, CSV_PKG):
        run([str(MARGO), "test"], pkg)
        run([str(MARGO), "test", "--interp"], pkg)

    print("=== 2. Cross-package acceptance tests ===")
    run([str(MARGO), "test"], PROJECT)
    run([str(MARGO), "test", "--interp"], PROJECT)

    print("=== 3. Compiled-only ABI-v9 provider validation ===")
    run([sys.executable, "tests/check_source_free.py"], PROJECT)

    print("=== 4. Differential test harness against Python ===")
    run([sys.executable, str(REPO / "tests" / "differential" / "run_differential_tests.py")], REPO)

    print("\nALL TEAM D PHASE 24 DATA SDK ACCEPTANCE GATES PASSED!")


if __name__ == "__main__":
    main()
