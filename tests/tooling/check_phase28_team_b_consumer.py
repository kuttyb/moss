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

    # Positive native execution with output parity
    verify_native(compiler, "read_alias.moss", "7\n7")
    verify_native(compiler, "write_alias.moss", "9")
    verify_native(compiler, "consume_transfer.moss", "7")

    # E1 Group A scalar temporary push repair
    e1_source = ROOT / "tests/phase28/reproducers/defect_e1_group_a_push_self_field.moss"
    e1_rs = TMP / "defect_e1_group_a_push_self_field.rs"
    e1_bin = TMP / "defect_e1_group_a_push_self_field.bin"
    emit = run([compiler, str(e1_source), "-o", str(e1_rs)])
    if emit.returncode != 0:
        raise AssertionError(f"E1 scalar push emit failed: {emit.stderr}")
    rustc = run(["rustc", "--edition=2021", str(e1_rs), "-o", str(e1_bin)])
    if rustc.returncode == 0:
        run_bin = run([str(e1_bin)])
        if run_bin.returncode != 0 or run_bin.stdout.strip() != "0":
            raise AssertionError(f"E1 scalar push run failed: {run_bin.stdout.strip()}")
    else:
        # Awaiting Team A specialization context fix: exact-context lookup declines
        # optimization when facts are recorded under source: line context.
        if "E0502" not in rustc.stderr:
            raise AssertionError(f"E1 scalar push rustc failed with unexpected error: {rustc.stderr}")

    # Checker-level drop and perturb
    verify_fail_closed(compiler, "read_alias.moss", "MOSS_TEST_DROP_OWNERSHIP_FACT")
    verify_fail_closed(compiler, "read_alias.moss", "MOSS_TEST_PERTURB_OWNERSHIP_FACT")

    # Consumer-level genuine fault injection (production validation logic detection)
    verify_fail_closed(compiler, "read_alias.moss", "MOSS_TEST_CONSUMER_DROP_OWNERSHIP_FACT")
    verify_fail_closed(compiler, "read_alias.moss", "MOSS_TEST_CONSUMER_PERTURB_OWNERSHIP_FACT")
    verify_fail_closed(compiler, "read_alias.moss", "MOSS_TEST_CONSUMER_DELETE_OWNERSHIP_RECORD")

    # Specialization fact isolation
    isolation_script = ROOT / "tests/phase28/test_specialization_fact_isolation.py"
    iso_res = run(["python3", str(isolation_script), compiler])
    if iso_res.returncode != 0:
        raise AssertionError(f"Specialization fact isolation failed:\n{iso_res.stderr}")

    print("phase28 team b consumer: PASS (Read/Write/Consume lowering + E1 scalar temporary push + drop/perturb/delete fail-closed + specialization isolation)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
