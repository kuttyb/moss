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
import random
import subprocess
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[2]
MARGO_BIN = REPO_ROOT / "margo"
HARNESS_DIR = REPO_ROOT / "tmp" / "adversarial_numerics_harness"

RECOVER_ALL = """  recover linalg.LinalgError.InvalidDimensions:
    echo "ERROR:InvalidDimensions"
  recover linalg.LinalgError.DimensionMismatch:
    echo "ERROR:DimensionMismatch"
  recover linalg.LinalgError.NonSquareMatrix:
    echo "ERROR:NonSquareMatrix"
  recover linalg.LinalgError.SingularMatrix:
    echo "ERROR:SingularMatrix"
  recover linalg.LinalgError.IndexOutOfBounds:
    echo "ERROR:IndexOutOfBounds"
  recover sparse.SparseError.InvalidDimensions:
    echo "ERROR:SparseInvalidDimensions"
  recover sparse.SparseError.DimensionMismatch:
    echo "ERROR:SparseDimensionMismatch"
  recover sparse.SparseError.IndexOutOfBounds:
    echo "ERROR:SparseIndexOutOfBounds"
  recover statistics.StatsError.EmptyInput:
    echo "ERROR:EmptyInput"
  recover statistics.StatsError.InsufficientData:
    echo "ERROR:InsufficientData"
  recover statistics.StatsError.DimensionMismatch:
    echo "ERROR:StatsDimensionMismatch"
  recover statistics.StatsError.InvalidQuantile:
    echo "ERROR:InvalidQuantile"
  recover statistics.StatsError.InvalidWeight:
    echo "ERROR:InvalidWeight"
  recover statistics.StatsError.ZeroVariance:
    echo "ERROR:ZeroVariance"
  recover statistics.StatsError.InvalidValue:
    echo "ERROR:InvalidValue"
"""


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


def run_moss_probe(moss_code: str, timeout: int = 60) -> tuple[int, str, str]:
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


# ==============================================================================
# SECTION 1: Dense Linear Algebra Differential Tests
# ==============================================================================

def test_random_rectangular_dense_matmul_and_matvec():
    print("Testing random rectangular dense mat_mul and mat_vec_mul...")
    rng = random.Random(42)

    # Rectangular matrix multiplication: A (4x7) * B (7x5) = C (4x5)
    m, k, n = 4, 7, 5
    a_data = [round(rng.uniform(-5.0, 5.0), 3) for _ in range(m * k)]
    b_data = [round(rng.uniform(-5.0, 5.0), 3) for _ in range(k * n)]

    # Reference Python mat_mul
    ref_c = [0.0] * (m * n)
    for i in range(m):
        for j in range(n):
            s = 0.0
            for p in range(k):
                s += a_data[i * k + p] * b_data[p * n + j]
            ref_c[i * n + j] = s

    # Reference mat_vec_mul: A (4x7) * x (7) = y (4)
    x_data = [round(rng.uniform(-5.0, 5.0), 3) for _ in range(k)]
    ref_y = [0.0] * m
    for i in range(m):
        s = 0.0
        for p in range(k):
            s += a_data[i * k + p] * x_data[p]
        ref_y[i] = s

    probe = f"""module main
import linalg
import sparse
import statistics
import math

fn main():
  try:
    let a = linalg.from_vector({m}, {k}, {a_data})
    let b = linalg.from_vector({k}, {n}, {b_data})
    let c = linalg.mat_mul(a, b)
    var max_err_c = 0.0
    let ref_c = {ref_c}
    var i = 0
    while i < {m}:
      var j = 0
      while j < {n}:
        let diff = math.abs(linalg.matrix_get(c, i, j) - ref_c[i * {n} + j])
        if diff > max_err_c:
          max_err_c = diff
        j = j + 1
      i = i + 1
    echo "TAG:max_err_c:", max_err_c

    let x = {x_data}
    let y = linalg.mat_vec_mul(a, x)
    let ref_y = {ref_y}
    var max_err_y = 0.0
    var r = 0
    while r < {m}:
      let diff_y = math.abs(y[r] - ref_y[r])
      if diff_y > max_err_y:
        max_err_y = diff_y
      r = r + 1
    echo "TAG:max_err_y:", max_err_y
{RECOVER_ALL}"""
    code, stdout, stderr = run_moss_probe(probe)
    assert code == 0, f"Probe failed:\n{stdout}\n{stderr}"
    tags = parse_tags(stdout)
    max_err_c = float(tags["max_err_c"])
    max_err_y = float(tags["max_err_y"])
    assert max_err_c < 1e-10, f"mat_mul error too large: {max_err_c}"
    assert max_err_y < 1e-10, f"mat_vec_mul error too large: {max_err_y}"
    print(f"  PASS: mat_mul max error={max_err_c:.2e}, mat_vec_mul max error={max_err_y:.2e}")


