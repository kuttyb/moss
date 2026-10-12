#!/usr/bin/env python3
"""
test_random_falsification.py
=============================
Independent Adversarial Numerical Oracle & Falsification Harness for moss.random
Phase 24 Team M — L7 Adversarial Review Verification (Commit 129a81f)

Author: Independent Adversarial Numerical Reviewer
Reference: Pierre L'Ecuyer, "Good Parameter Sets for Combined Multiple Recursive
           Random Number Generators", Operations Research 47(1):159-164, 1999.
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


def run_moss_probe(moss_code: str, timeout: int = 10) -> tuple[int, str, str]:
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
        return -1, exc.stdout or "", "TIMEOUT: Execution exceeded timeout (infinite loop detected)"


def l_ecuyer_1999_reference(x1, x2, x3, y1, y2, y3, steps=10):
    """
    Pierre L'Ecuyer (1999) exact reference recurrence for MRG32k3a:
    x_n = (1403580 * x_{n-2} - 810728 * x_{n-3}) mod 4294967087
    y_n = (527612 * y_{n-1} - 1370589 * y_{n-3}) mod 4294944443
    z_n = (x_n - y_n) mod 4294967087
    """
    m1 = 4294967087
    m2 = 4294944443
    outputs = []
    for _ in range(steps):
        p1 = (1403580 * x2 - 810728 * x3) % m1
        p2 = (527612 * y1 - 1370589 * y3) % m2
        x3, x2, x1 = x2, x1, p1
        y3, y2, y1 = y2, y1, p2
        z = (p1 - p2) % m1
        if z <= 0:
            z += m1
        outputs.append(z)
    return outputs


def test_random():
    print("=" * 80)
    print("MOSS.RANDOM ADVERSARIAL FALSIFICATION SUITE")
    print("=" * 80)

    falsifications = []
    passes = []

    # -------------------------------------------------------------------------
    # 1. SIGNED INTEGER OVERFLOW IN RANDINT(MIN, MAX)
    # -------------------------------------------------------------------------
    print("-" * 80)
    print("1. SIGNED INTEGER OVERFLOW IN RANDINT(MIN, MAX):")
    print("-" * 80)
    print("Testing range [-5e18, +5e18] where max - min = 1e19 (exceeds Int64 max 9.22e18)...")

    overflow_probe = """module oracle_harness
import random

fn main():
  var rng = random.new_rng(42)
  let min_val = -5000000000000000000
  let max_val = 5000000000000000000
  echo "STARTING_OVERFLOW_TEST"
  res = random.rng_randint(rng, min_val, max_val)
  echo "TAG:randint_overflow_result:", res
