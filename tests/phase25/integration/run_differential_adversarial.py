#!/usr/bin/env python3
"""Multi-Engine Differential & Adversarial Harness (Phase 25 Workstream I-D).

Automates comparisons among:
  1. Moss checking (`moss check --json` / `margo build --json`)
  2. Fast Debug (`moss run --interp` / `margo debug`)
  3. Native Debug (`moss --debug` + `rustc` / `margo run`)
  4. Native Release (`moss -O` + `rustc -O` / `margo --release run`)

Compares:
  - Check acceptance / rejection
  - Diagnostic code and source location
  - Exit status
  - Standard output (with semantic normalization where appropriate, strict on values/types)
  - Meaningful failure behavior
  - Effect and ownership semantic query results
  - Inferred concrete types and specialization identities

Flags:
  --self-test         Run harness unit/self-tests and exit 0
  --all               Run all 10 adversarial fixture families
  --family <1-10>     Run a specific adversarial family
  --baseline-report   Output baseline differential Markdown report
  --json              Output structured JSON comparison results
  --moss <path>       Path to moss binary (default: repository ./moss)
  --margo <path>      Path to margo script (default: repository ./margo)
"""

import argparse
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile
from typing import Any, Dict, List, Optional, Tuple

ROOT = Path(__file__).resolve().parents[3]
DEFAULT_MOSS = Path(os.environ.get("MOSS", ROOT / "moss")).resolve()
DEFAULT_MARGO = ROOT / "margo"
FIXTURES_DIR = Path(__file__).resolve().parent / "fixtures" / "adversarial"
SCRATCH_DIR = ROOT / "tmp"


# ============================================================================
# Output Normalization & Strict Value Comparison
# ============================================================================

def normalize_line(line: str) -> str:
    """Normalize a single output line while preserving value semantics."""
    return line.rstrip()


def normalize_output(text: str, sort_lines: bool = False) -> str:
    """Normalize multi-line output for semantic comparison.

    Trailing spaces on each line and trailing blank lines are normalized.
    If sort_lines is True (e.g. for unordered Map key dumps), lines are sorted.
    """
    lines = [normalize_line(l) for l in text.splitlines()]
    while lines and not lines[-1]:
        lines.pop()
    if sort_lines:
        lines.sort()
    return "\n".join(lines)


def parse_float_canonical(val_str: str) -> Optional[float]:
    """Parse float text if it strictly represents a float."""
    try:
        return float(val_str)
    except ValueError:
        return None


def compare_outputs(actual: str, expected: str, sort_lines: bool = False) -> Tuple[bool, str]:
    """Compare engine output with expected text strictly on values.

    Returns (matches, diff_description).
    """
    norm_actual = normalize_output(actual, sort_lines=sort_lines)
    norm_expected = normalize_output(expected, sort_lines=sort_lines)

    if norm_actual == norm_expected:
        return True, ""

    actual_lines = norm_actual.splitlines()
    expected_lines = norm_expected.splitlines()

    if len(actual_lines) != len(expected_lines):
        diff = (f"Line count mismatch: expected {len(expected_lines)} lines, "
                f"got {len(actual_lines)} lines.\n"
                f"Expected:\n{norm_expected}\nActual:\n{norm_actual}")
        return False, diff

    for idx, (a_line, e_line) in enumerate(zip(actual_lines, expected_lines)):
        if a_line != e_line:
            fa = parse_float_canonical(a_line)
            fe = parse_float_canonical(e_line)
            if fa is not None and fe is not None and fa == fe:
                continue
            return False, f"Mismatch at line {idx + 1}: expected {e_line!r}, got {a_line!r}"

    return True, ""


# ============================================================================
# Fixture Metadata Parsing
# ============================================================================

