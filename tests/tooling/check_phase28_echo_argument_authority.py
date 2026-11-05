#!/usr/bin/env python3
"""Checker contract and perturb/drop checks for typed Echo argument facts."""
from __future__ import annotations

import json
import os
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]
COMPILER = Path(sys.argv[1]).resolve() if len(sys.argv) > 1 else ROOT / "moss"
TMP = ROOT / "tmp/phase28_echo_argument_authority"
TMP.mkdir(parents=True, exist_ok=True)
SOURCE = TMP / "vector_echo.moss"
SOURCE.write_text(
    "fn main():\n"
    "  values = [1, 2]\n"
    "  echo values[0]\n"
    "  echo 3\n",
    encoding="utf-8",
)
ORACLE_42_SOURCE = (
    ROOT / "tests/phase28/oracle_r7_005_1/fixtures/"
    "oracle_42_custom_wrapper_remove.moss"
)


def check(extra_env: dict[str, str], *, expected_ok: bool,
          source: Path = SOURCE) -> dict:
    env = dict(os.environ)
    env.update(extra_env)
    proc = subprocess.run(
        [str(COMPILER), "check", str(source), "--json"],
        cwd=ROOT,
        env=env,
        capture_output=True,
        text=True,
        timeout=60,
    )
    try:
        result = json.loads(proc.stdout)
    except json.JSONDecodeError as exc:
        raise AssertionError(
            f"invalid checker JSON: {proc.stdout}\n{proc.stderr}"
        ) from exc
    if expected_ok:
        if proc.returncode != 0 or not result.get("ok"):
            raise AssertionError(f"expected checker success, got {result}")
    else:
        error = result.get("error") or {}
        if proc.returncode == 0 or result.get("ok") or error.get("code") not in {
                "MOSS_INTERNAL_OR_IO_ERROR", "MOSS_INTERNAL_INVARIANT"}:
            raise AssertionError(f"expected missing/contradictory Echo fact, got {result}")
    return result


def main() -> int:
    validate = {
        "MOSS_TEST_VALIDATE_ECHO_ARGUMENT_CONTRACT": "1",
        "MOSS_TEST_REQUIRE_BORROWED_ECHO_ARGUMENT_FACT": "1",
    }
    check(validate, expected_ok=True)
    print("PASS exact Echo argument requirement/fact resolution", flush=True)

    check({**validate, "MOSS_TEST_DROP_ECHO_ARGUMENT_FACT": "1"},
          expected_ok=False)
    print("PASS dropped Echo argument fact fails closed", flush=True)

    check({**validate, "MOSS_TEST_PERTURB_ECHO_ARGUMENT_FACT": "1"},
          expected_ok=False)
    print("PASS perturbed Echo argument fact fails closed", flush=True)

    check({**validate,
           "MOSS_TEST_PERTURB_ECHO_ARGUMENT_FACT_CONTEXT": "1"},
          expected_ok=False)
    print("PASS wrong advertised EST context fails closed", flush=True)

    # ORACLE-42 uses a field-projected receiver (`b.elements[1]`), not a
    # root Vector local. The exact Echo-only borrow fact must cover this
    # checked Index occurrence without granting a local ownership fact.
    check(validate, expected_ok=True, source=ORACLE_42_SOURCE)
    print("PASS ORACLE-42 nested-field Vector Echo borrow fact", flush=True)
    for hook in ("MOSS_TEST_DROP_ECHO_ARGUMENT_FACT",
                 "MOSS_TEST_PERTURB_ECHO_ARGUMENT_FACT",
                 "MOSS_TEST_PERTURB_ECHO_ARGUMENT_FACT_CONTEXT"):
        check({**validate, hook: "1"}, expected_ok=False,
              source=ORACLE_42_SOURCE)
        print(f"PASS ORACLE-42 {hook} fails closed", flush=True)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