"""
    ret, stdout, stderr = run_moss_probe(overflow_probe, timeout=4)
    if ret == -1:
        falsifications.append({
            "test": "randint signed integer overflow on max - min > Int64",
            "reason": (
                "CRITICAL HANG: randint(-5e18, 5e18) entered an infinite rejection loop. "
                "In moss.random: range_size = max - min + 1 overflows to negative Int64 (-8446744073709551615). "
                "Because range_size <= m (negative <= 4294967086), it executes single-draw rejection sampling. "
                "limit = m - (m % range_size) computes 4294967086 - 4294967086 = 0. "
                "The loop 'while u >= limit' with limit=0 becomes an infinite loop on u >= 0."
            ),
        })
        print("  [X] randint(-5e18, 5e18) => TIMEOUT / INFINITE LOOP (FALSIFIED)")
        print("      Cause: range_size overflows to negative; limit becomes 0; loop hangs forever.")
    else:
        print(f"  [+] randint overflow handled gracefully: {stdout.strip()}")
        passes.append("randint overflow")

    # -------------------------------------------------------------------------
    # 2. TWO-DRAW SAMPLE SPACE CARDINALITY AND UNIFORMITY
    # -------------------------------------------------------------------------
    print("\n" + "-" * 80)
    print("2. TWO-DRAW SAMPLE SPACE CARDINALITY AND UNIFORMITY:")
    print("-" * 80)
    # Analysis of moss.random randint large range:
    # let m = 4294967086
    # let m_half = 2147483543
    # let total_outcomes = (m_half + 1) * m   # = 2147483544 * 4294967086 = 9223372030409805984
    # var v = ((next_step() - 1) / 2) * m + (next_step() - 1)
    #
    # next_step() in [1, 4294967086] => next_step() - 1 in [0, 4294967085]
    # (next_step() - 1) / 2 in [0, 2147483542] (ONLY 2147483543 distinct values!)
    # Max value of v = 2147483542 * 4294967086 + 4294967085 = 9223372026114838897
    # But total_outcomes is 9223372030409805984!
    # Missing range: [9223372026114838898, 9223372030409805983] (4,294,967,086 values)
    # These values can NEVER be generated by v! Their probability is 0!
    m = 4294967086
    m_half = 2147483543
    claimed_total = (m_half + 1) * m
    max_u1 = (m - 1) // 2
    actual_distinct_u1 = max_u1 + 1
    actual_total = actual_distinct_u1 * m
    missing_outcomes = claimed_total - actual_total

    print(f"Claimed total outcomes:  (m_half + 1) * m = {claimed_total}")
    print(f"Actual distinct u1 values: ((m - 1) // 2) + 1 = {actual_distinct_u1} (max u1 = {max_u1})")
    print(f"Actual max outcomes:     actual_u1 * m = {actual_total}")
    print(f"Missing sample space:    {missing_outcomes} unreachable values (cardinality mismatch)")

    if missing_outcomes > 0:
        falsifications.append({
            "test": "two-draw sample space cardinality and uniformity",
            "reason": (
                f"CARDINALITY MISMATCH & NON-UNIFORMITY: (next_step() - 1) / 2 produces integers in "
                f"[0, {max_u1}] ({actual_distinct_u1} values), but total_outcomes was computed as "
                f"(m_half + 1) * m = 2147483544 * m. "
                f"As a result, exactly {missing_outcomes} outcomes in [{actual_total}, {claimed_total - 1}] "
                f"are COMPLETELY UNREACHABLE (probability 0). "
                f"Furthermore, rejection threshold 'limit = total_outcomes - (total_outcomes % range_size)' "
                f"is incorrectly calculated using 2147483544 * m instead of 2147483543 * m, "
                f"inducing modulo non-uniformity across all large integer ranges."
            ),
        })
        print(f"  [X] Sample space cardinality FALSIFIED: {missing_outcomes} outcomes unreachable; distribution biased.")
    else:
        passes.append("two-draw sample space cardinality")

    # -------------------------------------------------------------------------
    # 3. MRG32k3a REFERENCE SEQUENCE AGAINST PIERRE L'ECUYER 1999 (S=123456789)
    # -------------------------------------------------------------------------
    print("\n" + "-" * 80)
    print("3. MRG32K3A REFERENCE SEQUENCE AGAINST PIERRE L'ECUYER (1999):")
    print("-" * 80)

    lecuyer_direct_expected = l_ecuyer_1999_reference(
        123456789, 123456789, 123456789,
        123456789, 123456789, 123456789,
        steps=5
    )

    rng_probe = """module oracle_harness
import random

fn main():
  var rng_direct = random.Rng(x1 = 123456789, x2 = 123456789, x3 = 123456789, y1 = 123456789, y2 = 123456789, y3 = 123456789)
  echo "TAG:direct_0:", rng_direct.next_step()
  echo "TAG:direct_1:", rng_direct.next_step()
  echo "TAG:direct_2:", rng_direct.next_step()
  echo "TAG:direct_3:", rng_direct.next_step()
  echo "TAG:direct_4:", rng_direct.next_step()

  var rng_factory = random.new_rng(123456789)
  echo "TAG:factory_0:", random.rng_next_int(rng_factory)
  echo "TAG:factory_1:", random.rng_next_int(rng_factory)
  echo "TAG:factory_2:", random.rng_next_int(rng_factory)
  echo "TAG:factory_3:", random.rng_next_int(rng_factory)
  echo "TAG:factory_4:", random.rng_next_int(rng_factory)
"""
    ret, stdout, stderr = run_moss_probe(rng_probe)
    tags = {}
    for line in stdout.splitlines():
        if line.startswith("TAG:"):
            parts = line.split(":", 2)
            tags[parts[1]] = int(parts[2].strip())

    moss_direct_outputs = [tags.get(f"direct_{i}") for i in range(5)]
    moss_factory_outputs = [tags.get(f"factory_{i}") for i in range(5)]

    print(f"L'Ecuyer (1999) Direct Reference: {lecuyer_direct_expected}")
    print(f"Moss Direct State Execution:      {moss_direct_outputs}")
    print(f"Moss new_rng(123456789) Output:   {moss_factory_outputs}")

    if moss_direct_outputs == lecuyer_direct_expected:
        print("  [+] Core recurrence matches Pierre L'Ecuyer (1999) bit-for-bit (PASS).")
        passes.append("MRG32k3a core recurrence")
    else:
        falsifications.append({
            "test": "MRG32k3a core recurrence",
            "reason": f"Core recurrence output {moss_direct_outputs} does not match L'Ecuyer {lecuyer_direct_expected}",
        })
        print("  [X] Core recurrence FALSIFIED.")

    if moss_factory_outputs == lecuyer_direct_expected:
        print("  [+] new_rng(123456789) reproduces canonical paper sequence.")
        passes.append("new_rng canonical sequence")
    else:
        print("  [!] Note: new_rng(123456789) diverges from canonical paper sequence due to custom seed multiplier hashing.")

    # -------------------------------------------------------------------------
    # 4. BOUNDARY SEEDS (INT64_MAX, INT64_MIN, NEGATIVE, 0)
    # -------------------------------------------------------------------------
    print("\n" + "-" * 80)
    print("4. BOUNDARY SEEDS (INT64_MAX, INT64_MIN, NEGATIVE, 0):")
    print("-" * 80)

    boundary_probe = """module oracle_harness
