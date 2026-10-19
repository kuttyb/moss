#!/usr/bin/env python3
"""Phase 25 Machine-Checkable Acceptance Ledger Runner.

Tracks, validates, and reports acceptance gates covering the frozen Phase 25
requirements across all workstreams:
- Type IR conformance gates (TYPE_IR_CONTRACT §10, GATE-TYPEIR-01..11)
- ABI-v10 proof matrix (ABI_V10_DESIGN §11, GATE-ABIV10-01..11)
- G1 semantics parity/correctness defects (GATE-G1-01..15)
- G3 ordering, equality, hashing & key protocol (GATE-G3-01..08)
- G4 string, char, Unicode & indexed layout (GATE-G4-01..06)
- G5 compiler/runtime integration & SDK matrix (GATE-G5-01..06)
- G6 adversarial falsification & final acceptance (GATE-G6-01..05)

CLI flags:
  --self-test    Executes harness self-test and exits 0 on main / prep branch
  --summary      Prints formatted markdown/tabular summary table
  --json         Outputs machine-readable JSON acceptance report
  --strict       Exits with non-zero code if any gate is not PASS
  --gate <ID>    Inspects / checks specific gate by identifier
"""

from __future__ import annotations

import argparse
import datetime
import json
import os
from pathlib import Path
import subprocess
import sys
from typing import Any

from live_acceptance import PROOFS, apply_proofs, parse_suite, run_live

ROOT = Path(__file__).resolve().parents[3]
DEFAULT_MATRIX_PATH = Path(__file__).resolve().parent / "acceptance_matrix.json"
DEFAULT_COMPILER = ROOT / "moss"

EXPECTED_GROUPS = {
    "TYPEIR": 11,
    "ABIV10": 11,
    "G1": 15,
    "G3": 8,
    "G4": 6,
    "G5": 6,
    "G6": 5,
}
VALID_STATUSES = {"PASS", "FAIL", "BLOCKED", "NOT IMPLEMENTED"}
VALID_OWNERS = {"Team G", "Team S", "Team I", "Joint"}
REQUIRED_FIELDS = (
    "id",
    "group",
    "requirement",
    "owning_team",
    "test_path",
    "engine_configs",
    "current_status",
    "blocking_dependency",
    "test_evidence",
    "source_contract",
)


def load_matrix(matrix_path: Path) -> list[dict[str, Any]]:
    if not matrix_path.is_file():
        raise FileNotFoundError(f"Acceptance matrix file not found: {matrix_path}")
    try:
        data = json.loads(matrix_path.read_text(encoding="utf-8"))
    except json.JSONDecodeError as exc:
        raise ValueError(f"Corrupt JSON in acceptance matrix {matrix_path}: {exc}") from exc
    if not isinstance(data, list):
        raise ValueError(f"Acceptance matrix must be a JSON array of gates, got {type(data)}")
    return data


def get_git_info(cwd: Path = ROOT) -> dict[str, str]:
    info = {"commit": "unknown", "branch": "unknown", "dirty": "unknown"}
    try:
        res = subprocess.run(
            ["git", "rev-parse", "HEAD"],
            cwd=cwd,
            capture_output=True,
            text=True,
            check=False,
        )
        if res.returncode == 0:
            info["commit"] = res.stdout.strip()
        res = subprocess.run(
            ["git", "rev-parse", "--abbrev-ref", "HEAD"],
            cwd=cwd,
            capture_output=True,
            text=True,
            check=False,
        )
        if res.returncode == 0:
            info["branch"] = res.stdout.strip()
        res = subprocess.run(
            ["git", "status", "--porcelain"],
            cwd=cwd,
            capture_output=True,
            text=True,
            check=False,
        )
        if res.returncode == 0:
            info["dirty"] = "true" if bool(res.stdout.strip()) else "false"
    except Exception:
        pass
    return info


