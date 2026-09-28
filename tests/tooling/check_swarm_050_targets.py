#!/usr/bin/env python3
"""Verify checked concrete outer calls keep their matching inner target."""

import re
import sys
from pathlib import Path


def main() -> int:
    if len(sys.argv) != 5:
        raise SystemExit(
            "usage: check_swarm_050_targets.py RUST OUTER INNER FIRST,SECOND"
        )
    rust_path, outer_name, inner_name, type_list = sys.argv[1:]
    concrete_types = type_list.split(",")
    if len(concrete_types) != 2 or len(set(concrete_types)) != 2:
        raise SystemExit("expected two distinct concrete types")

    source = Path(rust_path).read_text()
    marker = re.compile(
        r"// Moss tooling begin\|function\|fn:([^<@]+)<([^>]+)>@\d+\|([^|]+)\|"
    )
    inner_symbols = {concrete: set() for concrete in concrete_types}
    outer_bodies = {concrete: [] for concrete in concrete_types}
    for match in marker.finditer(source):
        name, concrete, symbol = match.groups()
        if concrete not in inner_symbols:
            continue
        if name == inner_name:
            inner_symbols[concrete].add(symbol)
        elif name == outer_name:
            end = source.find("// Moss tooling end|", match.end())
            if end < 0:
                raise AssertionError(f"missing function end marker for {symbol}")
            outer_bodies[concrete].append(source[match.end() : end])

    for concrete in concrete_types:
        if not inner_symbols[concrete] or not outer_bodies[concrete]:
            raise AssertionError(f"missing {inner_name}/{outer_name}<{concrete}>")
        other = next(item for item in concrete_types if item != concrete)
        for body in outer_bodies[concrete]:
            called = lambda symbol: re.search(r"\b" + re.escape(symbol) + r"\s*\(", body)
            if not any(called(symbol) for symbol in inner_symbols[concrete]):
                raise AssertionError(
                    f"{outer_name}<{concrete}> does not call {inner_name}<{concrete}>"
                )
            if any(called(symbol) for symbol in inner_symbols[other]):
                raise AssertionError(
                    f"{outer_name}<{concrete}> calls {inner_name}<{other}>"
                )
    print(f"{outer_name} concrete call targets match {inner_name}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
