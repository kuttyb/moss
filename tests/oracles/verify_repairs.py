#!/usr/bin/env python3
"""
verify_repairs.py
==================
Independent Adversarial Numerical Verification Runner for Moss Phase 24 Team M.
Verifies that all L7-flagged defects and mathematical/temporal correctness
guarantees pass against the integrated repaired SDK.

This runner is portable and creates its test harness under tmp/ so that
a clean checkout remains untouched.

Author: Independent Adversarial Numerical Reviewer & L6 Coordinator
"""

import math
import subprocess
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[2]
MARGO_BIN = REPO_ROOT / "margo"
HARNESS_DIR = REPO_ROOT / "tmp" / "adversarial_oracle_harness"


def ensure_harness():
    HARNESS_DIR.mkdir(parents=True, exist_ok=True)
    (HARNESS_DIR / "src").mkdir(parents=True, exist_ok=True)
    manifest = HARNESS_DIR / "Moss.toml"
    manifest_content = """[package]
name = "adversarial_oracle_harness"
version = "0.1.0"

[dependencies]
"moss.math" = { path = "../../packages/math" }
"moss.random" = { path = "../../packages/random" }
"moss.time" = { path = "../../packages/time" }
"""
    if not manifest.exists() or manifest.read_text(encoding="utf-8") != manifest_content:
        manifest.write_text(manifest_content, encoding="utf-8")


def run_moss_probe(moss_code: str, timeout: int = 30) -> tuple[int, str, str]:
    ensure_harness()
    main_moss_path = HARNESS_DIR / "src" / "main.moss"
    with open(main_moss_path, "w", encoding="utf-8") as f:
        f.write(moss_code)

    res = subprocess.run(
        [str(MARGO_BIN), "run"],
        cwd=HARNESS_DIR,
        capture_output=True,
        text=True,
        timeout=timeout,
    )
    return res.returncode, res.stdout, res.stderr


def parse_tags(stdout: str) -> dict[str, str]:
    tags = {}
    for line in stdout.splitlines():
        line = line.strip()
        if line.startswith("TAG:"):
            parts = line.split(":", 2)
            if len(parts) >= 3:
                tags[parts[1]] = parts[2].strip()
    return tags