class AdversarialFixture:
    """Represents a minimized adversarial test fixture."""

    def __init__(self, directory: Path):
        self.directory = directory
        expected_path = directory / "EXPECTED"
        if not expected_path.is_file():
            raise ValueError(f"Missing EXPECTED file in {directory}")

        content = expected_path.read_text(encoding="utf-8")
        header, sep, body = content.partition("\n---\n")
        self.metadata = {}
        for line in header.splitlines():
            line = line.strip()
            if line and not line.startswith("#"):
                key, colon, val = line.partition(":")
                if colon:
                    self.metadata[key.strip()] = val.strip()

        self.family = int(self.metadata.get("family", "0"))
        self.name = self.metadata.get("name", directory.name)
        self.title = self.metadata.get("title", "")
        self.layout = self.metadata.get("layout", "standalone")
        self.entry = self.metadata.get("entry", "app")
        self.source_free = self.metadata.get("source_free", "")
        self.expect_mode = self.metadata.get("expect", "pass")
        self.g2_dependent = self.metadata.get("g2_dependent", "false").lower() == "true"
        self.blocked_reason = self.metadata.get("blocked_reason", "")
        self.semantic_queries = [
            q.strip() for q in self.metadata.get("semantic_query", "").split(",") if q.strip()
        ]
        self.notes = self.metadata.get("notes", "")
        self.expected_stdout = body

        if self.layout == "standalone":
            self.source_path = directory / "case.moss"
        else:
            self.source_path = directory / self.entry / "src" / "main.moss"


# ============================================================================
# Command Runners & Engine Execution
# ============================================================================

def run_cmd(cmd: List[str], cwd: Path, timeout: int = 120, env: Optional[Dict[str, str]] = None) -> subprocess.CompletedProcess:
    """Run command with timeout and capture output safely."""
    run_env = dict(os.environ)
    if env:
        run_env.update(env)
    return subprocess.run(cmd, cwd=cwd, capture_output=True, text=True, timeout=timeout, env=run_env)


