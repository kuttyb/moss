"""Regression and contract tests for Phase 25 G1 compiler defects."""

import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]
MOSS = Path(sys.argv[1] if len(sys.argv) > 1 else "./moss").resolve()
MARGO = ROOT / "margo"
SCRATCH = ROOT / "tmp"
SCRATCH.mkdir(exist_ok=True)


def run_cmd(cmd, cwd=ROOT):
    res = subprocess.run(cmd, cwd=cwd, capture_output=True, text=True)
    return res


def test_negative_checker_cases():
    """Verify negative checker errors for G1 defects #2 and #3."""
    print("Testing negative checker cases (Defects #2, #3)...")

    # Defect #2: `<` / `>` unsupported user types
    code_point_cmp = """
type Point:
  x: Int
  y: Int

fn main():
  let p1 = Point(x = 1, y = 2)
  let p2 = Point(x = 3, y = 4)
  if p1 < p2:
    echo "bad"
"""
    with tempfile.NamedTemporaryFile("w", suffix=".moss", dir=SCRATCH, delete=False) as f:
        f.write(code_point_cmp)
        p = f.name
    try:
        res = run_cmd([str(MOSS), "check", p, "--json"])
        assert res.returncode != 0, "Expected Point < Point to fail checker"
        parsed = json.loads(res.stdout)
        errors = [d.get("code") for d in parsed.get("diagnostics", [])]
        if "error" in parsed and parsed["error"]:
            errors.append(parsed["error"].get("code"))
        assert "TYPE_MISMATCH" in errors, f"Expected TYPE_MISMATCH, got {errors}"
    finally:
        Path(p).unlink(missing_ok=True)

    # Defect #3: straight-line reassignment type mutation rejected
    code_reassign = """
fn main():
  var x = 42
  x = "hello"
  echo x
"""
    with tempfile.NamedTemporaryFile("w", suffix=".moss", dir=SCRATCH, delete=False) as f:
        f.write(code_reassign)
        p = f.name
    try:
        res = run_cmd([str(MOSS), "check", p, "--json"])
        assert res.returncode != 0, "Expected var x = 42; x = 'hello' to fail checker"
        parsed = json.loads(res.stdout)
        errors = [d.get("code") for d in parsed.get("diagnostics", [])]
        if "error" in parsed and parsed["error"]:
            errors.append(parsed["error"].get("code"))
        assert "TYPE_MISMATCH" in errors, f"Expected TYPE_MISMATCH, got {errors}"
    finally:
        Path(p).unlink(missing_ok=True)

    print("  PASS: Negative checker cases verified.")


def test_semantic_resolution_cases():
    """Verify semantic resolution for Defect #10."""
    print("Testing semantic resolution (Defect #10)...")
    code_named_ctor = """
type Config:
  port: Int

fn port():
  echo "dummy"

fn main():
  let c = Config(port = 9000)
  echo c.port
"""
    with tempfile.NamedTemporaryFile("w", suffix=".moss", dir=SCRATCH, delete=False) as f:
        f.write(code_named_ctor)
        p = f.name
    try:
        res = run_cmd([str(MOSS), "resolve", "port", "--source", p, "--json"])
        assert res.returncode == 0, f"moss resolve failed: {res.stderr}"
        parsed = json.loads(res.stdout)
        resolved = parsed.get("target", {}).get("kind", "")
        # Resolving port at the constructor call line 9 col 18
        res_loc = run_cmd([str(MOSS), "resolve", "at:9:18", "--source", p, "--json"])
        assert res_loc.returncode == 0, f"moss resolve at:9:18 failed: {res_loc.stderr}"
        parsed_loc = json.loads(res_loc.stdout)
        target = parsed_loc.get("result", {}).get("target", {})
        kind = target.get("kind", "")
        symbol = target.get("name", "")
        assert kind == "field", f"Expected field for Config(port = ...), got kind={kind}"
        assert "Config.port" in symbol, f"Expected Config.port symbol, got {symbol}"
    finally:
        Path(p).unlink(missing_ok=True)

    print("  PASS: Semantic resolution verified.")


