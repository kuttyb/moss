#!/usr/bin/env python3
"""Unary not cannot hide a speculative mapper's out-of-bounds panic."""

import json
from pathlib import Path
import re
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / "tests/tooling/fixtures/phase21_unary_not_speculation.moss"
MOSS = Path(sys.argv[1] if len(sys.argv) > 1 else ROOT / "moss").resolve()
OUT = ROOT / "tmp/phase21-unary-not-speculation"
OUT.mkdir(parents=True, exist_ok=True)
(ROOT / "tmp/phase21-unary-not-speculation.data").write_bytes(
    bytes([0]) + bytes([42]) * 4095 + bytes([1]))


def run(*args):
    return subprocess.run([str(arg) for arg in args], cwd=ROOT, text=True,
                          capture_output=True, timeout=120)


checked = run(MOSS, "check", SOURCE, "--json")
assert checked.returncode == 0, (checked.stdout, checked.stderr)
facts = run(MOSS, "effects", "mapper", "--source", SOURCE, "--json")
assert facts.returncode == 0, (facts.stdout, facts.stderr)
summary = json.loads(facts.stdout)["result"]["observable_effects"]
assert summary["may_panic"] and summary["raise_set"] == [
    "enum:MapFault:Early"], summary
assert not summary["unresolved"], summary

rust = OUT / "unary_not_speculation.rs"
lowered = run(MOSS, SOURCE, "--dump-functional-ir", "-o", rust)
assert lowered.returncode == 0, (lowered.stdout, lowered.stderr)
assert re.search(r"callable=mapper[^\n]*MAY_PANIC\+RAISE", lowered.stdout), (
    lowered.stdout, lowered.stderr)
assert re.search(r"chunk-plan:[^\n]*eligible=no[^\n]*possible panic",
                 lowered.stdout), lowered.stdout

for profile, flags in (("debug", []), ("optimized", ["-O"])):
    executable = OUT / profile
    compiled = run("rustc", "--edition=2021", "-D", "warnings",
                   *flags, rust, "-o", executable)
    assert compiled.returncode == 0, compiled.stderr
    result = run(executable)
    assert (result.returncode, result.stdout) == (0, "early\n"), (
        profile, result)

print("Phase 21 unary not speculative mapper gate passed.")
