#!/usr/bin/env python3
"""Deliberate Fault-Injection Test Suite for Phase 25 Integration Prep.

Validates that the acceptance ledger runner and cross-crate harness fail closed
against false-green results:
1. Missing dependencies cannot produce PASS
2. Unrelated compiler errors cannot produce PASS
3. Stale evidence (fake git SHA, non-existent files, pending markers) cannot produce PASS
4. Incomplete cross-consumer linking cannot produce PASS

Usage:
  python3 tests/phase25/integration/test_fault_injection.py
"""

from __future__ import annotations

from pathlib import Path
import copy
import hashlib
import json
import tempfile
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[3]
HARNESS_DIR = ROOT / "tests" / "phase25" / "integration"


def run_command(cmd: list[str], description: str) -> bool:
    print(f"\n[RUN] {description}: {' '.join(cmd)}")
    proc = subprocess.run(cmd, cwd=ROOT, text=True, capture_output=True)
    if proc.returncode != 0:
        print(f"[FAIL] {description} exited with code {proc.returncode}")
        print("STDOUT:", proc.stdout)
        print("STDERR:", proc.stderr)
        return False
    print(proc.stdout.strip())
    print(f"[OK] {description} PASSED.")
    return True


def test_strict_release_integrity() -> bool:
    """Exercise the CLI bypass and independently mutate a valid machine report."""
    import run_acceptance as acceptance

    gates = acceptance.load_matrix(HARNESS_DIR / "acceptance_matrix.json")
    head = acceptance.get_git_info()["commit"]
    scratch = ROOT / "tmp"
    scratch.mkdir(exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="acceptance-integrity-", dir=scratch) as temp:
        work = Path(temp)
        forged = copy.deepcopy(gates)
        for gate in forged:
            gate.update(current_status="PASS", blocking_dependency=None,
                        implementation_sha="0" * 40, test_path="missing-evidence.moss",
                        test_evidence="placeholder")
        duplicate = copy.deepcopy(gates)
        duplicate[1]["id"] = duplicate[0]["id"]
        malformed = copy.deepcopy(gates)
        malformed[0] = {**malformed[0], "current_status": ["PASS"]}
        for name, matrix in (("forged", forged), ("empty", []), ("duplicate", duplicate),
                             ("malformed", malformed)):
            matrix_path = work / f"{name}.json"
            matrix_path.write_text(json.dumps(matrix), encoding="utf-8")
            for extra in ([], ["--gate", "GATE-TYPEIR-01"]):
                proc = subprocess.run([sys.executable, str(HARNESS_DIR / "run_acceptance.py"),
                                       "--strict", "--matrix", str(matrix_path), *extra],
                                      cwd=ROOT, capture_output=True, text=True)
                if proc.returncode == 0 or "INVALID ACCEPTANCE EVIDENCE" not in proc.stderr:
                    raise AssertionError(f"strict CLI accepted {name}: {proc.stdout} {proc.stderr}")

        gate = copy.deepcopy(gates[0])
        gate.update(current_status="PASS", blocking_dependency=None, implementation_sha=head,
                    test_path="tests/phase25/integration/run_acceptance.py",
                    test_evidence="Execution report attached")
        log = work / "execution.log"
        log.write_text("test run completed\n", encoding="utf-8")
        report = {"schema_version": 1, "gate_id": gate["id"], "status": "PASS",
                  "evidence_sha": head, "results": [
                      {"engine": engine, "command": ["python3", "test.py"], "returncode": 0,
                       "log_path": str(log.relative_to(ROOT)),
                       "log_sha256": hashlib.sha256(log.read_bytes()).hexdigest()}
                      for engine in gate["engine_configs"]]}
        report_path = work / f"{gate['id']}.json"
        clean_info = {"commit": head, "dirty": "false"}

        def errors(candidate, git_info=clean_info):
            report_path.write_text(json.dumps(candidate), encoding="utf-8")
            return acceptance.validate_release_evidence(gate, work, git_info)

        assert not errors(report), "valid post-commit machine report was rejected"
        mutations = []
        for key, value in (("evidence_sha", "0" * 40), ("gate_id", "GATE-OTHER-01"),
                           ("status", "BLOCKED"), ("results", [])):
            candidate = copy.deepcopy(report)
            candidate[key] = value
            mutations.append(candidate)
        candidate = copy.deepcopy(report)
        candidate["results"][0]["returncode"] = 1
        mutations.append(candidate)
        candidate = copy.deepcopy(report)
        candidate["results"][0]["log_sha256"] = "0" * 64
        mutations.append(candidate)
        candidate = copy.deepcopy(report)
        candidate["results"][0]["engine"] = "unexecuted-engine"
        mutations.append(candidate)
        candidate = copy.deepcopy(report)
        candidate["results"][0]["command"] = []
        mutations.append(candidate)
        for candidate in mutations:
            assert errors(candidate), f"accepted invalid machine report: {candidate}"
        assert errors(report, {"commit": head, "dirty": "true"})
        report_path.unlink()
        assert acceptance.validate_release_evidence(gate, work, clean_info)
        errors(report)
        log.write_text("tampered output\n", encoding="utf-8")
        assert acceptance.validate_release_evidence(gate, work, clean_info)
    print("[OK] Strict CLI and machine evidence freshness/integrity regressions passed.")
    return True


def main() -> int:
    print("=" * 70)
    print("Phase 25 Deliberate Fault-Injection & Anti-False-Green Verification Suite")
    print("=" * 70)

    test_strict_release_integrity()

    # 1. Test Acceptance Ledger Fault Injection
    ok1 = run_command(
        [sys.executable, str(HARNESS_DIR / "run_acceptance.py"), "--fault-injection"],
        "Acceptance Ledger Anti-False-Green Verification",
    )
    if not ok1:
        return 1

    # 2. Test Cross-Crate Harness Fault Injection
    ok2 = run_command(
        [sys.executable, str(HARNESS_DIR / "cross_crate_harness.py"), "--fault-injection"],
        "Cross-Crate & ABI-v10 Harness Anti-False-Green Verification",
    )
    if not ok2:
        return 1

    # 3. Acceptance runner self-test (includes embedded fault injection)
    ok3 = run_command(
        [sys.executable, str(HARNESS_DIR / "run_acceptance.py"), "--self-test"],
        "Acceptance Runner Full Self-Test",
    )
    if not ok3:
        return 1

    # 4. Cross-crate harness self-test (includes embedded fault injection)
    ok4 = run_command(
        [sys.executable, str(HARNESS_DIR / "cross_crate_harness.py"), "--self-test"],
        "Cross-Crate Harness Full Self-Test",
    )
    if not ok4:
        return 1

    print("\n" + "=" * 70)
    print("ALL 4 DELIBERATE FAULT INJECTIONS VERIFIED CLEANLY: ZERO FALSE GREENS")
    print("=" * 70)
    return 0


if __name__ == "__main__":
    sys.exit(main())
