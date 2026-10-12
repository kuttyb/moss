#!/usr/bin/env python3
"""
test_numerics_falsification.py
==============================
Independent Adversarial Numerical Oracle & Falsification Suite for:
  - moss.linalg (Dense Linear Algebra)
  - moss.sparse (Compressed Sparse Matrices)
  - moss.statistics (Descriptive & Streaming Statistics)

Phase 24 Team N (Numerical Computing)
Author: Independent Adversarial Numerical Reviewer & L6 Coordinator
"""

import math
import subprocess
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[2]
MARGO_BIN = REPO_ROOT / "margo"
HARNESS_DIR = REPO_ROOT / "tmp" / "adversarial_numerics_harness"


def ensure_harness():
    HARNESS_DIR.mkdir(parents=True, exist_ok=True)
    (HARNESS_DIR / "src").mkdir(parents=True, exist_ok=True)
    manifest = HARNESS_DIR / "Moss.toml"
    manifest_content = """[package]
name = "adversarial_numerics_harness"
version = "0.1.0"

[dependencies]
linalg = { path = "../../packages/linalg" }
sparse = { path = "../../packages/sparse" }
statistics = { path = "../../packages/statistics" }
math = { path = "../../packages/math" }
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


def test_linalg_oracles():
    print("-" * 60)
    print("ORACLE 1: moss.linalg Dense Linear Algebra & Solver")
    print("-" * 60)

    probe = """module main
import linalg
import math

fn main():
  try:
    let a = [1.0, 2.0, 3.0]
    let b = [4.0, 5.0]
    let outer = linalg.vec_outer(a, b)
    echo "TAG:outer_0_0:", linalg.matrix_get(outer, 0, 0)
    echo "TAG:outer_2_1:", linalg.matrix_get(outer, 2, 1)

    let a_mat = linalg.from_vector(3, 3, [3.0, 2.0, -1.0, 2.0, -2.0, 4.0, -1.0, 0.5, -1.0])
    let b_vec = [1.0, -2.0, 0.0]
    let x_sol = linalg.solve(a_mat, b_vec)
    echo "TAG:x_0:", x_sol[0]
    echo "TAG:x_1:", x_sol[1]
    echo "TAG:x_2:", x_sol[2]

    let det = linalg.determinant(a_mat)
    echo "TAG:det:", det
    let log_det = linalg.matrix_log_det(a_mat)
    echo "TAG:log_det_sign:", log_det.sign
    echo "TAG:log_det_abs:", log_det.log_abs_det

    let inv = linalg.invert(a_mat)
    let prod = linalg.mat_mul(a_mat, inv)
    let diff_id0 = math.abs(linalg.matrix_get(prod, 0, 0) - 1.0)
    let diff_id1 = math.abs(linalg.matrix_get(prod, 1, 2) - 0.0)
    echo "TAG:inv_diag_err:", diff_id0
    echo "TAG:inv_off_err:", diff_id1

    let huge_v = [1.0e150, 2.0e150, -2.0e150]
    let huge_norm = linalg.vec_norm(huge_v)
    echo "TAG:huge_norm_ratio:", huge_norm / 1.0e150
  recover linalg.LinalgError.InvalidDimensions:
    pass
  recover linalg.LinalgError.DimensionMismatch:
    pass
  recover linalg.LinalgError.NonSquareMatrix:
    pass
  recover linalg.LinalgError.SingularMatrix:
    pass
  recover linalg.LinalgError.IndexOutOfBounds:
    pass