def run_code_in_all_modes(moss_code, test_name):
    """Compile and run moss_code in:
    1) Fast Debug (moss run --interp)
    2) Native Debug (moss --debug -o rs, rustc)
    3) Native Release (moss -O -o rs, rustc -O)
    Assert all three modes exit 0 and produce identical stdout."""
    with tempfile.NamedTemporaryFile("w", suffix=".moss", dir=SCRATCH, delete=False) as f:
        f.write(moss_code)
        p = f.name
    try:
        # 1) Fast Debug
        res_interp = run_cmd([str(MOSS), "run", "--interp", p])
        assert res_interp.returncode == 0, (
            f"[{test_name}] Fast Debug failed (code {res_interp.returncode}):\n{res_interp.stderr}"
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
        rs_rel = str(Path(p).with_suffix(".rel.rs"))
        bin_rel = str(Path(p).with_suffix(".rel.bin"))
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

        # Check parity
        assert interp_out == dbg_out, (
            f"[{test_name}] Fast Debug output mismatch with Native Debug:\n"
            f"Fast Debug: {interp_out!r}\nNative Debug: {dbg_out!r}"
        )
        assert dbg_out == rel_out, (
            f"[{test_name}] Native Debug output mismatch with Native Release:\n"
            f"Native Debug: {dbg_out!r}\nNative Release: {rel_out!r}"
        )

        return interp_out
    finally:
        for ext in [".moss", ".debug.rs", ".debug.bin", ".debug.mossmap",
                    ".rel.rs", ".rel.bin", ".rel.mossmap"]:
            Path(p).with_suffix(ext).unlink(missing_ok=True)


def test_execution_parity_cases():
    """Verify runtime execution and bit-for-bit parity across all three execution modes."""
    print("Testing execution parity across 3 modes (Defects #1, #4, #5, #6, #7, #8, #12/#15)...")

    # Defect #1: 64-bit Int precision loss beyond 2^53
    code_int64 = """
fn main():
  let a = 9007199254740992
  let b = 9007199254740993
  let max_val = 9223372036854775807
  let min_val = -9223372036854775808
  echo a
  echo b
  echo b - a
  echo max_val
  echo min_val
"""
    out = run_code_in_all_modes(code_int64, "Defect #1 Int64 Precision")
    assert "9007199254740992\n9007199254740993\n1\n9223372036854775807\n-9223372036854775808\n" == out
    print("  PASS: Defect #1 (64-bit Int precision beyond 2^53)")

    # Defect #4: Matrix index assignment borrow lowering
    code_matrix_borrow = """
type Matrix:
  rows: Int
  cols: Int
  data: Vector[Int]

fn set_at(m: Matrix, r: Int, c: Int, value: Int):
  m.data[r * m.cols + c] = value

fn main():
  var m = Matrix(rows = 2, cols = 3, data = [0, 0, 0, 0, 0, 0])
  set_at(m, 1, 2, 42)
  echo m.data[5]
"""
    out = run_code_in_all_modes(code_matrix_borrow, "Defect #4 Borrow Lowering")
    assert out.strip() == "42"
    print("  PASS: Defect #4 (parameter-based index expression borrow lowering)")

    # Defect #5: Discard assignment `_ = f()`
    code_discard = """
fn compute(x: Int) -> Int:
  return x * 2

fn main():
  _ = compute(10)
  var y = 5
  _ = compute(y)
  echo "ok"
"""
    out = run_code_in_all_modes(code_discard, "Defect #5 Discard Assignment")
    assert out.strip() == "ok"
    print("  PASS: Defect #5 (_ = f() discard)")

    # Defect #6: Assert with '{' and '}'
    code_assert = """
fn main():
  assert("{json: key}" != "")
  assertEqual("{a: 1}", "{a: 1}")
  echo "assert_ok"
"""
    out = run_code_in_all_modes(code_assert, "Defect #6 Assert escaping")
    assert out.strip() == "assert_ok"
    print("  PASS: Defect #6 (assert formatting escaping)")

    # Defect #7: Float.to_string() parity
    code_float_str = """
fn main():
  let f1 = 1.0
  let f2 = 1.5
  let f3 = 0.0
  let f4 = -2.5
  echo f1.to_string()
  echo f2.to_string()
  echo f3.to_string()
  echo f4.to_string()
"""
    out = run_code_in_all_modes(code_float_str, "Defect #7 Float.to_string")
    assert out == "1\n1.5\n0\n-2.5\n"
    print("  PASS: Defect #7 (Float.to_string agreement)")

    # Defect #8: Fast Debug for loop
    code_for_loop = """
fn main():
  for i in range(0, 3):
    echo i

  for x in [10, 20]:
    echo x

  for z in range(5, 5):
    echo "empty"

  echo "done"
"""
    out = run_code_in_all_modes(code_for_loop, "Defect #8 For Loop")
    assert out == "0\n1\n2\n10\n20\ndone\n"
    print("  PASS: Defect #8 (Fast Debug for loop parity)")

    # Defect #12 / #15: Statement member-call with dotted path receiver (h.data.push(3))
    code_member_call = """
type Container:
  data: Vector[Int]

fn main():
  var c = Container(data = [1, 2])
  c.data.push(3)
  c.data.push(4)
  echo c.data[2]
  echo c.data[3]
"""
    out = run_code_in_all_modes(code_member_call, "Defect #12/#15 Dotted Receiver Push")
    assert out == "3\n4\n"
    print("  PASS: Defect #12 / #15 (dotted receiver statement member-call)")


def test_benchmark_raising_case():
    """Verify Defect #14: Raising calls inside benchmarks."""
    print("Testing Defect #14 (Raising calls inside benchmarks)...")
    with tempfile.TemporaryDirectory(prefix="phase25-bench-", dir=SCRATCH) as name:
        work = Path(name)
        (work / "src").mkdir(parents=True)
        (work / "benches").mkdir(parents=True)

        (work / "Moss.toml").write_text(
            '[package]\nname = "moss.calc"\nversion = "0.1.0"\n'
        )
        (work / "src" / "main.moss").write_text(
            'module calc\n'
            '\n'
            'export enum CalcError:\n'
            '  InvalidInput\n'
            '\n'
            'export fn risky(x: Int) -> Int:\n'
            '  if x < 0:\n'
            '    raise CalcError.InvalidInput\n'
            '  return x + 1\n'
        )
        (work / "benches" / "bench_main.moss").write_text(
            'import calc\n'
            '\n'
            'bench "risky_bench":\n'
            '  try:\n'
            '    let v = calc.risky(10)\n'
            '    assert(v == 11)\n'
            '  recover calc.CalcError.InvalidInput:\n'
            '    assert(false)\n'
        )
        # Test building or running margo bench
        res = run_cmd([str(MARGO), "bench"], cwd=work)
        assert res.returncode == 0, f"margo bench failed:\n{res.stderr}\n{res.stdout}"
        print("  PASS: Defect #14 (raising calls inside benchmarks compile and run)")


def main():
    print("=== Running Phase 25 G1 Regressions Suite ===")
    test_negative_checker_cases()
    test_semantic_resolution_cases()
    test_execution_parity_cases()
    test_benchmark_raising_case()
    print("=== All Phase 25 G1 Regressions Passed Cleanly! ===")


if __name__ == "__main__":
    main()
