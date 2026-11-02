#!/usr/bin/env python3
"""Phase 28 E1-E4 defect regression gate.

Enforces expected behavior across checker, Fast Debug, and native compilation.
Distinguishes PASS, KNOWN FAILURE, and UNEXPECTED FAILURE.
Exits with nonzero returncode on any regression or unexpected outcome.
"""
from __future__ import annotations

import os
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]
COMPILER = ROOT / "moss"
TMP = ROOT / "tmp/phase28_defects"
TMP.mkdir(parents=True, exist_ok=True)

# Test cases with explicit expected outcomes.
# kind: "PASS" (fully working with parity) or "KNOWN_FAILURE" (documented compiler defect).
TEST_CASES = [
    {
        "id": "E1_group_a_push_self_field",
        "description": "Group A push_self_field_read: whole-self field accessor borrowing conflict",
        "source": ROOT / "tests/phase28/reproducers/defect_e1_group_a_push_self_field.moss",
        "kind": "KNOWN_FAILURE",
        "expected_checker": "PASS",
        "expected_fast_debug": "0",
        "expected_rustc": "FAIL",
        "expected_rustc_error": "E0502",
        "defect_note": "Open defect: whole-self field accessors via MossAccess_* trait take &mut self, conflicting with immutable field borrows",
    },
    {
        "id": "E1_group_a_field_self_assign",
        "description": "Group A field_self_assign_struct: object replacement with RHS-before-LHS temporary",
        "source": ROOT / "tests/phase28/reproducers/defect_e1_group_a_field_self_assign.moss",
        "kind": "PASS",
        "expected_checker": "PASS",
        "expected_fast_debug": "1",
        "expected_rustc": "PASS",
        "expected_native": "1",
        "defect_note": "Repaired in Round 1 via independent RHS-before-LHS assignment temporary binding in native generator",
    },
    {
        "id": "E2_swarm_035_nested_mutating_calls",
        "description": "SWARM-035 canonical reproducer: nested mutating calls on the same receiver",
        "source": ROOT / "tests/swarm_035_nested_mutating_calls.moss",
        "kind": "PASS",
        "expected_checker": "PASS",
        "expected_fast_debug": "1",
        "expected_rustc": "PASS",
        "expected_native": "1",
        "defect_note": "Passes on canonical fixture via call_argument_temp_ in emitter; generalized nested call sequencing awaits checker facts",
    },
    {
        "id": "E3_t6_untyped_for_read",
        "description": "T6 untyped read-only loop: vector iteration in standalone function",
        "source": ROOT / "tests/phase28/reproducers/defect_e3_t6_untyped_for_read.moss",
        "kind": "PASS",
        "expected_checker": "PASS",
        "expected_fast_debug": "3",
        "expected_rustc": "PASS",
        "expected_native": "3",
        "defect_note": "Standalone read-only loop passes; complex multi-field Shapes scenario remains a checker inference failure",
    },
    {
        "id": "E3_t6_shapes_inference",
        "description": "T6 Shapes complex scenario: multi-trait collection methods trigger checker failure",
        "source": ROOT / "examples/swarm/Surface/Shapes/src/main.moss",
        "kind": "KNOWN_FAILURE",
        "expected_checker": "FAIL",
        "expected_checker_error": "unresolved callable argument",
        "expected_fast_debug": "SKIP",
        "expected_rustc": "SKIP",
        "defect_note": "Open checker defect: synchronization_require fails with 'unresolved callable argument' during leaf-effect analysis",
    },
    {
        "id": "E4_mutate_loop_element",
        "description": "mutate_loop_element: in-place mutation of vector element within loop",
        "source": ROOT / "tests/phase28/reproducers/defect_e4_mutate_loop_element.moss",
        "kind": "KNOWN_FAILURE",
        "expected_checker": "PASS",
        "expected_fast_debug": "7 17",
        "expected_rustc": "FAIL",
        "expected_rustc_error": "E0596",
        "defect_note": "Open defect: checker infers receiver as READ instead of WRITE; emitter hardcodes .iter() and immutable binding",
    },
]


def run(cmd: list[str]) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, cwd=ROOT
    )


