#!/usr/bin/env python3
"""Run the Phase 26.3 helper-bearing package native test gate."""
from __future__ import annotations

import json
import os
from pathlib import Path
import re
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]
MOSS = Path(sys.argv[1]).resolve() if len(sys.argv) > 1 else ROOT / "moss"
PACKAGES = (
    "sdk/encoding",
    "sdk/cbor",
    "packages/compress",
    "sdk/text-encoding-acceptance",
    "sdk/binary",
)


def main() -> None:
    env = os.environ.copy()
    env["MOSS"] = str(MOSS)
    for relative in PACKAGES:
        package = ROOT / relative
        declarations = 0
        for directory in (package / "src", package / "tests"):
            if directory.exists():
                for source in directory.rglob("*.moss"):
                    declarations += len(re.findall(
                        r'^\s*test\s+"', source.read_text(encoding="utf-8"),
                        re.MULTILINE))
        result = subprocess.run(
            [str(ROOT / "margo"), "test", "--json"], cwd=package, env=env,
            text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
            timeout=600, check=False)
        try:
            data = json.loads(result.stdout)
        except json.JSONDecodeError as error:
            raise AssertionError(
                f"{relative}: expected JSON, got {result.stdout!r} {result.stderr!r}") from error
        assert result.returncode == 0 and data.get("ok"), (relative, result.returncode, data)
        summary = data["result"]["summary"]
        assert summary["failed"] == 0, (relative, summary)
        assert summary["total"] == declarations, (relative, summary, declarations)
        print(f"{relative}: {summary['passed']}/{summary['total']} passed, 0 failed")


if __name__ == "__main__":
    main()
