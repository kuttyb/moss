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

from test_sdk_matrix import PRIMARY_PACKAGES as SDK_PRIMARY_PACKAGES


ROOT = Path(__file__).resolve().parents[3]
G2_PATH = "tests/tooling/check_phase25_conformance.py"
E3_PATH = "tests/phase25/integration/cross_crate_harness.py"
KEY_PATH = "tests/phase25/key_protocol/run_fixtures.py"
B_PATH = "tests/phase25/check_phase25_b_acceptance.py"
G1_LEDGER_PATH = "tests/phase25/ledger/run_ledger.py"
G1_REGRESSION_PATH = "tests/tooling/check_phase25_g1_regressions.py"
S3_PATH = "tests/tooling/check_phase25_s3_key_protocol.py"
G3_ORDERING_PATH = "tests/tooling/check_phase25_g3_ordering.py"
L7_PATH = "tests/tooling/check_phase25_l7_repairs.py"
G4_CHAR_PATH = "tests/tooling/check_phase25_s2_char_string.py"
G4_INDEXED_PATH = "tests/tooling/check_phase25_indexed_string.py"
G4_LINKAGE_PATH = "tests/phase25/runtime/check_shared_string_linkage.py"
G5_RUNTIME_PATH = "tests/phase25/runtime/check_runtime.py"
G5_JOINT_PATH = "tests/phase25/integration/test_joint_ledger_12.py"
G5_SDK_PATH = "tests/phase25/integration/test_sdk_matrix.py"
G3_SNAPSHOT_PATH = "tests/phase25/integration/test_map_snapshot_complexity.py"

G1_LEDGER_ROWS = {
    "1": "#1 Int ordering above 2^53",
    "2": "#2 ordering on user types rejected",
    "3": "#3 one type per binding",
    "4": "#4 parameter-based indexed assignment",
    "5": "#5 repeated discard",
    "6": "#6 assert text with braces",
    "7": "#7 Float.to_string",
    "8": "#8 for traversal",
    "12d": "#12d local collection member call",
    "12": "#12 open receiver (joint Team G G2)",
    "15": "#15 local field member call",
    "10": "#10 cross-package constructor label",
    "14": "#14 raising call in bench",
}