def test_lu_solve_residuals_and_singularities():
    print("Testing LU solve residuals across sizes, ill-conditioned, and singular matrices...")
    # Test 1: Random 4x4 well-conditioned matrix
    rng = random.Random(1337)
    n = 4
    # Diagonally dominant to ensure non-singularity
    a_mat = []
    for i in range(n):
        row = [round(rng.uniform(-2.0, 2.0), 3) for _ in range(n)]
        row[i] += 10.0 * (1 if row[i] >= 0 else -1)
        a_mat.extend(row)
    b_vec = [round(rng.uniform(-5.0, 5.0), 3) for _ in range(n)]

    # Test 2: Singular matrix (linearly dependent rows: row 2 = row 0 + row 1)
    a_sing = [
        1.0, 2.0, 3.0,
        4.0, 5.0, 6.0,
        5.0, 7.0, 9.0
    ]
    b_sing = [1.0, 1.0, 1.0]

    # Test 3: Hilbert 3x3 matrix (ill-conditioned: cond ~ 500)
    a_hilbert = [
        1.0, 1.0/2.0, 1.0/3.0,
        1.0/2.0, 1.0/3.0, 1.0/4.0,
        1.0/3.0, 1.0/4.0, 1.0/5.0
    ]
    b_hilbert = [1.0, 0.0, 0.0]

    probe = f"""module main
import linalg
import sparse
import statistics
import math

fn run_well_cond():
  try:
    let a = linalg.from_vector({n}, {n}, {a_mat})
    let b = {b_vec}
    let x = linalg.solve(a, b)
    let ax = linalg.mat_vec_mul(a, x)
    var max_res = 0.0
    var i = 0
    while i < {n}:
      let diff = math.abs(ax[i] - b[i])
      if diff > max_res:
        max_res = diff
      i = i + 1
    echo "TAG:well_cond_res:", max_res
  recover linalg.LinalgError.SingularMatrix:
    echo "TAG:well_cond_res:", -1.0
  recover linalg.LinalgError.InvalidDimensions:
    echo "TAG:well_cond_res:", -1.0
  recover linalg.LinalgError.DimensionMismatch:
    echo "TAG:well_cond_res:", -1.0
  recover linalg.LinalgError.NonSquareMatrix:
    echo "TAG:well_cond_res:", -1.0
  recover linalg.LinalgError.IndexOutOfBounds:
    echo "TAG:well_cond_res:", -1.0

fn run_sing():
  try:
    let a_s = linalg.from_vector(3, 3, {a_sing})
    let b_s = {b_sing}
    let x_s = linalg.solve(a_s, b_s)
    echo "TAG:sing_detected:false"
  recover linalg.LinalgError.SingularMatrix:
    echo "TAG:sing_detected:true"
  recover linalg.LinalgError.InvalidDimensions:
    echo "TAG:sing_detected:false"
  recover linalg.LinalgError.DimensionMismatch:
    echo "TAG:sing_detected:false"
  recover linalg.LinalgError.NonSquareMatrix:
    echo "TAG:sing_detected:false"
  recover linalg.LinalgError.IndexOutOfBounds:
    echo "TAG:sing_detected:false"

fn run_hilbert():
  try:
    let a_h = linalg.from_vector(3, 3, {a_hilbert})
    let b_h = {b_hilbert}
    let x_h = linalg.solve(a_h, b_h)
    let ax_h = linalg.mat_vec_mul(a_h, x_h)
    var max_res_h = 0.0
    var j = 0
    while j < 3:
      let diff_h = math.abs(ax_h[j] - b_h[j])
      if diff_h > max_res_h:
        max_res_h = diff_h
      j = j + 1
    echo "TAG:hilbert_res:", max_res_h
  recover linalg.LinalgError.SingularMatrix:
    echo "TAG:hilbert_res:", -1.0
  recover linalg.LinalgError.InvalidDimensions:
    echo "TAG:hilbert_res:", -1.0
  recover linalg.LinalgError.DimensionMismatch:
    echo "TAG:hilbert_res:", -1.0
  recover linalg.LinalgError.NonSquareMatrix:
    echo "TAG:hilbert_res:", -1.0
  recover linalg.LinalgError.IndexOutOfBounds:
    echo "TAG:hilbert_res:", -1.0

fn main():
  run_well_cond()
  run_sing()
  run_hilbert()
"""
    code, stdout, stderr = run_moss_probe(probe)
    assert code == 0, f"Probe failed:\n{stdout}\n{stderr}"
    tags = parse_tags(stdout)
    well_cond_res = float(tags["well_cond_res"])
    assert well_cond_res < 1e-10, f"LU solve residual too large: {well_cond_res}"
    assert tags["sing_detected"] == "true", "Failed to detect singular matrix"
    hilbert_res = float(tags["hilbert_res"])
    assert hilbert_res < 1e-8, f"Hilbert solve residual too large: {hilbert_res}"
    print(f"  PASS: well-conditioned residual={well_cond_res:.2e}, singular detected=True, Hilbert residual={hilbert_res:.2e}")


