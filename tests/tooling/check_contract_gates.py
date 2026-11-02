#!/usr/bin/env python3
"""Ensure each gate script named by the compiler contract is run by both checks."""

from __future__ import annotations

import re
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
CONTRACT = ROOT / "docs/MOSS_COMPILER_CONTRACT.md"
MAKEFILE = ROOT / "Makefile"
PARALLEL = ROOT / "tests/run_parallel.py"


def recipe(text: str, target: str) -> str:
    match = re.search(rf"(?ms)^{re.escape(target)}:[^\n]*\n((?:\t[^\n]*\n|\n)*)", text)
    return match.group(1) if match else ""


def target_line(text: str, target: str) -> str:
    match = re.search(rf"(?m)^{re.escape(target)}:[^\n]*", text)
    return match.group(0) if match else ""


def main() -> int:
    contract = CONTRACT.read_text(encoding="utf-8")
    makefile = MAKEFILE.read_text(encoding="utf-8")
    parallel = PARALLEL.read_text(encoding="utf-8")
    failures: list[str] = []

    default = recipe(makefile, "check")
    shadow = recipe(makefile, "check-shadow")
    preflight = recipe(makefile, "check-preflight")
    if ("check-preflight" not in target_line(makefile, "check") or
            "check-preflight" not in target_line(makefile, "check-shadow")):
        failures.append("check and check-shadow must both run check-preflight")
    if "tests/run_parallel.py" not in default or "tests/run_parallel.py" not in shadow:
        failures.append("check and check-shadow must both run tests/run_parallel.py")

    named = sorted(set(re.findall(r"`((?:tests/tooling/)?[A-Za-z0-9_./-]+\.py)`", contract)))
    test_registry_match = re.search(
        r"(?s)TOOLING_TESTS\s*:\s*list\[tuple\]\s*=\s*\[(.*?)\n\]",
        parallel,
    )
    test_registry = test_registry_match.group(1) if test_registry_match else ""
    if not test_registry_match:
        failures.append("tests/run_parallel.py has no recognizable TOOLING_TESTS registry")
    for script in named:
        path = ROOT / script if "/" in script else ROOT / "tests/tooling" / script
        if not path.is_file():
            failures.append(f"contract names missing gate script: {script}")
            continue
        if script == "tests/run_parallel.py":
            continue
        in_preflight = script in preflight
        in_both_recipes = script in default and script in shadow
        in_parallel_registry = script in test_registry
        if not (in_preflight or in_both_recipes or in_parallel_registry):
            failures.append(
                f"contract gate is not registered in both check modes: {script}"
            )

    if failures:
        print("Compiler contract gate registration check failed:", file=sys.stderr)
        for failure in failures:
            print(f"- {failure}", file=sys.stderr)
        return 1
    print(f"Compiler contract gates registered: {len(named)} scripts in check and check-shadow")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
