#!/usr/bin/env python3
"""Phase 15.14 SWARM-039 project-wide Fast Debug testing regression coverage.

Verifies:
- Project-wide test execution via Fast Debug without requiring rustc
- Multi-file, multi-module project tests
- Passing and failing test reporting and exit codes
- Test filtering by name / identity
- Trace generation with --trace
- Parity between moss test --interp and margo test --interp
"""
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile

REPO = Path(__file__).resolve().parents[2]


def run(command, *, cwd, env=None):
    return subprocess.run(command, cwd=cwd, env=env or dict(os.environ), text=True,
                          stdout=subprocess.PIPE, stderr=subprocess.PIPE, check=False)


def env_for(compiler, **extra):
    env = dict(os.environ)
    env["MOSS"] = str(compiler)
    env.update(extra)
    return env


def require_ok(result, context):
    if result.returncode:
        raise AssertionError(f"{context} failed (rc={result.returncode}):\n"
                             f"stdout: {result.stdout}\nstderr: {result.stderr}")


def test_multifile_project_fast_debug_tests(compiler):
    margo = compiler.parent / "margo"
    with tempfile.TemporaryDirectory() as tmpdir:
        root = Path(tmpdir)
        src = root / "src"
        tests = root / "tests"
        src.mkdir(parents=True)
        tests.mkdir(parents=True)

        (root / "Moss.toml").write_text(
            '[package]\nname = "calc_proj"\nversion = "0.1.0"\n\n'
            '[build]\nsource = "src"\n', encoding="utf-8")

        (src / "math_ops.moss").write_text(
            'module math_ops\n'
            'export fn add_two(x: Int, y: Int) -> Int:\n'
            '  return x + y\n'
            'export fn mul_two(x: Int, y: Int) -> Int:\n'
            '  return x * y\n', encoding="utf-8")

        (src / "main.moss").write_text(
            'module calc_proj\n'
            'import math_ops\n'
            'fn main():\n'
            '  x = math_ops.add_two(2, 3)\n', encoding="utf-8")

        (tests / "test_add.moss").write_text(
            'module test_add\n'
            'import math_ops\n'
            'test "add_positive":\n'
            '  assert math_ops.add_two(10, 20) == 30\n'
            'test "add_zero":\n'
            '  assert math_ops.add_two(5, 0) == 5\n', encoding="utf-8")

        (tests / "test_mul.moss").write_text(
            'module test_mul\n'
            'import math_ops\n'
            'test "mul_positive":\n'
            '  assert math_ops.mul_two(6, 7) == 42\n'
            'test "mul_zero":\n'
            '  assert math_ops.mul_two(10, 0) == 0\n', encoding="utf-8")

        no_rustc_env = env_for(compiler, RUSTC="/definitely/not/rustc")

        # Run project tests via moss test --interp directly
        moss_res = run([str(compiler), "test", "--interp"], cwd=root, env=no_rustc_env)
        require_ok(moss_res, "moss test --interp")
        if "PASS add_positive" not in moss_res.stdout or "PASS mul_positive" not in moss_res.stdout:
            raise AssertionError(f"Expected all tests to pass, got:\n{moss_res.stdout}")
        if "4 passed" not in moss_res.stdout:
            raise AssertionError(f"Expected 4 passed, got:\n{moss_res.stdout}")

        # Run project tests via margo test --interp
        margo_res = run([str(margo), "test", "--interp"], cwd=root, env=no_rustc_env)
        require_ok(margo_res, "margo test --interp")
        if "PASS add_positive" not in margo_res.stdout or "PASS mul_positive" not in margo_res.stdout:
            raise AssertionError(f"Expected all tests to pass via margo, got:\n{margo_res.stdout}")
        if "4 passed" not in margo_res.stdout:
            raise AssertionError(f"Expected 4 passed via margo, got:\n{margo_res.stdout}")

        # Test filtering: only test_mul
        filter_res = run([str(margo), "test", "mul", "--interp"], cwd=root, env=no_rustc_env)
        require_ok(filter_res, "margo test mul --interp")
        if "PASS mul_positive" not in filter_res.stdout or "PASS mul_zero" not in filter_res.stdout:
            raise AssertionError(f"Expected mul tests to pass, got:\n{filter_res.stdout}")
        if "add_positive" in filter_res.stdout:
            raise AssertionError(f"add_positive should have been filtered out:\n{filter_res.stdout}")
        if "2 passed" not in filter_res.stdout:
            raise AssertionError(f"Expected 2 passed, got:\n{filter_res.stdout}")

        # Test tracing: --trace
        trace_res = run([str(margo), "test", "add_positive", "--interp", "--trace"], cwd=root, env=no_rustc_env)
        require_ok(trace_res, "margo test with --trace")
        trace_lines = [json.loads(l) for l in trace_res.stderr.splitlines() if l.strip()]
        if not trace_lines:
            raise AssertionError(f"Expected trace events on stderr, got:\n{trace_res.stderr}")
        events = [ev.get("event") for ev in trace_lines]
        if "FunctionEnter" not in events or "FunctionExit" not in events:
            raise AssertionError(f"Expected FunctionEnter/FunctionExit in trace events, got: {events}")

    print("  [PASS] multifile project fast debug tests and filtering")