# ==============================================================================
# SECTION 2: Sparse Matrix Differential Tests
# ==============================================================================

def test_sparse_randomized_triplets_and_roundtrips():
    print("Testing randomized CSR/CSC roundtrips, duplicate summation, SpMV, and SpMM...")
    rng = random.Random(999)
    rows, cols = 5, 6
    num_triplets = 25
    triplets_dict = {}
    triplets_list = []

    for _ in range(num_triplets):
        r = rng.randint(0, rows - 1)
        c = rng.randint(0, cols - 1)
        val = round(rng.uniform(-3.0, 3.0), 2)
        triplets_list.append((r, c, val))
        triplets_dict[(r, c)] = triplets_dict.get((r, c), 0.0) + val

    moss_triplets_code = ", ".join(
        f"sparse.Triplet(row: {r}, col: {c}, value: {v})" for r, c, v in triplets_list
    )

    # Reference SpMV vector x (length 6)
    x_vec = [round(rng.uniform(-2.0, 2.0), 2) for _ in range(cols)]
    ref_spmv = [0.0] * rows
    for (r, c), v in triplets_dict.items():
        ref_spmv[r] += v * x_vec[c]

    probe = f"""module main
import linalg
import sparse
import statistics
import math

fn main():
  try:
    var triplets = [{moss_triplets_code}]
    let csr = sparse.from_triplets({rows}, {cols}, triplets)
    echo "TAG:valid_csr:", sparse.validate_csr(csr)

    # Check additive duplicate summation
    var sum_err = 0.0
"""
    for (r, c), expected_val in triplets_dict.items():
        probe += f"""    let val_{r}_{c} = sparse.csr_get(csr, {r}, {c})
    let diff_{r}_{c} = math.abs(val_{r}_{c} - {expected_val})
    if diff_{r}_{c} > sum_err:
      sum_err = diff_{r}_{c}
"""
    probe += f"""    echo "TAG:dup_sum_err:", sum_err

    # Check SpMV
    let x = {x_vec}
    let y = sparse.csr_mat_vec_mul(csr, x)
    let ref_y = {ref_spmv}
    var max_spmv_err = 0.0
    var r = 0
    while r < {rows}:
      let d = math.abs(y[r] - ref_y[r])
      if d > max_spmv_err:
        max_spmv_err = d
      r = r + 1
    echo "TAG:spmv_err:", max_spmv_err

    # Check CSC conversion and roundtrip equivalence
    let csc = sparse.csr_to_csc(csr)
    echo "TAG:valid_csc:", sparse.validate_csc(csc)
    let csr_roundtrip = sparse.csc_to_csr(csc)
    var csc_match = true
    var ir = 0
    while ir < {rows}:
      var ic = 0
      while ic < {cols}:
        let v_csr = sparse.csr_get(csr, ir, ic)
        let v_csc = sparse.csr_get(csr_roundtrip, ir, ic)
        if math.abs(v_csr - v_csc) > 1.0e-12:
          csc_match = false
        ic = ic + 1
      ir = ir + 1
    echo "TAG:csc_match:", csc_match
{RECOVER_ALL}"""
    code, stdout, stderr = run_moss_probe(probe)
    assert code == 0, f"Probe failed:\n{stdout}\n{stderr}"
    tags = parse_tags(stdout)
    assert tags["valid_csr"] == "true", "CSR validation failed"
    dup_sum_err = float(tags["dup_sum_err"])
    assert dup_sum_err < 1e-10, f"Duplicate summation error: {dup_sum_err}"
    spmv_err = float(tags["spmv_err"])
    assert spmv_err < 1e-10, f"SpMV error: {spmv_err}"
    assert tags["valid_csc"] == "true", "CSC validation failed"
    assert tags["csc_match"] == "true", "CSC get did not match CSR get"
    print(f"  PASS: dup summation error={dup_sum_err:.2e}, SpMV error={spmv_err:.2e}, CSC roundtrip match=True")