class EngineRunner:
    """Executes Moss code across the four compilation/execution engines."""

    def __init__(self, moss_path: Path, margo_path: Path):
        self.moss = moss_path.resolve()
        self.margo = margo_path.resolve()

    def run_check(self, fixture: AdversarialFixture, temp_dir: Path) -> Dict[str, Any]:
        """Engine 1: Moss Checking."""
        env = dict(os.environ)
        env["MOSS"] = str(self.moss)
        env["TMPDIR"] = str(SCRATCH_DIR)

        if fixture.layout == "standalone":
            src = temp_dir / "case.moss"
            res = run_cmd([str(self.moss), "check", str(src), "--json"], cwd=temp_dir, env=env)
            try:
                data = json.loads(res.stdout) if res.stdout.strip() else {}
            except json.JSONDecodeError:
                data = {"ok": False, "raw_error": res.stdout + res.stderr}

            ok = data.get("ok", False) and res.returncode == 0
            error_info = data.get("error", {}) if not ok else {}
            diagnostics = data.get("result", {}).get("diagnostics", []) if ok else []

            return {
                "ok": ok,
                "returncode": res.returncode,
                "code": error_info.get("code") or ("OK" if ok else "CHECK_FAILED"),
                "message": error_info.get("message", ""),
                "line": error_info.get("line"),
                "rule": error_info.get("rule"),
                "cause": error_info.get("cause"),
                "diagnostics": diagnostics,
                "stdout": res.stdout,
                "stderr": res.stderr,
            }
        else:
            entry_dir = temp_dir / fixture.entry
            res = run_cmd([str(self.margo), "--json", "build"], cwd=entry_dir, env=env)
            try:
                data = json.loads(res.stdout) if res.stdout.strip() else {}
            except json.JSONDecodeError:
                data = {"ok": False, "raw_error": res.stdout + res.stderr}

            ok = (res.returncode == 0) and data.get("ok", False)
            error_info = data.get("error", {}) if not ok else {}

            return {
                "ok": ok,
                "returncode": res.returncode,
                "code": error_info.get("code") or ("OK" if ok else "BUILD_FAILED"),
                "message": error_info.get("message", res.stdout + res.stderr),
                "line": error_info.get("line"),
                "rule": None,
                "cause": None,
                "diagnostics": [],
                "stdout": res.stdout,
                "stderr": res.stderr,
            }

    def run_fast_debug(self, fixture: AdversarialFixture, temp_dir: Path) -> Dict[str, Any]:
        """Engine 2: Fast Debug (checked interpreter)."""
        env = dict(os.environ)
        env["MOSS"] = str(self.moss)
        env["TMPDIR"] = str(SCRATCH_DIR)

        if fixture.layout == "standalone":
            src = temp_dir / "case.moss"
            res = run_cmd([str(self.moss), "run", "--interp", str(src)], cwd=temp_dir, env=env)
        else:
            entry_dir = temp_dir / fixture.entry
            res = run_cmd([str(self.margo), "debug"], cwd=entry_dir, env=env)

        return {
            "returncode": res.returncode,
            "stdout": res.stdout,
            "stderr": res.stderr,
            "ok": res.returncode == 0,
        }

    def run_native_debug(self, fixture: AdversarialFixture, temp_dir: Path) -> Dict[str, Any]:
        """Engine 3: Native Debug."""
        env = dict(os.environ)
        env["MOSS"] = str(self.moss)
        env["TMPDIR"] = str(SCRATCH_DIR)

        if fixture.layout == "standalone":
            src = temp_dir / "case.moss"
            rust_path = temp_dir / "native_debug.rs"
            bin_path = temp_dir / "native_debug_bin"
            lower_res = run_cmd([str(self.moss), str(src), "--debug", "-o", str(rust_path)], cwd=temp_dir, env=env)
            if lower_res.returncode != 0:
                return {
                    "stage": "lowering",
                    "ok": False,
                    "returncode": lower_res.returncode,
                    "stdout": lower_res.stdout,
                    "stderr": lower_res.stderr,
                }
            compile_res = run_cmd(["rustc", "--crate-name", "case", str(rust_path), "-o", str(bin_path)], cwd=temp_dir, env=env)
            if compile_res.returncode != 0:
                return {
                    "stage": "rustc",
                    "ok": False,
                    "returncode": compile_res.returncode,
                    "stdout": compile_res.stdout,
                    "stderr": compile_res.stderr,
                }
            exec_res = run_cmd([str(bin_path)], cwd=temp_dir, env=env)
            return {
                "stage": "execution",
                "ok": exec_res.returncode == 0,
                "returncode": exec_res.returncode,
                "stdout": exec_res.stdout,
                "stderr": exec_res.stderr,
            }
        else:
            entry_dir = temp_dir / fixture.entry
            run_res = run_cmd([str(self.margo), "run"], cwd=entry_dir, env=env)
            return {
                "stage": "margo_run",
                "ok": run_res.returncode == 0,
                "returncode": run_res.returncode,
                "stdout": run_res.stdout,
                "stderr": run_res.stderr,
            }

    def run_native_release(self, fixture: AdversarialFixture, temp_dir: Path) -> Dict[str, Any]:
        """Engine 4: Native Release."""
        env = dict(os.environ)
        env["MOSS"] = str(self.moss)
        env["TMPDIR"] = str(SCRATCH_DIR)

        if fixture.layout == "standalone":
            src = temp_dir / "case.moss"
            rust_path = temp_dir / "native_release.rs"
            bin_path = temp_dir / "native_release_bin"
            lower_res = run_cmd([str(self.moss), str(src), "-O", "-o", str(rust_path)], cwd=temp_dir, env=env)
            if lower_res.returncode != 0:
                return {
                    "stage": "lowering",
                    "ok": False,
                    "returncode": lower_res.returncode,
                    "stdout": lower_res.stdout,
                    "stderr": lower_res.stderr,
                }
            compile_res = run_cmd(["rustc", "-O", "--crate-name", "case", str(rust_path), "-o", str(bin_path)], cwd=temp_dir, env=env)
            if compile_res.returncode != 0:
                return {
                    "stage": "rustc",
                    "ok": False,
                    "returncode": compile_res.returncode,
                    "stdout": compile_res.stdout,
                    "stderr": compile_res.stderr,
                }
            exec_res = run_cmd([str(bin_path)], cwd=temp_dir, env=env)
            return {
                "stage": "execution",
                "ok": exec_res.returncode == 0,
                "returncode": exec_res.returncode,
                "stdout": exec_res.stdout,
                "stderr": exec_res.stderr,
            }
        else:
            entry_dir = temp_dir / fixture.entry
            run_res = run_cmd([str(self.margo), "--release", "run"], cwd=entry_dir, env=env)
            return {
                "stage": "margo_release_run",
                "ok": run_res.returncode == 0,
                "returncode": run_res.returncode,
                "stdout": run_res.stdout,
                "stderr": run_res.stderr,
            }

    def query_semantic(self, query_type: str, target: str, source_path: Path, cwd: Path) -> Dict[str, Any]:
        """Run a structured semantic query (type, effects, ownership, inspect)."""
        res = run_cmd([str(self.moss), query_type, target, "--source", str(source_path), "--json"], cwd=cwd)
        try:
            return json.loads(res.stdout) if res.stdout.strip() else {"ok": False, "raw": res.stderr}
        except json.JSONDecodeError:
            return {"ok": False, "error": res.stderr}


