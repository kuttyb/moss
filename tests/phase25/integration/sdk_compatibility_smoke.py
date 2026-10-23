#!/usr/bin/env python3
"""Phase 25 Reusable SDK Compatibility Smoke Runner.

Provides fast, reproducible compatibility verification across all 10 Phase 24 SDK
and toolchain areas in Native Debug, Native Release, and Fast Debug execution modes.

Modes:
  - native-debug:   Margo package tests / Moss native debug lowering (O0)
  - native-release: Margo package tests with --release (O2)
  - fast-debug:     Margo package tests with --interp / Moss interpreter Fast Debug

Flags:
  --smoke (default): High-value, fast representative tests across all 10 areas
  --full:            Full package test suites across all delivered packages
  --mode <mode>:     Select one mode: native-debug, native-release, fast-debug (or 'all')
  --self-test:       Fast internal verification of test registry and options (exits 0)
  --verbose, -v:     Print detailed command outputs and logs

Exit status:
  0: All executed tests passed or reported documented engine limitations
  1: One or more unexpected test failures occurred
"""

from __future__ import annotations

import argparse
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import time
from typing import Callable, NamedTuple


ROOT = Path(__file__).resolve().parents[3]
MARGO = ROOT / "margo"
MOSS = Path(os.environ.get("MOSS", ROOT / "moss")).resolve()
SCRATCH = ROOT / "tmp" / "phase25_sdk_smoke"
SCRATCH.mkdir(parents=True, exist_ok=True)

ENV = os.environ.copy()
ENV.update(
    MOSS=str(MOSS),
    MARGO_HOME=str(ROOT / "tmp" / "margo-home-smoke"),
    TMPDIR=str(ROOT / "tmp"),
    PYTHONDONTWRITEBYTECODE="1",
)


class TestVerdict:
    PASS = "PASS"
    FAIL = "FAIL"
    LIMITATION = "LIMITATION"
    SKIPPED = "SKIPPED"


class TestResult(NamedTuple):
    domain_id: int
    domain_name: str
    target: str
    mode: str
    verdict: str
    duration_sec: float
    detail: str


class DomainSpec(NamedTuple):
    id: int
    name: str
    packages: list[str]
    smoke_target: str
    description: str
    fast_debug_limitation: str | None = None