def compute_summary(gates: list[dict[str, Any]]) -> dict[str, Any]:
    total = len(gates)
    counts = {"PASS": 0, "FAIL": 0, "BLOCKED": 0, "NOT IMPLEMENTED": 0}
    by_group: dict[str, dict[str, int]] = {}
    by_owner: dict[str, dict[str, int]] = {}

    for gate in gates:
        status = gate.get("current_status", "NOT IMPLEMENTED")
        group = gate.get("group", "OTHER")
        owner = gate.get("owning_team", "UNKNOWN")

        counts[status] = counts.get(status, 0) + 1

        if group not in by_group:
            by_group[group] = {"TOTAL": 0, "PASS": 0, "FAIL": 0, "BLOCKED": 0, "NOT IMPLEMENTED": 0}
        by_group[group]["TOTAL"] += 1
        by_group[group][status] = by_group[group].get(status, 0) + 1

        if owner not in by_owner:
            by_owner[owner] = {"TOTAL": 0, "PASS": 0, "FAIL": 0, "BLOCKED": 0, "NOT IMPLEMENTED": 0}
        by_owner[owner]["TOTAL"] += 1
        by_owner[owner][status] = by_owner[owner].get(status, 0) + 1

    return {
        "total_gates": total,
        "counts": counts,
        "by_group": by_group,
        "by_owner": by_owner,
    }


def format_markdown_summary(summary: dict[str, Any], git_info: dict[str, str]) -> str:
    lines = [
        "# Phase 25 Machine-Checkable Acceptance Gates Summary",
        "",
        f"- **Timestamp:** `{datetime.datetime.now(datetime.timezone.utc).isoformat()}`",
        f"- **Git Branch:** `{git_info.get('branch', 'unknown')}`",
        f"- **Git Commit:** `{git_info.get('commit', 'unknown')}`",
        f"- **Total Gates:** {summary['total_gates']}",
        f"- **PASS:** {summary['counts']['PASS']}",
        f"- **FAIL:** {summary['counts']['FAIL']}",
        f"- **BLOCKED:** {summary['counts']['BLOCKED']}",
        f"- **NOT IMPLEMENTED:** {summary['counts']['NOT IMPLEMENTED']}",
        "",
        "## Acceptance Breakdown by Area",
        "",
        "| Area | Total | Pass | Fail | Blocked | Not Implemented | Status |",
        "|---|---:|---:|---:|---:|---:|---|",
    ]

    group_labels = {
        "TYPEIR": "Type IR Conformance (TYPE_IR_CONTRACT §10)",
        "ABIV10": "ABI-v10 Proof Matrix (ABI_V10_DESIGN §11)",
        "G1": "G1 Semantics & Correctness (#1–#15)",
        "G3": "G3 Ordering, Equality, Hashing & Key Protocol",
        "G4": "G4 String, Char, Unicode & Indexed Layout",
        "G5": "G5 Compiler/Runtime Integration & SDK Matrix",
        "G6": "G6 Adversarial Falsification & Final Sign-Off",
    }

    for grp, counts in summary["by_group"].items():
        label = group_labels.get(grp, grp)
        status_tag = "READY" if counts["PASS"] == counts["TOTAL"] else (
            "FAILING" if counts["FAIL"] > 0 else "IN PROGRESS / BLOCKED"
        )
        lines.append(
            f"| {label} | {counts['TOTAL']} | {counts['PASS']} | "
            f"{counts['FAIL']} | {counts['BLOCKED']} | {counts['NOT IMPLEMENTED']} | {status_tag} |"
        )

    lines.extend([
        "",
        "## Acceptance Breakdown by Owning Team",
        "",
        "| Owning Team | Total | Pass | Fail | Blocked | Not Implemented |",
        "|---|---:|---:|---:|---:|---:|",
    ])

    for owner, counts in summary["by_owner"].items():
        lines.append(
            f"| {owner} | {counts['TOTAL']} | {counts['PASS']} | "
            f"{counts['FAIL']} | {counts['BLOCKED']} | {counts['NOT IMPLEMENTED']} |"
        )

    lines.append("")
    return "\n".join(lines)