"""
    code, stdout, stderr = run_moss_probe(probe)
    if code != 0:
        print(f"FAILED to run linalg probe:\nSTDOUT: {stdout}\nSTDERR: {stderr}")
        return False

    tags = parse_tags(stdout)
    assert abs(float(tags["outer_0_0"]) - 4.0) < 1e-12, f"outer_0_0 mismatch: {tags['outer_0_0']}"
    assert abs(float(tags["outer_2_1"]) - 15.0) < 1e-12, f"outer_2_1 mismatch: {tags['outer_2_1']}"

    assert abs(float(tags["x_0"]) - 1.0) < 1e-8, f"x_0 mismatch: {tags['x_0']}"
    assert abs(float(tags["x_1"]) - (-2.0)) < 1e-8, f"x_1 mismatch: {tags['x_1']}"
    assert abs(float(tags["x_2"]) - (-2.0)) < 1e-8, f"x_2 mismatch: {tags['x_2']}"

    det_val = float(tags["det"])
    assert abs(det_val - (-3.0)) < 1e-8, f"det mismatch: {det_val}"
    assert float(tags["log_det_sign"]) == -1.0, f"log_det_sign mismatch: {tags['log_det_sign']}"
    assert abs(float(tags["log_det_abs"]) - math.log(3.0)) < 1e-8, f"log_det_abs mismatch: {tags['log_det_abs']}"

    assert float(tags["inv_diag_err"]) < 1e-12, f"inv_diag_err: {tags['inv_diag_err']}"
    assert float(tags["inv_off_err"]) < 1e-12, f"inv_off_err: {tags['inv_off_err']}"

    assert abs(float(tags["huge_norm_ratio"]) - 3.0) < 1e-12, f"huge_norm_ratio: {tags['huge_norm_ratio']}"

    print("PASS: moss.linalg verified against exact mathematical references.")
    return True


def test_sparse_oracles():
    print("-" * 60)
    print("ORACLE 2: moss.sparse Triplet Duplicate Summation & SpMV/SpMM")
    print("-" * 60)

    probe = """module main
import sparse
import math

fn main():
  try:
    var triplets = [sparse.Triplet(row: 1, col: 2, value: 1.5), sparse.Triplet(row: 0, col: 1, value: 2.0), sparse.Triplet(row: 1, col: 2, value: 3.5), sparse.Triplet(row: 0, col: 1, value: -1.0), sparse.Triplet(row: 1, col: 2, value: -2.0)]
    let sp = sparse.from_triplets(2, 3, triplets)
    echo "TAG:val_0_1:", sparse.csr_get(sp, 0, 1)
    echo "TAG:val_1_2:", sparse.csr_get(sp, 1, 2)
    echo "TAG:valid_csr:", sparse.validate_csr(sp)

    var b_triplets = [sparse.Triplet(row: 1, col: 0, value: 4.0), sparse.Triplet(row: 2, col: 1, value: 5.0)]
    let sp_b = sparse.from_triplets(3, 2, b_triplets)
    let c = sparse.csr_mat_mul(sp, sp_b)
    echo "TAG:c_0_0:", sparse.csr_get(c, 0, 0)
    echo "TAG:c_1_1:", sparse.csr_get(c, 1, 1)
    echo "TAG:c_0_1:", sparse.csr_get(c, 0, 1)
    echo "TAG:c_valid:", sparse.validate_csr(c)
  recover sparse.SparseError.InvalidDimensions:
    pass
  recover sparse.SparseError.DimensionMismatch:
    pass
  recover sparse.SparseError.InvalidStructure:
    pass
  recover sparse.SparseError.IndexOutOfBounds:
    pass
"""
    code, stdout, stderr = run_moss_probe(probe)
    if code != 0:
        print(f"FAILED to run sparse probe:\nSTDOUT: {stdout}\nSTDERR: {stderr}")
        return False

    tags = parse_tags(stdout)
    assert abs(float(tags["val_0_1"]) - 1.0) < 1e-12, f"val_0_1 mismatch: {tags['val_0_1']}"
    assert abs(float(tags["val_1_2"]) - 3.0) < 1e-12, f"val_1_2 mismatch: {tags['val_1_2']}"
    assert tags["valid_csr"] == "true", f"valid_csr not true: {tags['valid_csr']}"

    assert abs(float(tags["c_0_0"]) - 4.0) < 1e-12, f"c_0_0 mismatch: {tags['c_0_0']}"
    assert abs(float(tags["c_1_1"]) - 15.0) < 1e-12, f"c_1_1 mismatch: {tags['c_1_1']}"
    assert abs(float(tags["c_0_1"]) - 0.0) < 1e-12, f"c_0_1 mismatch: {tags['c_0_1']}"
    assert tags["c_valid"] == "true", f"c_valid not true: {tags['c_valid']}"

    print("PASS: moss.sparse verified against additive duplicate summation and SpMM references.")
    return True


def test_statistics_oracles():
    print("-" * 60)
    print("ORACLE 3: moss.statistics Numerical Stability & Quantiles")
    print("-" * 60)

    probe = """module main