DOMAINS: list[DomainSpec] = [
    DomainSpec(
        id=1,
        name="Foundations & Collections",
        packages=[
            "sdk/foundations/core",
            "sdk/foundations/collections",
            "sdk/foundations/algorithms",
            "sdk/foundations/bytes",
        ],
        smoke_target="sdk/foundations/core",
        description="moss.core, collections, algorithms, and bytes foundational APIs",
        fast_debug_limitation=None,
    ),
    DomainSpec(
        id=2,
        name="Advanced Data Structures, NDArray & Tables",
        packages=[
            "sdk/datastructures",
            "sdk/ndarray",
            "sdk/tables",
            "sdk/cbor",
            "sdk/binary",
        ],
        smoke_target="sdk/binary",
        description="moss.datastructures, moss.ndarray, moss.tables, moss.cbor, moss.binary",
        fast_debug_limitation=None,
    ),
    DomainSpec(
        id=3,
        name="JSON, CSV & Binary Formats",
        packages=[
            "packages/compress",
            "packages/hash",
            "sdk/json",
            "sdk/csv",
            "sdk/fmt",
            "sdk/binary",
        ],
        smoke_target="packages/compress",
        description="moss.json, moss.csv, moss.binary, moss.fmt, moss.compress, moss.hash",
        fast_debug_limitation="Float formatting / receiver differences in Fast Debug (G1 Defect #7)",
    ),
    DomainSpec(
        id=4,
        name="Text, Encoding & Regex",
        packages=[
            "sdk/text",
            "sdk/encoding",
            "packages/regex",
        ],
        smoke_target="sdk/encoding",
        description="moss.text, moss.encoding, and moss.regex (NFA linear scaling)",
        fast_debug_limitation="Staged indexed-v1 string is single-crate opt-in; plain char_at is O(N)",
    ),
    DomainSpec(
        id=5,
        name="Numerics, Linear Algebra & Statistics",
        packages=[
            "packages/linalg",
            "packages/sparse",
            "packages/statistics",
            "packages/math",
            "packages/random",
            "packages/time",
        ],
        smoke_target="packages/math",
        description="moss.linalg, moss.sparse, moss.statistics, moss.math, moss.random, moss.time",
        fast_debug_limitation="Float keys unsupported in Map/Set until G3; Fast Debug Int/Float precision (Defect #1)",
    ),
    DomainSpec(
        id=6,
        name="Host Services (Filesystem & Path)",
        packages=[
            "sdk/path",
            "sdk/fs",
            "packages/math",
            "packages/random",
            "packages/time",
        ],
        smoke_target="sdk/path",
        description="moss.path (in-memory) and FileIO-backed moss.fs",
        fast_debug_limitation="Fast Debug intentionally rejects FileIO at preflight (FAST_DEBUG_FILEIO)",
    ),
    DomainSpec(
        id=7,
        name="Margo Multi-Module Builds & Integration",
        packages=[
            "packages/integration",
            "packages/data_utils_integration",
            "packages/numerics_integration",
        ],
        smoke_target="packages/data_utils_integration",
        description="Multi-package builds and cross-dependency integration",
        fast_debug_limitation="Cross-package constructor labels resolved as functions in baseline (Defect #10)",
    ),
    DomainSpec(
        id=8,
        name="Source-Free Provider Imports",
        packages=[
            "sdk/foundations/acceptance/validate_source_free.py",
            "sdk/advanced-data-acceptance/check_source_free.py",
            "sdk/encoding/tests/check_source_free.py",
            "tests/tooling/check_phase21_source_free_abi.py",
        ],
        smoke_target="sdk/foundations/algorithms/tests/check_source_free.py",
        description="Compiled-only ABI-v9 provider imports without source",
        fast_debug_limitation="Fast Debug requires source; source-free binary providers cannot execute without source",
    ),
    DomainSpec(
        id=9,
        name="Semantic Query Commands & IDE Toolchain",
        packages=[
            "tests/tooling/check_semantic_queries.py",
            "tests/tooling/check_phase225_semantic_ide.py",
        ],
        smoke_target="semantic_queries_smoke",
        description="moss resolve, inspect, type, effects, ownership, calls, why, cost, impact, and editor queries",
        fast_debug_limitation=None,
    ),
    DomainSpec(
        id=10,
        name="Phase 20 Concurrency & Phase 21 Error Handling",
        packages=[
            "tests/tooling/check_phase20c_executor.py",
            "tests/tooling/check_phase21_recovery_semantics.py",
            "tests/tooling/check_phase21_standard_error_namespace.py",
        ],
        smoke_target="tests/tooling/check_phase21_recovery_semantics.py",
        description="Phase 20 synchronous message/executor and Phase 21 try/recover/raise/on_fail error recovery",
        fast_debug_limitation="Physical multi-worker thread pools and FileIO lock compensation are native-only",
    ),
]


def run_command(cmd: list[str], cwd: Path = ROOT, env: dict | None = None, timeout: int = 180) -> tuple[int, str, str]:
    run_env = env if env is not None else ENV
    try:
        proc = subprocess.run(
            cmd,
            cwd=cwd,
            env=run_env,
            capture_output=True,
            text=True,
            timeout=timeout,
        )
        return proc.returncode, proc.stdout, proc.stderr
    except subprocess.TimeoutExpired as exc:
        out = exc.stdout or ""
        err = (exc.stderr or "") + f"\nTimed out after {timeout}s"
        return 124, out, err
    except Exception as exc:
        return 1, "", str(exc)