# ==============================================================================
# SECTION 3: Statistics Streaming vs Batch Differential Tests
# ==============================================================================

def test_statistics_streaming_vs_batch_partitions():
    print("Testing streaming vs batch statistics across varied partition sizes...")
    rng = random.Random(777)
    n = 120
    data = [round(rng.gauss(100.0, 15.0), 3) for _ in range(n)]

    # Compute batch reference in Python
    ref_mean = sum(data) / n
    ref_sample_var = sum((x - ref_mean) ** 2 for x in data) / (n - 1)

    # 3 partitions of lengths 40, 50, 30
    p1 = data[:40]
    p2 = data[40:90]
    p3 = data[90:]

    probe = f"""module main
import linalg
import sparse
import statistics
import math

fn main():
  try:
    let all_data = {data}
    let b_mean = statistics.mean(all_data)
    let b_var = statistics.variance(all_data, true)
    echo "TAG:batch_mean_diff:", math.abs(b_mean - {ref_mean})
    echo "TAG:batch_var_diff:", math.abs(b_var - {ref_sample_var})

    # Partition 1
    var s1 = statistics.new_running_stats()
    let p1 = {p1}
    var i = 0
    while i < (p1 |> count):
      statistics.running_update(s1, p1[i])
      i = i + 1

    # Partition 2
    var s2 = statistics.new_running_stats()
    let p2 = {p2}
    i = 0
    while i < (p2 |> count):
      statistics.running_update(s2, p2[i])
      i = i + 1

    # Partition 3
    var s3 = statistics.new_running_stats()
    let p3 = {p3}
    i = 0
    while i < (p3 |> count):
      statistics.running_update(s3, p3[i])
      i = i + 1

    # Merge s1 + s2, then + s3
    let s12 = statistics.running_combine(s1, s2)
    let s123 = statistics.running_combine(s12, s3)

    let stream_mean = statistics.running_mean(s123)
    let stream_var = statistics.running_variance(s123, true)

    echo "TAG:stream_batch_mean_diff:", math.abs(stream_mean - b_mean)
    echo "TAG:stream_batch_var_diff:", math.abs(stream_var - b_var)
{RECOVER_ALL}"""
    code, stdout, stderr = run_moss_probe(probe)
    assert code == 0, f"Probe failed:\n{stdout}\n{stderr}"
    tags = parse_tags(stdout)
    batch_mean_diff = float(tags["batch_mean_diff"])
    batch_var_diff = float(tags["batch_var_diff"])
    stream_batch_mean_diff = float(tags["stream_batch_mean_diff"])
    stream_batch_var_diff = float(tags["stream_batch_var_diff"])

    assert batch_mean_diff < 1e-11, f"Batch mean diff: {batch_mean_diff}"
    assert batch_var_diff < 1e-10, f"Batch var diff: {batch_var_diff}"
    assert stream_batch_mean_diff < 1e-11, f"Streaming-batch mean diff: {stream_batch_mean_diff}"
    assert stream_batch_var_diff < 1e-10, f"Streaming-batch var diff: {stream_batch_var_diff}"
    print(f"  PASS: batch diff={batch_var_diff:.2e}, 3-partition streaming combine diff={stream_batch_var_diff:.2e}")