def test_fast_debug_test_failures(compiler):
    margo = compiler.parent / "margo"
    with tempfile.TemporaryDirectory() as tmpdir:
        root = Path(tmpdir)
        src = root / "src"
        tests = root / "tests"
        src.mkdir(parents=True)
        tests.mkdir(parents=True)

        (root / "Moss.toml").write_text(
            '[package]\nname = "fail_proj"\nversion = "0.1.0"\n\n'
            '[build]\nsource = "src"\n', encoding="utf-8")

        (src / "main.moss").write_text(
            'fn main():\n'
            '  x = 0\n', encoding="utf-8")

        (tests / "test_checks.moss").write_text(
            'test "passing_one":\n'
            '  assert 1 + 1 == 2\n'
            'test "failing_one":\n'
            '  assert 1 + 1 == 3\n', encoding="utf-8")

        no_rustc_env = env_for(compiler, RUSTC="/definitely/not/rustc")

        res = run([str(margo), "test", "--interp"], cwd=root, env=no_rustc_env)
        if res.returncode == 0:
            raise AssertionError(f"Expected test suite to fail, but exited 0:\n{res.stdout}")
        if "PASS passing_one" not in res.stdout:
            raise AssertionError(f"Expected PASS passing_one in output:\n{res.stdout}")
        if "FAIL failing_one" not in res.stdout:
            raise AssertionError(f"Expected FAIL failing_one in output:\n{res.stdout}")
        if "1 passed, 1 failed" not in res.stdout:
            raise AssertionError(f"Expected '1 passed, 1 failed' in output:\n{res.stdout}")

    print("  [PASS] fast debug test failure reporting and nonzero exit")


def test_dependency_source_closure(compiler):
    margo = compiler.parent / "margo"
    scratch = REPO / "tmp"
    scratch.mkdir(exist_ok=True)
    with tempfile.TemporaryDirectory(dir=scratch) as tmpdir:
        root = Path(tmpdir)
        dependency = root / "mathlib"
        app = root / "app"
        (dependency / "src").mkdir(parents=True)
        (app / "src").mkdir(parents=True)
        (app / "tests").mkdir(parents=True)

        (dependency / "Moss.toml").write_text(
            '[package]\nname = "mathlib"\nversion = "0.1.0"\n\n'
            '[build]\nsource = "src"\n', encoding="utf-8")
        (dependency / "src" / "mathlib.moss").write_text(
            'module mathlib\n\n'
            'export fn triple(x: Int) -> Int:\n'
            '  return x * 3\n', encoding="utf-8")

        (app / "Moss.toml").write_text(
            '[package]\nname = "dep_tests"\nversion = "0.1.0"\n\n'
            '[build]\nsource = "src"\n\n'
            '[dependencies]\nmathlib = { path = "../mathlib" }\n',
            encoding="utf-8")
        (app / "src" / "main.moss").write_text(
            'module dep_tests\n'
            'import mathlib\n\n'
            'fn main():\n'
            '  value = mathlib.triple(2)\n', encoding="utf-8")
        (app / "tests" / "test_dependency.moss").write_text(
            'module dependency_tests\n'
            'import mathlib\n\n'
            'test "dependency_source":\n'
            '  assertEqual(mathlib.triple(7), 21)\n', encoding="utf-8")

        no_rustc_env = env_for(compiler, RUSTC="/definitely/not/rustc")
        result = run([str(margo), "test", "dependency_source", "--interp"],
                     cwd=app, env=no_rustc_env)
        require_ok(result, "margo test --interp with path dependency")
        if "PASS dependency_source" not in result.stdout or "1 passed" not in result.stdout:
            raise AssertionError(f"Expected dependency-backed test to pass:\n{result.stdout}")

    print("  [PASS] dependency source closure without rustc")


def main():
    compiler = REPO / "moss"
    if not compiler.exists():
        compiler = Path(os.environ.get("MOSS", "moss"))
    print("Testing SWARM-039 project-wide Fast Debug testing...")
    test_multifile_project_fast_debug_tests(compiler)
    test_fast_debug_test_failures(compiler)
    test_dependency_source_closure(compiler)
    print("SWARM-039 tests passed.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
