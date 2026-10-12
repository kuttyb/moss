#!/usr/bin/env python3
"""
test_math_falsification.py
===========================
Independent Adversarial Numerical Oracle & Falsification Harness for moss.math
Phase 24 Team M — L7 Adversarial Review Verification (Commit 129a81f)

Author: Independent Adversarial Numerical Reviewer
Reference: IEEE-754-2019 Standard for Floating-Point Arithmetic
"""

import math
import os
import re
import subprocess
import sys
from pathlib import Path

# Paths
REPO_ROOT = Path("/home/owner/daji/code/moss2").resolve()
WORKTREE_ROOT = REPO_ROOT
MARGO_BIN = REPO_ROOT / "margo"
MOSS_BIN = REPO_ROOT / "moss"
HARNESS_DIR = Path(__file__).resolve().parent / "harness"


def compute_ulp_error(actual: float, expected: float) -> float:
    """Compute error in units in the last place (ULPs)."""
    if math.isnan(actual) and math.isnan(expected):
        return 0.0
    if math.isinf(actual) and math.isinf(expected):
        return 0.0 if (actual > 0) == (expected > 0) else float("inf")
    if math.isnan(actual) or math.isnan(expected) or math.isinf(actual) or math.isinf(expected):
        return float("inf")
    diff = abs(actual - expected)
    if diff == 0.0:
        return 0.0
    # math.ulp(expected): if expected is 0, ulp is minimum subnormal (5e-324)
    ulp_val = math.ulp(expected) if expected != 0.0 else sys.float_info.min * (2 ** -52)
    return diff / ulp_val


def run_moss_probe(moss_code: str) -> dict[str, str]:
    """Compile and execute a Moss probe in the oracle harness package."""
    main_moss_path = HARNESS_DIR / "src" / "main.moss"
    main_moss_path.parent.mkdir(parents=True, exist_ok=True)
    with open(main_moss_path, "w", encoding="utf-8") as f:
        f.write(moss_code)

    res = subprocess.run(
        [str(MARGO_BIN), "run"],
        cwd=HARNESS_DIR,
        capture_output=True,
        text=True,
        timeout=30,
    )
    if res.returncode != 0:
        raise RuntimeError(f"Moss build/run failed:\nSTDOUT:\n{res.stdout}\nSTDERR:\n{res.stderr}")

    outputs = {}
    for line in res.stdout.splitlines():
        line = line.strip()
        if line.startswith("TAG:"):
            parts = line.split(":", 2)
            if len(parts) >= 3:
                tag = parts[1]
                val = parts[2].strip()
                outputs[tag] = val
    return outputs


