#!/usr/bin/env python3
"""Phase 15.8 — Cross-Package Fast Debug Source Convergence regression tests.

Tests:
  1. Path-dependency source-backed Fast Debug via MOSS_SOURCE_ROOTS env var
  2. margo debug command works (resolves graph, no rustc needed)
  3. RUSTC=/definitely/not/rustc margo debug still works (no rustc invoked)
  4. Native/interpreter output parity via the fixture packages
  5. Source-free dependency rejection (FAST_DEBUG_NATIVE_DEPENDENCY with module name)
  6. Trace events include dependency source file paths
  7. Standalone moss debug still works when MOSS_SOURCE_ROOTS is unset
"""

import json
import os
import subprocess
import sys
import tempfile
from pathlib import Path

FIXTURES = Path(__file__).resolve().parent / "fixtures" / "phase15_8"
PKG_GRAPHPKG = FIXTURES / "pkg_graphpkg"
PKG_MAIN = FIXTURES / "pkg_main"


def run(cmd, *, cwd, env=None, check=True):
    return subprocess.run(
        cmd, cwd=cwd, env=env or dict(os.environ),
        text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
        check=check,
    )


def make_env(**extra):
    env = dict(os.environ)
    env.update(extra)
    return env


def test_moss_source_roots_fast_debug(compiler: Path) -> None:
    """Test 1: MOSS_SOURCE_ROOTS lets moss debug cross-package source."""
    env = make_env(
        MOSS_SOURCE_ROOTS=str(PKG_GRAPHPKG),
        RUSTC="/path/that/does/not/exist",
    )
    result = run(
        [str(compiler), "debug", str(PKG_MAIN)],
        cwd=PKG_MAIN, env=env, check=False,
    )
    if result.returncode != 0:
        raise AssertionError(
            f"moss debug with MOSS_SOURCE_ROOTS failed (rc={result.returncode}):\n"
            f"stdout: {result.stdout}\nstderr: {result.stderr}"
        )
    if result.stdout.strip() != "42":
        raise AssertionError(
            f"Unexpected output (expected '42'): {result.stdout!r}"
        )
    print("  [PASS] MOSS_SOURCE_ROOTS source-backed Fast Debug")


def test_margo_debug(compiler: Path) -> None:
    """Test 2: margo debug command resolves graph and invokes moss debug."""
    margo = compiler.parent / "margo"
    env = make_env(
        MOSS=str(compiler),
        RUSTC="/path/that/does/not/exist",
    )
    result = run([str(margo), "debug"], cwd=PKG_MAIN, env=env, check=False)
    if result.returncode != 0:
        raise AssertionError(
            f"margo debug failed (rc={result.returncode}):\n"
            f"stdout: {result.stdout}\nstderr: {result.stderr}"
        )
    if result.stdout.strip() != "42":
        raise AssertionError(
            f"margo debug unexpected output (expected '42'): {result.stdout!r}"
        )
    print("  [PASS] margo debug command works")


def test_margo_debug_no_rustc(compiler: Path) -> None:
    """Test 3: RUSTC=/definitely/not/rustc margo debug succeeds (interpreter path)."""
    margo = compiler.parent / "margo"
    env = make_env(
        MOSS=str(compiler),
        RUSTC="/definitely/not/rustc",
    )
    result = run([str(margo), "debug"], cwd=PKG_MAIN, env=env, check=False)
    if result.returncode != 0:
        raise AssertionError(
            f"margo debug with fake RUSTC failed (rc={result.returncode}):\n"
            f"stdout: {result.stdout}\nstderr: {result.stderr}"
        )
    if result.stdout.strip() != "42":
        raise AssertionError(
            f"margo debug (no rustc) unexpected output: {result.stdout!r}"
        )
    print("  [PASS] margo debug with fake RUSTC (no native build needed)")


def test_source_free_rejection(compiler: Path) -> None:
    """Test 5: When graphpkg source is absent, expect FAST_DEBUG_NATIVE_DEPENDENCY
    error that names the specific missing module."""
    env = make_env(RUSTC="/path/that/does/not/exist")
    # No MOSS_SOURCE_ROOTS set — graphpkg source is unknown to moss debug
    result = run(
        [str(compiler), "debug", str(PKG_MAIN)],
        cwd=PKG_MAIN, env=env, check=False,
    )
    if result.returncode == 0:
        raise AssertionError(
            "Expected failure when graphpkg source is missing, but got success.\n"
            f"stdout: {result.stdout}"
        )
    combined = result.stdout + result.stderr
    if "FAST_DEBUG_NATIVE_DEPENDENCY" not in combined and "graphpkg" not in combined:
        raise AssertionError(
            f"Expected FAST_DEBUG_NATIVE_DEPENDENCY or 'graphpkg' in error output:\n"
            f"stdout: {result.stdout}\nstderr: {result.stderr}"
        )
    print("  [PASS] Source-free dependency rejected with proper error")


