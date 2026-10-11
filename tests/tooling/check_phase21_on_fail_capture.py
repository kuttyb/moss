#!/usr/bin/env python3
"""Check D7 owned on_fail capture proofs and native frame handoff."""

import json
from pathlib import Path
import subprocess
import sys


repo = Path(__file__).resolve().parents[2]
compiler = Path(sys.argv[1]).resolve()
fixtures = repo / "tests/tooling/fixtures"
scratch = repo / "tmp/phase21-on-fail-capture"
scratch.mkdir(parents=True, exist_ok=True)


def run(*args):
    return subprocess.run([str(item) for item in args], cwd=repo,
                          capture_output=True, text=True, timeout=180)


for name, output in (
        ("owned_string", "hello\n"),
        ("message_result", "hello\n"),
        ("try_local", "9\n"),
        ("owned_payload", "hello\n"),
        ("slot_combined", "seven\n")):
    source = fixtures / f"phase21_capture_{name}.moss"
    checked = run(compiler, "check", source, "--json")
    assert checked.returncode == 0, (name, checked.stdout, checked.stderr)
    generated = scratch / f"{name}.rs"
    executable = scratch / name
    lowered = run(compiler, source, "-o", generated)
    assert lowered.returncode == 0, (name, lowered.stdout, lowered.stderr)
    compiled = run("rustc", "--edition=2021", "-D", "warnings",
                   generated, "-o", executable)
    assert compiled.returncode == 0, (name, compiled.stderr)
    executed = run(executable)
    assert executed.returncode == 0 and executed.stdout == output, (
        name, executed.returncode, executed.stdout, executed.stderr)


for name, code in (
        ("preinit", "ON_FAIL_CAPTURE_NOT_INITIALIZED"),
        ("consumed", "ON_FAIL_CAPTURE_CONSUMED"),
        ("fileio", "ON_FAIL_CAPTURE_NOT_OWNED"),
        ("borrowed_payload", "ON_FAIL_CAPTURE_NOT_OWNED")):
    source = fixtures / f"phase21_capture_{name}.moss"
    checked = run(compiler, "check", source, "--json")
    assert checked.returncode != 0, (name, checked.stdout, checked.stderr)
    diagnostic = json.loads(checked.stdout)
    assert diagnostic["error"]["code"] == code, (name, diagnostic)


print("Phase 21 D7 owned capture proof and native handoff passed.")
