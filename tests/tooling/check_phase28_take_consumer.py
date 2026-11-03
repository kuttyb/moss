#!/usr/bin/env python3
"""Exercise Phase 28 Team B native and Fast Debug consumers of checker-owned take facts.

Verifies strict checker semantic authority and fail-closed invariants:
1. Native and Fast Debug execution on valid take fixtures
2. Fact availability drop fails closed with MOSS_INTERNAL_INVARIANT
3. Fact effect perturbation (WRITE -> READ) fails closed with MOSS_INTERNAL_INVARIANT
4. Fact record deletion fails closed with MOSS_INTERNAL_INVARIANT
5. Requirement record deletion fails closed with MOSS_INTERNAL_INVARIANT
6. Duplicate fact records fail closed with MOSS_INTERNAL_INVARIANT
7. Duplicate requirement records fail closed with MOSS_INTERNAL_INVARIANT
8. Perturbed place occurrence ID fails closed with MOSS_INTERNAL_INVARIANT
"""
from __future__ import annotations

import os
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]
TMP = ROOT / "tmp/phase28_take_consumer"
TMP.mkdir(parents=True, exist_ok=True)
POSITIVE_FIXTURE = ROOT / "tests/phase28/take_fixtures/take_vector_field.moss"


def run(cmd: list[str], env: dict[str, str] | None = None) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, cwd=ROOT, env=env
    )


def verify_fail_closed(compiler: str, hook: str) -> None:
    # 1. Native emission fail-closed
    rs_path = TMP / f"take_fail_{hook}.rs"
    env = os.environ.copy()
    env[hook] = "1"
    proc_native = run([compiler, str(POSITIVE_FIXTURE), "-o", str(rs_path)], env=env)
    if proc_native.returncode == 0:
        raise AssertionError(f"Hook {hook}: expected native failure, but compilation succeeded!")
    combined_native = proc_native.stderr + proc_native.stdout
    if "MOSS_INTERNAL_INVARIANT" not in combined_native:
        raise AssertionError(
            f"Hook {hook}: expected MOSS_INTERNAL_INVARIANT in native, got output:\n{combined_native}"
        )

    # 2. Fast Debug execution fail-closed
    proc_fd = run([compiler, "run", "--interp", str(POSITIVE_FIXTURE)], env=env)
    if proc_fd.returncode == 0:
        raise AssertionError(f"Hook {hook}: expected Fast Debug failure, but execution succeeded!")
    combined_fd = proc_fd.stderr + proc_fd.stdout
    if "MOSS_INTERNAL_INVARIANT" not in combined_fd:
        raise AssertionError(
            f"Hook {hook}: expected MOSS_INTERNAL_INVARIANT in Fast Debug, got output:\n{combined_fd}"
        )


def verify_selector_fail_closed(compiler: str, hook: str, fixture_path: Path | None = None) -> None:
    fixture = fixture_path or (ROOT / "tests/phase28/take_fixtures/take_nested_projection.moss")
    rs_path = TMP / f"selector_fail_{hook}.rs"
    env = os.environ.copy()
    env[hook] = "1"
    proc_native = run([compiler, str(fixture), "-o", str(rs_path)], env=env)
    if proc_native.returncode == 0:
        raise AssertionError(f"Selector hook {hook}: expected native failure, but compilation succeeded!")
    combined = proc_native.stderr + proc_native.stdout
    if "MOSS_INTERNAL_INVARIANT" not in combined:
        raise AssertionError(f"Selector hook {hook}: expected MOSS_INTERNAL_INVARIANT, got:\n{combined}")


