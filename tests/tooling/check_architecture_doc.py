#!/usr/bin/env python3
"""Check that the compiler architecture guide's cited paths and symbols exist."""

from __future__ import annotations

import re
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
DOC = ROOT / "docs/COMPILER_ARCHITECTURE.md"
START = "<!-- architecture-citations:start -->"
END = "<!-- architecture-citations:end -->"


def main() -> int:
    text = DOC.read_text(encoding="utf-8")
    if text.count(START) != 1 or text.count(END) != 1:
        print("architecture citation registry markers are missing or repeated",
              file=sys.stderr)
        return 1
    registry = text.split(START, 1)[1].split(END, 1)[0]
    failures: list[str] = []
    rows = 0
    for line in registry.splitlines():
        match = re.fullmatch(r"\| `([^`]+)` \| (.*?) \|", line.strip())
        if not match:
            continue
        path_text, symbols_text = match.groups()
        path = ROOT / path_text
        rows += 1
        if not path.is_file():
            failures.append(f"missing cited path: {path_text}")
            continue
        source = path.read_text(encoding="utf-8", errors="replace")
        symbols = re.findall(r"`([^`]+)`", symbols_text)
        if not symbols:
            failures.append(f"no symbols cited for: {path_text}")
        for symbol in symbols:
            pattern = r"(?<![A-Za-z0-9_])" + re.escape(symbol) + r"(?![A-Za-z0-9_])"
            if not re.search(pattern, source):
                failures.append(f"{path_text}: missing symbol {symbol}")
    if rows == 0:
        failures.append("citation registry contains no source rows")
    if failures:
        print("Architecture documentation citation check failed:",
              file=sys.stderr)
        for failure in failures:
            print(f"- {failure}", file=sys.stderr)
        return 1
    print(f"Architecture documentation citations verified: {rows} paths")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