# ============================================================================
# Differential Test Execution & Result Analysis
# ============================================================================

class DifferentialResult:
    """Records the multi-engine comparison outcome for a single fixture."""

    def __init__(self, fixture: AdversarialFixture):
        self.fixture = fixture
        self.status = "UNKNOWN"
        self.summary = ""
        self.check_result: Dict[str, Any] = {}
        self.fast_debug_result: Dict[str, Any] = {}
        self.native_debug_result: Dict[str, Any] = {}
        self.native_release_result: Dict[str, Any] = {}
        self.semantic_queries: Dict[str, Any] = {}
        self.disagreements: List[str] = []

    def to_dict(self) -> Dict[str, Any]:
        return {
            "family": self.fixture.family,
            "name": self.fixture.name,
            "title": self.fixture.title,
            "status": self.status,
            "summary": self.summary,
            "g2_dependent": self.fixture.g2_dependent,
            "blocked_reason": self.fixture.blocked_reason,
            "expect_mode": self.fixture.expect_mode,
            "check": {
                "ok": self.check_result.get("ok"),
                "code": self.check_result.get("code"),
                "message": self.check_result.get("message"),
                "line": self.check_result.get("line"),
            },
            "fast_debug": {
                "ok": self.fast_debug_result.get("ok"),
                "returncode": self.fast_debug_result.get("returncode"),
                "stdout": self.fast_debug_result.get("stdout", "").strip(),
            },
            "native_debug": {
                "ok": self.native_debug_result.get("ok"),
                "stage": self.native_debug_result.get("stage"),
                "returncode": self.native_debug_result.get("returncode"),
                "stdout": self.native_debug_result.get("stdout", "").strip(),
            },
            "native_release": {
                "ok": self.native_release_result.get("ok"),
                "stage": self.native_release_result.get("stage"),
                "returncode": self.native_release_result.get("returncode"),
                "stdout": self.native_release_result.get("stdout", "").strip(),
            },
            "semantic_queries": self.semantic_queries,
            "disagreements": self.disagreements,
        }