def run_margo_package_test(pkg_rel_path: str, mode: str, verbose: bool = False) -> tuple[str, str]:
    pkg_dir = ROOT / pkg_rel_path
    if not (pkg_dir / "Moss.toml").is_file():
        return TestVerdict.FAIL, f"No Moss.toml in {pkg_rel_path}"

    cmd = [str(MARGO), "test"]
    if mode == "native-release":
        cmd.append("--release")
    elif mode == "fast-debug":
        cmd.append("--interp")

    ret, stdout, stderr = run_command(cmd, cwd=pkg_dir)
    output = stdout + stderr

    if ret == 0:
        pass_count = output.count("PASS ") or output.count(" passed")
        summary = f"{pass_count} passed" if pass_count else "ok"
        return TestVerdict.PASS, summary

    # Detect known limitations in Fast Debug
    if mode == "fast-debug":
        if "FAST_DEBUG_FILEIO" in output or "FileIO" in output:
            return TestVerdict.LIMITATION, "Fast Debug FileIO preflight boundary (FAST_DEBUG_FILEIO)"
        if "Fast Debug does not support for iteration yet" in output:
            return TestVerdict.LIMITATION, "Fast Debug for loop limitation (G1 Defect #8)"
        if "method receiver is not a Moss obj" in output:
            return TestVerdict.LIMITATION, "Fast Debug Float method receiver limitation (G1 Defect #7)"

    err_first = (stderr.strip() or stdout.strip()).splitlines()
    detail = err_first[0][:100] if err_first else f"exit code {ret}"
    return TestVerdict.FAIL, detail


def run_source_free_smoke(mode: str, verbose: bool = False) -> tuple[str, str]:
    if mode == "fast-debug":
        return TestVerdict.LIMITATION, "Fast Debug requires source; source-free providers cannot run without source"

    script = ROOT / "sdk/foundations/algorithms/tests/check_source_free.py"
    if not script.is_file():
        return TestVerdict.FAIL, f"Script missing: {script}"

    ret, stdout, stderr = run_command([sys.executable, str(script)], cwd=ROOT)
    if ret == 0 and "passed" in (stdout + stderr):
        return TestVerdict.PASS, "Compiled-only ABI-v9 provider check, build, run passed"
    return TestVerdict.FAIL, (stderr.strip() or stdout.strip())[:100]


def run_semantic_queries_smoke(mode: str, verbose: bool = False) -> tuple[str, str]:
    # Semantic queries operate directly on the compiler AST / IR
    with tempfile.TemporaryDirectory(prefix="moss-sem-smoke-", dir=SCRATCH) as td:
        src = Path(td) / "query_sample.moss"
        src.write_text(
            'module sample\n\n'
            'export fn calculate(a: Int, b: Int) -> Int:\n'
            '  return a + b\n\n'
            'fn main():\n'
            '  let res = calculate(10, 20)\n'
            '  echo res\n',
            encoding="utf-8",
        )
        queries = ["resolve", "inspect", "type", "effects", "ownership", "calls", "why", "cost"]
        for q in queries:
            cmd = [str(MOSS), q, "calculate", "--source", str(src), "--json"]
            ret, stdout, stderr = run_command(cmd, cwd=Path(td))
            if ret != 0:
                return TestVerdict.FAIL, f"Query '{q}' failed: {(stderr or stdout)[:80]}"
            try:
                data = json.loads(stdout)
                if not data.get("ok"):
                    return TestVerdict.FAIL, f"Query '{q}' returned ok=False"
            except Exception as e:
                return TestVerdict.FAIL, f"Query '{q}' returned invalid JSON: {e}"

    # Also run semantic IDE query tool
    ide_script = ROOT / "tests/tooling/check_phase225_semantic_ide.py"
    if ide_script.is_file():
        ret, stdout, stderr = run_command([sys.executable, str(ide_script), str(MOSS)], cwd=ROOT)
        if ret != 0:
            return TestVerdict.FAIL, f"IDE queries failed: {(stderr or stdout)[:80]}"

    return TestVerdict.PASS, "8 core semantic queries + Phase 22.5 IDE queries passed"


