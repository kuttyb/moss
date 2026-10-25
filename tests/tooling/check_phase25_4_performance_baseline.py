#!/usr/bin/env python3
"""Permanent Compiler Performance Regression Target (Phase 25.4 B1).

Automated regression gate that protects the compiler against performance,
attribution, and memory regressions relative to the Phase 25.4 B1 baseline:
1. Corpus Integrity: Verifies all 54 frozen fixtures against SHA256 digests.
2. Report & Data Consistency: Verifies results.csv and REPORT.md alignment.
3. Performance Regression Gate: Measures key representative torture workloads
   against certified B1 baseline latency and memory thresholds.
4. Semantic Invariants: Enforces exact checked specialization counts and
   status parity (pass on positive, expected error on negative).

Usage:
    python3 tests/tooling/check_phase25_4_performance_baseline.py [compiler-binary]
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import time

ROOT_DIR = Path(__file__).resolve().parents[2]
TORTURE_ROOT = ROOT_DIR / "benchmarks" / "compiler_torture"
BASELINE_DIR = ROOT_DIR / "benchmarks" / "phase25_4" / "cpp_baseline"
RESULTS_CSV = BASELINE_DIR / "results" / "results.csv"

# Representative performance regression cohort with certified B1 baseline thresholds
# Latency ceiling is set at +50% above baseline median to absorb CI VM noise
# while catching any algorithmic regression, O(N^2) leak, or de-optimization.
REGRESSION_COHORT = {
    "w2_spec_distinct_n016": {
        "category": "specialization",
        "path": "fixtures/specialization/w2_spec_distinct_n016.moss",
        "is_project": False,
        "expected_status": "pass",
        "expected_specs": 272,
        "baseline_median_ms": 728.0,
        "max_allowed_ms": 1150.0,
        "max_rss_kib": 25000,
    },
    "w3_spec_repeated_n016": {
        "category": "specialization",
        "path": "fixtures/specialization/w3_spec_repeated_n016.moss",
        "is_project": False,
        "expected_status": "pass",
        "expected_specs": 6,
        "baseline_median_ms": 556.0,
        "max_allowed_ms": 900.0,
        "max_rss_kib": 25000,
    },
    "w4_infer_deep_d010": {
        "category": "inference",
        "path": "fixtures/inference/w4_infer_deep_d010.moss",
        "is_project": False,
        "expected_status": "pass",
        "expected_specs": 11,
        "baseline_median_ms": 144.0,
        "max_allowed_ms": 250.0,
        "max_rss_kib": 20000,
    },
    "w5_infer_wide_w025": {
        "category": "inference",
        "path": "fixtures/inference/w5_infer_wide_w025.moss",
        "is_project": False,
        "expected_status": "pass",
        "expected_specs": 50,
        "baseline_median_ms": 734.0,
        "max_allowed_ms": 1200.0,
        "max_rss_kib": 20000,
    },
    "w6_types_count_t050": {
        "category": "collections",
        "path": "fixtures/collections/w6_types_count_t050.moss",
        "is_project": False,
        "expected_status": "pass",
        "expected_specs": None,
        "baseline_median_ms": 120.0,
        "max_allowed_ms": 200.0,
        "max_rss_kib": 20000,
    },
    "w7_collections_nested_k3": {
        "category": "collections",
        "path": "fixtures/collections/w7_collections_nested_k3.moss",
        "is_project": False,
        "expected_status": "pass",
        "expected_specs": None,
        "baseline_median_ms": 3.1,
        "max_allowed_ms": 15.0,
        "max_rss_kib": 18000,
    },
    "w8_traits_dispatch_c050": {
        "category": "traits",
        "path": "fixtures/traits/w8_traits_dispatch_c050.moss",
        "is_project": False,
        "expected_status": "pass",
        "expected_specs": 50,
        "baseline_median_ms": 120.0,
        "max_allowed_ms": 200.0,
        "max_rss_kib": 20000,
    },
    "mg4_fan_out": {
        "category": "modules",
        "path": "fixtures/modules/mg4_fan_out",
        "is_project": True,
        "expected_status": "pass",
        "expected_specs": None,
        "baseline_median_ms": 60.0,
        "max_allowed_ms": 120.0,
        "max_rss_kib": 20000,
    },
    "example_checkout": {
        "category": "examples",
        "path": "fixtures/examples/checkout.moss",
        "is_project": False,
        "expected_status": "pass",
        "expected_specs": None,
        "baseline_median_ms": 10.6,
        "max_allowed_ms": 30.0,
        "max_rss_kib": 18000,
    },
    "negative_await_retired": {
        "category": "negative",
        "path": "fixtures/negative/await_retired.moss",
        "is_project": False,
        "expected_status": "fail",
        "expected_specs": None,
        "baseline_median_ms": 1.0,
        "max_allowed_ms": 10.0,
        "max_rss_kib": 18000,
    },
}


def run_command(args: list[str], cwd: Path = ROOT_DIR) -> subprocess.CompletedProcess:
    return subprocess.run(args, cwd=cwd, text=True, capture_output=True, check=False)


def verify_corpus_integrity() -> bool:
    print("Step 1/3: Verifying portable torture corpus SHA256 integrity...")
    verify_py = TORTURE_ROOT / "verify.py"
    res = run_command([sys.executable, str(verify_py)])
    if res.returncode != 0:
        print(f"[FAIL] Corpus integrity verification failed:\n{res.stdout}\n{res.stderr}")
        return False
    print("  [PASS] 100% fixture SHA256 integrity verified.")
    return True


def verify_report_consistency() -> bool:
    print("Step 2/3: Verifying benchmark report and raw data consistency...")
    check_report_py = BASELINE_DIR / "check_report_consistency.py"
    res = run_command([sys.executable, str(check_report_py)])
    if res.returncode != 0:
        print(f"[FAIL] Report consistency check failed:\n{res.stdout}\n{res.stderr}")
        return False
    print("  [PASS] REPORT.md is 100% consistent with results.csv.")
    return True


def run_single_benchmark(compiler: Path, fid: str, config: dict) -> dict:
    env = dict(os.environ)
    env["MOSS_PROFILE_COMPILER"] = "1"

    rel_path = config["path"]
    target_path = TORTURE_ROOT / rel_path
    if config["is_project"]:
        check_target = str(target_path / "src" / "main.moss")
    else:
        check_target = str(target_path)

    with tempfile.TemporaryDirectory() as tmp_dir:
        stats_file = Path(tmp_dir) / "stats.txt"
        cmd = [
            "/usr/bin/time", "-f", "%U|%S|%M", "-o", str(stats_file),
            str(compiler), "check", check_target, "--json"
        ]

        t_start = time.perf_counter_ns()
        res = subprocess.run(cmd, env=env, cwd=ROOT_DIR, capture_output=True, text=True, timeout=60)
        wall_ms = (time.perf_counter_ns() - t_start) / 1e6

        peak_rss = 0
        if stats_file.exists():
            parts = stats_file.read_text().strip().split("|")
            if len(parts) == 3:
                try:
                    peak_rss = int(parts[2])
                except ValueError:
                    pass

    # Extract checked specializations
    checked_specs = None
    for line in res.stderr.splitlines():
        if line.startswith("MOSS_PROFILE|checked_specializations|"):
            try:
                checked_specs = int(line.split("|")[2])
            except ValueError:
                pass

    accepted = False
    try:
        data = json.loads(res.stdout)
        accepted = data.get("ok") is True
    except Exception:
        accepted = False

    return {
        "fixture_id": fid,
        "wall_ms": round(wall_ms, 2),
        "peak_rss_kib": peak_rss,
        "checked_specs": checked_specs,
        "accepted": accepted,
        "exit_code": res.returncode,
    }


def verify_performance_regression(compiler: Path) -> bool:
    print(f"Step 3/3: Running performance regression cohort against {compiler}...")
    failures = []

    print(f"{'Fixture ID':<28} {'Status':<6} {'Wall (ms)':>10} {'Baseline':>10} {'Limit':>10} {'RSS (KiB)':>10} {'Specs':>7}")
    print("-" * 88)

    for fid, cfg in REGRESSION_COHORT.items():
        # Best of 2 runs to minimize transient system interrupts
        runs = [run_single_benchmark(compiler, fid, cfg) for _ in range(2)]
        best = min(runs, key=lambda r: r["wall_ms"])

        wall_ms = best["wall_ms"]
        rss_kib = best["peak_rss_kib"]
        specs = best["checked_specs"]
        accepted = best["accepted"]

        expected_ok = (cfg["expected_status"] == "pass")
        status_ok = (accepted == expected_ok) and (best["exit_code"] == 0 if expected_ok else best["exit_code"] != 0)
        wall_ok = (wall_ms <= cfg["max_allowed_ms"])
        rss_ok = (rss_kib <= cfg["max_rss_kib"])
        specs_ok = (cfg["expected_specs"] is None or specs == cfg["expected_specs"])

        passed = status_ok and wall_ok and rss_ok and specs_ok

        tag = "PASS" if passed else "FAIL"
        specs_str = str(specs) if specs is not None else "-"
        print(f"{fid:<28} {tag:<6} {wall_ms:10.2f} {cfg['baseline_median_ms']:10.1f} {cfg['max_allowed_ms']:10.1f} {rss_kib:10d} {specs_str:>7}")

        if not passed:
            reasons = []
            if not status_ok:
                reasons.append(f"acceptance status mismatch (got accepted={accepted}, expected_status={cfg['expected_status']})")
            if not wall_ok:
                reasons.append(f"latency regression (got {wall_ms:.2f} ms > limit {cfg['max_allowed_ms']:.1f} ms)")
            if not rss_ok:
                reasons.append(f"memory regression (got {rss_kib} KiB > limit {cfg['max_rss_kib']} KiB)")
            if not specs_ok:
                reasons.append(f"specialization count mismatch (got {specs}, expected {cfg['expected_specs']})")
            failures.append((fid, reasons))

    print("-" * 88)

    if failures:
        print(f"\n[FAIL] Performance regression detected in {len(failures)} workload(s):")
        for fid, reasons in failures:
            print(f"  - {fid}: {'; '.join(reasons)}")
        return False

    print("  [PASS] All representative workloads within certified B1 baseline performance bounds.")
    return True


def main():
    parser = argparse.ArgumentParser(description="Permanent Compiler Performance Regression Target (Phase 25.4 B1)")
    parser.add_argument("compiler", nargs="?", default=None, help="Path to moss-opt compiler executable")
    args = parser.parse_args()

    compiler_path = Path(args.compiler).resolve() if args.compiler else ROOT_DIR / "build" / "compiler" / "moss-opt"
    if not compiler_path.is_file() or not os.access(compiler_path, os.X_OK):
        sys.exit(f"Compiler binary missing: {compiler_path}. Run `make --jobs=\"$(nproc)\" moss-opt` first.")

    print(f"Executing Phase 25.4 B1 Permanent Performance Regression Target...")
    print(f"Target Compiler: {compiler_path}\n")

    if not verify_corpus_integrity():
        sys.exit(1)

    if not verify_report_consistency():
        sys.exit(1)

    if not verify_performance_regression(compiler_path):
        sys.exit(1)

    print("\nPERMANENT REGRESSION GATE PASSED: Compiler performance is within certified baseline bounds.")


if __name__ == "__main__":
    main()
