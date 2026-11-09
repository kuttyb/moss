#!/usr/bin/env python3
"""Run marked ownership documentation examples through both Moss engines."""
from __future__ import annotations

import json
from pathlib import Path
import re
import subprocess
import sys


REPO = Path(__file__).resolve().parents[2]
COMPILER = Path(sys.argv[1]).resolve()
OUT = Path(sys.argv[2]).resolve()
OUT.mkdir(parents=True, exist_ok=True)
MARKER = re.compile(r'<!-- moss-test: (pass output=("(?:[^"\\]|\\.)*")|fail code=([A-Z0-9_]+)) -->')
FENCE = re.compile(r"```moss\s*\n(.*?)\n```", re.DOTALL)


def run(args: list[object], expected: int | None = None) -> subprocess.CompletedProcess[str]:
    proc = subprocess.run([str(arg) for arg in args], cwd=REPO,
                          capture_output=True, text=True, timeout=120)
    if expected is not None:
        assert proc.returncode == expected, (args, proc.returncode, proc.stdout, proc.stderr)
    return proc


count = 0
for doc_name in ("docs/OWNERSHIP.md", "docs/Moss_Dev_Cheat_Sheet.md"):
    content = (REPO / doc_name).read_text()
    for index, match in enumerate(FENCE.finditer(content), 1):
        prefix = content[:match.start()].rstrip().splitlines()
        assert prefix, (doc_name, index, "missing test marker")
        marker = MARKER.fullmatch(prefix[-1])
        assert marker, (doc_name, index, "missing or invalid moss-test marker")
        source = match.group(1) + "\n"
        path = OUT / f"{Path(doc_name).stem}-{index}.moss"
        path.write_text(source)
        case = marker.group(1)
        if case.startswith("pass"):
            expected_output = json.loads(marker.group(2))
            checked = run([COMPILER, "check", path, "--json"], 0)
            payload = json.loads(checked.stdout)
            assert payload.get("ok", True), (doc_name, index, payload)
            rust = OUT / f"{path.stem}.rs"
            binary = OUT / path.stem
            run([COMPILER, path, "-o", rust], 0)
            run(["rustc", "-D", "warnings", rust, "-o", binary], 0)
            native = run([binary], 0).stdout.strip()
            fast_debug = run([COMPILER, "run", "--interp", path], 0).stdout.strip()
            assert native == expected_output, (doc_name, index, "native", native, expected_output)
            assert fast_debug == expected_output, (doc_name, index, "Fast Debug", fast_debug, expected_output)
            assert native == fast_debug, (doc_name, index, native, fast_debug)
        else:
            expected_code = marker.group(3)
            checked = run([COMPILER, "check", path, "--json"], 1)
            payload = json.loads(checked.stdout)
            error = payload.get("error", {})
            assert error.get("code") == expected_code, (doc_name, index, payload)
            rust = OUT / f"{path.stem}.rs"
            native_error = run([COMPILER, path, "-o", rust], 1)
            debug_error = run([COMPILER, "run", "--interp", path], 1)
            for engine, result in (("native", native_error), ("Fast Debug", debug_error)):
                assert expected_code in result.stdout + result.stderr, (doc_name, index, engine, result.stdout, result.stderr)
        count += 1
        print(f"PASS {doc_name} example {index}: {case}")

assert count, "no marked Moss examples found"
print(f"PASS {count} documentation examples in Native and Fast Debug")
