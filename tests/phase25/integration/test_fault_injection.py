#!/usr/bin/env python3
"""Deliberate Fault-Injection Test Suite for Phase 25 Integration Prep.

Validates that the acceptance ledger runner and cross-crate harness fail closed
against false-green results:
1. Missing dependencies cannot produce PASS
2. Unrelated compiler errors cannot produce PASS
3. Stale evidence (fake git SHA, non-existent files, pending markers) cannot produce PASS
4. Incomplete cross-consumer linking cannot produce PASS

Usage:
  python3 tests/phase25/integration/test_fault_injection.py
"""

from __future__ import annotations

from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[3]
HARNESS_DIR = ROOT / "tests" / "phase25" / "integration"


def run_command(cmd: list[str], description: str) -> bool:
    print(f"\n[RUN] {description}: {' '.join(cmd)}")
    proc = subprocess.run(cmd, cwd=ROOT, text=True, capture_output=True)
    if proc.returncode != 0:
        print(f"[FAIL] {description} exited with code {proc.returncode}")
        print("STDOUT:", proc.stdout)
        print("STDERR:", proc.stderr)
        return False
    print(proc.stdout.strip())
    print(f"[OK] {description} PASSED.")
    return True


def main() -> int:
    print("=" * 70)
    print("Phase 25 Deliberate Fault-Injection & Anti-False-Green Verification Suite")
    print("=" * 70)

    # 1. Test Acceptance Ledger Fault Injection
    ok1 = run_command(
        [sys.executable, str(HARNESS_DIR / "run_acceptance.py"), "--fault-injection"],
        "Acceptance Ledger Anti-False-Green Verification",
    )
    if not ok1:
        return 1

    # 2. Test Cross-Crate Harness Fault Injection
    ok2 = run_command(
        [sys.executable, str(HARNESS_DIR / "cross_crate_harness.py"), "--fault-injection"],
        "Cross-Crate & ABI-v10 Harness Anti-False-Green Verification",
    )
    if not ok2:
        return 1

    # 3. Acceptance runner self-test (includes embedded fault injection)
    ok3 = run_command(
        [sys.executable, str(HARNESS_DIR / "run_acceptance.py"), "--self-test"],
        "Acceptance Runner Full Self-Test",
    )
    if not ok3:
        return 1

    # 4. Cross-crate harness self-test (includes embedded fault injection)
    ok4 = run_command(
        [sys.executable, str(HARNESS_DIR / "cross_crate_harness.py"), "--self-test"],
        "Cross-Crate Harness Full Self-Test",
    )
    if not ok4:
        return 1

    print("\n" + "=" * 70)
    print("ALL 4 DELIBERATE FAULT INJECTIONS VERIFIED CLEANLY: ZERO FALSE GREENS")
    print("=" * 70)
    return 0


if __name__ == "__main__":
    sys.exit(main())