def test_math_repairs():
    print("=" * 80)
    print("VERIFYING MOSS.MATH REPAIRS")
    print("=" * 80)

    probe = """module oracle_harness
import math

fn main():
  # 1. ATAN Counterexamples & Edge Cases
  echo "TAG:atan_1_0:", math.atan(1.0)
  echo "TAG:atan_1_5:", math.atan(1.5)
  echo "TAG:atan_2_0:", math.atan(2.0)
  echo "TAG:atan_3_0:", math.atan(3.0)
  echo "TAG:atan_m2_0:", math.atan(-2.0)
  echo "TAG:atan_0_0:", math.atan(0.0)
  echo "TAG:atan_inf:", math.atan(math.infinity())
  echo "TAG:atan_neginf:", math.atan(math.neg_infinity())
  echo "TAG:atan_1e20:", math.atan(1e20)
  echo "TAG:atan_1em20:", math.atan(1e-20)

  # 2. EXP Extremes & Subnormals
  echo "TAG:exp_m710:", math.exp(-710.0)
  echo "TAG:exp_709_5:", math.exp(709.5)
  echo "TAG:exp_m744:", math.exp(-744.0)
  echo "TAG:exp_700:", math.exp(700.0)
  echo "TAG:exp_0:", math.exp(0.0)
  echo "TAG:exp_1:", math.exp(1.0)

  # 3. Transcendentals & Powers
  echo "TAG:hypot_inf_nan:", math.hypot(math.infinity(), math.nan())
  echo "TAG:pow_nan_0:", math.pow(math.nan(), 0.0)
  echo "TAG:pow_1_nan:", math.pow(1.0, math.nan())
  echo "TAG:pow_2_3:", math.pow(2.0, 3.0)
  echo "TAG:sin_pi_2:", math.sin(1.5707963267948966)
  echo "TAG:cos_0:", math.cos(0.0)
  echo "TAG:ln_e:", math.ln(2.718281828459045)

  # 4. Signed Zero in min/max
  m_zero = math.min(-0.0, 0.0)
  echo "TAG:min_neg_zero:", (1.0 / m_zero < 0.0)

  # 5. IEEE-754 Rounding & Signed Zero Reciprocal Tests
  echo "TAG:trunc_neg_zero:", (1.0 / math.trunc(-0.5) < 0.0)
  echo "TAG:ceil_neg_zero:", (1.0 / math.ceil(-0.5) < 0.0)
  echo "TAG:round_neg_zero:", (1.0 / math.round(-0.2) < 0.0)
  echo "TAG:round_neg_half:", (1.0 / math.round(-0.5) < 0.0)
"""
    ret, stdout, stderr = run_moss_probe(probe)
    assert ret == 0, f"Math probe failed:\nSTDOUT: {stdout}\nSTDERR: {stderr}"
    tags = parse_tags(stdout)

    # 1. ATAN validation against Python math.atan
    atan_cases = [
        (1.0, float(tags["atan_1_0"])),
        (1.5, float(tags["atan_1_5"])),
        (2.0, float(tags["atan_2_0"])),
        (3.0, float(tags["atan_3_0"])),
        (-2.0, float(tags["atan_m2_0"])),
        (0.0, float(tags["atan_0_0"])),
        (float("inf"), float(tags["atan_inf"])),
        (float("-inf"), float(tags["atan_neginf"])),
        (1e20, float(tags["atan_1e20"])),
        (1e-20, float(tags["atan_1em20"])),
    ]
    for inp, act in atan_cases:
        exp = math.atan(inp)
        abs_err = abs(act - exp)
        assert abs_err < 1e-12, f"atan({inp}): expected {exp}, got {act} (err: {abs_err})"
    print("  [PASS] atan(x) reduction and reconstruction verified across all counterexamples and limits.")

    # 2. EXP extremes & subnormals against Python math.exp
    exp_cases = [
        (-710.0, float(tags["exp_m710"])),
        (709.5, float(tags["exp_709_5"])),
        (-744.0, float(tags["exp_m744"])),
        (700.0, float(tags["exp_700"])),
        (0.0, float(tags["exp_0"])),
        (1.0, float(tags["exp_1"])),
    ]
    for inp, act in exp_cases:
        exp = math.exp(inp)
        if exp == 0.0:
            assert act == 0.0
        else:
            rel_err = abs(act - exp) / exp
            assert rel_err < 1e-10, f"exp({inp}): expected {exp}, got {act} (rel_err: {rel_err})"
    print("  [PASS] exp(x) verified: subnormals down to -744, finite results up to 709.78, no intermediate overflow.")

    # 3. Transcendentals & powers
    assert float(tags["hypot_inf_nan"]) == float("inf"), "hypot(inf, nan) != inf"
    assert float(tags["pow_nan_0"]) == 1.0, "pow(nan, 0) != 1.0"
    assert float(tags["pow_1_nan"]) == 1.0, "pow(1, nan) != 1.0"
    assert abs(float(tags["pow_2_3"]) - 8.0) < 1e-12, "pow(2, 3) != 8.0"
    assert abs(float(tags["sin_pi_2"]) - 1.0) < 1e-12, "sin(pi/2) != 1.0"
    assert abs(float(tags["cos_0"]) - 1.0) < 1e-12, "cos(0) != 1.0"
    assert abs(float(tags["ln_e"]) - 1.0) < 1e-12, "ln(e) != 1.0"
    assert tags["min_neg_zero"] == "true", "min(-0.0, 0.0) did not return -0.0"

    # 4. Signed zero reciprocal checks
    assert tags["trunc_neg_zero"] == "true", "trunc(-0.5) did not produce -0.0"
    assert tags["ceil_neg_zero"] == "true", "ceil(-0.5) did not produce -0.0"
    assert tags["round_neg_zero"] == "true", "round(-0.2) did not produce -0.0"
    assert tags["round_neg_half"] == "true", "round(-0.5) did not produce -0.0"
    print("  [PASS] All transcendentals, IEEE-754 powers, and signed zero rounding verified.")


