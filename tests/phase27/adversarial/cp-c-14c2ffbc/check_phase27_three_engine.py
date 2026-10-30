#!/usr/bin/env python3
"""Compare Fast Debug, native, and optimized output plus callback counts."""
import json
import os
from pathlib import Path
import re
import subprocess
import sys
import time
from phase27_allowlist_paths import missing_fixture_paths, self_test as allowlist_self_test

ROOT = Path(__file__).resolve().parents[2]
COMPILER = Path(sys.argv[1]).resolve()
SOURCE = ROOT / "tests/phase27/conformance/three_engine.moss"
ALLOW = ROOT / "tests/phase27/conformance/three_engine_allowlist.json"
MARKER = "999999"


def run(args, cwd=ROOT, env=None):
    return subprocess.run(args, cwd=cwd, env=env, text=True,
                          stdout=subprocess.PIPE, stderr=subprocess.PIPE)


def ratchet(observed, allowed):
    new = set(observed) - allowed
    stale = allowed - set(observed)
    if new or stale:
        raise AssertionError(f"new={sorted(new)} fixed_allowlist={sorted(stale)}")


def self_test():
    allowlist_self_test()
    ratchet({"seed": "mismatch"}, {"seed"})
    for observed, allowed in (({"new": "mismatch"}, set()), ({}, {"fixed"})):
        try: ratchet(observed, allowed)
        except AssertionError: continue
        raise AssertionError("ratchet accepted a new mismatch or stale entry")


def output(args, env=None):
    result = run(args, env=env)
    if result.returncode:
        raise RuntimeError(result.stdout + result.stderr)
    return result.stdout


def case_mismatch(source, work, env):
    checked = run([str(COMPILER), "check", str(source), "--json"], env=env)
    try:
        if not json.loads(checked.stdout).get("ok"):
            return None  # rejected fixtures are outside this gate
    except json.JSONDecodeError:
        return "checker did not return JSON"
    fast_result = run([str(COMPILER), "run", "--interp", str(source)], env=env)
    rust = work / "case.rs"
    emitted = run([str(COMPILER), str(source), "-o", str(rust)], env=env)
    if fast_result.returncode or emitted.returncode:
        return "Fast Debug or emitter failed"
    values = [fast_result.stdout]
    for label, flags in (("native", []), ("optimized", ["-O"])):
        binary = work / label
        compiled = run(["rustc", "--edition=2021", *flags, str(rust), "-o", str(binary)])
        if compiled.returncode:
            return f"rustc {label} compile failed"
        native = run([str(binary)])
        if native.returncode:
            return f"{label} execution failed"
        values.append(native.stdout)
    counts = [sum(1 for line in value.splitlines() if line.strip() == MARKER)
              for value in values]
    return None if values[0] == values[1] == values[2] and counts[0] == counts[1] == counts[2] else "output/callback count differs"


def main():
    self_test()
    work = ROOT / "tmp" / "phase27-three-engine" / str(time.time_ns())
    work.mkdir(parents=True)
    env = os.environ.copy(); env["MOSS_EST"] = "off"
    fast = output([str(COMPILER), "run", "--interp", str(SOURCE)], env=env)
    rust = work / "program.rs"
    emitted = run([str(COMPILER), str(SOURCE), "-o", str(rust)], env=env)
    if emitted.returncode:
        raise SystemExit("could not emit three-engine fixture:\n" + emitted.stderr)
    outputs = {}
    for label, flags in (("native", []), ("optimized", ["-O"])):
        binary = work / label
        compiled = run(["rustc", "--edition=2021", *flags, str(rust), "-o", str(binary)])
        if compiled.returncode:
            raise SystemExit(f"rustc {label} compile failed:\n{compiled.stderr}")
        outputs[label] = output([str(binary)])
    observed = {}
    expected = None
    for label, value in (("fast-debug", fast), *outputs.items()):
        invocation_count = sum(1 for line in value.splitlines() if line.strip() == MARKER)
        signature = (value, invocation_count)
        if expected is None: expected = signature
        elif signature != expected: observed[label] = "output or callback count differs"
    allowed_json = json.loads(ALLOW.read_text(encoding="utf-8"))
    missing = missing_fixture_paths(ROOT, allowed_json["cases"])
    if missing:
        raise SystemExit("allowlist names missing fixtures: " + ", ".join(missing))
    allowed = set()
    observed = {}
    for entry in allowed_json["cases"]:
        source = ROOT / entry["path"]
        if not source.exists():
            continue
        allowed.add(entry["id"])
        mismatch = case_mismatch(source, work / entry["id"].replace("/", "_"), env)
        if mismatch:
            observed[entry["id"]] = mismatch
    ratchet(observed, allowed)
    print(f"three-engine passed: stdout identical; callback invocations={expected[1]}")


if __name__ == "__main__":
    main()
