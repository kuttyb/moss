#!/usr/bin/env python3
"""
test_time_falsification.py
===========================
Independent Adversarial Numerical Oracle & Falsification Harness for moss.time
Phase 24 Team M — L7 Adversarial Review Verification (Commit 129a81f)

Author: Independent Adversarial Numerical Reviewer
Reference: ISO 8601-1:2019 Date and Time Format; Proleptic Gregorian Calendar Rules
"""

import os
import subprocess
import sys
from pathlib import Path

# Paths
REPO_ROOT = Path(__file__).resolve().parents[2]
WORKTREE_ROOT = REPO_ROOT
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


def run_moss_probe(moss_code: str, timeout: int = 15) -> tuple[int, str, str]:
    """Compile and execute a Moss probe in the oracle harness package."""
    ensure_harness()
    main_moss_path = HARNESS_DIR / "src" / "main.moss"
    main_moss_path.parent.mkdir(parents=True, exist_ok=True)
    with open(main_moss_path, "w", encoding="utf-8") as f:
        f.write(moss_code)

    try:
        res = subprocess.run(
            [str(MARGO_BIN), "run"],
            cwd=HARNESS_DIR,
            capture_output=True,
            text=True,
            timeout=timeout,
        )
        return res.returncode, res.stdout, res.stderr
    except subprocess.TimeoutExpired as exc:
        return -1, exc.stdout or "", "TIMEOUT: Execution exceeded timeout"


