#!/usr/bin/env python3
"""Compile the explicit-take corpus controls for field ownership."""
from __future__ import annotations

from pathlib import Path
import shutil
import subprocess
import sys


ROOT = Path(__file__).resolve().parents[2]
SOURCES = (
    "tests/swarm_027_indexed_collection_method.moss",
    "tests/swarm_028_self_method_argument_borrow.moss",
    "tests/swarm_030_string_comparison_borrowed.moss",
)


def run(args: list[str]) -> subprocess.CompletedProcess[str]:
    result = subprocess.run(args, cwd=ROOT, capture_output=True, text=True,
                            timeout=120)
    if result.returncode:
        raise RuntimeError(
            f"command failed ({result.returncode}): {args}\n"
            f"{result.stdout}{result.stderr}")
    return result


def main() -> int:
    if len(sys.argv) != 2:
        print("usage: check_phase28_l8_field_controls.py COMPILER", file=sys.stderr)
        return 2
    compiler = str(Path(sys.argv[1]).resolve())
    scratch = ROOT / "tmp" / f"phase28-l8-field-controls-{__import__('os').getpid()}"
    scratch.mkdir(parents=True, exist_ok=True)
    try:
        for index, relative in enumerate(SOURCES):
            source = ROOT / relative
            run([compiler, "run", "--interp", str(source)])
            rust = scratch / f"control-{index}.rs"
            binary = scratch / f"control-{index}"
            run([compiler, str(source), "-o", str(rust)])
            run(["rustc", "--edition=2021", "-D", "warnings", str(rust),
                 "-o", str(binary)])
            run([str(binary)])
            print(f"PASS {relative} with explicit take")
    finally:
        shutil.rmtree(scratch, ignore_errors=True)
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, RuntimeError, subprocess.TimeoutExpired) as error:
        print(f"FAIL check_phase28_l8_field_controls: {error}", file=sys.stderr)
        raise SystemExit(1)