def run_self_test(matrix_path: Path) -> int:
    """Self-test harness validating matrix structure and integrity."""
    print("Running acceptance runner self-test...")
    errors: list[str] = []

    # 1. Load matrix
    try:
        gates = load_matrix(matrix_path)
    except Exception as exc:
        print(f"[FAIL] Could not load matrix: {exc}")
        return 1

    # 2. Check total gate count
    expected_total = sum(EXPECTED_GROUPS.values())
    if len(gates) != expected_total:
        errors.append(f"Expected {expected_total} gates in matrix, found {len(gates)}")

    # 3. Check group counts
    found_groups: dict[str, int] = {}
    gate_ids = set()
    for i, gate in enumerate(gates, 1):
        gid = gate.get("id")
        if not gid:
            errors.append(f"Gate index {i} missing 'id'")
            continue
        if gid in gate_ids:
            errors.append(f"Duplicate gate ID: {gid}")
        gate_ids.add(gid)

        # Check required fields
        for field in REQUIRED_FIELDS:
            if field not in gate or gate[field] is None:
                errors.append(f"Gate {gid}: missing required field '{field}'")
            elif isinstance(gate[field], str) and not gate[field].strip():
                errors.append(f"Gate {gid}: empty string in required field '{field}'")

        # Check group
        group = gate.get("group")
        if group not in EXPECTED_GROUPS:
            errors.append(f"Gate {gid}: unexpected group '{group}'")
        found_groups[group] = found_groups.get(group, 0) + 1

        # Check status
        status = gate.get("current_status")
        if status not in VALID_STATUSES:
            errors.append(f"Gate {gid}: invalid status '{status}'")

        # Check owner
        owner = gate.get("owning_team")
        if owner not in VALID_OWNERS:
            errors.append(f"Gate {gid}: invalid owner '{owner}'")

        # Check engine configs
        configs = gate.get("engine_configs")
        if not isinstance(configs, list) or len(configs) == 0:
            errors.append(f"Gate {gid}: engine_configs must be a non-empty array")

        # Check blocking dependency rules
        dep = gate.get("blocking_dependency")
        if status == "BLOCKED" and (not dep or not dep.strip()):
            errors.append(f"Gate {gid}: status is BLOCKED but blocking_dependency is missing")
        elif status != "BLOCKED" and dep is not None and dep.strip():
            pass

        # Strict evidence validation against false-greens
        gate_errs = validate_gate_evidence(gate, ROOT)
        errors.extend(gate_errs)

    for grp, expected_count in EXPECTED_GROUPS.items():
        actual_count = found_groups.get(grp, 0)
        if actual_count != expected_count:
            errors.append(f"Group {grp}: expected {expected_count} gates, found {actual_count}")

    # 4. Check summary calculation
    summary = compute_summary(gates)
    if summary["total_gates"] != len(gates):
        errors.append("compute_summary total mismatch")

    # 5. Check markdown summary generation
    md = format_markdown_summary(summary, {"commit": "test", "branch": "test"})
    if "# Phase 25 Machine-Checkable Acceptance Gates Summary" not in md:
        errors.append("format_markdown_summary failed to produce header")

    # 6. Check JSON output format
    json_repr = json.dumps(summary)
    if not json_repr:
        errors.append("Failed to serialize summary to JSON")

    # 7. Run deliberate fault-injection suite (anti-false-green proof)
    fi_rc = run_fault_injection_tests(ROOT)
    if fi_rc != 0:
        errors.append("Fault injection tests failed")

    # A suite exit alone cannot close a gate: every numbered/counted case must
    # be present, and a partial or forged PASS report must fail closed.
    known_ids = {gate.get("id") for gate in gates}
    if set(PROOFS) - known_ids:
        errors.append("Live proof mapping names unknown gate IDs")
    if parse_suite("g2", "G2 gate 10: PASS\nG2 conformance: 11/11 actual Moss gates passed", 0).get("10"):
        errors.append("Partial G2 output was accepted as complete")
    if parse_suite("key", "  PASS: 04-two-consumers-shared-map -> passes\n=== all fixtures activated ===", 0).get("all30"):
        errors.append("Partial key fixture output was accepted as complete")
    forged_e3 = json.dumps({"profile": "debug", "results": [{"scenario": 1, "status": "PASS"}],
                            "summary": {"total": 11, "passed": 11, "blocked": 0, "failed": 0}})
    if parse_suite("e3-debug", forged_e3, 0).get("1"):
        errors.append("Partial E3 JSON was accepted as complete")
    partial_ledger = "| reproducer | STAGED |\n|---|---|\n| #1 Int ordering above 2^53 | pass |\n"
    if parse_suite("g1-ledger", partial_ledger, 0).get("1"):
        errors.append("Partial G1 ledger table was accepted as complete")
    partial_g1 = ("  PASS: Defect #1 (64-bit Int precision beyond 2^53)\n"
                  "=== All Phase 25 G1 Regressions Passed Cleanly! ===\n")
    if parse_suite("g1-regression", partial_g1, 0).get("1"):
        errors.append("Partial G1 regression output was accepted as complete")
    partial_laws = ("  PASS: Float ordering laws\n"
                    "=== All Phase 25 L7 repair regressions passed ===\n")
    if parse_suite("l7", partial_laws, 0).get("laws"):
        errors.append("Partial five-type law corpus was accepted as complete")
    partial_g4 = ("  PASS: 14 escapes x 4 forms (3 engines)\n"
                  "=== Phase 25 G4 Char/String cases passed ===\n")
    if parse_suite("g4-char", partial_g4, 0).get("escapes"):
        errors.append("Partial G4 Char suite was accepted as complete")
    partial_indexed = ("  PASS: shared runtime 12800 randomized comparisons, offsets and limit\n"
                       "=== Phase 25 indexed String checks passed ===\n")
    if parse_suite("g4-indexed", partial_indexed, 0).get("layout"):
        errors.append("Shared runtime-only result was accepted without native parity")
    partial_linkage = (
        "shared indexed String debug: two source-free consumers passed with sealed imports\n"
        "shared indexed String runtime mismatch rejected before native linking\n")
    if parse_suite("g4-linkage", partial_linkage, 0).get("source-free"):
        errors.append("One source-free consumer profile was accepted as complete")
    fake_suites = {"g2": {"cases": {"1": True}, "stdout_sha256": "a" * 64}}
    forged_gate = {"id": "GATE-TYPEIR-01", "current_status": "PASS",
                   "blocking_dependency": None}
    if apply_proofs([forged_gate], fake_suites, "a" * 40, False)[0]["current_status"] == "PASS":
        errors.append("Dirty or changed HEAD retained a forged PASS")
    forged_gate["id"] = "GATE-G6-05"
    if apply_proofs([forged_gate], fake_suites, "a" * 40, True)[0]["current_status"] == "PASS":
        errors.append("Unmapped final sign-off gate retained a forged PASS")

    if errors:
        print(f"[FAIL] Self-test found {len(errors)} issues:")
        for err in errors:
            print(f"  - {err}")
        return 1

    print(f"[PASS] Self-test passed cleanly: verified all {len(gates)} gates, 7 groups, matrix invariants, and 4 deliberate fault injections.")
    return 0


