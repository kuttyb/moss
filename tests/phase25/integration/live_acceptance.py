"""Execute bounded Phase 25 gate proofs against this checkout's HEAD.

The matrix is a historical ledger.  Only the explicit case mappings below may
promote a gate in a live report; a suite's zero exit status is never, by itself,
evidence for an unrelated gate.
"""

from __future__ import annotations

import copy
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import sys
from typing import Any


ROOT = Path(__file__).resolve().parents[3]
G2_PATH = "tests/tooling/check_phase25_conformance.py"
E3_PATH = "tests/phase25/integration/cross_crate_harness.py"
KEY_PATH = "tests/phase25/key_protocol/run_fixtures.py"
B_PATH = "tests/phase25/check_phase25_b_acceptance.py"


# Every mapped case is an exact assertion in the named executable suite.
# Gates with broader, untested requirements deliberately have no mapping.
PROOFS: dict[str, tuple[tuple[str, str], ...]] = {
    **{f"GATE-TYPEIR-{n:02d}": (("g2", str(n)),) for n in range(1, 12)},
    "GATE-ABIV10-01": (("e3-debug", "1"), ("e3-release", "1"),
                        ("e3-debug", "4"), ("e3-debug", "5")),
    "GATE-ABIV10-02": (("e3-debug", "4"), ("e3-release", "4")),
    "GATE-ABIV10-03": (("g2", "4"), ("e3-debug", "5"),
                        ("e3-release", "5"), ("e3-debug", "6"),
                        ("e3-release", "6")),
    "GATE-ABIV10-06": (("g2", "5"), ("e3-debug", "7"),
                        ("e3-release", "7")),
    "GATE-ABIV10-07": (("g2", "7"), ("e3-debug", "8"),
                        ("e3-release", "8")),
    "GATE-ABIV10-08": (("g2", "10"), ("b", "gate10"),
                        ("key", "04-two-consumers-shared-map")),
    "GATE-ABIV10-10": (("e3-debug", "9"), ("e3-release", "9"),
                        ("e3-debug", "11"), ("e3-release", "11")),
    "GATE-ABIV10-11": (("g2", "11"),),
    "GATE-G3-06": (("g2", "10"), ("b", "gate10"),
                   ("key", "03-imported-nominal-keys-source-free")),
    "GATE-G3-07": (("b", "gate10"),
                   ("key", "05-coherence-mismatch-method-override")),
    "GATE-G5-04": (("key", "all30"),),
}

# Gate 10's exact sealed proof identity is asserted by Team B, beyond G2's
# two native consumers and nominal conflict negative.
PROOFS["GATE-TYPEIR-10"] += (("b", "gate10"),)

SUITE_PATHS = {"g2": G2_PATH, "e3-debug": E3_PATH,
               "e3-release": E3_PATH, "key": KEY_PATH, "b": B_PATH}
KEY_EXACT_STATUS = {
    "03-imported-nominal-keys-source-free": "passes",
    "04-two-consumers-shared-map": "passes",
    "05-coherence-mismatch-method-override":
        "fail-closed KEY_PROOF_COHERENCE_MISMATCH",
}


def parse_suite(suite: str, stdout: str, returncode: int) -> dict[str, bool]:
    """Require each suite's complete success marker and exact case identities."""
    cases: dict[str, bool] = {}
    if suite == "g2":
        matches = re.findall(r"(?m)^G2 gate (\d+): (PASS|FAIL|PENDING)(?:\b|:)", stdout)
        complete = (returncode == 0 and len(matches) == 11 and
                    {int(n) for n, _ in matches} == set(range(1, 12)) and
                    "G2 conformance: 11/11 actual Moss gates passed" in stdout)
        for n, status in matches:
            cases[n] = complete and status == "PASS"
    elif suite.startswith("e3-"):
        try:
            data = json.loads(stdout)
            profile = suite.partition("-")[2]
            results = data["results"]
            complete = (returncode == 0 and data["profile"] == profile and
                        len(results) == 11 and
                        {r["scenario"] for r in results} == set(range(1, 12)) and
                        data["summary"] == {"total": 11, "passed": 11,
                                            "blocked": 0, "failed": 0})
            for result in results:
                cases[str(result["scenario"])] = complete and result["status"] == "PASS"
        except (ValueError, KeyError, TypeError):
            pass
    elif suite == "key":
        matches = re.findall(r"(?m)^  (PASS|FAIL): ([a-zA-Z0-9_-]+) -> ([^\n]+)$", stdout)
        fixture_names = {p.parent.name for p in (ROOT / "tests/phase25/key_protocol").glob("*/EXPECTED")}
        complete = (returncode == 0 and len(matches) == 30 and
                    fixture_names == {name for _, name, _ in matches} and
                    "=== all fixtures activated ===" in stdout)
        cases = {
            name: complete and status == "PASS" and
            (name not in KEY_EXACT_STATUS or observed == KEY_EXACT_STATUS[name])
            for status, name, observed in matches
        }
        cases["all30"] = complete and all(status == "PASS" for status, _, _ in matches)
    elif suite == "b":
        complete = (returncode == 0 and
                    "Total Verification Checks: 23 PASS, 0 PENDING, 0 FAIL" in stdout and
                    "Joint Gate 10 integration verified with live source-free Moss artifacts." in stdout)
        cases["gate10"] = complete and bool(re.search(
            r"G2 Gate 10: one sealed nominal proof sha256:[0-9a-f]{64}; "
            r"two independent source-free native consumers", stdout))
    return cases