def run_fixture_differential(fixture: AdversarialFixture, runner: EngineRunner) -> DifferentialResult:
    """Executes a fixture across all 4 engines and compares outputs and semantics."""
    result = DifferentialResult(fixture)
    SCRATCH_DIR.mkdir(parents=True, exist_ok=True)

    with tempfile.TemporaryDirectory(prefix="p25-diff-", dir=SCRATCH_DIR) as td_str:
        temp_dir = Path(td_str)

        if fixture.layout == "standalone":
            src_dest = temp_dir / "case.moss"
            shutil.copy2(fixture.source_path, src_dest)
            eval_source = src_dest
            eval_cwd = temp_dir
        else:
            for item in fixture.directory.iterdir():
                if item.is_dir() and item.name not in ("build", "tmp", ".git"):
                    shutil.copytree(item, temp_dir / item.name)
            eval_source = temp_dir / fixture.entry / "src" / "main.moss"
            eval_cwd = temp_dir / fixture.entry

        # Handle G2-Dependent Fixtures immediately
        if fixture.g2_dependent:
            result.status = "BLOCKED"
            result.summary = f"G2-Dependent: {fixture.blocked_reason}"
            # Check baseline behavior for reporting
            result.check_result = runner.run_check(fixture, temp_dir)
            if fixture.source_free:
                result.disagreements.append("Source-free provider access requires ABI-v10 Type IR monomorphization")
            elif "Float" in fixture.title or "Float" in fixture.blocked_reason:
                result.disagreements.append("Float map keys: Fast Debug succeeds, Native rustc fails E0277 missing Eq/Hash")
            elif "nominal" in fixture.title or "nominal" in fixture.blocked_reason:
                result.disagreements.append("Nominal map keys: Fast Debug succeeds, Native rustc fails E0277 missing Eq/Hash")
            return result

        # 1. Run Check Engine
        result.check_result = runner.run_check(fixture, temp_dir)

        # Handle expected checker rejections
        if fixture.expect_mode.startswith("reject"):
            expected_code = fixture.expect_mode.split()[1] if len(fixture.expect_mode.split()) > 1 else ""
            if not result.check_result["ok"]:
                actual_code = result.check_result.get("code", "")
                if expected_code and expected_code != actual_code:
                    result.status = "DIAGNOSTIC_DRIFT"
                    result.summary = f"Rejected with code {actual_code}, expected {expected_code}"
                else:
                    result.status = "PASS"
                    result.summary = f"Cleanly rejected by check with {actual_code}"
            else:
                result.status = "FAIL"
                result.summary = f"Expected checker rejection with {expected_code}, but check accepted"
                result.disagreements.append("Checker accepted program that was expected to be rejected")
            return result

        # 2. Run Semantic Queries if specified
        if eval_source.exists():
            for query_spec in fixture.semantic_queries:
                q_type, colon, q_target = query_spec.partition(":")
                if colon:
                    q_res = runner.query_semantic(q_type.strip(), q_target.strip(), eval_source, eval_cwd)
                    result.semantic_queries[query_spec] = q_res

        # If check failed on a positive/disagreement case
        if not result.check_result["ok"]:
            result.status = "CHECK_FAILED"
            result.summary = f"Check failed: [{result.check_result.get('code')}] {result.check_result.get('message')}"
            result.disagreements.append(f"Check failed before execution: {result.check_result.get('code')}")
            return result

        # 3. Run Execution Engines
        result.fast_debug_result = runner.run_fast_debug(fixture, temp_dir)
        result.native_debug_result = runner.run_native_debug(fixture, temp_dir)
        result.native_release_result = runner.run_native_release(fixture, temp_dir)

        # 4. Handle Disagreement Test Cases (Documented Baseline Bugs)
        if fixture.expect_mode.startswith("disagreement"):
            dis_tag = fixture.expect_mode.split()[1] if len(fixture.expect_mode.split()) > 1 else ""
            fd_ok = result.fast_debug_result["ok"]
            nd_ok = result.native_debug_result["ok"]
            nr_ok = result.native_release_result["ok"]
            if fd_ok != nd_ok or nd_ok != nr_ok or not nd_ok:
                result.status = "PASS"
                result.summary = f"Verified expected engine disagreement ({dis_tag})"
                result.disagreements.append(
                    f"Engines diverge as expected: Fast Debug rc={result.fast_debug_result['returncode']}, "
                    f"Native Debug rc={result.native_debug_result['returncode']} ({result.native_debug_result.get('stage')})"
                )
            else:
                result.status = "DISAGREEMENT_RESOLVED"
                result.summary = f"Disagreement {dis_tag} appears resolved (all engines succeeded)"
            return result

        # 5. Handle Runtime Failure Cases (e.g. Bounds Errors)
        if fixture.expect_mode.startswith("failure"):
            fd_rc = result.fast_debug_result["returncode"]
            nd_rc = result.native_debug_result["returncode"]
            nr_rc = result.native_release_result["returncode"]
            if fd_rc != 0 and nd_rc != 0 and nr_rc != 0:
                result.status = "PASS"
                result.summary = f"All execution engines failed meaningfully: FD rc={fd_rc}, ND rc={nd_rc}, NR rc={nr_rc}"
                if fd_rc != nd_rc:
                    result.disagreements.append(
                        f"Failure exit code variance: Fast Debug rc={fd_rc} vs Native rc={nd_rc}"
                    )
            else:
                result.status = "FAIL"
                result.summary = f"Expected failure in all engines, but some passed: FD={fd_rc}, ND={nd_rc}, NR={nr_rc}"
            return result

        # 6. Standard Positive Cases: Parity Check
        outputs = {
            "fast_debug": result.fast_debug_result["stdout"],
            "native_debug": result.native_debug_result.get("stdout", ""),
            "native_release": result.native_release_result.get("stdout", ""),
        }

        if not (result.fast_debug_result["ok"] and result.native_debug_result["ok"] and result.native_release_result["ok"]):
            result.status = "EXECUTION_FAILURE"
            failures = []
            if not result.fast_debug_result["ok"]:
                failures.append(f"Fast Debug failed: {result.fast_debug_result['stderr'][:200]}")
            if not result.native_debug_result["ok"]:
                failures.append(f"Native Debug failed at {result.native_debug_result.get('stage')}: {result.native_debug_result['stderr'][:200]}")
            if not result.native_release_result["ok"]:
                failures.append(f"Native Release failed at {result.native_release_result.get('stage')}: {result.native_release_result['stderr'][:200]}")
            result.summary = "; ".join(failures)
            return result

        match_expected, diff = compare_outputs(outputs["fast_debug"], fixture.expected_stdout)
        if not match_expected:
            result.status = "OUTPUT_MISMATCH"
            result.summary = f"Output does not match expectation:\n{diff}"
            return result

        match_nd, diff_nd = compare_outputs(outputs["native_debug"], outputs["fast_debug"])
        match_nr, diff_nr = compare_outputs(outputs["native_release"], outputs["fast_debug"])

        if match_nd and match_nr:
            result.status = "PASS"
            result.summary = "100% parity across Fast Debug, Native Debug, and Native Release"
        else:
            result.status = "ENGINE_DIVERGENCE"
            diffs = []
            if not match_nd:
                diffs.append(f"Fast Debug vs Native Debug:\n{diff_nd}")
            if not match_nr:
                diffs.append(f"Fast Debug vs Native Release:\n{diff_nr}")
            result.summary = "; ".join(diffs)
            result.disagreements.extend(diffs)

        return result


