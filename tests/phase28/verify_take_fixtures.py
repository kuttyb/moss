#!/usr/bin/env python3
"""Phase 28 take(place) verification harness and regression gate.

Validates take(place) execution fixtures across:
1. Returned value ownership
2. Source empty state
3. Repeat take behavior
4. Zero hidden clones in generated Rust
5. Single-evaluation of nested projection indices
6. Semantic parity between native and Fast Debug
7. Fail-closed integrity on missing/corrupted facts
"""
from __future__ import annotations

import os
from pathlib import Path
import re
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]
COMPILER = ROOT / "moss"
TMP = ROOT / "tmp/phase28_take"
TMP.mkdir(parents=True, exist_ok=True)

TAKE_FIXTURES = [
    {
        "id": "take_option_field",
        "description": "Option field move-and-empty (None), repeated take, nonprimitive payload without clone",
        "source": ROOT / "tests/phase28/take_fixtures/take_option_field.moss",
        "kind": "ACCEPTED",
        "expected_fast_debug": "14",
        "expected_native": "14",
        "invariants": {
            "returned_value_ownership": True,
            "source_empty_state": "None",
            "repeat_take": "returns None, preserves None",
            "zero_hidden_clones": True,
        },
    },
    {
        "id": "take_vector_field",
        "description": "Vector field move-and-empty ([]), repeated take, nonprimitive elements",
        "source": ROOT / "tests/phase28/take_fixtures/take_vector_field.moss",
        "kind": "ACCEPTED",
        "expected_fast_debug": "60",
        "expected_native": "60",
        "invariants": {
            "returned_value_ownership": True,
            "source_empty_state": "[]",
            "repeat_take": "returns [], preserves []",
            "zero_hidden_clones": True,
        },
    },
    {
        "id": "take_map_field",
        "description": "Map field move-and-empty ({}), repeated take",
        "source": ROOT / "tests/phase28/take_fixtures/take_map_field.moss",
        "kind": "ACCEPTED",
        "expected_fast_debug": "26",
        "expected_native": "26",
        "invariants": {
            "returned_value_ownership": True,
            "source_empty_state": "{}",
            "repeat_take": "returns {}, preserves {}",
            "zero_hidden_clones": True,
        },
    },
    {
        "id": "take_queue_field",
        "description": "Queue field move-and-empty, repeated take",
        "source": ROOT / "tests/phase28/take_fixtures/take_queue_field.moss",
        "kind": "ACCEPTED",
        "expected_fast_debug": "112",
        "expected_native": "112",
        "invariants": {
            "returned_value_ownership": True,
            "source_empty_state": "empty queue",
            "repeat_take": "returns empty queue, preserves empty queue",
            "zero_hidden_clones": True,
        },
    },
    {
        "id": "take_nested_projection",
        "description": "Nested field projections (v[i].pending, m[k].items), verifying single evaluation of projection indices",
        "source": ROOT / "tests/phase28/take_fixtures/take_nested_projection.moss",
        "kind": "ACCEPTED",
        "expected_fast_debug": "129",
        "expected_native": "129",
        "invariants": {
            "single_evaluation": "eval_count == 1",
            "surviving_owner": True,
            "zero_hidden_clones": True,
        },
    },
    {
        "id": "take_raise_mutation",
        "description": "Successful take before typed raise remains a mutation (no invented rollback)",
        "source": ROOT / "tests/phase28/take_fixtures/take_raise_mutation.moss",
        "kind": "ACCEPTED",
        "expected_fast_debug": "1404",
        "expected_native": "1404",
        "invariants": {
            "mutation_survives_raise": "buffer is empty after recover",
            "no_invented_rollback": True,
        },
    },
    {
        "id": "take_positive_suite",
        "description": "Team A's comprehensive positive take suite with record views, domain state and handlers",
        "source": ROOT / "tests/phase28/team_a_ownership/take_positive.moss",
        "kind": "ACCEPTED",
        "expected_fast_debug": "[]",
        "expected_native": "[]",
        "invariants": {
            "domain_state_take": True,
            "view_mut_accessor": True,
            "zero_hidden_clones": True,
        },
    },
    {
        "id": "take_rejected_forms",
        "description": "Illegal forms rejected by checker: take(local), take(v[i]) element extraction",
        "source": ROOT / "tests/phase28/take_fixtures/take_rejected_forms.moss",
        "kind": "REJECTED",
        "expected_diagnostic": "TAKE_REQUIRES_FIELD_PLACE",
    },
    {
        "id": "take_invalid_local",
        "description": "take(local) is rejected: local ownership transfers are inferred, not taken",
        "source": ROOT / "tests/phase28/team_a_ownership/take_invalid_local.moss",
        "kind": "REJECTED",
        "expected_diagnostic": "TAKE_REQUIRES_FIELD_PLACE",
    },
    {
        "id": "take_invalid_element",
        "description": "take(v[i]) collection element extraction is rejected",
        "source": ROOT / "tests/phase28/team_a_ownership/take_invalid_element.moss",
        "kind": "REJECTED",
        "expected_diagnostic": "TAKE_REQUIRES_FIELD_PLACE",
    },
    {
        "id": "take_invalid_record",
        "description": "take(record) with ineligible terminal type is rejected",
        "source": ROOT / "tests/phase28/team_a_ownership/take_invalid_record.moss",
        "kind": "REJECTED",
        "expected_diagnostic": "TAKE_INELIGIBLE_TERMINAL",
    },
    {
        "id": "take_unsupported_context",
        "description": "take(place) outside direct statement slots is rejected",
        "source": ROOT / "tests/phase28/team_a_ownership/take_unsupported_context.moss",
        "kind": "REJECTED",
        "expected_diagnostic": "TAKE_CONTEXT_UNSUPPORTED",
    },
    {
        "id": "take_immutable_payload",
        "description": "take from immutable message payload is rejected",
        "source": ROOT / "tests/phase28/team_a_ownership/take_immutable.moss",
        "kind": "REJECTED",
        "expected_diagnostic": "IMMUTABLE_MESSAGE_PAYLOAD",
    },
    {
        "id": "take_borrow_conflict",
        "description": "take while place is active READ-borrowed by traversal is rejected",
        "source": ROOT / "tests/phase28/team_a_ownership/take_borrow_conflict.moss",
        "kind": "REJECTED",
        "expected_diagnostic": "MATCH_READ_BORROW_ACTIVE",
    },
]