def validate_gate_evidence(gate: dict[str, Any], repo_root: Path = ROOT) -> list[str]:
    """Strictly validate evidence for a gate against false-green vulnerabilities."""
    errors: list[str] = []
    gid = gate.get("id", "UNKNOWN")
    status = gate.get("current_status")

    if status == "PASS":
        # Rule 1: test_path must exist on disk and be non-empty
        test_path_str = gate.get("test_path")
        if not test_path_str or not isinstance(test_path_str, str):
            errors.append(f"Gate {gid}: PASS requires non-empty 'test_path'")
        else:
            full_path = repo_root / test_path_str
            if not full_path.exists():
                errors.append(f"Gate {gid}: test_path '{test_path_str}' does not exist on disk")
            elif full_path.is_file() and full_path.stat().st_size == 0:
                errors.append(f"Gate {gid}: test_path '{test_path_str}' is an empty file")

        # Rule 2: implementation_sha must be a valid commit SHA existing in git repository
        impl_sha = gate.get("implementation_sha")
        if not impl_sha or not isinstance(impl_sha, str) or not impl_sha.strip():
            errors.append(f"Gate {gid}: PASS requires valid 'implementation_sha'")
        else:
            sha_clean = impl_sha.strip()
            res = subprocess.run(
                ["git", "cat-file", "-e", f"{sha_clean}^{{commit}}"],
                cwd=repo_root,
                capture_output=True,
                check=False,
            )
            if res.returncode != 0:
                errors.append(f"Gate {gid}: implementation_sha '{sha_clean}' does not exist in git history")

        # Rule 3: test_evidence must be non-empty and free of pending/placeholder/mock markers
        evidence = str(gate.get("test_evidence", "")).strip()
        if not evidence:
            errors.append(f"Gate {gid}: PASS requires non-empty 'test_evidence'")
        else:
            evidence_lower = evidence.lower()
            forbidden = ("feasibility", "pending fixture", "placeholder", "mock", "tbd", "todo")
            for f in forbidden:
                if f in evidence_lower:
                    errors.append(f"Gate {gid}: violates rule: contains forbidden marker '{f}' in test_evidence")

        # Rule 4: No blocking dependency allowed when PASS
        dep = gate.get("blocking_dependency")
        if dep and dep.strip():
            errors.append(f"Gate {gid}: status is PASS but blocking_dependency is present: '{dep}'")

    elif status == "BLOCKED":
        dep = gate.get("blocking_dependency")
        if not dep or not isinstance(dep, str) or not dep.strip():
            errors.append(f"Gate {gid}: status is BLOCKED but blocking_dependency is missing or empty")

    return errors