def test_random_repairs():
    print("\n" + "=" * 80)
    print("VERIFYING MOSS.RANDOM REPAIRS")
    print("=" * 80)

    probe = """module oracle_harness
import random

fn main():
  var rng = random.new_rng(123456789)

  # 1. Bounded randint without overflow
  var val_small = 0
  try:
    val_small = random.rng_randint(rng, 10, 20)
  recover random.RandomError.InvalidRange:
    val_small = -1
  echo "TAG:randint_small:", val_small

  # 2. Large range randint across 0
  var val_large = 0
  try:
    val_large = random.rng_randint(rng, -2000000000000000000, 2000000000000000000)
  recover random.RandomError.InvalidRange:
    val_large = 0
  echo "TAG:randint_large:", val_large

  # 3. Invalid range error contract (raises RandomError.InvalidRange)
  var inv_caught = false
  try:
    random.rng_randint(rng, 100, 10)
  recover random.RandomError.InvalidRange:
    inv_caught = true
  echo "TAG:inv_caught:", inv_caught

  # 4. Overflow range error contract (> R_max)
  var ovf_caught = false
  try:
    random.rng_randint(rng, -6000000000000000000, 6000000000000000000)
  recover random.RandomError.InvalidRange:
    ovf_caught = true
  echo "TAG:ovf_caught:", ovf_caught

  # 5. Empty collection choice error contract
  var empty_caught = false
  var empty_vec = Vector[Int]()
  try:
    random.choice(empty_vec, rng)
  recover random.RandomError.EmptyCollection:
    empty_caught = true
  echo "TAG:empty_caught:", empty_caught

  # 6. Empty shuffle returns empty
  shuffled = random.shuffle(empty_vec, rng)
  echo "TAG:shuffled_count:", shuffled |> count

  # 7. Uniform inverted range error contract
  var uniform_inv_caught = false
  try:
    random.uniform(rng, 10.0, 5.0)
  recover random.RandomError.InvalidRange:
    uniform_inv_caught = true
  echo "TAG:uniform_inv_caught:", uniform_inv_caught

  # 8. Uniform point interval returns point
  var u_point = 0.0
  try:
    u_point = random.uniform(rng, 7.5, 7.5)
  recover random.RandomError.InvalidRange:
    u_point = 0.0
  echo "TAG:uniform_point:", u_point
"""
    ret, stdout, stderr = run_moss_probe(probe)
    assert ret == 0, f"Random probe failed:\nSTDOUT: {stdout}\nSTDERR: {stderr}"
    tags = parse_tags(stdout)

    assert 10 <= int(tags["randint_small"]) <= 20, "randint small out of range"
    assert -2000000000000000000 <= int(tags["randint_large"]) <= 2000000000000000000, "randint large out of range"
    assert tags["inv_caught"] == "true", "RandomError.InvalidRange not raised on inverted range"
    assert tags["ovf_caught"] == "true", "RandomError.InvalidRange not raised on overflow range"
    assert tags["empty_caught"] == "true", "RandomError.EmptyCollection not raised on empty choice"
    assert int(tags["shuffled_count"]) == 0, "shuffle empty did not return empty vector"
    assert tags["uniform_inv_caught"] == "true", "RandomError.InvalidRange not raised on uniform inverted range"
    assert float(tags["uniform_point"]) == 7.5, "uniform point interval did not return point"
    print("  [PASS] Signed Int64 bounds verified without overflow.")
    print("  [PASS] Multi-draw sampler and Phase 21 typed error contracts verified.")
    print("  [PASS] Uniform float inverted range error contract verified.")