def test_quantiles_hyndman_fan_method7():
    print("Testing quantiles against Hyndman-Fan Method 7 and extreme finite values...")
    rng = random.Random(555)
    data = [round(rng.uniform(1.0, 100.0), 3) for _ in range(25)]
    sorted_data = sorted(data)
    n = len(sorted_data)

    # Reference Hyndman-Fan Method 7
    def ref_quantile(q):
        pos = (n - 1) * q
        idx = int(pos)
        f = pos - idx
        if idx >= n - 1:
            return sorted_data[-1]
        return (1.0 - f) * sorted_data[idx] + f * sorted_data[idx + 1]

    qs = [0.0, 0.1, 0.25, 0.5, 0.75, 0.9, 1.0]
    ref_vals = {f"q_{int(q*100)}": ref_quantile(q) for q in qs}

    probe = f"""module main
import linalg
import sparse
import statistics
import math

fn main():
  try:
    let d = {data}
"""
    for q in qs:
        tag = f"q_{int(q*100)}"
        probe += f"""    let val_{tag} = statistics.quantile(d, {q})
    echo "TAG:{tag}:", val_{tag}
"""
    probe += f"""    # Extreme finite quantile overflow test: [-1e308, 1e308]
    let extreme = [-1.0e308, 1.0e308]
    let q_ext = statistics.quantile(extreme, 0.5)
    echo "TAG:q_extreme_median:", q_ext
{RECOVER_ALL}"""
    code, stdout, stderr = run_moss_probe(probe)
    assert code == 0, f"Probe failed:\n{stdout}\n{stderr}"
    tags = parse_tags(stdout)

    for q in qs:
        tag = f"q_{int(q*100)}"
        moss_val = float(tags[tag])
        expected = ref_vals[tag]
        assert abs(moss_val - expected) < 1e-10, f"Mismatch at q={q}: {moss_val} vs {expected}"

    q_extreme = float(tags["q_extreme_median"])
    assert abs(q_extreme - 0.0) < 1e-10, f"Extreme median overflow: {q_extreme}"
    print(f"  PASS: All 7 quantiles matched Method 7; extreme [-1e308, 1e308] median={q_extreme}")


# ==============================================================================
# SECTION 4: Nonfinite-Value and Invalid-Input Contracts
# ==============================================================================

