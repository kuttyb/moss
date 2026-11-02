#!/usr/bin/env python3
"""Run Phase 28 E1-E4 defect reproducers across Fast Debug and native execution."""
import os
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]
COMPILER = ROOT / "moss"
TMP = ROOT / "tmp/phase28_defects"
TMP.mkdir(parents=True, exist_ok=True)

REPRODUCERS = [
    ("E1_group_a_push_self_field", ROOT / "tests/phase28/reproducers/defect_e1_group_a_push_self_field.moss"),
    ("E1_group_a_field_self_assign", ROOT / "tests/phase28/reproducers/defect_e1_group_a_field_self_assign.moss"),
    ("E2_swarm_035_nested_mutating_calls", ROOT / "tests/phase28/reproducers/defect_e2_swarm_035_nested_mutating_calls.moss"),
    ("E3_t6_untyped_for_read", ROOT / "tests/phase28/reproducers/defect_e3_t6_untyped_for_read.moss"),
    ("E4_mutate_loop_element", ROOT / "tests/phase28/reproducers/defect_e4_mutate_loop_element.moss"),
]

def run(cmd):
    return subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, cwd=ROOT)

def main():
    print("=== Phase 28 Defect Census and Baseline Matrix ===")
    results = {}
    for name, path in REPRODUCERS:
        print(f"\nEvaluating {name} ({path.name}):")
        # 1. Checker check
        check = run([str(COMPILER), "check", str(path), "--json"])
        checker_ok = (check.returncode == 0)
        print(f"  Checker: {'PASS' if checker_ok else 'FAIL'}")
        if not checker_ok:
            print(f"    stderr: {check.stderr.strip()[:200]}")

        # 2. Fast Debug
        interp = run([str(COMPILER), "run", "--interp", str(path)])
        interp_ok = (interp.returncode == 0)
        interp_out = interp.stdout.strip()
        print(f"  Fast Debug: {'PASS (' + interp_out + ')' if interp_ok else 'FAIL (' + interp.stderr.strip()[:100] + ')'}")

        # 3. Native emit
        rs_path = TMP / f"{name}.rs"
        bin_path = TMP / name
        emit = run([str(COMPILER), str(path), "-o", str(rs_path)])
        emit_ok = (emit.returncode == 0)

        # 4. rustc compile
        rustc_ok = False
        rustc_err = ""
        bin_out = ""
        if emit_ok:
            rustc = run(["rustc", "--edition=2021", str(rs_path), "-o", str(bin_path)])
            rustc_ok = (rustc.returncode == 0)
            if rustc_ok:
                run_bin = run([str(bin_path)])
                bin_out = run_bin.stdout.strip()
            else:
                for line in rustc.stderr.splitlines():
                    if "error[" in line or "error:" in line:
                        rustc_err = line.strip()
                        break
        print(f"  Native emit: {'PASS' if emit_ok else 'FAIL'}")
        print(f"  Rustc compile: {'PASS' if rustc_ok else 'FAIL (' + rustc_err + ')'}")
        if rustc_ok:
            print(f"  Native binary output: {bin_out}")
            parity = (interp_out == bin_out)
            print(f"  Parity (Fast Debug == Native): {parity}")
        else:
            print(f"  Parity: BLOCKED (rustc rejected)")

        results[name] = {
            "checker_ok": checker_ok,
            "fast_debug_ok": interp_ok,
            "fast_debug_out": interp_out,
            "emit_ok": emit_ok,
            "rustc_ok": rustc_ok,
            "rustc_err": rustc_err,
            "native_out": bin_out,
        }

    return results

if __name__ == "__main__":
    main()