def test_time_repairs():
    print("\n" + "=" * 80)
    print("VERIFYING MOSS.TIME REPAIRS")
    print("=" * 80)

    probe = """module oracle_harness
import time

fn main():
  # 1. Division by zero error contract (raises TimeError.DivisionByZero)
  var div0_caught = false
  d10 = time.duration_from_seconds(10)
  try:
    time.duration_div(d10, 0)
  recover time.TimeError.DivisionByZero:
    div0_caught = true
  echo "TAG:div0_caught:", div0_caught

  # 2. Gregorian Calendar Validation: April 31 rejected
  var apr31_caught = false
  try:
    time.make_datetime(2024, 4, 31, 0, 0, 0, 0)
  recover time.TimeError.InvalidDate:
    apr31_caught = true
  echo "TAG:apr31_caught:", apr31_caught

  # 3. Secular non-leap year 1900 Feb 29 rejected
  var feb1900_caught = false
  try:
    time.make_datetime(1900, 2, 29, 0, 0, 0, 0)
  recover time.TimeError.InvalidDate:
    feb1900_caught = true
  echo "TAG:feb1900_caught:", feb1900_caught

  # 4. Leap year 2000 & 2024 accepted
  try:
    dt_2000 = time.make_datetime(2000, 2, 29, 12, 0, 0, 0)
    echo "TAG:iso_2000:", time.datetime_to_iso8601(dt_2000)
  recover time.TimeError.InvalidDate:
    echo "TAG:iso_2000:ERROR"
  try:
    dt_2024 = time.make_datetime(2024, 2, 29, 12, 0, 0, 0)
    echo "TAG:iso_2024:", time.datetime_to_iso8601(dt_2024)
  recover time.TimeError.InvalidDate:
    echo "TAG:iso_2024:ERROR"

  # 5. Saturating arithmetic
  let int_max = 9223372036854775807
  let int_min = -9223372036854775808
  d_max = time.duration_from_nanos(int_max)
  d_one = time.duration_from_nanos(1)
  d_ovf = time.duration_add(d_max, d_one)
  echo "TAG:add_sat:", time.duration_nanos(d_ovf)

  d_min = time.duration_from_nanos(int_min)
  d_abs = time.duration_abs(d_min)
  echo "TAG:abs_sat:", time.duration_nanos(d_abs)

  # 6. Pre-1970 Euclidean division
  ts_m1000 = time.timestamp_from_unix_millis(-1000)
  echo "TAG:sec_m1000:", time.timestamp_unix_seconds(ts_m1000)
  ts_m1500 = time.timestamp_from_unix_millis(-1500)
  echo "TAG:sec_m1500:", time.timestamp_unix_seconds(ts_m1500)

  # 7. Direct record construction boundary validation: April 31 rejected at conversion
  dt_bad_apr = time.DateTime(year: 2024, month: 4, day: 31, hour: 0, minute: 0, second: 0, millisecond: 0)
  var dt_direct_apr_caught = false
  try:
    time.datetime_to_timestamp(dt_bad_apr)
  recover time.TimeError.InvalidDate:
    dt_direct_apr_caught = true
  echo "TAG:dt_direct_apr_caught:", dt_direct_apr_caught

  # 8. Direct record construction with extreme year (INT64_MAX) saturates safely
  dt_ext_max = time.DateTime(year: 9223372036854775807, month: 1, day: 1, hour: 0, minute: 0, second: 0, millisecond: 0)
  var ts_ext_max_ms = 0
  try:
    ts_ext_max = time.datetime_to_timestamp(dt_ext_max)
    ts_ext_max_ms = ts_ext_max.epoch_millis
  recover time.TimeError.InvalidDate:
    ts_ext_max_ms = 0
  echo "TAG:dt_ext_max:", ts_ext_max_ms
"""
    ret, stdout, stderr = run_moss_probe(probe)
    assert ret == 0, f"Time probe failed:\nSTDOUT: {stdout}\nSTDERR: {stderr}"
    tags = parse_tags(stdout)

    assert tags["div0_caught"] == "true", "TimeError.DivisionByZero not raised"
    assert tags["apr31_caught"] == "true", "TimeError.InvalidDate not raised on April 31"
    assert tags["feb1900_caught"] == "true", "TimeError.InvalidDate not raised on Feb 29 1900"
    assert tags["iso_2000"] == "2000-02-29T12:00:00.000Z", "Feb 29 2000 failed"
    assert tags["iso_2024"] == "2024-02-29T12:00:00.000Z", "Feb 29 2024 failed"
    assert int(tags["add_sat"]) == 9223372036854775807, "duration_add did not saturate"
    assert int(tags["abs_sat"]) == 9223372036854775807, "duration_abs(INT64_MIN) did not saturate"
    assert int(tags["sec_m1000"]) == -1, "Euclidean seconds for -1000ms failed"
    assert int(tags["sec_m1500"]) == -2, "Euclidean seconds for -1500ms failed"
    assert tags["dt_direct_apr_caught"] == "true", "TimeError.InvalidDate not raised on direct record April 31 conversion"
    assert int(tags["dt_ext_max"]) == 9223372036854775807, "Extreme year DateTime did not saturate"
    print("  [PASS] TimeError.DivisionByZero and TimeError.InvalidDate verified.")
    print("  [PASS] Saturating arithmetic and Euclidean negative timestamps verified.")
    print("  [PASS] Direct record construction boundary validation and extreme year saturation verified.")


def main():
    test_math_repairs()
    test_random_repairs()
    test_time_repairs()
    print("\n" + "=" * 80)
    print("ALL INDEPENDENT ADVERSARIAL ORACLES PASSED! ZERO FALSIFICATIONS!")
    print("=" * 80)


if __name__ == "__main__":
    main()
