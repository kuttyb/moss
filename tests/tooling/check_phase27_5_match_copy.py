#!/usr/bin/env python3
"""D2 shared Copy classification regression for match payloads."""
import json
from pathlib import Path
import subprocess
import time
import sys

ROOT = Path(__file__).resolve().parents[2]
COMPILER = Path(sys.argv[1]).resolve()
SOURCE = ROOT / "tests/tooling/fixtures/phase27_5_match_copy.moss"


def run(args):
    return subprocess.run(args, cwd=ROOT, text=True,
                          stdout=subprocess.PIPE, stderr=subprocess.PIPE)


def require_ok(result, label):
    if result.returncode:
        raise SystemExit(f"{label} failed:\n{result.stdout}{result.stderr}")


def main():
    check = run([str(COMPILER), "check", str(SOURCE), "--json"])
    require_ok(check, "check")
    if not json.loads(check.stdout).get("ok"):
        raise SystemExit(f"checker rejected fixture: {check.stdout}")
    work = ROOT / "tmp" / "phase27-5-match-copy" / str(time.time_ns())
    work.mkdir(parents=True)
    fast = run([str(COMPILER), "run", "--interp", str(SOURCE)])
    require_ok(fast, "Fast Debug")
    emitted_path = work / "match.rs"
    emitted = run([str(COMPILER), str(SOURCE), "-o", str(emitted_path)])
    require_ok(emitted, "native emit")
    rust = emitted_path.read_text(encoding="utf-8")
    if rust.count("let value = *value;") < 3:
        raise SystemExit("Char/Option match payloads did not lower as Copy values")
    lines = rust.splitlines()
    string_arm = next(i for i, line in enumerate(lines)
                      if "StringPayload::Value" in line)
    if any("clone()" in line for line in lines[string_arm:string_arm + 8]):
        raise SystemExit("String match control unexpectedly cloned its payload")
    binary = work / "native"
    compiled = run(["rustc", "--edition=2021", str(emitted_path), "-o", str(binary)])
    require_ok(compiled, "rustc")
    native = run([str(binary)])
    require_ok(native, "native run")
    if fast.stdout != native.stdout:
        raise SystemExit(f"Fast Debug/native mismatch: {fast.stdout!r} vs {native.stdout!r}")
    print("Phase 27.5 D2 targeted fixture passed")


if __name__ == "__main__":
    main()