G1_REGRESSION_MARKERS = {
    "2-3": "  PASS: Negative checker cases verified.",
    "10": "  PASS: Semantic resolution verified.",
    "1": "  PASS: Defect #1 (64-bit Int precision beyond 2^53)",
    "4": "  PASS: Defect #4 (parameter-based index expression borrow lowering)",
    "5": "  PASS: Defect #5 (_ = f() discard)",
    "6": "  PASS: Defect #6 (assert formatting escaping)",
    "7": "  PASS: Defect #7 (Float.to_string agreement)",
    "8": "  PASS: Defect #8 (Fast Debug for loop parity)",
    "12-15": "  PASS: Defect #12 / #15 (dotted receiver statement member-call)",
    "14": "  PASS: Defect #14 (raising calls inside benchmarks compile and run)",
}


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
    "GATE-G3-08": (("g3-snapshot", "all"),
                   ("key", "07-keys-snapshot-control"),
                   ("key", "07-keys-snapshot-float"),
                   ("key", "07-keys-snapshot-nominal")),
    "GATE-G5-04": (("key", "all30"),),
    "GATE-G5-01": (("g5-runtime", "all"), ("b", "gate10"),
                    ("g4-linkage", "source-free")),
    "GATE-G5-02": (("g5-joint", "all"), ("g1-ledger", "12")),
    "GATE-G5-03": (("g5-sdk", "all"),),
    "GATE-G1-07": (("g1-ledger", "7"), ("g1-regression", "7")),
    "GATE-G1-01": (("g1-ledger", "1"), ("g1-regression", "1")),
    "GATE-G1-02": (("g1-ledger", "2"), ("g1-regression", "2-3")),
    "GATE-G1-03": (("g1-ledger", "3"), ("g1-regression", "2-3")),
    "GATE-G1-04": (("g1-ledger", "4"), ("g1-regression", "4")),
    "GATE-G1-05": (("g1-ledger", "5"), ("g1-regression", "5")),
    "GATE-G1-06": (("g1-ledger", "6"), ("g1-regression", "6")),
    "GATE-G1-09": (("g2", "5"),),
    "GATE-G1-10": (("g1-ledger", "10"), ("g1-regression", "10")),
    "GATE-G1-11": (("g2", "2"),),
    "GATE-G1-12": (("g1-ledger", "12"),),
    "GATE-G1-13": (("g1-ledger", "12d"), ("g1-regression", "12-15")),
    "GATE-G1-14": (("g1-ledger", "14"), ("g1-regression", "14")),
    "GATE-G1-15": (("g1-ledger", "15"), ("g1-regression", "12-15")),
    "GATE-G3-01": (("s3", "scalar-hashes"), ("g3-ordering", "scalar"),
                   ("l7", "laws")),
    "GATE-G3-02": (("g3-ordering", "float"), ("g3-ordering", "float-keys"),
                   ("s3", "nan"), ("l7", "float-payload"),
                   ("key", "01-float-keys-map"),
                   ("key", "02-float-keys-set")),
    "GATE-G3-03": (("l7", "laws"), ("l7", "float-payload")),
    "GATE-G3-04": (("l7", "laws"), ("l7", "float-payload")),
    "GATE-G4-01": (("g4-char", "char"), ("g4-char", "unicode"),
                   ("l7", "char")),
    "GATE-G4-02": (("g4-char", "constructor"), ("l7", "char")),
    "GATE-G4-03": (("g4-char", "escapes"), ("l7", "invalid-escapes")),
    "GATE-G4-04": (("g4-indexed", "layout"),
                   ("g4-linkage", "source-free")),
    "GATE-G4-06": (("g4-linkage", "source-free"),
                   ("g4-linkage", "mismatch")),
}

# Gate 10's exact sealed proof identity is asserted by Team B, beyond G2's
# two native consumers and nominal conflict negative.
PROOFS["GATE-TYPEIR-10"] += (("b", "gate10"),)

SUITE_PATHS = {"g2": G2_PATH, "e3-debug": E3_PATH,
               "e3-release": E3_PATH, "key": KEY_PATH, "b": B_PATH,
               "g1-ledger": G1_LEDGER_PATH,
               "g1-regression": G1_REGRESSION_PATH,
               "s3": S3_PATH, "g3-ordering": G3_ORDERING_PATH,
               "l7": L7_PATH, "g4-char": G4_CHAR_PATH,
               "g4-indexed": G4_INDEXED_PATH,
               "g4-linkage": G4_LINKAGE_PATH,
               "g5-runtime": G5_RUNTIME_PATH,
               "g5-joint": G5_JOINT_PATH,
               "g5-sdk": G5_SDK_PATH,
               "g3-snapshot": G3_SNAPSHOT_PATH}
