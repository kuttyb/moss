#!/usr/bin/env python3
"""Consumer-boundary corruption coverage for Phase 28 B4."""
from __future__ import annotations

import os
from pathlib import Path
import subprocess
import sys


ROOT = Path(__file__).resolve().parents[2]
TMP = ROOT / "tmp/phase28_b4_consumer"
TMP.mkdir(parents=True, exist_ok=True)
OPTION_FIXTURE = ROOT / "tests/phase28/consumer_repair/fixtures/oracle_06_match_consume_option_native.moss"
SPECIALIZED_FIXTURE = ROOT / "tests/phase28/consumer_repair/specialized_match_facts.moss"


def run(command: list[str], env: dict[str, str] | None = None) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        command, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True,
        cwd=ROOT, env=env,
    )


def verify_positive(compiler: str, fixture: Path, expected: str) -> None:
    fast = run([compiler, "run", "--interp", str(fixture)])
    if fast.returncode != 0 or fast.stdout.strip() != expected:
        raise AssertionError(
            f"Fast Debug mismatch for {fixture.name}: exit={fast.returncode}, "
            f"stdout={fast.stdout!r}, stderr={fast.stderr!r}"
        )

    for optimized in (False, True):
        suffix = "_opt" if optimized else ""
        rust = TMP / f"{fixture.stem}{suffix}.rs"
        binary = TMP / f"{fixture.stem}{suffix}.bin"
        command = [compiler]
        if optimized:
            command.append("-O")
        command += [str(fixture), "-o", str(rust)]
        emitted = run(command)
        if emitted.returncode != 0:
            raise AssertionError(f"Native emit failed for {fixture.name}: {emitted.stderr}")
        compiled = run(["rustc", "--edition=2021", *( ["-O"] if optimized else [] ),
                        str(rust), "-o", str(binary)])
        if compiled.returncode != 0:
            raise AssertionError(f"rustc failed for {fixture.name}: {compiled.stderr}")
        native = run([str(binary)])
        if native.returncode != 0 or native.stdout.strip() != expected:
            raise AssertionError(
                f"Native mismatch for {fixture.name} optimized={optimized}: "
                f"exit={native.returncode}, stdout={native.stdout!r}, "
                f"stderr={native.stderr!r}"
            )


def verify_corruption(compiler: str, hook: str, mode: str) -> None:
    env = os.environ.copy()
    env[hook] = "1"
    fixture = SPECIALIZED_FIXTURE if hook == "MOSS_TEST_CONSUMER_COLLIDE_MATCH_SPECIALIZATION_IDENTITY" else OPTION_FIXTURE
    if mode == "fast":
        command = [compiler, "run", "--interp", str(fixture)]
    else:
        rust = TMP / f"corrupt_{hook}_{mode}.rs"
        command = [compiler]
        if mode == "native-opt":
            command.append("-O")
        command += [str(fixture), "-o", str(rust)]
    result = run(command, env=env)
    combined = result.stdout + "\n" + result.stderr
    if result.returncode == 0 or "MOSS_INTERNAL_INVARIANT" not in combined:
        raise AssertionError(
            f"{mode} accepted {hook}; exit={result.returncode}\n{combined}"
        )


def main() -> int:
    compiler = sys.argv[1] if len(sys.argv) > 1 else str(ROOT / "moss")
    verify_positive(compiler, OPTION_FIXTURE, "target")
    verify_positive(compiler, SPECIALIZED_FIXTURE, "7\ntext")

    hooks = (
        "MOSS_TEST_DROP_MATCH_FACT",
        "MOSS_TEST_PERTURB_MATCH_FACT",
        "MOSS_TEST_PERTURB_MATCH_SCRUTINEE",
        "MOSS_TEST_DUPLICATE_MATCH_FACT",
        "MOSS_TEST_DUPLICATE_MATCH_REQUIREMENT",
        "MOSS_TEST_CONSUMER_DROP_MATCH_REQUIREMENT",
        "MOSS_TEST_CONSUMER_COLLIDE_MATCH_SPECIALIZATION_IDENTITY",
    )
    for hook in hooks:
        for mode in ("fast", "native", "native-opt"):
            verify_corruption(compiler, hook, mode)

    print("phase28 B4 consumer boundary: PASS (typed match parity, generic context isolation, 21 fail-closed corruption cases)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