def test_nonfinite_and_invalid_input_contracts():
    print("Testing IEEE 754 non-finite contracts and malformed record validation...")

    probe = """module main
import linalg
import sparse
import statistics
import math

fn test_mat_mul_ieee():
  try:
    let a_zero = linalg.from_vector(1, 1, [0.0])
    let b_inf = linalg.from_vector(1, 1, [math.infinity()])
    let b_nan = linalg.from_vector(1, 1, [math.nan()])
    let prod_inf = linalg.mat_mul(a_zero, b_inf)
    let prod_nan = linalg.mat_mul(a_zero, b_nan)
    echo "TAG:prod_inf_nan:", math.is_nan(linalg.matrix_get(prod_inf, 0, 0))
    echo "TAG:prod_nan_nan:", math.is_nan(linalg.matrix_get(prod_nan, 0, 0))
  recover linalg.LinalgError.InvalidDimensions:
    echo "TAG:prod_inf_nan:error"
  recover linalg.LinalgError.DimensionMismatch:
    echo "TAG:prod_inf_nan:error"
  recover linalg.LinalgError.NonSquareMatrix:
    echo "TAG:prod_inf_nan:error"
  recover linalg.LinalgError.SingularMatrix:
    echo "TAG:prod_inf_nan:error"
  recover linalg.LinalgError.IndexOutOfBounds:
    echo "TAG:prod_inf_nan:error"

fn test_bad_matrix():
  try:
    let bad_m = linalg.Matrix(rows: 2, cols: 2, data: [1.0, 2.0])
    let g = linalg.matrix_get(bad_m, 0, 0)
    echo "TAG:bad_matrix_raised:false"
  recover linalg.LinalgError.InvalidDimensions:
    echo "TAG:bad_matrix_raised:true"
  recover linalg.LinalgError.DimensionMismatch:
    echo "TAG:bad_matrix_raised:false"
  recover linalg.LinalgError.NonSquareMatrix:
    echo "TAG:bad_matrix_raised:false"
  recover linalg.LinalgError.SingularMatrix:
    echo "TAG:bad_matrix_raised:false"
  recover linalg.LinalgError.IndexOutOfBounds:
    echo "TAG:bad_matrix_raised:false"

fn test_bad_csr():
  try:
    let bad_csr = sparse.CsrMatrix(rows: 2, cols: 2, row_ptrs: [0, 1], col_indices: [0], values: [1.0])
    let sv = sparse.csr_get(bad_csr, 0, 0)
    echo "TAG:bad_csr_raised:false"
  recover sparse.SparseError.InvalidDimensions:
    echo "TAG:bad_csr_raised:true"
  recover sparse.SparseError.DimensionMismatch:
    echo "TAG:bad_csr_raised:false"
  recover sparse.SparseError.IndexOutOfBounds:
    echo "TAG:bad_csr_raised:false"

fn test_nan_update():
  try:
    var s = statistics.new_running_stats()
    statistics.running_update(s, math.nan())
    echo "TAG:nan_update_rejected:false"
  recover statistics.StatsError.InvalidValue:
    echo "TAG:nan_update_rejected:true"
  recover statistics.StatsError.EmptyInput:
    echo "TAG:nan_update_rejected:false"
  recover statistics.StatsError.InsufficientData:
    echo "TAG:nan_update_rejected:false"
  recover statistics.StatsError.DimensionMismatch:
    echo "TAG:nan_update_rejected:false"
  recover statistics.StatsError.InvalidQuantile:
    echo "TAG:nan_update_rejected:false"
  recover statistics.StatsError.InvalidWeight:
    echo "TAG:nan_update_rejected:false"
  recover statistics.StatsError.ZeroVariance:
    echo "TAG:nan_update_rejected:false"

fn test_kahan_inf():
  try:
    let v_inf_sum = [1.0e10, math.infinity(), 2.0e10]
    let sum_inf = statistics.kahan_sum(v_inf_sum)
    echo "TAG:kahan_inf_resilience:", math.is_infinite(sum_inf)
  recover statistics.StatsError.EmptyInput:
    echo "TAG:kahan_inf_resilience:false"
  recover statistics.StatsError.InsufficientData:
    echo "TAG:kahan_inf_resilience:false"
  recover statistics.StatsError.DimensionMismatch:
    echo "TAG:kahan_inf_resilience:false"
  recover statistics.StatsError.InvalidQuantile:
    echo "TAG:kahan_inf_resilience:false"
  recover statistics.StatsError.InvalidWeight:
    echo "TAG:kahan_inf_resilience:false"
  recover statistics.StatsError.ZeroVariance:
    echo "TAG:kahan_inf_resilience:false"
  recover statistics.StatsError.InvalidValue:
    echo "TAG:kahan_inf_resilience:false"

fn main():
  test_mat_mul_ieee()

  # Vector norm with infinity and NaN
  let v_inf = [1.0, math.infinity(), -2.0]
  let v_nan = [1.0, math.nan(), -2.0]
  echo "TAG:v_inf_norm_inf:", math.is_infinite(linalg.vec_norm(v_inf))
  echo "TAG:v_nan_norm_nan:", math.is_nan(linalg.vec_norm(v_nan))

  test_bad_matrix()
  test_bad_csr()
  test_nan_update()
  test_kahan_inf()
"""
    code, stdout, stderr = run_moss_probe(probe)
    assert code == 0, f"Probe failed:\n{stdout}\n{stderr}"
    tags = parse_tags(stdout)

    assert tags["prod_inf_nan"] == "true", "0.0 * inf did not yield NaN in mat_mul"
    assert tags["prod_nan_nan"] == "true", "0.0 * NaN did not yield NaN in mat_mul"
    assert tags["v_inf_norm_inf"] == "true", "vec_norm([inf]) did not return inf"
    assert tags["v_nan_norm_nan"] == "true", "vec_norm([nan]) did not return nan"
    assert tags["bad_matrix_raised"] == "true", "Direct malformed Matrix did not raise InvalidDimensions"
    assert tags["bad_csr_raised"] == "true", "Direct malformed CsrMatrix did not raise InvalidDimensions"
    assert tags["nan_update_rejected"] == "true", "running_update(nan) did not raise InvalidValue"
    assert tags["kahan_inf_resilience"] == "true", "kahan_sum with inf did not return inf"

    print("  PASS: IEEE non-finite contracts, NaN rejections, and malformed record validation verified.")


# ==============================================================================
# MAIN RUNNER
# ==============================================================================

def main():
    print("=" * 75)
    print("MOSS PHASE 24 TEAM N — ADVERSARIAL REFERENCE ORACLES & FALSIFICATION")
    print("=" * 75)

    test_random_rectangular_dense_matmul_and_matvec()
    test_lu_solve_residuals_and_singularities()
    test_sparse_randomized_triplets_and_roundtrips()
    test_statistics_streaming_vs_batch_partitions()
    test_quantiles_hyndman_fan_method7()
    test_nonfinite_and_invalid_input_contracts()

    print("=" * 75)
    print("ALL ADVERSARIAL DIFFERENTIAL ORACLES PASSED WITH 0 FALSIFICATIONS.")
    print("=" * 75)
    sys.exit(0)


if __name__ == "__main__":
    main()
