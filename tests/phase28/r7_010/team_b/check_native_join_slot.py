#!/usr/bin/env python3
"""Check native branch-slot assignment against the frozen ORACLE-51 result."""
from __future__ import annotations

import argparse
import json
import os
from pathlib import Path
import subprocess
import sys


ROOT = Path(__file__).resolve().parents[4]
FIXTURE = (ROOT / "tests/phase28/oracle_r7_005_1/fixtures/"
          "oracle_51_join_borrowed_provenance_sets.moss")
EXPECTATIONS = ROOT / "tests/phase28/oracle_r7_005_1/expectations.json"


def run(command: list[str], cwd: Path,
        env: dict[str, str] | None = None) -> subprocess.CompletedProcess[str]:
    return subprocess.run(command, cwd=cwd, text=True, capture_output=True,
                          check=False, env=env)


def require_ok(result: subprocess.CompletedProcess[str], label: str) -> str:
    if result.returncode:
        raise RuntimeError(
            f"{label} failed ({result.returncode})\n"
            f"stdout:\n{result.stdout}\nstderr:\n{result.stderr}")
    return result.stdout.strip()


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("compiler", type=Path)
    args = parser.parse_args()
    compiler = args.compiler.resolve()
    expected_row = next(
        row for row in json.loads(EXPECTATIONS.read_text())["fixtures"]
        if row["fixture_id"] == "ORACLE-51-JOIN-BORROWED-PROVENANCE-SETS")
    expected = expected_row["expected"]["native_debug"]["stdout"]
    output_dir = ROOT / "tmp/team-b/native-join-slot"
    output_dir.mkdir(parents=True, exist_ok=True)

    check = run([str(compiler), "check", "--json", str(FIXTURE)], ROOT)
    require_ok(check, "ORACLE-51 checker")

    fast = run([str(compiler), "run", "--interp", str(FIXTURE)], ROOT)
    if require_ok(fast, "ORACLE-51 Fast Debug") != expected:
        raise RuntimeError("Fast Debug output differs from frozen oracle")

    for mode, compiler_flags, rustc_flags in (
        ("debug", [], []), ("optimized", ["-O"], ["-O"])
    ):
        rust = output_dir / f"oracle51-{mode}.rs"
        binary = output_dir / f"oracle51-{mode}"
        emit = run([str(compiler), *compiler_flags, str(FIXTURE), "-o", str(rust)], ROOT)
        require_ok(emit, f"ORACLE-51 native {mode} emission")
        native_compile = run(["rustc", *rustc_flags, str(rust), "-o", str(binary)], ROOT)
        require_ok(native_compile, f"ORACLE-51 rustc {mode}")
        native = run([str(binary)], ROOT)
        if require_ok(native, f"ORACLE-51 native {mode} execution") != expected:
            raise RuntimeError(f"native {mode} output differs from frozen oracle")

    copy_fixture = (ROOT / "tests/phase28/oracle_r7_005_1/fixtures/"
                    "oracle_10a_explicit_copy_only.moss")
    for hook in (
        "MOSS_TEST_CONSUMER_DROP_COPY_FACT",
        "MOSS_TEST_CONSUMER_PERTURB_COPY_FACT",
        "MOSS_TEST_CONSUMER_DELETE_COPY_REQUIREMENT",
        "MOSS_TEST_CONSUMER_DUPLICATE_COPY_FACT",
        "MOSS_TEST_CONSUMER_DUPLICATE_COPY_REQUIREMENT",
    ):
        env = os.environ.copy()
        env[hook] = "1"
        output = output_dir / f"copy-corruption-{hook}.rs"
        corrupted = run([str(compiler), str(copy_fixture), "-o", str(output)],
                        ROOT, env)
        if corrupted.returncode == 0 or "MOSS_INTERNAL_INVARIANT" not in (
                corrupted.stdout + corrupted.stderr):
            raise RuntimeError(
                f"explicit copy did not fail closed for {hook}: "
                f"exit={corrupted.returncode}\n{corrupted.stdout}\n{corrupted.stderr}")

    print("ORACLE-51 branch-slot native Debug/Optimized and Fast Debug parity PASS")
    print("explicit copy checked-fact drop/perturb/delete/duplicate cases PASS")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, RuntimeError, StopIteration, KeyError, TypeError,
            json.JSONDecodeError) as error:
        print(error, file=sys.stderr)
        raise SystemExit(1)