def run_fault_injection_tests(repo_root: Path = ROOT) -> int:
    """Deliberate fault-injection suite testing the acceptance runner against 4 failure modes:
    1. Missing dependencies cannot produce PASS
    2. Unrelated compiler errors / feasibility cannot produce PASS
    3. Stale evidence / fake SHA / non-existent file cannot produce PASS
    4. Incomplete cross-consumer linking / empty evidence cannot produce PASS
    """
    print("--- Running Acceptance Ledger Fault-Injection Tests ---")
    failures = []

    # Fault 1: Missing dependency gate claiming PASS
    print("  [Fault 1/4] Injecting missing dependency gate marked PASS...")
    fake_gate_1 = {
        "id": "FAULT-GATE-01",
        "current_status": "PASS",
        "blocking_dependency": "BLOCKED: Team G G2 ABI-v10 source-free compiler support",
        "test_path": "tests/phase25/integration/run_acceptance.py",
        "implementation_sha": "bcedb8b",
        "test_evidence": "Genuine test run output",
    }
    errs1 = validate_gate_evidence(fake_gate_1, repo_root)
    if not errs1 or not any("blocking_dependency is present" in e for e in errs1):
        failures.append("Fault 1 failed: Gate with blocking dependency claiming PASS was not rejected")
    else:
        print("    -> PASS: Gate with blocking dependency claiming PASS rejected correctly.")

    # Fault 2: Gate marked PASS with feasibility / pending marker
    print("  [Fault 2/4] Injecting feasibility/pending fixture marked PASS...")
    fake_gate_2 = {
        "id": "FAULT-GATE-02",
        "current_status": "PASS",
        "blocking_dependency": None,
        "test_path": "tests/phase25/integration/run_acceptance.py",
        "implementation_sha": "bcedb8b",
        "test_evidence": "Pending fixture test evidence placeholder",
    }
    errs2 = validate_gate_evidence(fake_gate_2, repo_root)
    if not errs2 or not any("forbidden marker" in e for e in errs2):
        failures.append("Fault 2 failed: Gate with pending fixture marker claiming PASS was not rejected")
    else:
        print("    -> PASS: Feasibility/pending marker claiming PASS rejected correctly.")

    # Fault 3: Gate marked PASS with non-existent file and forged git SHA
    print("  [Fault 3/4] Injecting stale/forged evidence (bad file, fake git SHA)...")
    fake_gate_3a = {
        "id": "FAULT-GATE-03A",
        "current_status": "PASS",
        "blocking_dependency": None,
        "test_path": "non_existent/path/to/test_probe_xyz.moss",
        "implementation_sha": "bcedb8b",
        "test_evidence": "Execution succeeded",
    }
    errs3a = validate_gate_evidence(fake_gate_3a, repo_root)
    if not errs3a or not any("does not exist on disk" in e for e in errs3a):
        failures.append("Fault 3A failed: Gate with non-existent test_path was not rejected")
    else:
        print("    -> PASS: Non-existent test_path rejected correctly.")

    fake_gate_3b = {
        "id": "FAULT-GATE-03B",
        "current_status": "PASS",
        "blocking_dependency": None,
        "test_path": "tests/phase25/integration/run_acceptance.py",
        "implementation_sha": "0000000000000000000000000000000000000000",
        "test_evidence": "Execution succeeded",
    }
    errs3b = validate_gate_evidence(fake_gate_3b, repo_root)
    if not errs3b or not any("does not exist in git history" in e for e in errs3b):
        failures.append("Fault 3B failed: Gate with forged git SHA was not rejected")
    else:
        print("    -> PASS: Forged git SHA rejected correctly.")

    # Fault 4: Incomplete verification / empty evidence
    print("  [Fault 4/4] Injecting empty/incomplete evidence marked PASS...")
    fake_gate_4 = {
        "id": "FAULT-GATE-04",
        "current_status": "PASS",
        "blocking_dependency": None,
        "test_path": "tests/phase25/integration/run_acceptance.py",
        "implementation_sha": "bcedb8b",
        "test_evidence": "   ",
    }
    errs4 = validate_gate_evidence(fake_gate_4, repo_root)
    if not errs4 or not any("non-empty 'test_evidence'" in e for e in errs4):
        failures.append("Fault 4 failed: Gate with empty evidence was not rejected")
    else:
        print("    -> PASS: Empty evidence marked PASS rejected correctly.")

    if failures:
        print(f"[FAIL] Acceptance fault-injection suite failed: {failures}")
        return 1

    print("  [OK] All 4 acceptance ledger fault-injection tests PASSED.")
    return 0


