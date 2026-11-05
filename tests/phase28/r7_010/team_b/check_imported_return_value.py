#!/usr/bin/env python3
"""Check imported source-free ReturnValue authority in package Fast Debug."""
from __future__ import annotations

import os
from pathlib import Path
import subprocess
import sys


ROOT = Path(__file__).resolve().parents[4]
MARGO = ROOT / "margo"
PROJECT = ROOT / "tests/phase28/oracle/fixtures/oracle_54_imported_mossi_effects/consumer"
EXPECTED = "imported_missing_key\n"
HOOKS = (
    "MOSS_TEST_CONSUMER_DROP_RETURN_VALUE_REQUIREMENT",
    "MOSS_TEST_CONSUMER_DUPLICATE_RETURN_VALUE_REQUIREMENT",
    "MOSS_TEST_CONSUMER_DROP_RETURN_VALUE_FACT",
    "MOSS_TEST_CONSUMER_DUPLICATE_RETURN_VALUE_FACT",
    "MOSS_TEST_CONSUMER_PERTURB_RETURN_VALUE_FACT",
    "MOSS_TEST_CONSUMER_WRONG_RETURN_VALUE_CONTEXT",
    "MOSS_TEST_CONSUMER_WRONG_RETURN_VALUE_SPECIALIZATION",
    "MOSS_TEST_CONSUMER_PERTURB_IMPORTED_RETURN_MODULE_ID",
)
ARTIFACT_HOOKS = (
    "MOSS_TEST_DROP_RETURN_VALUE_ARTIFACT_FACT",
    "MOSS_TEST_PERTURB_RETURN_VALUE_ARTIFACT_FACT",
)


def run(env: dict[str, str], ok: bool = True) -> subprocess.CompletedProcess[str]:
    result = subprocess.run([str(MARGO), "debug"], cwd=PROJECT, env=env,
                            text=True, capture_output=True, timeout=180)
    if (result.returncode == 0) != ok:
        raise AssertionError(
            f"margo debug returned {result.returncode}, expected "
            f"{0 if ok else 'nonzero'}\nstdout:\n{result.stdout}\nstderr:\n{result.stderr}")
    return result


def main() -> None:
    compiler = Path(sys.argv[1] if len(sys.argv) > 1 else "./moss").resolve()
    work = ROOT / "tmp/team-b-consumer/oracle54"
    work.mkdir(parents=True, exist_ok=True)
    base = dict(os.environ)
    base["MOSS"] = str(compiler)
    base["TMPDIR"] = str(work / "tmp")
    base["MARGO_HOME"] = str(work / "margo-home")
    Path(base["TMPDIR"]).mkdir(parents=True, exist_ok=True)
    Path(base["MARGO_HOME"]).mkdir(parents=True, exist_ok=True)

    result = run(base)
    if result.stdout != EXPECTED:
        raise AssertionError(f"expected {EXPECTED!r}, got {result.stdout!r}")

    for hook in HOOKS:
        env = dict(base)
        env[hook] = "1"
        failure = run(env, ok=False)
        detail = failure.stdout + failure.stderr
        if "MOSS_INTERNAL_INVARIANT" not in detail or \
                "return-value" not in detail.lower():
            raise AssertionError(f"{hook}: unrelated Fast Debug failure:\n{detail}")

    for hook in ARTIFACT_HOOKS:
        env = dict(base)
        env[hook] = "1"
        failure = run(env, ok=False)
        detail = failure.stdout + failure.stderr
        if "SOURCE_FREE_EXECUTION_BODY_INVALID" not in detail:
            raise AssertionError(
                f"{hook}: artifact corruption did not fail interface validation:\n{detail}")

    print("ORACLE-54 imported ReturnValue Fast Debug output parity PASS")
    print("imported exact ReturnValue requirement/fact/context/module corruption: 8/8 fail closed")
    print(".mossi ReturnValue artifact drop/perturb validation: 2/2 rejected before execution")


if __name__ == "__main__":
    main()