def test_time():
    print("=" * 80)
    print("MOSS.TIME ADVERSARIAL FALSIFICATION SUITE")
    print("=" * 80)

    falsifications = []
    passes = []

    # -------------------------------------------------------------------------
    # 1. GREGORIAN CALENDAR VALIDATION & ROUNDTRIP
    # -------------------------------------------------------------------------
    print("-" * 80)
    print("1. GREGORIAN CALENDAR VALIDATION & ROUNDTRIP:")
    print("-" * 80)

    calendar_probe = """module oracle_harness
import time

fn main():
  # 1. April 31 (invalid: April only has 30 days)
  dt_apr31 = time.make_datetime(2024, 4, 31, 12, 0, 0, 0)
  echo "TAG:apr31_iso:", time.datetime_to_iso8601(dt_apr31)
  ts_apr31 = time.datetime_to_timestamp(dt_apr31)
  dt_apr31_rt = time.timestamp_to_datetime(ts_apr31)
  echo "TAG:apr31_rt_iso:", time.datetime_to_iso8601(dt_apr31_rt)

  # 2. Feb 29 on secular non-leap year 1900 (invalid)
  dt_feb29_1900 = time.make_datetime(1900, 2, 29, 0, 0, 0, 0)
  echo "TAG:feb29_1900_iso:", time.datetime_to_iso8601(dt_feb29_1900)
  ts_feb29_1900 = time.datetime_to_timestamp(dt_feb29_1900)
  dt_feb29_1900_rt = time.timestamp_to_datetime(ts_feb29_1900)
  echo "TAG:feb29_1900_rt_iso:", time.datetime_to_iso8601(dt_feb29_1900_rt)

  # 3. Feb 29 on leap year 2000 (valid)
  dt_feb29_2000 = time.make_datetime(2000, 2, 29, 0, 0, 0, 0)
  ts_feb29_2000 = time.datetime_to_timestamp(dt_feb29_2000)
  dt_feb29_2000_rt = time.timestamp_to_datetime(ts_feb29_2000)
  echo "TAG:feb29_2000_rt_iso:", time.datetime_to_iso8601(dt_feb29_2000_rt)

  # 4. Feb 29 on leap year 2024 (valid)
  dt_feb29_2024 = time.make_datetime(2024, 2, 29, 0, 0, 0, 0)
  ts_feb29_2024 = time.datetime_to_timestamp(dt_feb29_2024)
  dt_feb29_2024_rt = time.timestamp_to_datetime(ts_feb29_2024)
  echo "TAG:feb29_2024_rt_iso:", time.datetime_to_iso8601(dt_feb29_2024_rt)

  # 5. Pre-1970 negative Unix timestamp: 1969-12-31 23:59:59.000 (-1000 ms)
  ts_m1000 = time.timestamp_from_unix_millis(-1000)
  dt_m1000 = time.timestamp_to_datetime(ts_m1000)
  echo "TAG:neg_epoch_1000_iso:", time.datetime_to_iso8601(dt_m1000)
  ts_m1000_rt = time.datetime_to_timestamp(dt_m1000)
  echo "TAG:neg_epoch_1000_rt_ms:", time.timestamp_unix_millis(ts_m1000_rt)

  # 6. Pre-1970 negative timestamp: 1969-12-30 00:00:00.000 (-172800000 ms = -2 days)
  ts_m2days = time.timestamp_from_unix_millis(-172800000)
  dt_m2days = time.timestamp_to_datetime(ts_m2days)
  echo "TAG:neg_epoch_2days_iso:", time.datetime_to_iso8601(dt_m2days)

  # 7. Negative ms to seconds truncation
  ts_m1500 = time.timestamp_from_unix_millis(-1500)
  echo "TAG:neg_ms_to_sec:", time.timestamp_unix_seconds(ts_m1500)
"""
    ret, stdout, stderr = run_moss_probe(calendar_probe)
    tags = {}
    for line in stdout.splitlines():
        if line.startswith("TAG:"):
            parts = line.split(":", 2)
            tags[parts[1]] = parts[2].strip()

    # April 31 check
    apr31_input = tags.get("apr31_iso")
    apr31_rt = tags.get("apr31_rt_iso")
    if apr31_input == "2024-04-31T12:00:00.000Z":
        falsifications.append({
            "test": "make_datetime April 31 calendar validation",
            "reason": (
                f"CALENDAR VALIDATION FLAW: make_datetime accepted invalid date April 31 ({apr31_input}). "
                f"April has only 30 days. When converted to timestamp and back, the date silently morphed to "
                f"{apr31_rt} (May 1, 2024), violating round-trip integrity."
            ),
        })
        print(f"  [X] April 31 accepted as {apr31_input} -> morphed to {apr31_rt} (FALSIFIED)")
    else:
        passes.append("April 31 validation")
        print(f"  [+] April 31 correctly clamped: {apr31_input} (PASS)")

    # Feb 29 on 1900 check
    feb1900_input = tags.get("feb29_1900_iso")
    feb1900_rt = tags.get("feb29_1900_rt_iso")
    if feb1900_input == "1900-02-29T00:00:00.000Z":
        falsifications.append({
            "test": "make_datetime Feb 29 on non-leap 1900 calendar validation",
            "reason": (
                f"LEAP YEAR VALIDATION FLAW: 1900 is NOT a leap year (divisible by 100 but not 400). "
                f"make_datetime accepted Feb 29 1900 ({feb1900_input}), which silently morphed to "
                f"{feb1900_rt} (March 1, 1900) on round-trip."
            ),
        })
        print(f"  [X] Feb 29 1900 accepted as {feb1900_input} -> morphed to {feb1900_rt} (FALSIFIED)")
    else:
        passes.append("Feb 29 1900 validation")
        print(f"  [+] Feb 29 1900 correctly rejected/clamped: {feb1900_input} (PASS)")

    # Feb 29 on 2000 & 2024 checks
    feb2000_rt = tags.get("feb29_2000_rt_iso")
    if feb2000_rt == "2000-02-29T00:00:00.000Z":
        print(f"  [+] Feb 29 2000 (leap year) roundtrips: {feb2000_rt} (PASS)")
        passes.append("Feb 29 2000 leap year")
    else:
        falsifications.append({"test": "Feb 29 2000 leap year", "reason": f"Expected 2000-02-29, got {feb2000_rt}"})
        print(f"  [X] Feb 29 2000 FALSIFIED.")

    feb2024_rt = tags.get("feb29_2024_rt_iso")
    if feb2024_rt == "2024-02-29T00:00:00.000Z":
        print(f"  [+] Feb 29 2024 (leap year) roundtrips: {feb2024_rt} (PASS)")
        passes.append("Feb 29 2024 leap year")
    else:
        falsifications.append({"test": "Feb 29 2024 leap year", "reason": f"Expected 2024-02-29, got {feb2024_rt}"})
        print(f"  [X] Feb 29 2024 FALSIFIED.")

    # Pre-1970 negative epoch check
    neg1000_iso = tags.get("neg_epoch_1000_iso")
    neg1000_rt_ms = tags.get("neg_epoch_1000_rt_ms")
    if neg1000_iso == "1969-12-31T23:59:59.000Z" and neg1000_rt_ms == "-1000":
        print(f"  [+] Pre-1970 epoch -1000ms correctly maps to {neg1000_iso} and roundtrips (PASS)")
        passes.append("pre-1970 epoch -1000ms")
    else:
        falsifications.append({"test": "pre-1970 epoch -1000ms", "reason": f"Got {neg1000_iso}, rt_ms: {neg1000_rt_ms}"})
        print(f"  [X] Pre-1970 epoch -1000ms FALSIFIED.")

    neg2days_iso = tags.get("neg_epoch_2days_iso")
    if neg2days_iso == "1969-12-30T00:00:00.000Z":
        print(f"  [+] Pre-1970 epoch -2 days correctly maps to {neg2days_iso} (PASS)")
        passes.append("pre-1970 epoch -2days")
    else:
        falsifications.append({"test": "pre-1970 epoch -2days", "reason": f"Got {neg2days_iso}"})
        print(f"  [X] Pre-1970 epoch -2days FALSIFIED.")

    # Negative ms to seconds division
    neg_ms_sec = tags.get("neg_ms_to_sec")
    print(f"  [!] timestamp_unix_seconds(-1500 ms) = {neg_ms_sec} (truncation towards zero vs Euclidean floor -2s)")

    # -------------------------------------------------------------------------
    # 2. DURATION ARITHMETIC OVERFLOW & DIVISION CONTRACTS
    # -------------------------------------------------------------------------
    print("\n" + "-" * 80)
    print("2. DURATION ARITHMETIC OVERFLOW & DIVISION CONTRACTS:")
    print("-" * 80)

    overflow_probe = """module oracle_harness
import time

fn main():
  let int_max = 9223372036854775807
  let int_min = -9223372036854775808

  # 1. duration_div by zero contract
  d10 = time.duration_from_seconds(10)
  d_div0 = time.duration_div(d10, 0)
  echo "TAG:div_zero:", time.duration_nanos(d_div0)

  # 2. duration_div INT64_MIN by -1 overflow
  d_min = time.duration_from_nanos(int_min)
  d_div_neg1 = time.duration_div(d_min, -1)
  echo "TAG:div_min_neg1:", time.duration_nanos(d_div_neg1)

  # 3. duration_abs(INT64_MIN) overflow
  d_abs_min = time.duration_abs(d_min)
  echo "TAG:abs_min:", time.duration_nanos(d_abs_min)

  # 4. duration_add overflow
  d_max = time.duration_from_nanos(int_max)
  d_one = time.duration_from_nanos(1)
  d_add_ovf = time.duration_add(d_max, d_one)
  echo "TAG:add_overflow:", time.duration_nanos(d_add_ovf)

  # 5. duration_scale overflow
  d_scale_ovf = time.duration_scale(d_max, 2)
  echo "TAG:scale_overflow:", time.duration_nanos(d_scale_ovf)

  # 6. instant_diff overflow
  inst_max = time.make_instant(int_max)
  inst_neg1 = time.make_instant(-1)
  diff_ovf = time.instant_diff(inst_max, inst_neg1)
  echo "TAG:instant_diff_overflow:", time.duration_nanos(diff_ovf)

  # 7. timestamp_diff nanos overflow
  ts_max = time.timestamp_from_unix_millis(int_max)
  ts_zero = time.timestamp_from_unix_millis(0)
  ts_diff_ovf = time.timestamp_diff(ts_max, ts_zero)
  echo "TAG:timestamp_diff_overflow:", time.duration_nanos(ts_diff_ovf)
"""
    ret, stdout, stderr = run_moss_probe(overflow_probe)
    o_tags = {}
    for line in stdout.splitlines():
        if line.startswith("TAG:"):
            parts = line.split(":", 2)
            o_tags[parts[1]] = int(parts[2].strip())

    # Division by zero contract
    div_zero = o_tags.get("div_zero")
    if div_zero == 0:
        print(f"  [+] duration_div(d, 0) returns 0 duration contract satisfied (PASS)")
        passes.append("duration_div zero contract")
    else:
        falsifications.append({"test": "duration_div zero contract", "reason": f"Expected 0, got {div_zero}"})
        print(f"  [X] duration_div zero contract FALSIFIED.")

    # duration_abs INT64_MIN
    abs_min = o_tags.get("abs_min")
    if abs_min == -9223372036854775808:
        falsifications.append({
            "test": "duration_abs(INT64_MIN) non-negativity contract",
            "reason": (
                f"ARITHMETIC OVERFLOW: duration_abs(Duration(-9223372036854775808)) computed '0 - d.nanos' "
                f"which wraps back to -9223372036854775808. duration_abs returned a NEGATIVE duration, "
                f"violating the fundamental mathematical contract that abs(d) >= 0."
            ),
        })
        print(f"  [X] duration_abs(INT64_MIN) = {abs_min} (NEGATIVE DURATION RETURNED! FALSIFIED)")
    else:
        passes.append("duration_abs non-negativity")
        print(f"  [+] duration_abs(INT64_MIN) correctly clamped: {abs_min} (PASS)")

    # duration_div INT64_MIN / -1
    div_min_neg1 = o_tags.get("div_min_neg1")
    if div_min_neg1 == -9223372036854775808:
        falsifications.append({
            "test": "duration_div(INT64_MIN, -1) arithmetic correctness",
            "reason": (
                f"ARITHMETIC OVERFLOW: duration_div(Duration(INT64_MIN), -1) wrapped to -9223372036854775808. "
                f"Dividing a negative duration by -1 returned a negative duration!"
            ),
        })
        print(f"  [X] duration_div(INT64_MIN, -1) = {div_min_neg1} (NEGATIVE RESULT! FALSIFIED)")
    else:
        passes.append("duration_div INT64_MIN / -1")
        print(f"  [+] duration_div(INT64_MIN, -1) correctly clamped (PASS)")

    # duration_add INT64_MAX + 1
    add_ovf = o_tags.get("add_overflow")
    if add_ovf == -9223372036854775808:
        falsifications.append({
            "test": "duration_add(INT64_MAX, 1) monotonicity",
            "reason": (
                f"ARITHMETIC OVERFLOW: duration_add(Duration(INT64_MAX), Duration(1)) wrapped to -9223372036854775808. "
                f"Adding positive durations yielded a large negative duration."
            ),
        })
        print(f"  [X] duration_add(INT64_MAX, 1) = {add_ovf} (WRAPPED TO NEGATIVE! FALSIFIED)")
    else:
        passes.append("duration_add overflow")

    # duration_scale INT64_MAX * 2
    scale_ovf = o_tags.get("scale_overflow")
    if scale_ovf == -2:
        falsifications.append({
            "test": "duration_scale(INT64_MAX, 2) overflow",
            "reason": (
                f"ARITHMETIC OVERFLOW: duration_scale(Duration(INT64_MAX), 2) wrapped to -2."
            ),
        })
        print(f"  [X] duration_scale(INT64_MAX, 2) = {scale_ovf} (WRAPPED TO -2! FALSIFIED)")
    else:
        passes.append("duration_scale overflow")

    # instant_diff INT64_MAX - (-1)
    diff_ovf = o_tags.get("instant_diff_overflow")
    if diff_ovf == -9223372036854775808:
        falsifications.append({
            "test": "instant_diff(INT64_MAX, -1) overflow",
            "reason": (
                f"ARITHMETIC OVERFLOW: instant_diff(INT64_MAX, -1) wrapped to -9223372036854775808."
            ),
        })
        print(f"  [X] instant_diff(INT64_MAX, -1) = {diff_ovf} (WRAPPED TO NEGATIVE! FALSIFIED)")
    else:
        passes.append("instant_diff overflow")

    # timestamp_diff INT64_MAX * 1e6
    ts_diff_ovf = o_tags.get("timestamp_diff_overflow")
    if ts_diff_ovf < 0:
        falsifications.append({
            "test": "timestamp_diff nanos multiplication overflow",
            "reason": (
                f"ARITHMETIC OVERFLOW: timestamp_diff multiplied millisecond difference by 1,000,000 "
                f"without checking Int64 capacity, wrapping to {ts_diff_ovf}."
            ),
        })
        print(f"  [X] timestamp_diff(INT64_MAX, 0) = {ts_diff_ovf} (OVERFLOW WRAP! FALSIFIED)")
    else:
        passes.append("timestamp_diff overflow")

    print("\n" + "=" * 80)
    print("MOSS.TIME FALSIFICATION SUMMARY:")
    print(f"Passed Checks:      {len(passes)}")
    print(f"FALSIFIED Claims:   {len(falsifications)}")
    print("=" * 80)
    return falsifications


if __name__ == "__main__":
    fails = test_time()
    if fails:
        print(f"\n[FALSIFICATION CONFIRMED] {len(fails)} defect(s) in moss.time!")
        sys.exit(1)
    else:
        print("\n[ALL CLAIMS VERIFIED] No falsifications detected.")
        sys.exit(0)
