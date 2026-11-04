#!/usr/bin/env python3
"""Exercise Phase 28 R7-003 Consumer Repairs:
- ORACLE-06: Option match consume (Native Debug, Native Optimized, Fast Debug)
- ORACLE-09: User enum Some/None (Fast Debug, Native Debug, Native Optimized)
- ORACLE-40: Mutable Vector iteration (Fast Debug, Native Debug, Native Optimized)
- ORACLE-02: Indexed field reads without clone (Provisional)
- Invariant & contract verification for ORACLE-01 and ORACLE-04
"""
from __future__ import annotations

import os
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]
FIXTURES = ROOT / "tests/phase28/consumer_repair/fixtures"
TMP = ROOT / "tmp/phase28_r7_003"
TMP.mkdir(parents=True, exist_ok=True)


def run(cmd: list[str], env: dict[str, str] | None = None) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, cwd=ROOT, env=env
    )


def verify_execution(compiler: str, fixture_path: Path, expected_output: str) -> None:
    stem = fixture_path.stem
    rs_path = TMP / f"{stem}.rs"
    bin_path = TMP / f"{stem}.bin"
    bin_opt_path = TMP / f"{stem}_opt.bin"

    # Fast Debug
    fast_debug = run([compiler, "run", "--interp", str(fixture_path)])
    if fast_debug.returncode != 0:
        raise AssertionError(f"{fixture_path.name}: Fast Debug failed: {fast_debug.stderr}")
    if fast_debug.stdout.strip() != expected_output.strip():
        raise AssertionError(
            f"{fixture_path.name}: Fast Debug mismatch: expected '{expected_output.strip()}', got '{fast_debug.stdout.strip()}'"
        )

    # Native emit
    emit = run([compiler, str(fixture_path), "-o", str(rs_path)])
    if emit.returncode != 0:
        raise AssertionError(f"{fixture_path.name}: Native emit failed: {emit.stderr}")

    # Rustc compile (Debug)
    rustc = run(["rustc", "--edition=2021", str(rs_path), "-o", str(bin_path)])
    if rustc.returncode != 0:
        raise AssertionError(f"{fixture_path.name}: rustc compilation failed: {rustc.stderr}")

    # Run native binary (Debug)
    run_bin = run([str(bin_path)])
    if run_bin.returncode != 0:
        raise AssertionError(f"{fixture_path.name}: native binary crashed: {run_bin.stderr}")
    if run_bin.stdout.strip() != expected_output.strip():
        raise AssertionError(
            f"{fixture_path.name}: native output mismatch: expected '{expected_output.strip()}', got '{run_bin.stdout.strip()}'"
        )

    # Rustc compile (Optimized)
    rustc_opt = run(["rustc", "--edition=2021", "-O", str(rs_path), "-o", str(bin_opt_path)])
    if rustc_opt.returncode != 0:
        raise AssertionError(f"{fixture_path.name}: rustc -O compilation failed: {rustc_opt.stderr}")

    # Run native binary (Optimized)
    run_bin_opt = run([str(bin_opt_path)])
    if run_bin_opt.returncode != 0:
        raise AssertionError(f"{fixture_path.name}: native optimized binary crashed: {run_bin_opt.stderr}")
    if run_bin_opt.stdout.strip() != expected_output.strip():
        raise AssertionError(
            f"{fixture_path.name}: native optimized output mismatch: expected '{expected_output.strip()}', got '{run_bin_opt.stdout.strip()}'"
        )


def main() -> int:
    compiler = sys.argv[1] if len(sys.argv) > 1 else str(ROOT / "moss")

    print(f"=== Testing P28-R7-003 Consumer Repairs on {compiler} ===")

    # 1. ORACLE-06: Option match consume
    oracle_06 = FIXTURES / "oracle_06_match_consume_option_native.moss"
    print(f"Checking ORACLE-06: {oracle_06.name}...")
    verify_execution(compiler, oracle_06, "target")

    # 2. ORACLE-09: User enum Some/None
    oracle_09 = FIXTURES / "oracle_09_user_enum_some_none.moss"
    print(f"Checking ORACLE-09: {oracle_09.name}...")
    verify_execution(compiler, oracle_09, "42")

    # 3. ORACLE-40: Mutable Vector iteration
    oracle_40 = FIXTURES / "oracle_40_vector_for_write.moss"
    print(f"Checking ORACLE-40: {oracle_40.name}...")
    verify_execution(compiler, oracle_40, "11\n21")

    # 4. ORACLE-02: Indexed field reads without clone (Provisional)
    oracle_02 = FIXTURES / "oracle_02_indexed_field_read_no_clone.moss"
    print(f"Checking ORACLE-02: {oracle_02.name}...")
    verify_execution(compiler, oracle_02, "hello")
    # Verify no .clone() calls in main body in emitted Rust
    rs_02 = (TMP / f"{oracle_02.stem}.rs").read_text()
    main_body = rs_02[rs_02.rfind("fn main()"): ]
    if ".clone()" in main_body:
        raise AssertionError("ORACLE-02: Emitted Rust main contains unexpected .clone() call!")

    # 5. ORACLE-01: Vector element write-through
    oracle_01 = FIXTURES / "oracle_01_write_through_vector.moss"
    print(f"Checking ORACLE-01: {oracle_01.name}...")
    verify_execution(compiler, oracle_01, "9\n9")

    # 6. ORACLE-04: Map entry write-through
    oracle_04 = FIXTURES / "oracle_04_write_through_map_entry.moss"
    print(f"Checking ORACLE-04: {oracle_04.name}...")
    verify_execution(compiler, oracle_04, "9\n9")

    print("ALL P28-R7-003 CONSUMER REPAIRS PASSED!")
    return 0


if __name__ == "__main__":
    sys.exit(main())