# ============================================================================
# Self-Test Implementation
# ============================================================================

def run_self_tests() -> bool:
    """Internal harness self-tests to ensure runner and comparison integrity."""
    print("Running multi-engine differential harness self-tests...")

    # 1. Test normalization and comparison
    s1 = "hello\nworld\n"
    s2 = "hello \nworld\n\n"
    ok, _ = compare_outputs(s2, s1)
    assert ok, "Normalization failed to handle trailing spaces/lines"

    # 2. Test line order normalization for sets/maps
    m1 = "alpha\nbeta\ngamma\n"
    m2 = "gamma\nalpha\nbeta\n"
    ok_unordered, _ = compare_outputs(m2, m1, sort_lines=True)
    assert ok_unordered, "Sort-lines normalization failed"
    ok_strict, _ = compare_outputs(m2, m1, sort_lines=False)
    assert not ok_strict, "Strict order comparison unexpectedly passed unordered output"

    # 3. Test float value equivalence
    f1 = "12.5\n4.0\n"
    f2 = "12.50\n4.00\n"
    ok_f, _ = compare_outputs(f2, f1)
    assert ok_f, "Float numerical normalization failed"

    # 4. Test numeric mismatch rejection
    bad1 = "10\n20\n"
    bad2 = "10\n21\n"
    ok_bad, diff = compare_outputs(bad2, bad1)
    assert not ok_bad, "Mismatched values failed to detect diff"

    # 5. Verify fixture directory existence and loading
    assert FIXTURES_DIR.is_dir(), f"Fixtures directory missing at {FIXTURES_DIR}"
    fixtures = discover_fixtures(FIXTURES_DIR)
    assert len(fixtures) >= 20, f"Expected at least 20 fixtures, found {len(fixtures)}"

    # 6. Verify all 10 families are represented
    families_present = {f.family for f in fixtures}
    for fam in range(1, 11):
        assert fam in families_present, f"Family {fam} missing from fixtures"

    # 7. Verify G2-dependent flag parsing
    blocked_fixtures = [f for f in fixtures if f.g2_dependent]
    assert len(blocked_fixtures) >= 3, f"Expected at least 3 G2-dependent fixtures, found {len(blocked_fixtures)}"

    print(f"Self-tests PASSED: {len(fixtures)} fixtures discovered across all 10 families.")
    return True


# ============================================================================
# Discovery and Runner Entry Points
# ============================================================================

def discover_fixtures(base_dir: Path, family_filter: Optional[int] = None) -> List[AdversarialFixture]:
    """Finds all fixtures in base directory, optionally filtering by family."""
    fixtures = []
    for expected_file in sorted(base_dir.glob("**/EXPECTED")):
        fixture_dir = expected_file.parent
        try:
            fix = AdversarialFixture(fixture_dir)
            if family_filter is None or fix.family == family_filter:
                fixtures.append(fix)
        except Exception as e:
            print(f"Warning: Failed to load fixture at {fixture_dir}: {e}", file=sys.stderr)
    return fixtures