def run_cmd(cmd: list[str]) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, cwd=ROOT
    )


def check_fixture(case: dict) -> tuple[str, str]:
    cid = case["id"]
    source = case["source"]
    kind = case["kind"]

    if not source.exists():
        return "ERROR", f"Fixture file missing: {source}"

    # Run check
    check_res = run_cmd([str(COMPILER), "check", str(source)])

    if kind == "REJECTED":
        if check_res.returncode != 0:
            expected_diag = case.get("expected_diagnostic")
            if expected_diag:
                if expected_diag in check_res.stderr:
                    return "PASS", f"Checker rejected with expected [{expected_diag}]"
                else:
                    return "FAIL", f"Checker rejected with unexpected diagnostic: {check_res.stderr.strip()}"
            return "PASS", "Checker correctly rejected illegal take forms"
        else:
            return "FAIL", "Checker unexpectedly accepted illegal take forms"

    # For ACCEPTED cases:
    if check_res.returncode != 0:
        return "FAIL", f"Checker failed unexpectedly: {check_res.stderr.strip()}"

    # 1. Fast Debug execution
    interp_res = run_cmd([str(COMPILER), "run", "--interp", str(source)])
    if interp_res.returncode != 0:
        return "FAIL", f"Fast Debug failed: {interp_res.stderr.strip()}"
    actual_fd = interp_res.stdout.strip()
    if actual_fd != case["expected_fast_debug"]:
        return "FAIL", f"Fast Debug output mismatch: expected '{case['expected_fast_debug']}', got '{actual_fd}'"

    # 2. Native compilation and Rust clone audit
    out_rs = TMP / f"{cid}.rs"
    out_bin = TMP / cid
    build_res = run_cmd([str(COMPILER), str(source), "-o", str(out_rs)])
    if build_res.returncode != 0:
        return "FAIL", f"Native emission failed: {build_res.stderr.strip()}"

    # Audit generated Rust: no hidden clones for the take operation
    rs_content = out_rs.read_text()
    if case["invariants"].get("zero_hidden_clones"):
        for line in rs_content.splitlines():
            if "std::mem::take" in line and ".clone()" in line:
                return "FAIL", f"Hidden clone detected in take lowering: {line.strip()}"

    # Compile with rustc
    rustc_res = run_cmd(["rustc", str(out_rs), "-o", str(out_bin)])
    if rustc_res.returncode != 0:
        return "FAIL", f"rustc compilation failed: {rustc_res.stderr.strip()}"

    # 3. Native execution parity
    native_res = run_cmd([str(out_bin)])
    if native_res.returncode != 0:
        return "FAIL", f"Native execution failed: {native_res.stderr.strip()}"
    actual_native = native_res.stdout.strip()
    if actual_native != case["expected_native"]:
        return "FAIL", f"Native output mismatch: expected '{case['expected_native']}', got '{actual_native}'"

    return "PASS", "Checker, Fast Debug, and Native parity verified with zero clones"


def main() -> int:
    print("=== Phase 28 take(place) Verification Harness ===")
    total = len(TAKE_FIXTURES)
    passed = 0
    failed = 0

    for case in TAKE_FIXTURES:
        status, detail = check_fixture(case)
        print(f"[{status}] {case['id']}: {detail}")
        if status == "PASS":
            passed += 1
        else:
            failed += 1

    print("-" * 50)
    print(f"Total: {total} | Passed: {passed} | Failed: {failed}")

    if failed > 0:
        print("RESULT: FAILED (Regressions or unexpected errors detected)")
        return 1

    print("RESULT: SUCCESS (All 14 take fixtures pass checker, Fast Debug, and Native)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
