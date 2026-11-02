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
GAPS_START = "<!-- architecture-gaps:start -->"
GAPS_END = "<!-- architecture-gaps:end -->"
DEBT_START = "<!-- declared-debt:start -->"
DEBT_END = "<!-- declared-debt:end -->"


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

    contract = ROOT / "docs/MOSS_COMPILER_CONTRACT.md"
    contract_text = contract.read_text(encoding="utf-8")
    if (text.count(GAPS_START) != 1 or text.count(GAPS_END) != 1 or
            contract_text.count(DEBT_START) != 1 or
            contract_text.count(DEBT_END) != 1):
        failures.append("gap/debt registry markers are missing or repeated")
    else:
        gaps = text.split(GAPS_START, 1)[1].split(GAPS_END, 1)[0]
        debt = contract_text.split(DEBT_START, 1)[1].split(DEBT_END, 1)[0]
        contract_ids = set(re.findall(r"`(D-[A-Z-]+)`", debt))
        contract_anchors = set(re.findall(r'id="(d-[a-z-]+)"', debt))
        architecture_ids = {
            identifier.upper()
            for identifier in re.findall(
                r"MOSS_COMPILER_CONTRACT\.md#(d-[a-z-]+)", gaps)
        }
        if contract_ids != architecture_ids:
            missing = sorted(contract_ids - architecture_ids)
            extra = sorted(architecture_ids - contract_ids)
            if missing:
                failures.append("contract debt entries missing from architecture gaps: " +
                                ", ".join(missing))
            if extra:
                failures.append("architecture gaps cite undeclared contract debt: " +
                                ", ".join(extra))
        normalized_anchors = {anchor.upper() for anchor in contract_anchors}
        if normalized_anchors != contract_ids:
            failures.append("contract debt entry IDs and link anchors do not match")

    # Keep measured debt values in the contract/report, not duplicated here.
    debt_measure = re.compile(
        r"\b\d[\d,]*(?:/\d[\d,]*)?\s+"
        r"(?:reparses?|text[- ]cache lookups?|text[- ]node lookups?|"
        r"tree rebuilds?|ownership entries|suite jobs|failures?|KiB)\b|"
        r"[+-]\d+(?:\.\d+)?%")
    if debt_measure.search(text):
        failures.append("architecture guide repeats a numeric debt or cost measurement")
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