def test_math():
    print("=" * 80)
    print("MOSS.MATH ADVERSARIAL FALSIFICATION SUITE")
    print("=" * 80)

    # 1. ATAN FALSIFICATION
    atan_cases = [
        # Counterexamples flagged by L7
        ("atan_1_0", 1.0),
        ("atan_1_5", 1.5),
        ("atan_2_0", 2.0),
        ("atan_3_0", 3.0),
        ("atan_neg_2_0", -2.0),
        # Negative values
        ("atan_neg_0_5", -0.5),
        ("atan_neg_1_0", -1.0),
        ("atan_neg_1_5", -1.5),
        ("atan_neg_3_0", -3.0),
        ("atan_neg_10_0", -10.0),
        ("atan_neg_1e8", -1e8),
        # Zero
        ("atan_pos_zero", 0.0),
        ("atan_neg_zero", -0.0),
        # Near 1
        ("atan_near_1_below", 0.999999),
        ("atan_near_1_above", 1.000001),
        ("atan_near_1_next", 1.0 + 2**-52),
        # Small magnitudes
        ("atan_small_1e10", 1e-10),
        ("atan_small_1e50", 1e-50),
        ("atan_small_1e100", 1e-100),
        ("atan_small_1e300", 1e-300),
        # Large magnitudes
        ("atan_large_1e4", 1e4),
        ("atan_large_1e8", 1e8),
        ("atan_large_1e50", 1e50),
        ("atan_large_1e100", 1e100),
        # Infinities
        ("atan_inf", float("inf")),
        ("atan_neg_inf", float("-inf")),
    ]

    # 2. EXP FALSIFICATION
    exp_cases = [
        ("exp_m750", -750.0),
        ("exp_m740", -740.0),
        ("exp_m720", -720.0),
        ("exp_m710", -710.0),  # Subnormal counterexample
        ("exp_m708_3964", -708.3964),
        ("exp_m700", -700.0),
        ("exp_m100", -100.0),
        ("exp_m10", -10.0),
        ("exp_m1", -1.0),
        ("exp_0", 0.0),
        ("exp_1", 1.0),
        ("exp_2", 2.0),
        ("exp_10", 10.0),
        ("exp_100", 100.0),
        ("exp_700", 700.0),
        ("exp_708", 708.0),
        ("exp_709", 709.0),
        ("exp_709_5", 709.5),  # Finite counterexample
        ("exp_710", 710.0),
        ("exp_715", 715.0),
    ]

    # 3. OTHER TRANSCENDENTALS & OPERATIONS
    other_cases = [
        # sin, cos, tan
        ("sin_0", "math.sin(0.0)", math.sin(0.0)),
        ("sin_half_pi", "math.sin(1.5707963267948966)", math.sin(1.5707963267948966)),
        ("sin_pi", "math.sin(3.141592653589793)", math.sin(3.141592653589793)),
        ("sin_10", "math.sin(10.0)", math.sin(10.0)),
        ("sin_1e6", "math.sin(1000000.0)", math.sin(1000000.0)),
        ("sin_1e8", "math.sin(100000000.0)", math.sin(100000000.0)),
        ("cos_0", "math.cos(0.0)", math.cos(0.0)),
        ("cos_half_pi", "math.cos(1.5707963267948966)", math.cos(1.5707963267948966)),
        ("cos_10", "math.cos(10.0)", math.cos(10.0)),
        ("tan_0", "math.tan(0.0)", math.tan(0.0)),
        ("tan_1", "math.tan(1.0)", math.tan(1.0)),
        # asin, acos, atan2
        ("asin_0_5", "math.asin(0.5)", math.asin(0.5)),
        ("asin_0_9", "math.asin(0.9)", math.asin(0.9)),
        ("asin_1_0", "math.asin(1.0)", math.asin(1.0)),
        ("acos_0_5", "math.acos(0.5)", math.acos(0.5)),
        ("acos_0_9", "math.acos(0.9)", math.acos(0.9)),
        ("atan2_2_1", "math.atan2(2.0, 1.0)", math.atan2(2.0, 1.0)),
        ("atan2_m2_1", "math.atan2(-2.0, 1.0)", math.atan2(-2.0, 1.0)),
        # ln, log2, log10
        ("ln_1", "math.ln(1.0)", 0.0),
        ("ln_2", "math.ln(2.0)", math.log(2.0)),
        ("ln_e", "math.ln(2.718281828459045)", 1.0),
        ("ln_10", "math.ln(10.0)", math.log(10.0)),
        ("log2_8", "math.log2(8.0)", 3.0),
        ("log10_100", "math.log10(100.0)", 2.0),
        # pow, pow_int
        ("pow_2_3", "math.pow(2.0, 3.0)", 8.0),
        ("pow_m2_3", "math.pow(-2.0, 3.0)", -8.0),
        ("pow_nan_0", "math.pow(math.nan(), 0.0)", 1.0),
        ("pow_1_nan", "math.pow(1.0, math.nan())", 1.0),
        ("pow_int_2_10", "math.pow_int(2, 10)", 1024),
        ("pow_int_m2_3", "math.pow_int(-2, 3)", -8),
        # sinh, cosh, tanh
        ("sinh_0", "math.sinh(0.0)", 0.0),
        ("sinh_1e8", "math.sinh(0.00000001)", math.sinh(1e-8)),
        ("sinh_1", "math.sinh(1.0)", math.sinh(1.0)),
        ("cosh_0", "math.cosh(0.0)", 1.0),
        ("cosh_1", "math.cosh(1.0)", math.cosh(1.0)),
        ("tanh_0", "math.tanh(0.0)", 0.0),
        ("tanh_1", "math.tanh(1.0)", math.tanh(1.0)),
        ("tanh_20", "math.tanh(20.0)", math.tanh(20.0)),
        ("tanh_1000", "math.tanh(1000.0)", 1.0),
        # hypot
        ("hypot_3_4", "math.hypot(3.0, 4.0)", 5.0),
        ("hypot_large", "math.hypot(3e200, 4e200) / 1e200", 5.0),
        # rounding
        ("round_2_5", "math.round(2.5)", 2.0),
        ("round_3_5", "math.round(3.5)", 4.0),
        ("round_m2_5", "math.round(-2.5)", -2.0),
        ("round_m3_5", "math.round(-3.5)", -4.0),
        # signed zero & nan
        ("abs_neg_zero", "1.0 / math.abs(0.0 - 0.0) > 0.0", True),
        ("min_neg_zero", "1.0 / math.min(0.0 - 0.0, 0.0) < 0.0", True),
        ("max_neg_zero", "1.0 / math.max(0.0 - 0.0, 0.0) > 0.0", True),
        ("min_nan_val", "math.min(math.nan(), 5.0)", 5.0),
        ("max_nan_val", "math.max(math.nan(), 5.0)", 5.0),
    ]

    # Generate Moss probe code
    moss_lines = [
        "module oracle_harness",
        "import math",
        "",
        "fn main():",
    ]

    for tag, x in atan_cases:
        if math.isinf(x):
            expr = "math.infinity()" if x > 0 else "math.neg_infinity()"
        elif math.isnan(x):
            expr = "math.nan()"
        elif x == 0.0 and math.copysign(1.0, x) < 0:
            expr = "0.0 - 0.0"
        else:
            expr = f"{x}"
        moss_lines.append(f'  echo "TAG:{tag}:", math.atan({expr})')

    for tag, x in exp_cases:
        moss_lines.append(f'  echo "TAG:{tag}:", math.exp({x})')

    for tag, expr, _ in other_cases:
        moss_lines.append(f'  echo "TAG:{tag}:", {expr}')

    moss_code = "\n".join(moss_lines) + "\n"

    # Run the probe
    print("Compiling and executing Moss probe via margo run...")
    outputs = run_moss_probe(moss_code)
    print(f"Received {len(outputs)} probe outputs.\n")

    # Evaluate results
    falsifications = []
    passes = []
    max_abs_err = 0.0
    max_rel_err = 0.0
    max_ulp_err = 0.0

    print("-" * 80)
    print("1. ATAN(X) ADVERSARIAL EVALUATION:")
    print("-" * 80)
    for tag, x in atan_cases:
        expected = math.atan(x)
        raw_val = outputs.get(tag, "MISSING")
        try:
            actual = float(raw_val)
        except ValueError:
            actual = float("nan")

        abs_err = abs(actual - expected) if not (math.isnan(actual) and math.isnan(expected)) else 0.0
        rel_err = (abs_err / abs(expected)) if expected != 0.0 else abs_err
        ulp_err = compute_ulp_error(actual, expected)

        if not math.isinf(abs_err):
            max_abs_err = max(max_abs_err, abs_err)
        if not math.isinf(rel_err):
            max_rel_err = max(max_rel_err, rel_err)
        if not math.isinf(ulp_err):
            max_ulp_err = max(max_ulp_err, ulp_err)

        # Threshold for acceptable approximation: 100 ULPs (~2e-14 relative)
        status = "PASS" if ulp_err <= 100.0 else "FALSIFIED"
        if status == "FALSIFIED":
            falsifications.append({
                "test": f"atan({x})",
                "tag": tag,
                "input": x,
                "expected": expected,
                "actual": actual,
                "abs_err": abs_err,
                "rel_err": rel_err,
                "ulp_err": ulp_err,
                "reason": f"Expected {expected}, got {actual} (AbsErr: {abs_err:.6e}, RelErr: {rel_err:.2%}, ULP: {ulp_err:.1e})",
            })
            print(f"  [X] {tag:<20} x={x:<10} => actual: {actual:<20} expected: {expected:<20} RelErr: {rel_err:.2%} ({status})")
        else:
            passes.append(f"atan({x})")
            print(f"  [+] {tag:<20} x={x:<10} => actual: {actual:<20} expected: {expected:<20} ULP: {ulp_err:.1f} ({status})")

    print("\n" + "-" * 80)
    print("2. EXP(X) ADVERSARIAL EVALUATION:")
    print("-" * 80)
    for tag, x in exp_cases:
        try:
            expected = math.exp(x)
        except OverflowError:
            expected = float("inf")

        raw_val = outputs.get(tag, "MISSING")
        try:
            actual = float(raw_val)
        except ValueError:
            actual = float("nan")

        abs_err = abs(actual - expected) if not (math.isnan(actual) and math.isnan(expected)) else 0.0
        rel_err = (abs_err / abs(expected)) if expected != 0.0 else (0.0 if actual == 0.0 else float("inf"))
        ulp_err = compute_ulp_error(actual, expected)

        # Allow 100 ULPs; if expected is 0 or actual is 0 when subnormal expected:
        status = "PASS" if (ulp_err <= 100.0 or (actual == expected)) else "FALSIFIED"
        if status == "FALSIFIED":
            falsifications.append({
                "test": f"exp({x})",
                "tag": tag,
                "input": x,
                "expected": expected,
                "actual": actual,
                "abs_err": abs_err,
                "rel_err": rel_err,
                "ulp_err": ulp_err,
                "reason": f"Expected {expected}, got {actual} (AbsErr: {abs_err:.6e}, RelErr: {rel_err:.2%}, ULP: {ulp_err:.1e})",
            })
            print(f"  [X] {tag:<20} x={x:<10} => actual: {actual:<20} expected: {expected:<20} RelErr: {rel_err:.2%} ({status})")
        else:
            passes.append(f"exp({x})")
            print(f"  [+] {tag:<20} x={x:<10} => actual: {actual:<20} expected: {expected:<20} ULP: {ulp_err:.1f} ({status})")

    print("\n" + "-" * 80)
    print("3. OTHER TRANSCENDENTALS & ARITHMETIC EVALUATION:")
    print("-" * 80)
    for tag, expr, expected in other_cases:
        raw_val = outputs.get(tag, "MISSING")
        if isinstance(expected, bool):
            actual = (raw_val.lower() == "true")
            status = "PASS" if actual == expected else "FALSIFIED"
            abs_err, rel_err, ulp_err = (0.0, 0.0, 0.0) if status == "PASS" else (1.0, 1.0, float("inf"))
        else:
            try:
                actual = float(raw_val)
            except ValueError:
                actual = float("nan")
            abs_err = abs(actual - expected) if not (math.isnan(actual) and math.isnan(expected)) else 0.0
            rel_err = (abs_err / abs(expected)) if expected != 0.0 else abs_err
            ulp_err = compute_ulp_error(actual, expected)
            status = "PASS" if (ulp_err <= 1000.0 or abs_err <= 1e-9) else "FALSIFIED"

        if status == "FALSIFIED":
            falsifications.append({
                "test": tag,
                "tag": tag,
                "input": expr,
                "expected": expected,
                "actual": actual,
                "abs_err": abs_err,
                "rel_err": rel_err,
                "ulp_err": ulp_err,
                "reason": f"{expr}: Expected {expected}, got {actual} (AbsErr: {abs_err:.6e}, RelErr: {rel_err:.2%})",
            })
            print(f"  [X] {tag:<20} => actual: {actual:<20} expected: {expected:<20} ({status})")
        else:
            passes.append(tag)
            print(f"  [+] {tag:<20} => actual: {actual:<20} expected: {expected:<20} ({status})")

    print("\n" + "=" * 80)
    print("MOSS.MATH FALSIFICATION SUMMARY:")
    print(f"Total Tests Run:    {len(atan_cases) + len(exp_cases) + len(other_cases)}")
    print(f"Passed Tests:       {len(passes)}")
    print(f"FALSIFIED Tests:    {len(falsifications)}")
    print(f"Max Absolute Error: {max_abs_err:.6e}")
    print(f"Max Relative Error: {max_rel_err:.2%}")
    print(f"Max Finite ULP Err: {max_ulp_err:.1e}")
    print("=" * 80)
    return falsifications


if __name__ == "__main__":
    fails = test_math()
    if fails:
        print(f"\n[FALSIFICATION CONFIRMED] {len(fails)} claims FALSIFIED in moss.math!")
        sys.exit(1)
    else:
        print("\n[ALL CLAIMS VERIFIED] No falsifications detected.")
        sys.exit(0)
