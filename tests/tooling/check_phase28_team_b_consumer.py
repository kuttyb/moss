#!/usr/bin/env python3
"""Exercise Phase 28 Team B native consumer of checker-owned local ownership facts."""
from __future__ import annotations

import os
from pathlib import Path
import subprocess
import sys


ROOT = Path(__file__).resolve().parents[2]
FIXTURES = ROOT / "tests/phase28/team_a_ownership"
TMP = ROOT / "tmp/phase28_team_b"
TMP.mkdir(parents=True, exist_ok=True)


def run(cmd: list[str], env: dict[str, str] | None = None) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, cwd=ROOT, env=env
    )


def verify_native(compiler: str, fixture: str, expected_output: str) -> None:
    source = FIXTURES / fixture
    stem = Path(fixture).stem
    rs_path = TMP / f"{stem}.rs"
    bin_path = TMP / f"{stem}.bin"

    # Fast Debug
    fast_debug = run([compiler, "run", "--interp", str(source)])
    if fast_debug.returncode != 0:
        raise AssertionError(f"{fixture}: Fast Debug crashed: {fast_debug.stderr}")
    if fast_debug.stdout.strip() != expected_output:
        raise AssertionError(
            f"{fixture}: Fast Debug mismatch: expected '{expected_output}', got '{fast_debug.stdout.strip()}'"
        )

    # Native emit
    emit = run([compiler, str(source), "-o", str(rs_path)])
    if emit.returncode != 0:
        raise AssertionError(f"{fixture}: Native emit failed: {emit.stderr}")

    # Rustc compile
    rustc = run(["rustc", "--edition=2021", str(rs_path), "-o", str(bin_path)])
    if rustc.returncode != 0:
        raise AssertionError(f"{fixture}: rustc compilation failed: {rustc.stderr}")

    # Run binary
    run_bin = run([str(bin_path)])
    if run_bin.returncode != 0:
        raise AssertionError(f"{fixture}: native binary crashed: {run_bin.stderr}")
    if run_bin.stdout.strip() != expected_output:
        raise AssertionError(
            f"{fixture}: native output mismatch: expected '{expected_output}', got '{run_bin.stdout.strip()}'"
        )


def verify_fail_closed(compiler: str, fixture: str, hook: str) -> None:
    source = FIXTURES / fixture
    stem = Path(fixture).stem
    rs_path = TMP / f"{stem}_{hook}.rs"
    env = os.environ.copy()
    env[hook] = "1"
    proc = run([compiler, str(source), "-o", str(rs_path)], env=env)
    if proc.returncode == 0:
        raise AssertionError(f"{fixture} with {hook}: expected failure, but succeeded!")
    if "MOSS_INTERNAL_INVARIANT" not in proc.stderr and "MOSS_INTERNAL_INVARIANT" not in proc.stdout:
        raise AssertionError(
            f"{fixture} with {hook}: expected MOSS_INTERNAL_INVARIANT, got: {proc.stderr}"
        )


def main() -> int:
    if len(sys.argv) != 2:
        raise SystemExit("usage: check_phase28_team_b_consumer.py COMPILER")
    compiler = sys.argv[1]

    verify_native(compiler, "read_alias.moss", "7\n7")
    verify_native(compiler, "write_alias.moss", "9")
    verify_native(compiler, "consume_transfer.moss", "7")

    verify_fail_closed(compiler, "read_alias.moss", "MOSS_TEST_DROP_OWNERSHIP_FACT")
    verify_fail_closed(compiler, "read_alias.moss", "MOSS_TEST_PERTURB_OWNERSHIP_FACT")

    print("phase28 team b consumer: PASS (Read/Write/Consume lowering + drop/perturb fail-closed)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