def inspect_gate(gate: dict[str, Any]) -> None:
    print(f"Gate Identifier: {gate.get('id')}")
    print(f"Group:           {gate.get('group')}")
    print(f"Requirement:     {gate.get('requirement')}")
    print(f"Source Contract: {gate.get('source_contract')}")
    print(f"Owning Team:     {gate.get('owning_team')}")
    print(f"Current Status:  {gate.get('current_status')}")
    print(f"Blocking Dep:    {gate.get('blocking_dependency') or 'None'}")
    print(f"Test Path:       {gate.get('test_path')}")
    print(f"Engine Configs:  {', '.join(gate.get('engine_configs', []))}")
    print(f"Impl SHA:        {gate.get('implementation_sha') or 'None'}")
    print(f"Test Evidence:   {gate.get('test_evidence')}")


def main() -> int:
    parser = argparse.ArgumentParser(description="Phase 25 Acceptance Gates Ledger Runner")
    parser.add_argument("--matrix", type=Path, default=DEFAULT_MATRIX_PATH, help="Path to acceptance_matrix.json")
    parser.add_argument("--compiler", type=Path, default=DEFAULT_COMPILER, help="Path to moss compiler")
    parser.add_argument("--self-test", action="store_true", help="Execute harness self-test")
    parser.add_argument("--fault-injection", action="store_true", help="Execute deliberate fault-injection suite")
    parser.add_argument("--summary", action="store_true", help="Print formatted markdown summary")
    parser.add_argument("--json", action="store_true", help="Output machine-readable JSON acceptance report")
    parser.add_argument("--strict", action="store_true", help="Exit with non-zero code if any gate is not PASS")
    parser.add_argument("--live", action="store_true", help="Execute mapped same-SHA gate proofs (implied by --strict)")
    parser.add_argument("--gate", type=str, default=None, help="Inspect / check specific gate by ID")

    args = parser.parse_args()

    if args.fault_injection:
        return run_fault_injection_tests(ROOT)

    if args.self_test:
        return run_self_test(args.matrix)

    try:
        gates = load_matrix(args.matrix)
    except Exception as exc:
        print(f"Error loading acceptance matrix: {exc}", file=sys.stderr)
        return 1

    live_report = None
    if args.live or args.strict:
        try:
            gates, live_report = run_live(gates, args.compiler)
        except (OSError, ValueError, subprocess.SubprocessError) as exc:
            print(f"Live acceptance cannot run: {exc}", file=sys.stderr)
            return 1

    git_info = get_git_info()
    summary = compute_summary(gates)

    if args.gate:
        target = next((g for g in gates if g.get("id") == args.gate), None)
        if not target:
            print(f"Gate '{args.gate}' not found in acceptance matrix.", file=sys.stderr)
            return 1
        if args.json:
            print(json.dumps(target, indent=2))
        else:
            inspect_gate(target)
        if args.strict and target.get("current_status") != "PASS":
            return 1
        return 0

    if args.json:
        report = {
            "timestamp": datetime.datetime.now(datetime.timezone.utc).isoformat(),
            "git": git_info,
            "compiler": str(args.compiler),
            "summary": summary,
            "gates": gates,
        }
        if live_report is not None:
            report["live"] = live_report
        print(json.dumps(report, indent=2))
    else:
        # Default or --summary
        print(format_markdown_summary(summary, git_info))

    if args.strict:
        evidence_errors = [error for gate in gates for error in validate_gate_evidence(gate, ROOT)]
        if evidence_errors:
            print("STRICT MODE EVIDENCE FAILURE:", file=sys.stderr)
            for error in evidence_errors:
                print(f"  {error}", file=sys.stderr)
            return 1
        not_pass = [g for g in gates if g.get("current_status") != "PASS"]
        if not_pass:
            print(f"STRICT MODE FAILURE: {len(not_pass)} gates are not PASS:", file=sys.stderr)
            for g in not_pass:
                print(f"  [{g.get('id')}] {g.get('current_status')}: {g.get('requirement')}", file=sys.stderr)
            return 1

    return 0


if __name__ == "__main__":
    sys.exit(main())