def main() -> None:
    print("=== Phase 28 Defect Regression Gate ===")
    total_count = len(TEST_CASES)
    pass_count = 0
    xpass_count = 0
    known_failure_count = 0
    unexpected_failure_count = 0

    for case in TEST_CASES:
        cid = case["id"]
        desc = case["description"]
        source = case["source"]
        kind = case["kind"]
        print(f"\n[{cid}] {desc}")
        print(f"  Source: {source.relative_to(ROOT)}")

        # 1. Check
        check = run([str(COMPILER), "check", str(source), "--json"])
        checker_passed = check.returncode == 0
        if case["expected_checker"] == "PASS":
            if not checker_passed:
                print(f"  [UNEXPECTED FAILURE] Checker rejected unexpectedly: {check.stderr.strip()[:200]}")
                unexpected_failure_count += 1
                continue
            print("  Checker: PASS (accepted as expected)")
        else:
            expected_err = case.get("expected_checker_error", "")
            if checker_passed:
                print("  [UNEXPECTED FAILURE] Checker accepted but was expected to fail")
                unexpected_failure_count += 1
                continue
            if expected_err and expected_err not in check.stderr and expected_err not in check.stdout:
                print(f"  [UNEXPECTED FAILURE] Checker failed without expected marker '{expected_err}': {check.stderr.strip()[:200]}")
                unexpected_failure_count += 1
                continue
            print(f"  Checker: KNOWN FAILURE (rejected with expected marker: {expected_err})")
            print(f"  Note: {case['defect_note']}")
            known_failure_count += 1
            continue

        # 2. Fast Debug
        if case["expected_fast_debug"] != "SKIP":
            interp = run([str(COMPILER), "run", "--interp", str(source)])
            if interp.returncode != 0:
                print(f"  [UNEXPECTED FAILURE] Fast Debug crashed: {interp.stderr.strip()[:200]}")
                unexpected_failure_count += 1
                continue
            interp_out = interp.stdout.strip()
            if interp_out != case["expected_fast_debug"]:
                print(f"  [UNEXPECTED FAILURE] Fast Debug output mismatch: expected '{case['expected_fast_debug']}', got '{interp_out}'")
                unexpected_failure_count += 1
                continue
            print(f"  Fast Debug: PASS (output: '{interp_out}')")

        # 3. Native emit
        rs_path = TMP / f"{cid}.rs"
        bin_path = TMP / cid
        emit = run([str(COMPILER), str(source), "-o", str(rs_path)])
        if emit.returncode != 0:
            print(f"  [UNEXPECTED FAILURE] Native emit crashed: {emit.stderr.strip()[:200]}")
            unexpected_failure_count += 1
            continue
        print("  Native emit: PASS")

        # 4. rustc compile
        rustc = run(["rustc", "--edition=2021", str(rs_path), "-o", str(bin_path)])
        rustc_passed = rustc.returncode == 0

        if case["expected_rustc"] == "PASS":
            if not rustc_passed:
                print(f"  [UNEXPECTED FAILURE] Rustc compilation failed: {rustc.stderr.strip()[:200]}")
                unexpected_failure_count += 1
                continue
            # Run binary and verify parity
            run_bin = run([str(bin_path)])
            if run_bin.returncode != 0:
                print(f"  [UNEXPECTED FAILURE] Native binary crashed: {run_bin.stderr.strip()[:200]}")
                unexpected_failure_count += 1
                continue
            native_out = run_bin.stdout.strip()
            if native_out != case["expected_native"]:
                print(f"  [UNEXPECTED FAILURE] Native output mismatch: expected '{case['expected_native']}', got '{native_out}'")
                unexpected_failure_count += 1
                continue
            print(f"  Rustc compile: PASS")
            print(f"  Native binary output: '{native_out}' (parity verified)")
            print(f"  Status: PASS ({case['defect_note']})")
            pass_count += 1

        elif case["expected_rustc"] == "FAIL":
            expected_err = case.get("expected_rustc_error", "")
            if rustc_passed:
                # XPASS: known failure fixture compiled successfully. Verify execution and output parity.
                run_bin = run([str(bin_path)])
                if run_bin.returncode != 0:
                    print(f"  [UNEXPECTED FAILURE] Rustc succeeded on known failure fixture, but binary crashed: {run_bin.stderr.strip()[:200]}")
                    unexpected_failure_count += 1
                    continue
                native_out = run_bin.stdout.strip()
                target_expected = case.get("expected_native") or case.get("expected_fast_debug")
                if target_expected and native_out != target_expected:
                    print(f"  [UNEXPECTED FAILURE] Rustc succeeded on known failure fixture, but native output mismatch: expected '{target_expected}', got '{native_out}'")
                    unexpected_failure_count += 1
                    continue
                print(f"  Rustc compile: XPASS (repaired!)")
                print(f"  Native binary output: '{native_out}' (matches Fast Debug parity)")
                print(f"  Status: XPASS (repaired known failure with verified parity: {case['defect_note']})")
                xpass_count += 1
                continue
            if expected_err and expected_err not in rustc.stderr:
                print(f"  [UNEXPECTED FAILURE] Rustc failed without expected error '{expected_err}': {rustc.stderr.strip()[:200]}")
                unexpected_failure_count += 1
                continue
            print(f"  Rustc compile: KNOWN FAILURE (failed with expected error: {expected_err})")
            print(f"  Status: KNOWN FAILURE ({case['defect_note']})")
            known_failure_count += 1

    print("\n=== Summary ===")
    print(f"Total: {total_count} | PASS: {pass_count} | XPASS: {xpass_count} | KNOWN FAILURES: {known_failure_count} | UNEXPECTED FAILURES: {unexpected_failure_count}")

    if unexpected_failure_count > 0:
        print("\nGATE RESULT: FAILED (regressions or unexpected outcomes detected)")
        sys.exit(1)
    else:
        print("\nGATE RESULT: PASSED (all expected passes and known failures confirmed)")
        sys.exit(0)


if __name__ == "__main__":
    main()
