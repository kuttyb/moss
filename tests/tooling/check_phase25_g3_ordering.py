#!/usr/bin/env python3
"""Phase 25 G3 Test Suite: Total Ordering, Equality, Hashing, Map[Float, V], sort, and Reverse."""

import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]
MOSS = Path(sys.argv[1] if len(sys.argv) > 1 else "./moss").resolve()
MARGO = ROOT / "margo"
SCRATCH = ROOT / "tmp"
SCRATCH.mkdir(exist_ok=True)


def run_cmd(cmd, cwd=ROOT):
    return subprocess.run(cmd, cwd=cwd, capture_output=True, text=True)


def run_code_in_all_modes(moss_code, test_name):
    """Execute code in Fast Debug, Native Debug, and Native Release."""
    with tempfile.NamedTemporaryFile("w", suffix=".moss", dir=SCRATCH, delete=False) as f:
        f.write(moss_code)
        p = f.name
    try:
        # 1) Fast Debug
        res_interp = run_cmd([str(MOSS), "run", "--interp", p])
        assert res_interp.returncode == 0, (
            f"[{test_name}] Fast Debug failed (code {res_interp.returncode}):\n{res_interp.stderr}\nStdout:\n{res_interp.stdout}"
        )
        interp_out = res_interp.stdout

        # 2) Native Debug
        rs_debug = str(Path(p).with_suffix(".debug.rs"))
        bin_debug = str(Path(p).with_suffix(".debug.bin"))
        res_gen_dbg = run_cmd([str(MOSS), p, "--debug", "-o", rs_debug])
        assert res_gen_dbg.returncode == 0, (
            f"[{test_name}] moss --debug code generation failed:\n{res_gen_dbg.stderr}"
        )
        res_rustc_dbg = run_cmd(["rustc", "--crate-name", "test_crate", rs_debug, "-o", bin_debug])
        assert res_rustc_dbg.returncode == 0, (
            f"[{test_name}] rustc debug compilation failed:\n{res_rustc_dbg.stderr}"
        )
        res_run_dbg = run_cmd([bin_debug])
        assert res_run_dbg.returncode == 0, (
            f"[{test_name}] Native Debug execution failed:\n{res_run_dbg.stderr}"
        )
        dbg_out = res_run_dbg.stdout

        # 3) Native Release
        rs_rel = str(Path(p).with_suffix(".release.rs"))
        bin_rel = str(Path(p).with_suffix(".release.bin"))
        res_gen_rel = run_cmd([str(MOSS), p, "-O", "-o", rs_rel])
        assert res_gen_rel.returncode == 0, (
            f"[{test_name}] moss -O code generation failed:\n{res_gen_rel.stderr}"
        )
        res_rustc_rel = run_cmd(["rustc", "-O", "--crate-name", "test_crate", rs_rel, "-o", bin_rel])
        assert res_rustc_rel.returncode == 0, (
            f"[{test_name}] rustc release compilation failed:\n{res_rustc_rel.stderr}"
        )
        res_run_rel = run_cmd([bin_rel])
        assert res_run_rel.returncode == 0, (
            f"[{test_name}] Native Release execution failed:\n{res_run_rel.stderr}"
        )
        rel_out = res_run_rel.stdout

        # Assert stdout agreement
        assert interp_out == dbg_out == rel_out, (
            f"[{test_name}] Output mismatch across engines:\n"
            f"  Fast Debug:     {interp_out!r}\n"
            f"  Native Debug:   {dbg_out!r}\n"
            f"  Native Release: {rel_out!r}"
        )
    finally:
        for ext in (".moss", ".debug.rs", ".debug.bin", ".release.rs", ".release.bin"):
            Path(p).with_suffix(ext).unlink(missing_ok=True)


def test_float_total_ordering_laws():
    print("Testing Float total ordering axioms (Julia isless/isequal/hash)...")
    code = """
fn main():
  nan = 0.0 / 0.0
  pos_zero = 0.0
  neg_zero = -0.0
  inf_pos = 1.0 / 0.0
  inf_neg = (0.0 - 1.0) / 0.0
  
  # 1. Total order between -0.0 and +0.0
  assertEqual(less(neg_zero, pos_zero), true)
  assertEqual(less(pos_zero, neg_zero), false)
  assertEqual(isequal(neg_zero, pos_zero), false)
  assertEqual(isequal(neg_zero, neg_zero), true)
  assertEqual(isequal(pos_zero, pos_zero), true)
  assertEqual(hash(neg_zero) != hash(pos_zero), true)
  
  # 2. Finite < NaN and NaN total equality
  assertEqual(less(pos_zero, nan), true)
  assertEqual(less(inf_pos, nan), true)
  assertEqual(less(1000000000.0, nan), true)
  assertEqual(less(inf_neg, nan), true)
  assertEqual(less(nan, inf_pos), false)
  assertEqual(less(nan, nan), false)
  assertEqual(isequal(nan, nan), true)
  assertEqual(hash(nan), hash(nan))
  
  # 3. Transitivity: -inf < -0.0 < +0.0 < inf < NaN
  assertEqual(less(inf_neg, neg_zero), true)
  assertEqual(less(neg_zero, pos_zero), true)
  assertEqual(less(pos_zero, inf_pos), true)
  assertEqual(less(inf_pos, nan), true)
  assertEqual(less(inf_neg, nan), true)
  
  # 4. Receiver methods
  assertEqual(neg_zero.less(pos_zero), true)
  assertEqual(pos_zero.less(neg_zero), false)
  assertEqual(nan.isequal(nan), true)
  assertEqual(neg_zero.isequal(pos_zero), false)
  
  # 5. Static methods
  assertEqual(Float.less(neg_zero, pos_zero), true)
  assertEqual(Float.isequal(nan, nan), true)
  echo "FLOAT TOTAL ORDERING LAWS PASS"
"""
    run_code_in_all_modes(code, "Float Total Ordering Laws")
    print("  PASS: Float total ordering axioms verified.")