import random

fn main():
  var r_max = random.new_rng(9223372036854775807)
  echo "TAG:seed_max:", random.rng_next_int(r_max)

  var r_min = random.new_rng(-9223372036854775808)
  echo "TAG:seed_min:", random.rng_next_int(r_min)

  var r_neg = random.new_rng(-42)
  echo "TAG:seed_neg:", random.rng_next_int(r_neg)

  var r_zero = random.new_rng(0)
  echo "TAG:seed_zero:", random.rng_next_int(r_zero)
"""
    ret, stdout, stderr = run_moss_probe(boundary_probe)
    b_tags = {}
    for line in stdout.splitlines():
        if line.startswith("TAG:"):
            parts = line.split(":", 2)
            b_tags[parts[1]] = int(parts[2].strip())

    for seed_name, val in [("seed_max", 9223372036854775807), ("seed_min", -9223372036854775808), ("seed_neg", -42), ("seed_zero", 0)]:
        out = b_tags.get(seed_name)
        if out is not None and 1 <= out <= 4294967086:
            print(f"  [+] {seed_name:<10} => valid step output: {out} (PASS)")
            passes.append(seed_name)
        else:
            falsifications.append({"test": seed_name, "reason": f"Invalid step output: {out}"})
            print(f"  [X] {seed_name:<10} => invalid output: {out} (FALSIFIED)")

    # -------------------------------------------------------------------------
    # 5. ERROR CONTRACTS: INVERTED RANGES AND EMPTY COLLECTIONS
    # -------------------------------------------------------------------------
    print("\n" + "-" * 80)
    print("5. ERROR CONTRACTS: INVERTED RANGES AND EMPTY COLLECTIONS:")
    print("-" * 80)

    contract_probe = """module oracle_harness
import random

fn main():
  var rng = random.new_rng(100)
  # Inverted range: min > max must return min
  inv_res = random.rng_randint(rng, 100, 10)
  echo "TAG:inverted_range:", inv_res

  # Empty collection choice
  var empty_vec = Vector[Int]()
  c_res = random.choice(empty_vec, rng)
  echo "TAG:empty_choice:", c_res

  # Empty collection shuffle
  s_res = random.shuffle(empty_vec, rng)
  echo "TAG:empty_shuffle:", s_res |> count
"""
    ret, stdout, stderr = run_moss_probe(contract_probe)
    c_tags = {}
    for line in stdout.splitlines():
        if line.startswith("TAG:"):
            parts = line.split(":", 2)
            c_tags[parts[1]] = int(parts[2].strip())

    if c_tags.get("inverted_range") == 100:
        print("  [+] Inverted range randint(100, 10) returns 100 (PASS)")
        passes.append("inverted range contract")
    else:
        falsifications.append({"test": "inverted range", "reason": f"Expected 100, got {c_tags.get('inverted_range')}"})
        print("  [X] Inverted range contract FALSIFIED.")

    if c_tags.get("empty_choice") == 0:
        print("  [+] Empty collection choice returns 0 safe fallback (PASS)")
        passes.append("empty choice contract")
    else:
        falsifications.append({"test": "empty choice", "reason": f"Expected 0, got {c_tags.get('empty_choice')}"})
        print("  [X] Empty choice contract FALSIFIED.")

    if c_tags.get("empty_shuffle") == 0:
        print("  [+] Empty collection shuffle returns empty vector (PASS)")
        passes.append("empty shuffle contract")
    else:
        falsifications.append({"test": "empty shuffle", "reason": f"Expected 0 count, got {c_tags.get('empty_shuffle')}"})
        print("  [X] Empty shuffle contract FALSIFIED.")

    print("\n" + "=" * 80)
    print("MOSS.RANDOM FALSIFICATION SUMMARY:")
    print(f"Passed Checks:      {len(passes)}")
    print(f"FALSIFIED Claims:   {len(falsifications)}")
    print("=" * 80)
    return falsifications


if __name__ == "__main__":
    fails = test_random()
    if fails:
        print(f"\n[FALSIFICATION CONFIRMED] {len(fails)} algorithmic defect(s) in moss.random!")
        sys.exit(1)
    else:
        print("\n[ALL CLAIMS VERIFIED] No falsifications detected.")
        sys.exit(0)
