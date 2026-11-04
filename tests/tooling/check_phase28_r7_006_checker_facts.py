#!/usr/bin/env python3
"""Exercise Phase 28 R7-006 Checker Fact Completion:
- ORACLE-01: Vector element write-through (Fast Debug, Native Debug, Native Optimized -> 9\n9)
- ORACLE-04: Map entry write-through (Fast Debug, Native Debug, Native Optimized -> 9\n9)
- ORACLE-40: Mutable Vector iteration fact synchronization (Fast Debug, Native Debug, Native Optimized -> 11\n21)
- Rejection of consuming collection elements (ORACLE-03 and ORACLE-14)
- Fail-closed verification for missing and perturbed facts in Native and Fast Debug
"""
from __future__ import annotations

import os
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]
FIXTURES = ROOT / "tests/phase28/consumer_repair/fixtures"
TMP = ROOT / "tmp/phase28_r7_006"
TMP.mkdir(parents=True, exist_ok=True)


def run(cmd: list[str], env: dict[str, str] | None = None) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, cwd=ROOT, env=env
    )


def verify_execution(compiler: str, fixture_path: Path, expected_output: str) -> None:
    stem = fixture_path.stem
    rs_path = TMP / f"{stem}.rs"
    bin_path = TMP / f"{stem}.bin"
    rs_opt_path = TMP / f"{stem}_opt.rs"
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

    # Native Optimized: `moss -O` emits a separate optimized Rust file, never
    # reusing the debug-generated Rust.
    emit_opt = run([compiler, "-O", str(fixture_path), "-o", str(rs_opt_path)])
    if emit_opt.returncode != 0:
        raise AssertionError(f"{fixture_path.name}: moss -O emit failed: {emit_opt.stderr}")
    if rs_opt_path == rs_path or not rs_opt_path.exists():
        raise AssertionError(f"{fixture_path.name}: optimized Rust file is not separate")
    rustc_opt = run(["rustc", "--edition=2021", "-O", str(rs_opt_path), "-o", str(bin_opt_path)])
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


def verify_rejected(compiler: str, fixture_path: Path, expected_code: str) -> None:
    res = run([compiler, "check", str(fixture_path)])
    if res.returncode == 0:
        raise AssertionError(f"{fixture_path.name}: expected rejection, but passed!")
    if expected_code not in res.stderr and expected_code not in res.stdout:
        raise AssertionError(
            f"{fixture_path.name}: expected error code '{expected_code}', got: {res.stderr}\n{res.stdout}"
        )


def verify_fail_closed(compiler: str, fixture_path: Path, hook: str) -> None:
    stem = fixture_path.stem
    rs_path = TMP / f"{stem}_{hook}.rs"
    env = os.environ.copy()
    env[hook] = "1"
    proc = run([compiler, str(fixture_path), "-o", str(rs_path)], env=env)
    if proc.returncode == 0:
        raise AssertionError(f"{fixture_path.name} with {hook}: expected failure, but succeeded!")
    if "MOSS_INTERNAL_INVARIANT" not in proc.stderr and "MOSS_INTERNAL_INVARIANT" not in proc.stdout:
        raise AssertionError(
            f"{fixture_path.name} with {hook}: expected MOSS_INTERNAL_INVARIANT, got: {proc.stderr}"
        )


def verify_fast_debug_fail_closed(compiler: str, fixture_path: Path, hook: str) -> None:
    env = os.environ.copy()
    env[hook] = "1"
    proc = run([compiler, "run", "--interp", str(fixture_path)], env=env)
    if proc.returncode == 0:
        raise AssertionError(f"Fast Debug {fixture_path.name} with {hook}: expected failure, but succeeded!")
    if "MOSS_INTERNAL_INVARIANT" not in proc.stderr and "MOSS_INTERNAL_INVARIANT" not in proc.stdout:
        raise AssertionError(
            f"Fast Debug {fixture_path.name} with {hook}: expected MOSS_INTERNAL_INVARIANT, got: {proc.stderr}"
        )


def ownership_facts(compiler: str, fixture_path: Path) -> list[dict[str, str]]:
    """Return checker-issued EST ownership facts (MOSS_EST_FACTS_DUMP)."""
    import re

    env = os.environ.copy()
    env["MOSS_EST_FACTS_DUMP"] = "1"
    proc = run([compiler, "check", str(fixture_path), "--json"], env=env)
    if proc.returncode != 0:
        raise AssertionError(f"{fixture_path.name}: check failed: {proc.stdout}{proc.stderr}")
    facts = []
    for line in proc.stderr.splitlines():
        if line.startswith("MOSS_EST_OWNERSHIP_FACT "):
            facts.append(dict(re.findall(r'(\w+)=("[^"]*"|\S+)', line)))
    return facts