def test_scalar_ordering_suite():
    print("Testing total ordering across Int, Bool, Char, String...")
    code = """
fn main():
  # Int
  assertEqual(less(1, 2), true)
  assertEqual(less(2, 1), false)
  assertEqual(less(2, 2), false)
  assertEqual(isequal(2, 2), true)
  assertEqual(isequal(1, 2), false)
  assertEqual(Int.less(5, 10), true)
  assertEqual(Int.isequal(5, 5), true)
  
  # Bool: false < true
  assertEqual(less(false, true), true)
  assertEqual(less(true, false), false)
  assertEqual(less(true, true), false)
  assertEqual(isequal(false, false), true)
  assertEqual(isequal(true, true), true)
  assertEqual(isequal(false, true), false)
  assertEqual(Bool.less(false, true), true)
  
  # Char
  assertEqual(less('a', 'b'), true)
  assertEqual(less('b', 'a'), false)
  assertEqual(less('a', 'a'), false)
  assertEqual(isequal('a', 'a'), true)
  assertEqual(Char.less('A', 'a'), true)
  assertEqual(Char.isequal('A', 'A'), true)
  
  # String
  assertEqual(less("apple", "banana"), true)
  assertEqual(less("banana", "apple"), false)
  assertEqual(isequal("moss", "moss"), true)
  assertEqual(String.less("abc", "abd"), true)
  echo "SCALAR ORDERING SUITE PASS"
"""
    run_code_in_all_modes(code, "Scalar Ordering Suite")
    print("  PASS: Scalar ordering suite verified.")


def test_map_float_key_dispatch():
    print("Testing Map with Float keys (zero separation, NaN keys, lookup, deletion)...")
    code = """
fn main():
  var m = Map()
  nan = 0.0 / 0.0
  pos_zero = 0.0
  neg_zero = -0.0
  
  m[pos_zero] = "pos_zero"
  m[neg_zero] = "neg_zero"
  m[nan] = "nan_val"
  m[42.5] = "answer"
  
  # Both zeros coexist as distinct keys
  assertEqual(m[pos_zero], "pos_zero")
  assertEqual(m[neg_zero], "neg_zero")
  assertEqual(m[nan], "nan_val")
  assertEqual(m[42.5], "answer")
  
  # .get() with fallback
  assertEqual(m.get(pos_zero, "missing"), "pos_zero")
  assertEqual(m.get(neg_zero, "missing"), "neg_zero")
  assertEqual(m.get(nan, "missing"), "nan_val")
  assertEqual(m.get(100.0, "missing"), "missing")
  
  # .delete()
  var deleted = false
  val = m.delete(42.5, "fallback", deleted)
  assertEqual(val, "answer")
  assertEqual(deleted, true)
  assertEqual(m.get(42.5, "missing"), "missing")
  
  echo "MAP FLOAT KEY DISPATCH PASS"
"""
    run_code_in_all_modes(code, "Map Float Key Dispatch")
    print("  PASS: Map Float key dispatch verified.")


def test_sort_and_reverse():
    print("Testing sort(v), sort(v, by: ...), and Reverse(key_fn)...")
    code = """
fn extract_key(x: Int) -> Int:
  return 0 - x

fn float_key(x: Float) -> Float:
  return 0.0 - x

fn main():
  # 1. Natural sort on Int
  var vi = [5, 2, 8, 1, 9]
  sort(vi)
  assertEqual(vi[0], 1)
  assertEqual(vi[1], 2)
  assertEqual(vi[2], 5)
  assertEqual(vi[3], 8)
  assertEqual(vi[4], 9)
  
  # 2. Natural sort on Float (with NaN, -0.0, +0.0, negatives)
  nan = 0.0 / 0.0
  var vf = [nan, 1.0, 0.0, -0.0, -10.0]
  sort(vf)
  assertEqual(vf[0], -10.0)
  assertEqual(isequal(vf[1], -0.0), true)
  assertEqual(isequal(vf[2], 0.0), true)
  assertEqual(vf[3], 1.0)
  assertEqual(isequal(vf[4], nan), true)
  
  # 3. sort(v, by: key_fn)
  var v_desc = [1, 2, 3, 4, 5]
  sort(v_desc, by: extract_key)
  assertEqual(v_desc[0], 5)
  assertEqual(v_desc[1], 4)
  assertEqual(v_desc[2], 3)
  assertEqual(v_desc[3], 2)
  assertEqual(v_desc[4], 1)
  
  # 4. sort(v, by: Reverse(key_fn))
  var v_rev = [5, 1, 4, 2]
  sort(v_rev, by: Reverse(extract_key))
  assertEqual(v_rev[0], 1)
  assertEqual(v_rev[1], 2)
  assertEqual(v_rev[2], 4)
  assertEqual(v_rev[3], 5)
  echo "SORT AND REVERSE PASS"
"""
    run_code_in_all_modes(code, "Sort and Reverse")
    print("  PASS: sort and Reverse verified.")


def main():
    print("=== Running Phase 25 G3 Ordering & Comparator Suite ===")
    test_float_total_ordering_laws()
    test_scalar_ordering_suite()
    test_map_float_key_dispatch()
    test_sort_and_reverse()
    print("=== All Phase 25 G3 Tests Passed Cleanly! ===")


if __name__ == "__main__":
    main()
