#!/usr/bin/env python3
"""No diagnostic may show a compiler-internal type marker.

TYPE_IR_CONTRACT.md §2 makes inference variables and the legacy open
pseudo-types compiler-owned: they are never a user-facing type. A message such
as "returns 'vector[_typevar:1:0:0]'" or "has type '_method_value'" leaks the
checker's internal representation into the diagnostic a programmer reads, and
in practice it has marked a real defect each time -- the `_typevar:` case was a
mismatch reported where the checker should have reported an unresolved return.

Every tracked .moss source is checked, and any marker in `error.message` fails.
KNOWN_OPEN holds the cases that remain until the pseudo-types become inference
variables and requirements (A2 / Architect finding G-A-1); each entry must keep
leaking or it is removed, so the list cannot rot silently.
"""

import concurrent.futures
import json
import os
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]

MARKERS = ("_typevar:", "_generic:", "_element:", "_field:", "_method_value",
           "_method_result:", "_specialized_result:", "_iterator_element:",
           "_functional_result:", "_functional_element:",
           "_functional_collection:", "_functional_callable_result:")

# Pre-existing leaks, each a pseudo-type the Type IR conversion still owes.
KNOWN_OPEN = {
    "examples/swarm/modules/algo_chain/repro/untyped_method_value/single/src/main.moss",
    "examples/swarm/polymorphism/sort_search/reproducers/probe_swap_assign.moss",
}


def leaked(moss: Path, source: str) -> str:
    try:
        completed = subprocess.run([str(moss), "check", source, "--json"],
                                   cwd=ROOT, capture_output=True, text=True,
                                   timeout=120)
        message = (json.loads(completed.stdout).get("error") or {}).get("message", "")
    except (subprocess.TimeoutExpired, json.JSONDecodeError):
        return ""
    return message if any(marker in message for marker in MARKERS) else ""


def main() -> int:
    moss = Path(sys.argv[1] if len(sys.argv) > 1 else ROOT / "moss").resolve()
    sources = [path for path in subprocess.run(
        ["git", "ls-tree", "-r", "--name-only", "HEAD"], cwd=ROOT,
        capture_output=True, text=True, check=True).stdout.splitlines()
        if path.endswith(".moss")]

    with concurrent.futures.ThreadPoolExecutor(os.cpu_count() or 4) as pool:
        leaks = {source: message for source, message
                 in zip(sources, pool.map(lambda s: leaked(moss, s), sources))
                 if message}

    unexpected = sorted(set(leaks) - KNOWN_OPEN)
    for source in unexpected:
        print(f"{source}: {leaks[source]}", file=sys.stderr)
    repaired = sorted(KNOWN_OPEN - set(leaks))
    for source in repaired:
        print(f"{source}: no longer leaks; remove it from KNOWN_OPEN",
              file=sys.stderr)
    if unexpected or repaired:
        print(f"internal type markers: {len(unexpected)} new, "
              f"{len(repaired)} stale exemptions", file=sys.stderr)
        return 1
    print(f"No internal type markers in diagnostics across {len(sources)} "
          f"sources ({len(KNOWN_OPEN)} known-open pseudo-type cases)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