KEY_EXACT_STATUS = {
    "01-float-keys-map": "passes",
    "02-float-keys-set": "passes",
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
    elif suite == "g1-ledger":
        rows = re.findall(r"(?m)^\| (#[^|]+) \| ([^|]+) \|$", stdout)
        observed = {name: result.strip() for name, result in rows}
        complete = (returncode == 0 and "| reproducer | STAGED |" in stdout and
                    len(rows) == len(G1_LEDGER_ROWS) and
                    set(observed) == set(G1_LEDGER_ROWS.values()))
        cases = {number: complete and observed.get(name) == "pass"
                 for number, name in G1_LEDGER_ROWS.items()}
    elif suite == "g1-regression":
        lines = stdout.splitlines()
        complete = (returncode == 0 and
                    "=== All Phase 25 G1 Regressions Passed Cleanly! ===" in lines and
                    all(lines.count(marker) == 1 for marker in G1_REGRESSION_MARKERS.values()))
        cases = {number: complete and marker in lines
                 for number, marker in G1_REGRESSION_MARKERS.items()}
    elif suite == "s3":
        lines = stdout.splitlines()
        passed = [line for line in lines if line.startswith("  PASS:")]
        complete = (returncode == 0 and len(passed) == len(set(passed)) == 26 and
                    "=== All Phase 25 S3 key-protocol cases passed (26 cases) ===" in lines)
        cases["scalar-hashes"] = complete and (
            "  PASS: pinned hash values (Int identity, Float bits, Char code point, String FNV-1a)" in lines)
        cases["nan"] = complete and all(marker in lines for marker in (
            "  PASS: computed NaNs are isequal, hash-equal, unordered among themselves, last",
            "  PASS: eight NaN payloads: one hash, isequal, unordered, stable-last (debug and -O)",
        ))
    elif suite == "g3-ordering":
        lines = stdout.splitlines()
        markers = {
            "float": "  PASS: Float total ordering axioms verified.",
            "scalar": "  PASS: Scalar ordering suite verified.",
            "float-keys": "  PASS: staged Float Map/Set signed zeros and NaNs agree across engines.",
            "sort": "  PASS: sort and Reverse verified.",
        }
        complete = (returncode == 0 and
                    "=== All Phase 25 G3 Tests Passed Cleanly! ===" in lines and
                    all(lines.count(marker) == 1 for marker in markers.values()))
        cases = {name: complete and marker in lines for name, marker in markers.items()}
    elif suite == "l7":
        lines = stdout.splitlines()
        passed = [line for line in lines if line.startswith("  PASS:")]
        complete = (returncode == 0 and len(passed) == len(set(passed)) == 48 and
                    "=== All Phase 25 L7 repair regressions passed ===" in lines)
        laws = [f"  PASS: {type_name} ordering laws"
                for type_name in ("Float", "Int", "Char", "String", "Bool")]
        cases["laws"] = complete and all(lines.count(marker) == 1 for marker in laws)
        cases["float-payload"] = complete and (
            "  PASS: quiet/signaling/negative NaN payloads satisfy the Float laws" in lines)
        cases["char"] = complete and all(marker in lines for marker in (
            "  PASS: Char fields, parameters, returns, vectors, and conversions",
            "  PASS: surrogates and out-of-range scalars fail in every engine",
        ))
        invalid = [line for line in lines if line.startswith("  PASS: 010 escape ")]
        cases["invalid-escapes"] = (complete and len(invalid) == 11 and
                                    len(set(invalid)) == 11 and
                                    all(line.endswith(" -> INVALID_ESCAPE") for line in invalid))
    elif suite == "g4-char":
        lines = stdout.splitlines()
        passed = [line for line in lines if line.startswith("  PASS:")]
        complete = (returncode == 0 and len(passed) == len(set(passed)) == 21 and
                    "=== Phase 25 G4 Char/String cases passed ===" in lines)
        cases["char"] = complete and (
            "  PASS: Char literal and explicit to_string comparisons (3 engines)" in lines)
        invalid = [line for line in lines if line.startswith("  PASS: dynamic Char(")]
        cases["constructor"] = (complete and
            "  PASS: valid Char(Int) boundaries and a dynamic sweep (3 engines)" in lines and
            len(invalid) == 5 and len(set(invalid)) == 5)
        cases["escapes"] = complete and "  PASS: 14 escapes x 4 forms (3 engines)" in lines
        cases["unicode"] = complete and (
            "  PASS: 36 random strings, 880 property checks vs Python oracle (5 engines)" in lines)
    elif suite == "g4-indexed":
        lines = stdout.splitlines()
        markers = (
            "  PASS: inline runtime 12800 randomized comparisons, offsets and limit",
            "  PASS: shared runtime 12800 randomized comparisons, offsets and limit",
            "  PASS: identical output in Fast Debug and plain/staged debug and release",
            "  PASS: staged multi-crate execution and plain/indexed mismatch rejection",
        )
        cases["layout"] = (returncode == 0 and
            "=== Phase 25 indexed String checks passed ===" in lines and
            all(lines.count(marker) == 1 for marker in markers))
    elif suite == "g4-linkage":
        lines = stdout.splitlines()
        source_free = (
            "shared indexed String debug: two source-free consumers passed with sealed imports",
            "shared indexed String release: two source-free consumers passed with sealed imports",
        )
        mismatch = "shared indexed String runtime mismatch rejected before native linking"
        complete = (returncode == 0 and len(lines) == 3 and
                    all(lines.count(marker) == 1 for marker in (*source_free, mismatch)))
        cases["source-free"] = complete and all(marker in lines for marker in source_free)
        cases["mismatch"] = complete and mismatch in lines
    elif suite == "g5-runtime":
        lines = stdout.splitlines()
        identities = re.findall(
            r"^moss_runtime (debug|release) \(-[gO]\) passed; "
            r"artifact SHA-256: ([0-9a-f]{64})$", stdout, re.M)
        complete = (returncode == 0 and len(lines) == 3 and
                    len(identities) == 2 and
                    {profile for profile, _ in identities} == {"debug", "release"} and
                    lines[-1] == "moss_runtime all unit, profile, source-removal, "
                    "and identity-mismatch proofs passed;")
        cases["all"] = complete
    elif suite == "g5-joint":
        expected = (
            "G5-02 Fast Debug open Vector member admission PASS",
            "G5-02 Native debug open Vector member admission PASS",
            "G5-02 Native release open Vector member admission PASS",
            "G5-02 three-engine joint ledger #12 PASS",
        )
        cases["all"] = returncode == 0 and tuple(stdout.splitlines()) == expected
    elif suite == "g5-sdk":
        matches = re.findall(
            r"^G5 SDK (debug|release) (sdk/[\w/-]+) PASS sha256:([0-9a-f]{64})$",
            stdout, re.M)
        expected = {(profile, package)
                    for profile in ("debug", "release")
                    for package in SDK_PRIMARY_PACKAGES}
        summary = (f"G5 SDK ABI-v10 matrix: {len(expected)}/{len(expected)} builds, "
                   f"{len(SDK_PRIMARY_PACKAGES)} packages, 2 profiles")
        cases["all"] = (returncode == 0 and len(matches) == len(expected) and
                        {(profile, package) for profile, package, _ in matches} == expected and
                        len(stdout.splitlines()) == len(expected) + 1 and
                        stdout.splitlines()[-1] == summary)
    elif suite == "g3-snapshot":
        cases["all"] = (returncode == 0 and stdout.splitlines() == [
            "G3-08 sparse Map snapshot live-storage bound and detached clone PASS"])
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
    if suite == "g1-ledger":
        return [sys.executable, str(ROOT / G1_LEDGER_PATH), f"STAGED={compiler}"]
    if suite == "g1-regression":
        return [sys.executable, str(ROOT / G1_REGRESSION_PATH), str(compiler)]
    if suite == "s3":
        return [sys.executable, str(ROOT / S3_PATH), str(compiler)]
    if suite == "g3-ordering":
        return [sys.executable, str(ROOT / G3_ORDERING_PATH), str(compiler)]
    if suite == "l7":
        return [sys.executable, str(ROOT / L7_PATH), str(compiler)]
    if suite == "g4-char":
        return [sys.executable, str(ROOT / G4_CHAR_PATH), str(compiler), "--g4-only"]
    if suite == "g4-indexed":
        return [sys.executable, str(ROOT / G4_INDEXED_PATH), str(compiler)]
    if suite == "g4-linkage":
        return [sys.executable, str(ROOT / G4_LINKAGE_PATH)]
    if suite == "g5-runtime":
        return [sys.executable, str(ROOT / G5_RUNTIME_PATH)]
    if suite == "g5-joint":
        return [sys.executable, str(ROOT / G5_JOINT_PATH)]
    if suite == "g5-sdk":
        return [sys.executable, str(ROOT / G5_SDK_PATH)]
    if suite == "g3-snapshot":
        return [sys.executable, str(ROOT / G3_SNAPSHOT_PATH)]
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
