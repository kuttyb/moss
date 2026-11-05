#!/usr/bin/env python3
"""Check exact checker authority for bare re-raise carrier transfer."""
from __future__ import annotations

import json
import os
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]
COMPILER = Path(sys.argv[1]).resolve() if len(sys.argv) > 1 else ROOT / "moss"
SOURCE = ROOT / "tests/tooling/fixtures/phase21_recovery_native.moss"


def check(extra_env: dict[str, str], *, expected_ok: bool) -> dict:
    env = dict(os.environ)
    env.update(extra_env)
    proc = subprocess.run(
        [str(COMPILER), "check", str(SOURCE), "--json"],
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
            raise AssertionError(
                f"expected missing/contradictory bare-raise authority, got {result}"
            )
    return result


def main() -> int:
    validate = {
        "MOSS_TEST_VALIDATE_BARE_RAISE_TRANSFER_CONTRACT": "1",
        "MOSS_TEST_REQUIRE_BARE_RAISE_TRANSFER_FACT": "1",
    }
    check(validate, expected_ok=True)
    print("PASS exact selected-carrier transfer fact resolution", flush=True)

    check({**validate, "MOSS_TEST_DROP_BARE_RAISE_TRANSFER_FACT": "1"},
          expected_ok=False)
    print("PASS dropped selected-carrier transfer fails closed", flush=True)

    check({**validate, "MOSS_TEST_PERTURB_BARE_RAISE_TRANSFER_FACT": "1"},
          expected_ok=False)
    print("PASS perturbed selected-carrier transfer fails closed", flush=True)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
