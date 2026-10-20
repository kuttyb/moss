#!/usr/bin/env python3
"""Keep the Char/String-sensitive hash and regex SDK packages in make check."""

from __future__ import annotations

import os
from pathlib import Path
import subprocess
import sys


ROOT = Path(__file__).resolve().parents[2]
PACKAGES = ("packages/hash", "packages/regex")


def main() -> int:
    if len(sys.argv) != 3:
        raise SystemExit("usage: check_phase25_char_string_sdk_packages.py COMPILER OUTPUT_DIR")

    compiler = Path(sys.argv[1]).resolve()
    output = Path(sys.argv[2]).resolve()
    output.mkdir(parents=True, exist_ok=True)
    env = os.environ.copy()
    env["MOSS"] = str(compiler)
    env["MARGO_HOME"] = str(output / "margo-home")
    env["TMPDIR"] = str(ROOT / "tmp")
    env.pop("MOSS_STAGED_ABI", None)
    for key in ("MOSS_MODULE_PATH", "MOSS_SOURCE_ROOTS", "MOSS_FAST_DEBUG_SOURCE_ROOTS"):
        env.pop(key, None)
    for key in tuple(env):
        if key.startswith("MOSS_PHASE25_"):
            env.pop(key, None)

    margo = ROOT / "margo"
    for package in PACKAGES:
        result = subprocess.run(
            [str(margo), "test"],
            cwd=ROOT / package,
            env=env,
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            timeout=900,
            check=False,
        )
        output_text = result.stdout + result.stderr
        if result.returncode != 0 or "0 failed" not in output_text:
            raise SystemExit(
                f"{package} Native Debug package tests failed (rc={result.returncode}):\n"
                f"stdout:\n{result.stdout}\nstderr:\n{result.stderr}"
            )
        print(f"PASS {package} Native Debug package tests")


if __name__ == "__main__":
    raise SystemExit(main())