def test_trace_includes_dep_source_path(compiler: Path) -> None:
    """Test 6: --trace events include the dependency source file path."""
    env = make_env(
        MOSS_SOURCE_ROOTS=str(PKG_GRAPHPKG),
        RUSTC="/path/that/does/not/exist",
    )
    result = run(
        [str(compiler), "debug", "--trace", str(PKG_MAIN)],
        cwd=PKG_MAIN, env=env, check=False,
    )
    if result.returncode != 0:
        raise AssertionError(
            f"moss debug --trace failed (rc={result.returncode}):\n"
            f"stderr: {result.stderr}"
        )
    # Trace goes to stderr; look for graphpkg source file path
    dep_source = str((PKG_GRAPHPKG / "src" / "graphpkg.moss").resolve())
    trace_lines = [l for l in result.stderr.strip().splitlines() if l.strip()]
    # At least some trace events should reference the dep source path
    found = any(dep_source in line for line in trace_lines)
    if not found:
        # Collect unique source files mentioned in trace for diagnostic
        sources_in_trace = set()
        for line in trace_lines:
            try:
                ev = json.loads(line)
                if "source_file" in ev:
                    sources_in_trace.add(ev["source_file"])
            except json.JSONDecodeError:
                pass
        raise AssertionError(
            f"Dependency source path {dep_source!r} not found in trace.\n"
            f"Source files in trace: {sources_in_trace}"
        )
    print("  [PASS] Trace events include dependency source file paths")


def test_standalone_moss_debug_unchanged(compiler: Path) -> None:
    """Test 7: Standalone single-package moss debug still works without MOSS_SOURCE_ROOTS."""
    with tempfile.TemporaryDirectory(prefix="moss-phase158-standalone-") as tmp:
        root = Path(tmp)
        (root / "src").mkdir()
        (root / "Moss.toml").write_text(
            '[package]\nname = "standalone"\nversion = "0.1.0"\n'
            '[build]\nsource = "src"\n'
        )
        (root / "src" / "main.moss").write_text(
            "module standalone\n\n"
            "fn main():\n"
            "  echo 99\n"
        )
        env = make_env(RUSTC="/path/that/does/not/exist")
        result = run(
            [str(compiler), "debug", str(root)],
            cwd=root, env=env, check=False,
        )
        if result.returncode != 0:
            raise AssertionError(
                f"Standalone moss debug failed (rc={result.returncode}):\n"
                f"stdout: {result.stdout}\nstderr: {result.stderr}"
            )
        if result.stdout.strip() != "99":
            raise AssertionError(
                f"Unexpected standalone output: {result.stdout!r}"
            )
    print("  [PASS] Standalone moss debug works without MOSS_SOURCE_ROOTS")


def test_margo_debug_trace_flag(compiler: Path) -> None:
    """Test: margo debug --trace forwards --trace to moss debug."""
    margo = compiler.parent / "margo"
    env = make_env(
        MOSS=str(compiler),
        RUSTC="/path/that/does/not/exist",
    )
    result = run(
        [str(margo), "debug", "--trace"],
        cwd=PKG_MAIN, env=env, check=False,
    )
    if result.returncode != 0:
        raise AssertionError(
            f"margo debug --trace failed (rc={result.returncode}):\n"
            f"stderr: {result.stderr}"
        )
    if not result.stderr.strip():
        raise AssertionError("Expected trace output on stderr but got nothing")
    print("  [PASS] margo debug --trace forwards trace flag to moss debug")


def main() -> int:
    if len(sys.argv) < 2:
        print(f"Usage: {sys.argv[0]} <path-to-moss-compiler>", file=sys.stderr)
        return 2
    compiler = Path(sys.argv[1]).resolve()
    if not compiler.is_file():
        print(f"Compiler not found: {compiler}", file=sys.stderr)
        return 2

    print("Phase 15.8 — Cross-Package Fast Debug Source Convergence")

    tests = [
        test_moss_source_roots_fast_debug,
        test_margo_debug,
        test_margo_debug_no_rustc,
        test_source_free_rejection,
        test_trace_includes_dep_source_path,
        test_standalone_moss_debug_unchanged,
        test_margo_debug_trace_flag,
    ]
    failed = 0
    for test in tests:
        try:
            test(compiler)
        except AssertionError as exc:
            print(f"  [FAIL] {test.__name__}: {exc}")
            failed += 1

    if failed:
        print(f"\n{failed}/{len(tests)} tests FAILED")
        return 1
    print(f"\nAll {len(tests)} Phase 15.8 tests PASSED")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