def main() -> int:
    if len(sys.argv) != 2:
        raise SystemExit("usage: check_phase28_take_consumer.py COMPILER")
    compiler = sys.argv[1]

    # Verify baseline succeeds without hooks
    baseline_rs = TMP / "baseline.rs"
    base_res = run([compiler, str(POSITIVE_FIXTURE), "-o", str(baseline_rs)])
    if base_res.returncode != 0:
        raise AssertionError(f"Baseline compile failed: {base_res.stderr}")

    hooks = [
        "MOSS_TEST_CONSUMER_DROP_TAKE_FACT",
        "MOSS_TEST_CONSUMER_PERTURB_TAKE_FACT",
        "MOSS_TEST_CONSUMER_DELETE_TAKE_RECORD",
        "MOSS_TEST_CONSUMER_DELETE_TAKE_REQUIREMENT",
        "MOSS_TEST_CONSUMER_DUPLICATE_TAKE_FACT",
        "MOSS_TEST_CONSUMER_DUPLICATE_TAKE_REQUIREMENT",
        "MOSS_TEST_CONSUMER_PERTURB_TAKE_PLACE_ID",
    ]

    for hook in hooks:
        verify_fail_closed(compiler, hook)
        print(f"[PASS] {hook}: correctly failed closed with MOSS_INTERNAL_INVARIANT")

    selector_hooks = [
        "MOSS_TEST_CONSUMER_DROP_SELECTOR_TARGET",
        "MOSS_TEST_CONSUMER_PERTURB_SELECTOR_TARGET",
        "MOSS_TEST_CONSUMER_DROP_SELECTOR_EFFECT",
        "MOSS_TEST_CONSUMER_DROP_SELECTOR_OP_TYPE",
        "MOSS_TEST_CONSUMER_DROP_PRIMARY_SELECTOR_FACT",
        "MOSS_TEST_CONSUMER_DROP_SECONDARY_SELECTOR_FACT",
        "MOSS_TEST_CONSUMER_CONTRADICT_SELECTOR_TARGET",
        "MOSS_TEST_CONSUMER_CONTRADICT_SELECTOR_EFFECT",
        "MOSS_TEST_CONSUMER_PERTURB_PRIMARY_EFFECT",
    ]
    for hook in selector_hooks:
        verify_selector_fail_closed(compiler, hook)
        print(f"[PASS] {hook}: selector correctly failed closed with MOSS_INTERNAL_INVARIANT")

    # Binary arithmetic selector operator-fact corruption & context contradiction tests
    arithmetic_fixture = ROOT / "tests/phase28/take_fixtures/take_selector_operators.moss"
    verify_selector_fail_closed(compiler, "MOSS_TEST_CONSUMER_DROP_SELECTOR_OP_TYPE", arithmetic_fixture)
    print("[PASS] MOSS_TEST_CONSUMER_DROP_SELECTOR_OP_TYPE (binary arithmetic selector): correctly failed closed with MOSS_INTERNAL_INVARIANT")
    verify_selector_fail_closed(compiler, "MOSS_TEST_CONSUMER_CONTRADICT_SELECTOR_TYPE", arithmetic_fixture)
    print("[PASS] MOSS_TEST_CONSUMER_CONTRADICT_SELECTOR_TYPE (binary arithmetic selector): correctly failed closed with MOSS_INTERNAL_INVARIANT")

    # Real native specialized selector call: wrong specialization corruption test
    spec_fixture = ROOT / "tests/phase28/take_fixtures/take_specialized_selector.moss"
    verify_selector_fail_closed(compiler, "MOSS_TEST_CONSUMER_WRONG_SELECTOR_SPECIALIZATION", spec_fixture)
    print("[PASS] MOSS_TEST_CONSUMER_WRONG_SELECTOR_SPECIALIZATION (specialized selector call): correctly failed closed with MOSS_INTERNAL_INVARIANT")

    # Multi-specialization & targeted contract corruption test
    contract_bin = TMP / "typed_take_contract_test"
    contract_src = ROOT / "tests/tooling/typed_take_contract_test.cpp"
    compile_contract = run(["c++", "-std=c++17", "-Isrc", str(contract_src), "-o", str(contract_bin)])
    if compile_contract.returncode != 0:
        raise AssertionError(f"Failed to build typed_take_contract_test:\n{compile_contract.stderr}")
    run_contract = run([str(contract_bin)])
    if run_contract.returncode != 0:
        raise AssertionError(f"typed_take_contract_test failed:\n{run_contract.stderr}")
    print("[PASS] typed_take_contract_test: multi-specialization exact lookup and targeted corruption passed")

    print("-" * 50)
    print("RESULT: SUCCESS (All 20 take and selector semantic authority fail-closed tests passed)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