def suite_command(suite: str, compiler: Path) -> list[str]:
    if suite == "g2":
        return [sys.executable, str(ROOT / G2_PATH)]
    if suite.startswith("e3-"):
        return [sys.executable, str(ROOT / E3_PATH), "--all", "--json",
                "--profile", suite.partition("-")[2]]
    if suite == "key":
        return [sys.executable, str(ROOT / KEY_PATH), "--activate", "--moss", str(compiler)]
    if suite == "b":
        return [sys.executable, str(ROOT / B_PATH)]
    raise ValueError(f"Unknown live suite {suite}")


def git_head() -> str:
    return subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=ROOT, text=True).strip()


def clean_tree() -> bool:
    return not subprocess.check_output(["git", "status", "--porcelain"], cwd=ROOT, text=True).strip()


def apply_proofs(gates: list[dict[str, Any]], suites: dict[str, Any],
                 head: str, same_sha: bool) -> list[dict[str, Any]]:
    observed = copy.deepcopy(gates)
    for gate in observed:
        gid = gate["id"]
        gate["declared_status"] = gate["current_status"]
        requirements = PROOFS.get(gid)
        if not requirements:
            if gate["current_status"] == "PASS":
                gate["current_status"] = "BLOCKED"
                gate["blocking_dependency"] = "No exact executable same-SHA proof mapping"
            continue
        passed = all(suites[suite]["cases"].get(case) is True
                     for suite, case in requirements)
        gate["live_evidence"] = {
            "sha": head,
            "same_clean_head": same_sha,
            "cases": [{"suite": suite, "case": case,
                       "passed": suites[suite]["cases"].get(case) is True}
                      for suite, case in requirements],
        }
        if passed and same_sha:
            gate["current_status"] = "PASS"
            gate["blocking_dependency"] = None
            gate["implementation_sha"] = head
            gate["test_path"] = SUITE_PATHS[requirements[0][0]]
            gate["test_evidence"] = (
                f"Live at {head}: " + ", ".join(
                    f"{suite} case {case} ({suites[suite]['stdout_sha256']})"
                    for suite, case in requirements))
        elif not same_sha:
            gate["current_status"] = "BLOCKED"
            gate["blocking_dependency"] = "Live proof requires a clean, unchanged HEAD"
        else:
            gate["current_status"] = "FAIL"
            gate["blocking_dependency"] = None
    return observed


def run_live(gates: list[dict[str, Any]], compiler: Path) -> tuple[list[dict[str, Any]], dict[str, Any]]:
    """Run shared suites serially, then promote only exact passing gate cases."""
    if os.environ.get("MOSS_STAGED_ABI") != "v10":
        raise ValueError("live Phase 25 proof requires MOSS_STAGED_ABI=v10")
    if compiler.resolve() != (ROOT / "moss").resolve():
        raise ValueError("live Phase 25 proof requires this checkout's ./moss")
    if not (ROOT / "margo").is_file():
        raise ValueError("live Phase 25 proof requires this checkout's ./margo")
    start_head = git_head()
    initial_clean = clean_tree()
    suites: dict[str, Any] = {}
    env = dict(os.environ, MOSS_G2_ACTIVATE_JOINT="1", MOSS=str(compiler),
               MARGO=str(ROOT / "margo"), PYTHONDONTWRITEBYTECODE="1")
    # Worktrees share filesystem clocks; a compiler copied or rebuilt on a
    # different branch may appear newer than sources. Force this HEAD's build.
    build_command = ["make", "-B", f"--jobs={os.cpu_count() or 1}", "moss"]
    built = subprocess.run(build_command, cwd=ROOT, env=env, text=True,
                           capture_output=True, check=False)
    if built.returncode != 0:
        raise ValueError(f"same-checkout make failed: {(built.stdout + built.stderr)[-1200:]}")
    for suite in SUITE_PATHS:
        command = suite_command(suite, compiler)
        proc = subprocess.run(command, cwd=ROOT, env=env, text=True,
                              capture_output=True, check=False)
        cases = parse_suite(suite, proc.stdout, proc.returncode)
        suites[suite] = {
            "command": command,
            "path": SUITE_PATHS[suite],
            "returncode": proc.returncode,
            "cases": cases,
            "stdout_sha256": hashlib.sha256(proc.stdout.encode()).hexdigest(),
            "stderr_tail": proc.stderr[-1200:],
        }
    final_head = git_head()
    final_clean = clean_tree()
    same_sha = initial_clean and final_clean and start_head == final_head
    observed = apply_proofs(gates, suites, start_head, same_sha)
    return observed, {"head": start_head, "same_clean_head": same_sha,
                      "build": {"command": build_command, "returncode": built.returncode,
                                "stdout_sha256": hashlib.sha256(built.stdout.encode()).hexdigest()},
                      "suites": suites}
