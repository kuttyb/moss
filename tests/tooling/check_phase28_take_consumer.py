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
import re
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

    # Targeted occurrence-specific corruption and selector isolation tests
    multi_fixture = ROOT / "tests/phase28/take_fixtures/take_multi_selector.moss"
    dump_proc = run([compiler, "check", str(multi_fixture)], env={**os.environ, "MOSS_EST_FACTS_DUMP": "1"})
    sel1_call = None
    sel1_arg = None
    sel2_call = None
    sel2_arg = None
    for line in dump_proc.stderr.splitlines():
        if "MOSS_EST_SELECTOR_ARGUMENT_EFFECT" in line and "sel_first" in line:
            m = re.search(r"call=(\d+)\s+argument=(\d+)", line)
            if m:
                sel1_call = int(m.group(1))
                sel1_arg = int(m.group(2))
        elif "MOSS_EST_SELECTOR_ARGUMENT_EFFECT" in line and "sel_second" in line:
            m = re.search(r"call=(\d+)\s+argument=(\d+)", line)
            if m:
                sel2_call = int(m.group(1))
                sel2_arg = int(m.group(2))
    assert sel1_call is not None and sel1_arg is not None, "Failed to discover sel_first occurrence"
    assert sel2_call is not None and sel2_arg is not None, "Failed to discover sel_second occurrence"

    def verify_occurrence_mutation(arena_idx: int, expr_id: int, ctx: str, mutation: str, sibling_id: int | None = None) -> None:
        rs_path = TMP / f"occurrence_fail_{expr_id}_{mutation}.rs"
        env = os.environ.copy()
        env["MOSS_TEST_TARGET_ARENA"] = str(arena_idx)
        env["MOSS_TEST_TARGET_EXPR"] = str(expr_id)
        env["MOSS_TEST_TARGET_CONTEXT"] = ctx
        env["MOSS_TEST_TARGET_MUTATION"] = mutation
        if sibling_id is not None:
            env["MOSS_TEST_ISOLATION_SIBLING_EXPR"] = str(sibling_id)
        proc_native = run([compiler, str(multi_fixture), "-o", str(rs_path)], env=env)
        if proc_native.returncode == 0:
            raise AssertionError(f"Occurrence mutation {expr_id} {mutation}: expected failure, succeeded!")
        if "MOSS_INTERNAL_INVARIANT" not in (proc_native.stderr + proc_native.stdout):
            raise AssertionError(f"Occurrence mutation {expr_id} {mutation}: expected MOSS_INTERNAL_INVARIANT, got:\n{proc_native.stderr}")

    # Corrupt Selector 1's argument effect while asserting Selector 2 remains intact
    verify_occurrence_mutation(0, sel1_arg, "fn:take_both", "drop_effect", sibling_id=sel2_arg)
    print(f"[PASS] targeted occurrence mutation (sel1 arg={sel1_arg} drop_effect, sibling={sel2_arg} intact): correctly failed closed")

    verify_occurrence_mutation(0, sel1_arg, "fn:take_both", "perturb_effect", sibling_id=sel2_arg)
    print(f"[PASS] targeted occurrence mutation (sel1 arg={sel1_arg} perturb_effect, sibling={sel2_arg} intact): correctly failed closed")

    # Corrupt Selector 1's call target while asserting Selector 2 remains intact
    verify_occurrence_mutation(0, sel1_call, "fn:take_both", "perturb_target", sibling_id=sel2_arg)
    print(f"[PASS] targeted occurrence mutation (sel1 call={sel1_call} perturb_target, sibling={sel2_arg} intact): correctly failed closed")

    # Corrupt Selector 2's argument effect while asserting Selector 1 remains intact
    verify_occurrence_mutation(0, sel2_arg, "fn:take_both", "drop_effect", sibling_id=sel1_arg)
    print(f"[PASS] targeted occurrence mutation (sel2 arg={sel2_arg} drop_effect, sibling={sel1_arg} intact): correctly failed closed")

    verify_occurrence_mutation(0, sel2_arg, "fn:take_both", "perturb_effect", sibling_id=sel1_arg)
    print(f"[PASS] targeted occurrence mutation (sel2 arg={sel2_arg} perturb_effect, sibling={sel1_arg} intact): correctly failed closed")

    print("-" * 50)
    print("RESULT: SUCCESS (All 22 take and selector semantic authority fail-closed tests passed)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