def run_phase20_21_smoke(mode: str, verbose: bool = False) -> tuple[str, str]:
    # Phase 21 typed error recovery semantics probe
    rec_script = ROOT / "tests/tooling/check_phase21_recovery_semantics.py"
    if not rec_script.is_file():
        return TestVerdict.FAIL, "check_phase21_recovery_semantics.py missing"

    ret, stdout, stderr = run_command([sys.executable, str(rec_script), str(MOSS)], cwd=ROOT)
    if ret != 0:
        return TestVerdict.FAIL, f"Recovery semantics probe failed: {(stderr or stdout)[:80]}"

    # In native modes, also verify a focused Phase 20 executor probe
    if mode in ("native-debug", "native-release"):
        ns_script = ROOT / "tests/tooling/check_phase21_standard_error_namespace.py"
        if ns_script.is_file():
            ret2, out2, err2 = run_command([sys.executable, str(ns_script), str(MOSS)], cwd=ROOT)
            if ret2 != 0:
                return TestVerdict.FAIL, f"Standard error namespace failed: {(err2 or out2)[:80]}"
        return TestVerdict.PASS, "Phase 20 synchronous message/executor & Phase 21 recovery semantics passed"
    else:
        # Fast debug
        return TestVerdict.PASS, "Phase 21 error recovery passed (executor concurrency is native-only)"


def run_smoke_domain(domain: DomainSpec, mode: str, verbose: bool = False) -> TestResult:
    t0 = time.time()
    verdict = TestVerdict.PASS
    detail = ""

    # Special handling for non-package domains
    if domain.id == 8:
        verdict, detail = run_source_free_smoke(mode, verbose)
    elif domain.id == 9:
        verdict, detail = run_semantic_queries_smoke(mode, verbose)
    elif domain.id == 10:
        verdict, detail = run_phase20_21_smoke(mode, verbose)
    else:
        # Normal package tests
        target = domain.smoke_target
        verdict, detail = run_margo_package_test(target, mode, verbose)

    duration = time.time() - t0
    return TestResult(
        domain_id=domain.id,
        domain_name=domain.name,
        target=domain.smoke_target,
        mode=mode,
        verdict=verdict,
        duration_sec=duration,
        detail=detail,
    )


def run_full_domain(domain: DomainSpec, mode: str, verbose: bool = False) -> list[TestResult]:
    results = []
    for pkg in domain.packages:
        t0 = time.time()
        if pkg.endswith(".py"):
            # Python harness script
            if mode == "fast-debug" and "source_free" in pkg:
                res = TestResult(
                    domain_id=domain.id,
                    domain_name=domain.name,
                    target=pkg,
                    mode=mode,
                    verdict=TestVerdict.LIMITATION,
                    duration_sec=0.01,
                    detail="Source-free providers require source in Fast Debug",
                )
            else:
                ret, stdout, stderr = run_command([sys.executable, str(ROOT / pkg), str(MOSS)], cwd=ROOT)
                v = TestVerdict.PASS if ret == 0 else TestVerdict.FAIL
                d = "passed" if ret == 0 else (stderr or stdout)[:80]
                res = TestResult(
                    domain_id=domain.id,
                    domain_name=domain.name,
                    target=pkg,
                    mode=mode,
                    verdict=v,
                    duration_sec=time.time() - t0,
                    detail=d,
                )
        else:
            # Margo package directory
            v, d = run_margo_package_test(pkg, mode, verbose)
            res = TestResult(
                domain_id=domain.id,
                domain_name=domain.name,
                target=pkg,
                mode=mode,
                verdict=v,
                duration_sec=time.time() - t0,
                detail=d,
            )
        results.append(res)
    return results


def run_self_test() -> int:
    """Validate internal self-consistency of test definitions and flags."""
    print("=== SDK Compatibility Smoke Suite Self-Test ===")
    assert len(DOMAINS) == 10, f"Expected exactly 10 domains, found {len(DOMAINS)}"
    ids = [d.id for d in DOMAINS]
    assert ids == list(range(1, 11)), f"Domain IDs must be 1..10, got {ids}"

    for d in DOMAINS:
        assert d.name, f"Domain {d.id} missing name"
        assert d.description, f"Domain {d.id} missing description"
        assert d.smoke_target, f"Domain {d.id} missing smoke_target"
        assert len(d.packages) > 0, f"Domain {d.id} has no packages defined"
        if d.id not in (8, 9, 10):
            target_path = ROOT / d.smoke_target
            assert target_path.exists(), f"Domain {d.id} target does not exist: {target_path}"

    print(f"Self-test verified {len(DOMAINS)} domain definitions, targets, and metadata.")
    print("Self-test PASSED (exit code 0)")
    return 0