def verify_provenance(compiler: str, fixture_path: Path, binding: str, source_line: int, access: int) -> None:
    facts = ownership_facts(compiler, fixture_path)
    matches = [f for f in facts if f["definition"].strip('"').endswith(":" + binding)]
    if len(matches) != 1:
        raise AssertionError(f"{fixture_path.name}: expected one fact for '{binding}', got {facts}")
    fact = matches[0]
    if fact["source"].strip('"') != f"main@{source_line}:statement":
        raise AssertionError(f"{fixture_path.name}: wrong source storage {fact['source']} (want line {source_line})")
    if int(fact["access"]) != access or fact["availability"] != "1":
        raise AssertionError(f"{fixture_path.name}: wrong access/availability {fact} (want access {access})")


def main() -> int:
    compiler = sys.argv[1] if len(sys.argv) > 1 else str(ROOT / "moss")

    print(f"=== Testing P28-R7-006 Checker Fact Completion on {compiler} ===")

    # 1. ORACLE-01: Vector element write-through
    oracle_01 = FIXTURES / "oracle_01_write_through_vector.moss"
    print(f"Checking ORACLE-01: {oracle_01.name}...")
    verify_execution(compiler, oracle_01, "9\n9")

    # 2. ORACLE-04: Map entry write-through
    oracle_04 = FIXTURES / "oracle_04_write_through_map_entry.moss"
    print(f"Checking ORACLE-04: {oracle_04.name}...")
    verify_execution(compiler, oracle_04, "9\n9")

    # 3. ORACLE-40: Mutable Vector iteration
    oracle_40 = FIXTURES / "oracle_40_vector_for_write.moss"
    print(f"Checking ORACLE-40: {oracle_40.name}...")
    verify_execution(compiler, oracle_40, "11\n21")

    # 4. Reject implicit consumption of collection elements (ORACLE-03, ORACLE-14)
    oracle_03 = FIXTURES / "oracle_03_collection_element_consume_rejected.moss"
    print(f"Checking rejection on ORACLE-03: {oracle_03.name}...")
    verify_rejected(compiler, oracle_03, "OWNERSHIP_COLLECTION_ELEMENT_CONSUME")

    oracle_14 = FIXTURES / "oracle_14_assignment_consume_borrowed_rejected.moss"
    print(f"Checking rejection on ORACLE-14: {oracle_14.name}...")
    verify_rejected(compiler, oracle_14, "OWNERSHIP_COLLECTION_ELEMENT_CONSUME")

    # 4b. Indexed provenance: READ vs WRITE borrow and source storage (access 0=Read, 1=Write).
    read_fixture = FIXTURES / "indexed_read_borrow.moss"
    print(f"Checking indexed READ provenance: {read_fixture.name}...")
    verify_execution(compiler, read_fixture, "4\n4")
    verify_provenance(compiler, read_fixture, "b", 6, 0)
    print("Checking indexed WRITE provenance on ORACLE-01/04...")
    verify_provenance(compiler, oracle_01, "b", 6, 1)
    verify_provenance(compiler, oracle_04, "e", 6, 1)
    immutable = FIXTURES / "indexed_immutable_write_rejected.moss"
    print(f"Checking immutable-container rejection: {immutable.name}...")
    verify_rejected(compiler, immutable, "OWNERSHIP_MUTABLE_BORROW_IMMUTABLE")

    # 5. Fail-closed tests for dropped / perturbed checker facts
    if "moss-opt" not in compiler:
        print("Checking fail-closed hooks on ORACLE-01...")
        verify_fail_closed(compiler, oracle_01, "MOSS_TEST_DROP_OWNERSHIP_FACT")
        verify_fail_closed(compiler, oracle_01, "MOSS_TEST_PERTURB_OWNERSHIP_FACT")
        verify_fail_closed(compiler, oracle_01, "MOSS_TEST_CONSUMER_DROP_OWNERSHIP_FACT")
        verify_fail_closed(compiler, oracle_01, "MOSS_TEST_CONSUMER_PERTURB_OWNERSHIP_FACT")
        verify_fast_debug_fail_closed(compiler, oracle_01, "MOSS_TEST_CONSUMER_DROP_OWNERSHIP_FACT")
        verify_fast_debug_fail_closed(compiler, oracle_01, "MOSS_TEST_CONSUMER_PERTURB_OWNERSHIP_FACT")

        print("Checking missing-expression-tree corruption (Fast Debug)...")
        for fixture in (oracle_01, oracle_04):
            verify_fast_debug_fail_closed(compiler, fixture, "MOSS_TEST_CONSUMER_DROP_BORROW_EXPR_TREE")

        print("Checking fail-closed hooks on ORACLE-40...")
        verify_fail_closed(compiler, oracle_40, "MOSS_TEST_CONSUMER_DROP_OWNERSHIP_FACT")
        verify_fail_closed(compiler, oracle_40, "MOSS_TEST_CONSUMER_PERTURB_OWNERSHIP_FACT")

    print("ALL P28-R7-006 CHECKER FACT COMPLETION TESTS PASSED!")
    return 0


if __name__ == "__main__":
    sys.exit(main())
