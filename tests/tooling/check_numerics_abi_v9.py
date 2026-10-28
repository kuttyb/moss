#!/usr/bin/env python3
"""
check_numerics_abi_v9.py
========================
Exercise moss.linalg, moss.sparse, and moss.statistics from compiled-only
ABI-v9 provider artifacts without source files in MOSS_MODULE_PATH.

Phase 24 Team N (Numerical Computing)
"""

import json
import os
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[2]
MOSS_BIN = REPO_ROOT / "moss"
MARGO_BIN = REPO_ROOT / "margo"
SCRATCH_DIR = REPO_ROOT / "tmp"
SCRATCH_DIR.mkdir(exist_ok=True)

PACKAGES = {
    "math": REPO_ROOT / "packages" / "math",
    "linalg": REPO_ROOT / "packages" / "linalg",
    "sparse": REPO_ROOT / "packages" / "sparse",
    "statistics": REPO_ROOT / "packages" / "statistics",
}

ENV = os.environ.copy()
ENV.update(
    MOSS=str(MOSS_BIN),
    MARGO_HOME=str(SCRATCH_DIR / "margo-numerics-abi-v9"),
    TMPDIR=str(SCRATCH_DIR),
)


def build_packages():
    print("Building numerical packages...")
    for name, pkg_path in PACKAGES.items():
        cmd = [str(MARGO_BIN), "build", "--json"]
        res = subprocess.run(cmd, cwd=pkg_path, env=ENV, capture_output=True, text=True)
        assert res.returncode == 0, f"Failed to build {name}:\n{res.stdout}\n{res.stderr}"


