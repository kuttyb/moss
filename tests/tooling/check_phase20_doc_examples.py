#!/usr/bin/env python3
"""Keep canonical Phase 20 Moss snippets free of unsupported continuations."""

import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
DOCUMENT = ROOT / "docs/MOSS_PHASE_20_FILE_IO_AND_EXECUTORS.md"


def main():
    in_moss = False
    bad = []
    for number, line in enumerate(DOCUMENT.read_text().splitlines(), 1):
        if line.startswith("```moss"):
            in_moss = True
            continue
        if line.startswith("```"):
            in_moss = False
            continue
        if in_moss and (re.match(r"^\s+\.(?:[A-Za-z_]|\()", line) or
                        re.match(r"^\s+\|>", line)):
            bad.append(number)
    assert not bad, f"unsupported multiline Moss continuation at {DOCUMENT}:{bad}"
    print("Phase 20 Moss snippet continuation check: PASS")


if __name__ == "__main__":
    main()