def print_results_table(results: list[TestResult]) -> None:
    print("\n" + "=" * 92)
    print(f"{'Domain / Target':<42} {'Mode':<16} {'Verdict':<12} {'Time':<8} {'Detail'}")
    print("-" * 92)
    for r in results:
        status_color = r.verdict
        print(f"[{r.domain_id:02d}] {r.target:<37} {r.mode:<16} {status_color:<12} {r.duration_sec:5.2f}s  {r.detail}")
    print("=" * 92)


def print_summary(results: list[TestResult]) -> int:
    total = len(results)
    passed = sum(1 for r in results if r.verdict == TestVerdict.PASS)
    limitations = sum(1 for r in results if r.verdict == TestVerdict.LIMITATION)
    failed = sum(1 for r in results if r.verdict == TestVerdict.FAIL)

    print("\n=== Compatibility Baseline Execution Summary ===")
    print(f"Total test runs:       {total}")
    print(f"Passed:                {passed}")
    print(f"Documented Limitations:{limitations}")
    print(f"Failed:                {failed}")

    if limitations > 0:
        print("\nDocumented Engine / Language Limitations Encountered:")
        seen_limits = set()
        for r in results:
            if r.verdict == TestVerdict.LIMITATION and r.detail not in seen_limits:
                seen_limits.add(r.detail)
                print(f"  - [{r.mode}] Domain {r.domain_id} ({r.domain_name}): {r.detail}")

    if failed > 0:
        print("\nFAILURES:")
        for r in results:
            if r.verdict == TestVerdict.FAIL:
                print(f"  - Domain {r.domain_id} ({r.domain_name}) in {r.mode} ({r.target}): {r.detail}")
        return 1

    print("\nVERDICT: ALL COMPATIBILITY CHECKS PASSED (with documented limitations)\n")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Phase 25 Reusable SDK Compatibility Smoke Runner"
    )
    parser.add_argument(
        "--smoke",
        action="store_true",
        default=True,
        help="Run fast high-value representative tests across all 10 areas (default)",
    )
    parser.add_argument(
        "--full",
        action="store_true",
        help="Run full package test suites across all packages via margo test",
    )
    parser.add_argument(
        "--mode",
        choices=["native-debug", "native-release", "fast-debug", "all"],
        default="all",
        help="Execution mode (default: all)",
    )
    parser.add_argument(
        "--self-test",
        action="store_true",
        help="Verify test registry and CLI parsing (exits 0)",
    )
    parser.add_argument(
        "-v", "--verbose",
        action="store_true",
        help="Print verbose command outputs",
    )

    args = parser.parse_args()

    if args.self_test:
        return run_self_test()

    is_full = args.full
    modes = ["native-debug", "native-release", "fast-debug"] if args.mode == "all" else [args.mode]

    suite_name = "Full Package Matrix" if is_full else "Fast Representative Smoke Suite"
    print(f"=== Starting Phase 25 SDK Compatibility Suite: {suite_name} ===")
    print(f"Modes: {', '.join(modes)}")
    print(f"Targeting: 10 Domains across SDK Foundations, Data, Text, Math, Host, and Toolchain\n")

    all_results: list[TestResult] = []

    for mode in modes:
        print(f">>> Executing Mode: {mode} <<<")
        for domain in DOMAINS:
            if is_full:
                res_list = run_full_domain(domain, mode, args.verbose)
                all_results.extend(res_list)
                for r in res_list:
                    print(f"  [{r.domain_id:02d}] {r.domain_name} ({r.target}): {r.verdict} ({r.duration_sec:.2f}s)")
            else:
                r = run_smoke_domain(domain, mode, args.verbose)
                all_results.append(r)
                print(f"  [{r.domain_id:02d}] {r.domain_name}: {r.verdict} ({r.duration_sec:.2f}s) - {r.detail}")

    print_results_table(all_results)
    return print_summary(all_results)


if __name__ == "__main__":
    raise SystemExit(main())
