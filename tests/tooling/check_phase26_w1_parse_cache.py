#!/usr/bin/env python3
"""Exercise repeated checker syntax through accepted and rejected programs."""

import json
import os
from pathlib import Path
import re
import subprocess
import sys


ROOT = Path(__file__).resolve().parents[2]
POSITIVE = ROOT / "tests/phase26_w1_parse_cache.moss"
NEGATIVE = ROOT / "tests/negative/phase26_w1_parse_cache.moss"


def main() -> int:
    compiler = Path(sys.argv[1]).resolve() if len(sys.argv) > 1 else ROOT / "moss"
    env = dict(os.environ, MOSS_PROFILE_COMPILER="1")
    checked = subprocess.run(
        [str(compiler), "check", str(POSITIVE), "--json"],
        capture_output=True, text=True, env=env, check=False,
    )
    if checked.returncode or json.loads(checked.stdout).get("ok") is not True:
        raise AssertionError(f"positive check failed: {checked.stdout}\n{checked.stderr}")
    hits = re.search(r"^MOSS_PROFILE\|parse_cache_hits\|(\d+)$", checked.stderr, re.M)
    misses = re.search(r"^MOSS_PROFILE\|parse_cache_misses\|(\d+)$", checked.stderr, re.M)
    if not hits or not misses or int(hits.group(1)) == 0 or int(misses.group(1)) == 0:
        raise AssertionError(f"checker cache was not exercised: {checked.stderr}")

    interpreted = subprocess.run(
        [str(compiler), "run", "--interp", str(POSITIVE)],
        capture_output=True, text=True, check=False,
    )
    if interpreted.returncode or interpreted.stdout != "7\n8\n9\n2\n":
        raise AssertionError(
            f"positive execution changed: {interpreted.returncode} "
            f"{interpreted.stdout!r} {interpreted.stderr!r}"
        )

    rejected = subprocess.run(
        [str(compiler), "check", str(NEGATIVE), "--json"],
        capture_output=True, text=True, check=False,
    )
    diagnostic = json.loads(rejected.stdout)
    if rejected.returncode == 0 or diagnostic.get("error", {}).get("code") != "TYPE_MISMATCH":
        raise AssertionError(f"negative diagnostic changed: {rejected.stdout}\n{rejected.stderr}")
    print("Phase 26 W1 checker parse cache: PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