import statistics
import math

fn main():
  try:
    let shifted = [1000000001.0, 1000000002.0, 1000000003.0]
    let s_var = statistics.variance(shifted, true)
    let p_var = statistics.variance(shifted, false)
    echo "TAG:shifted_sample_var:", s_var
    echo "TAG:shifted_pop_var:", p_var

    let d = [10.0, 20.0, 30.0, 40.0]
    echo "TAG:q25:", statistics.quantile(d, 0.25)
    echo "TAG:q50:", statistics.quantile(d, 0.50)
    echo "TAG:q75:", statistics.quantile(d, 0.75)
    echo "TAG:iqr:", statistics.iqr(d)

    var sa = statistics.new_running_stats()
    statistics.running_update(sa, 1000000001.0)
    statistics.running_update(sa, 1000000002.0)
    var sb = statistics.new_running_stats()
    statistics.running_update(sb, 1000000003.0)
    let sm = statistics.running_combine(sa, sb)
    echo "TAG:cgl_var:", statistics.running_variance(sm, true)
  recover statistics.StatsError.EmptyInput:
    pass
  recover statistics.StatsError.InsufficientData:
    pass
  recover statistics.StatsError.DimensionMismatch:
    pass
  recover statistics.StatsError.InvalidQuantile:
    pass
  recover statistics.StatsError.InvalidWeight:
    pass
  recover statistics.StatsError.ZeroVariance:
    pass
  recover statistics.StatsError.InvalidValue:
    pass
"""
    code, stdout, stderr = run_moss_probe(probe)
    if code != 0:
        print(f"FAILED to run statistics probe:\nSTDOUT: {stdout}\nSTDERR: {stderr}")
        return False

    tags = parse_tags(stdout)
    assert abs(float(tags["shifted_sample_var"]) - 1.0) < 1e-12, f"shifted_sample_var: {tags['shifted_sample_var']}"
    assert abs(float(tags["shifted_pop_var"]) - (2.0 / 3.0)) < 1e-12, f"shifted_pop_var: {tags['shifted_pop_var']}"

    assert abs(float(tags["q25"]) - 17.5) < 1e-12, f"q25: {tags['q25']}"
    assert abs(float(tags["q50"]) - 25.0) < 1e-12, f"q50: {tags['q50']}"
    assert abs(float(tags["q75"]) - 32.5) < 1e-12, f"q75: {tags['q75']}"
    assert abs(float(tags["iqr"]) - 15.0) < 1e-12, f"iqr: {tags['iqr']}"

    assert abs(float(tags["cgl_var"]) - 1.0) < 1e-12, f"cgl_var: {tags['cgl_var']}"

    print("PASS: moss.statistics verified against catastrophic cancellation & Method 7 references.")
    return True


def main():
    print("=" * 70)
    print("MOSS PHASE 24 TEAM N — ADVERSARIAL REFERENCE ORACLES & FALSIFICATION")
    print("=" * 70)

    ok1 = test_linalg_oracles()
    ok2 = test_sparse_oracles()
    ok3 = test_statistics_oracles()

    if ok1 and ok2 and ok3:
        print("\nALL ADVERSARIAL ORACLES PASSED WITH 0 FALSIFICATIONS.")
        sys.exit(0)
    else:
        print("\nADVERSARIAL ORACLES DETECTED DEFECTS.")
        sys.exit(1)


if __name__ == "__main__":
    main()