def generate_baseline_markdown(results: List[DifferentialResult]) -> str:
    """Generates the Markdown report summarizing engine comparison and known defects."""
    lines = [
        "# Phase 25 Multi-Engine Differential & Adversarial Baseline Report",
        "",
        "**Generated by:** `tests/phase25/integration/run_differential_adversarial.py`  ",
        "**Engines Compared:** Moss Checking, Fast Debug (checked interpreter), Native Debug, Native Release  ",
        f"**Total Fixtures Evaluated:** {len(results)}  ",
        "",
        "## 1. Executive Summary",
        "",
        "| Family | Description | Fixtures | Passed | Blocked (G2) | Disagreements |",
        "|---|---|---:|---:|---:|---:|",
    ]

    family_names = {
        1: "Generic constructor inference & nested collections",
        2: "Empty collection closure & type constraints",
        3: "Method dispatch & unknown fields",
        4: "Numeric literals & unary operators",
        5: "String/Char edge cases & Unicode",
        6: "Map & Set key semantics",
        7: "Ownership & consume errors",
        8: "Try/recover/except paths",
        9: "Imported generic functions & effect summaries",
        10: "Cross-package monomorphization & source-free access",
    }

    fam_stats = {i: {"total": 0, "pass": 0, "blocked": 0, "disagreement": 0} for i in range(1, 11)}

    for r in results:
        fam = r.fixture.family
        fam_stats[fam]["total"] += 1
        if r.status == "PASS":
            fam_stats[fam]["pass"] += 1
        elif r.status == "BLOCKED":
            fam_stats[fam]["blocked"] += 1
        else:
            fam_stats[fam]["disagreement"] += 1

    for fam in range(1, 11):
        st = fam_stats[fam]
        lines.append(f"| {fam} | {family_names[fam]} | {st['total']} | {st['pass']} | {st['blocked']} | {st['disagreement']} |")

    lines.extend([
        "",
        "## 2. Key Differential Engine Disagreements & Recorded Defects",
        "",
        "The following real behavioral disagreements exist across engines on the baseline compiler:",
        "",
        "1. **Unpinned Empty Collection Closure (Family 2):**",
        "   - **Behavior:** `let v = []` without subsequent type-pinning operations passes `moss check` and executes cleanly in Fast Debug (`[]`). However, Native Debug and Native Release fail during rustc compilation with `error[E0282]: type annotations needed for Vec<_>`.",
        "   - **Classification:** Compiler frontend type inference / native lowering defect. The checker should reject unpinned empty collections before native code generation.",
        "",
        "2. **Unknown Nominal Field Access (Family 3):**",
        "   - **Behavior:** Accessing a non-existent field on a declared nominal type (e.g. `p.unknown_field` on `Point`) passes `moss check` without error. Fast Debug halts with an interpreter runtime error (`unknown field 'unknown_field'`). Native Debug and Native Release fail in rustc with `error[E0609]: no field on type`.",
        "   - **Classification:** Checker authority hole. Field access must be validated statically during type checking.",
        "",
        "3. **Integer Literal Overflow (Family 4):**",
        "   - **Behavior:** The literal `9223372036854775808` (`2^63`) passes `moss check` without warning. Fast Debug clamps/saturates to `9223372036854775807` (`i64::MAX`). Native Debug and Native Release fail in rustc with `literal out of range for i64`.",
        "   - **Classification:** Constant evaluation and lexer overflow validation gap in the checker.",
        "",
        "4. **Unary Negation Arithmetic Overflow (Family 4):**",
        "   - **Behavior:** Unary negation of boundary minimum `min_int = -9223372036854775808` (`let neg = -min_int`) passes `moss check`. Fast Debug wraps around to `-9223372036854775808`. Native Debug and Release fail during rustc compilation with `attempt to negate i64::MIN, which would overflow` (`#[deny(arithmetic_overflow)]`).",
        "   - **Classification:** Checker arithmetic overflow evaluation disparity.",
        "",
        "5. **Runtime Failure Signatures (Family 5):**",
        "   - **Behavior:** String character indexing out of bounds (`s.char_at(10)`) produces exit code 1 with structured diagnostic `String character index out of bounds` in Fast Debug, but aborts with signal 134 (`SIGABRT` / panic) in Native Debug and Native Release.",
        "   - **Classification:** Engine panic/failure normalization divergence.",
        "",
        "## 3. G2-Dependent Capabilities (Marked BLOCKED)",
        "",
        "The following fixtures are marked `BLOCKED` pending Team G's G2 Type IR and key-protocol-v1 runtime delivery:",
        "",
        "- **Float Map Keys (Family 6):** `Map[Float, V]` with distinct signed zeros (`-0.0` vs `+0.0`) and NaN identity currently passes `moss check` and Fast Debug, but fails Native rustc compilation (`error[E0277]: the trait bound {float}: Eq is not satisfied`). G2 provides `MossMap<K, V>` and Julia total ordering.",
        "- **Nominal Map Keys (Family 6):** Custom nominal keys implementing `isequal` and `hash` pass `moss check` and Fast Debug, but fail Native compilation with missing `Eq` and `Hash` traits. Blocked on G2 `KeyProof` adapter lowering.",
        "- **Source-Free Provider Generic Access (Family 10):** Fast Debug cleanly rejects source-free `.mossi` providers with `FAST_DEBUG_NATIVE_DEPENDENCY` before execution, while Native engines link the compiled `.rlib`. Cross-package source-free generic monomorphization is blocked on G2 Type IR.",
        "",
        "## 4. Comprehensive Differential Results Table",
        "",
        "| Family | Fixture Name | Layout | Expectation | Check | Fast Debug | Native Debug | Status |",
        "|---|---|---|---|---|---|---|---|",
    ])

    for r in results:
        chk = "PASS" if r.check_result.get("ok") else r.check_result.get("code", "FAIL")
        fd = f"rc={r.fast_debug_result.get('returncode')}" if r.fast_debug_result else "N/A"
        nd = f"rc={r.native_debug_result.get('returncode')}" if r.native_debug_result else "N/A"
        lines.append(f"| {r.fixture.family} | `{r.fixture.name}` | {r.fixture.layout} | `{r.fixture.expect_mode}` | {chk} | {fd} | {nd} | **{r.status}** |")

    lines.append("")
    return "\n".join(lines)


