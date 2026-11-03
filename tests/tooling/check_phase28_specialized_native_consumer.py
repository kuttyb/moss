#!/usr/bin/env python3
"""Exercise Phase 28 Team B native consumer on specialized ownership facts.

Verifies:
1. Generic function instantiated for two different nonprimitive types.
2. Correct ownership fact selected for each specialization.
3. Both specialized native executions produce expected results.
4. Deleting the entire required fact for one specialization fails closed.
5. Perturbing its access effect fails closed.
6. Perturbing its specialization or source identity fails closed.
7. Duplicating a required record or fact fails closed.
8. Explicitly unsupported fact remains distinguishable from missing authority.
9. No source-level nonprimitive copy or implicit clone is introduced.
"""
from __future__ import annotations

import os
from pathlib import Path
import re
import subprocess
import sys


ROOT = Path(__file__).resolve().parents[2]
FIXTURE = ROOT / "tests/phase28/team_a_ownership/specialized_identity.moss"
TMP = ROOT / "tmp/phase28_specialized_consumer"
TMP.mkdir(parents=True, exist_ok=True)


def run(cmd: list[str], env: dict[str, str] | None = None) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, cwd=ROOT, env=env
    )


def verify_fail_closed(compiler: str, env_vars: dict[str, str], expected_marker: str) -> None:
    env = os.environ.copy()
    env.update(env_vars)
    rs_path = TMP / "specialized_test.rs"
    proc = run([compiler, str(FIXTURE), "-o", str(rs_path)], env=env)
    if proc.returncode == 0:
        raise AssertionError(f"Expected failure with {env_vars}, but compilation succeeded!")
    combined = proc.stdout + "\n" + proc.stderr
    if "MOSS_INTERNAL_INVARIANT" not in combined:
        raise AssertionError(f"Expected MOSS_INTERNAL_INVARIANT with {env_vars}, got:\n{combined}")
    if expected_marker and expected_marker not in combined:
        raise AssertionError(f"Expected marker '{expected_marker}' with {env_vars}, got:\n{combined}")


def main() -> int:
    compiler = sys.argv[1] if len(sys.argv) > 1 else "./moss"

    # 1, 2, 3: Compile specialized_identity.moss, verify parity and output
    rs_path = TMP / "specialized_identity.rs"
    bin_path = TMP / "specialized_identity.bin"

    emit = run([compiler, str(FIXTURE), "-o", str(rs_path)])
    if emit.returncode != 0:
        raise AssertionError(f"Native emit failed:\n{emit.stderr}")

    rustc = run(["rustc", "--edition=2021", str(rs_path), "-o", str(bin_path)])
    if rustc.returncode != 0:
        raise AssertionError(f"rustc compilation failed:\n{rustc.stderr}")

    run_bin = run([str(bin_path)])
    if run_bin.returncode != 0:
        raise AssertionError(f"Native binary crashed:\n{run_bin.stderr}")
    expected_out = "1\n2"
    if run_bin.stdout.strip() != expected_out:
        raise AssertionError(f"Output mismatch: expected '{expected_out}', got '{run_bin.stdout.strip()}'")

    # 9: Verify no source-level copy or implicit clone is introduced on the alias
    rs_content = rs_path.read_text()
    found_alias = False
    for line in rs_content.splitlines():
        if "alias" in line and "source" in line and "=" in line:
            found_alias = True
            if ".clone()" in line:
                raise AssertionError(f"Found unexpected .clone() on alias: {line}")
    if not found_alias:
        raise AssertionError("Did not find alias = source lowering in generated Rust")

    # 4: Deleting the entire required fact for one specialization fails closed
    verify_fail_closed(
        compiler,
        {"MOSS_TEST_TARGET_SPECIALIZATION": "Second", "MOSS_TEST_CONSUMER_DELETE_OWNERSHIP_RECORD": "1"},
        "required local ownership fact missing",
    )

    # 5: Perturbing its access effect fails closed
    verify_fail_closed(
        compiler,
        {"MOSS_TEST_TARGET_SPECIALIZATION": "Second", "MOSS_TEST_CONSUMER_PERTURB_OWNERSHIP_FACT": "1"},
        "required local ownership fact contradicts requirement",
    )

    # 6: Perturbing specialization identity fails closed
    verify_fail_closed(
        compiler,
        {"MOSS_TEST_TARGET_SPECIALIZATION": "Second", "MOSS_TEST_CONSUMER_PERTURB_SPECIALIZATION_ID": "1"},
        "required local ownership fact missing",
    )

    # 6: Perturbing source identity fails closed
    verify_fail_closed(
        compiler,
        {"MOSS_TEST_TARGET_SPECIALIZATION": "Second", "MOSS_TEST_CONSUMER_PERTURB_SOURCE_ID": "1"},
        "required local ownership fact missing",
    )

    # 7: Duplicating a required fact fails closed
    verify_fail_closed(
        compiler,
        {"MOSS_TEST_TARGET_SPECIALIZATION": "Second", "MOSS_TEST_CONSUMER_DUPLICATE_OWNERSHIP_FACT": "1"},
        "duplicate local ownership fact",
    )

    # 7: Duplicating a required requirement fails closed
    verify_fail_closed(
        compiler,
        {"MOSS_TEST_TARGET_SPECIALIZATION": "Second", "MOSS_TEST_CONSUMER_DUPLICATE_OWNERSHIP_REQUIREMENT": "1"},
        "ambiguous required local ownership occurrence",
    )

    # 8: Unsupported fact remains distinguishable from missing authority
    unsupported_fixture = ROOT / "tests/phase28/team_a_ownership/control_flow_unsupported.moss"
    unsupported_rs = TMP / "control_flow_unsupported.rs"
    proc_unsupported = run([compiler, str(unsupported_fixture), "-o", str(unsupported_rs)])
    # Unsupported facts must decline bounded inference without raising invariant crash
    if "MOSS_INTERNAL_INVARIANT" in proc_unsupported.stderr:
        raise AssertionError(
            f"Unsupported fixture failed with MOSS_INTERNAL_INVARIANT:\n{proc_unsupported.stderr}"
        )

    print("phase28 specialized native consumer: PASS (multi-specialization, parity, 6 fail-closed modes, no clones)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