def run_source_free_test():
    build_packages()

    with tempfile.TemporaryDirectory(prefix="numerics-abi-v9-", dir=SCRATCH_DIR) as temp_dir:
        work_dir = Path(temp_dir)
        provider_dir = work_dir / "provider"
        app_dir = work_dir / "app"
        provider_dir.mkdir(parents=True, exist_ok=True)
        (app_dir / "src").mkdir(parents=True, exist_ok=True)

        # Copy compiled .mossi and .rlib artifacts ONLY
        for name, pkg_path in PACKAGES.items():
            debug_dir = pkg_path / "build" / "debug"
            mossi_file = debug_dir / f"{name}.mossi"
            # Note: rlib can be named lib{name}.rlib or libmoss_{name}.rlib
            rlib_candidates = list(debug_dir.glob("*.rlib"))
            assert mossi_file.is_file(), f"Missing {mossi_file}"
            assert rlib_candidates, f"Missing rlib for {name}"

            shutil.copy2(mossi_file, provider_dir / f"{name}.mossi")
            for rlib in rlib_candidates:
                shutil.copy2(rlib, provider_dir / rlib.name)

        # Verify ABI-v9 contract in header
        for name in ("linalg", "sparse", "statistics"):
            mossi_content = (provider_dir / f"{name}.mossi").read_text(encoding="utf-8")
            assert "native_abi 9" in mossi_content, f"{name}.mossi does not declare native_abi 9"
            print(f"Verified {name}.mossi declares native_abi 9")

        # Create consumer application
        (app_dir / "Moss.toml").write_text(
            '[package]\nname = "numerics_abi_v9_smoke"\nversion = "0.1.0"\n\n'
            '[build]\nsource = "src"\n',
            encoding="utf-8",
        )

        app_src = """module app
import linalg
import sparse
import statistics

fn test_linalg():
  try:
    let a = linalg.from_vector(2, 2, [3.0, 1.0, 1.0, 2.0])
    let b = [9.0, 8.0]
    let x = linalg.solve(a, b)
    assertEqual(x[0], 2.0)
    assertEqual(x[1], 3.0)
    echo "linalg source-free solve ok"
  recover linalg.LinalgError.InvalidDimensions:
    echo "ERROR:InvalidDimensions"
  recover linalg.LinalgError.DimensionMismatch:
    echo "ERROR:DimensionMismatch"
  recover linalg.LinalgError.NonSquareMatrix:
    echo "ERROR:NonSquareMatrix"
  recover linalg.LinalgError.SingularMatrix:
    echo "ERROR:SingularMatrix"
  recover linalg.LinalgError.IndexOutOfBounds:
    echo "ERROR:IndexOutOfBounds"

fn test_sparse():
  try:
    var triplets = [sparse.Triplet(row: 0, col: 0, value: 5.0), sparse.Triplet(row: 1, col: 1, value: 7.0)]
    let sp = sparse.from_triplets(2, 2, triplets)
    let y = sparse.csr_mat_vec_mul(sp, [2.0, 3.0])
    assertEqual(y[0], 10.0)
    assertEqual(y[1], 21.0)
    echo "sparse source-free SpMV ok"
  recover sparse.SparseError.InvalidDimensions:
    echo "ERROR:SparseInvalidDimensions"
  recover sparse.SparseError.DimensionMismatch:
    echo "ERROR:SparseDimensionMismatch"
  recover sparse.SparseError.IndexOutOfBounds:
    echo "ERROR:SparseIndexOutOfBounds"

fn test_statistics():
  try:
    let data = [10.0, 20.0, 30.0, 40.0]
    let m = statistics.mean(data)
    assertEqual(m, 25.0)
    let q = statistics.quantile(data, 0.5)
    assertEqual(q, 25.0)
    var s = statistics.new_running_stats()
    statistics.running_update(s, 10.0)
    statistics.running_update(s, 20.0)
    assertEqual(statistics.running_count(s), 2)
    assertEqual(statistics.running_mean(s), 15.0)
    echo "statistics source-free ok"
  recover statistics.StatsError.EmptyInput:
    echo "ERROR:StatsEmptyInput"
  recover statistics.StatsError.InsufficientData:
    echo "ERROR:StatsInsufficientData"
  recover statistics.StatsError.DimensionMismatch:
    echo "ERROR:StatsDimensionMismatch"
  recover statistics.StatsError.InvalidQuantile:
    echo "ERROR:StatsInvalidQuantile"
  recover statistics.StatsError.InvalidWeight:
    echo "ERROR:StatsInvalidWeight"
  recover statistics.StatsError.ZeroVariance:
    echo "ERROR:StatsZeroVariance"
  recover statistics.StatsError.InvalidValue:
    echo "ERROR:StatsInvalidValue"

fn main():
  test_linalg()
  test_sparse()
  test_statistics()
  echo "ALL NUMERICS ABI-V9 SOURCE-FREE TESTS OK"
"""
        (app_dir / "src" / "main.moss").write_text(app_src, encoding="utf-8")

        # Compile and check consumer WITHOUT package source files
        source_free_env = ENV | {"MOSS_MODULE_PATH": str(provider_dir)}

        print("Checking consumer against compiled-only providers with moss check...")
        chk = subprocess.run(
            [str(MOSS_BIN), "check", "src/main.moss", "--json"],
            cwd=app_dir,
            env=source_free_env,
            capture_output=True,
            text=True,
        )
        assert chk.returncode == 0, f"moss check failed:\n{chk.stdout}\n{chk.stderr}"
        chk_json = json.loads(chk.stdout)
        assert chk_json.get("ok") is True, f"Check ok!=True: {chk_json}"

        print("Building consumer against compiled-only providers with moss build...")
        bld = subprocess.run(
            [str(MARGO_BIN), "build", "--json"],
            cwd=app_dir,
            env=source_free_env,
            capture_output=True,
            text=True,
        )
        assert bld.returncode == 0, f"moss build failed:\n{bld.stdout}\n{bld.stderr}"

        exe_path = app_dir / "build" / "debug" / "numerics_abi_v9_smoke"
        print("Running consumer executable...")
        run_res = subprocess.run(
            [str(exe_path)],
            cwd=app_dir,
            env=source_free_env,
            capture_output=True,
            text=True,
        )
        assert run_res.returncode == 0, f"Executable failed:\n{run_res.stdout}\n{run_res.stderr}"
        expected_output = (
            "linalg source-free solve ok\n"
            "sparse source-free SpMV ok\n"
            "statistics source-free ok\n"
            "ALL NUMERICS ABI-V9 SOURCE-FREE TESTS OK\n"
        )
        assert run_res.stdout == expected_output, f"Unexpected output:\n{run_res.stdout}"
        print("SUCCESS: Compiled-only ABI-v9 consumer test executed with 100% parity!")


def main():
    print("=" * 70)
    print("MOSS PHASE 24 TEAM N: COMPILED-ONLY ABI-V9 CONSUMER ACCEPTANCE")
    print("=" * 70)
    run_source_free_test()
    print("=" * 70)
    print("ABI-V9 ACCEPTANCE PASSED")
    print("=" * 70)


if __name__ == "__main__":
    main()