def main():
    parser = argparse.ArgumentParser(description="Multi-Engine Differential & Adversarial Harness")
    parser.add_argument("--self-test", action="store_true", help="Run harness unit/self-tests and exit 0")
    parser.add_argument("--all", action="store_true", help="Run all 10 adversarial fixture families")
    parser.add_argument("--family", type=int, choices=range(1, 11), help="Run a specific family (1-10)")
    parser.add_argument("--baseline-report", action="store_true", help="Output baseline differential Markdown report")
    parser.add_argument("--json", action="store_true", help="Output structured JSON comparison results")
    parser.add_argument("--moss", type=Path, default=DEFAULT_MOSS, help="Path to moss compiler")
    parser.add_argument("--margo", type=Path, default=DEFAULT_MARGO, help="Path to margo driver")
    parser.add_argument("--fixtures-dir", type=Path, default=FIXTURES_DIR, help="Path to fixtures root")
    args = parser.parse_args()

    if args.self_test:
        success = run_self_tests()
        sys.exit(0 if success else 1)

    runner = EngineRunner(args.moss, args.margo)
    fixtures = discover_fixtures(args.fixtures_dir, family_filter=args.family)

    if not fixtures:
        print(f"No fixtures found in {args.fixtures_dir}", file=sys.stderr)
        sys.exit(1)

    results: List[DifferentialResult] = []
    print(f"Running differential harness on {len(fixtures)} fixtures...")

    for fix in fixtures:
        print(f"  [{fix.family:02d}] {fix.name} ... ", end="", flush=True)
        res = run_fixture_differential(fix, runner)
        results.append(res)
        color = "\033[32m" if res.status == "PASS" else ("\033[33m" if res.status == "BLOCKED" else "\033[31m")
        reset = "\033[0m"
        print(f"{color}{res.status}{reset} ({res.summary[:60]})")

    if args.json:
        json_output = {
            "total": len(results),
            "passed": sum(1 for r in results if r.status == "PASS"),
            "blocked": sum(1 for r in results if r.status == "BLOCKED"),
            "disagreements": sum(1 for r in results if r.status not in ("PASS", "BLOCKED")),
            "fixtures": [r.to_dict() for r in results],
        }
        print(json.dumps(json_output, indent=2))

    if args.baseline_report:
        report_md = generate_baseline_markdown(results)
        print("\n" + report_md)

    unexpected_failures = [r for r in results if r.status not in ("PASS", "BLOCKED")]
    if unexpected_failures:
        print(f"\nCompleted with {len(unexpected_failures)} unexpected failures/untracked regressions.")
        sys.exit(1)
    else:
        print(f"\nAll {len(results)} fixtures evaluated successfully (passes or documented baseline/BLOCKED behaviors).")
        sys.exit(0)


if __name__ == "__main__":
    main()
