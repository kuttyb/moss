#!/usr/bin/env python3
"""Recovery joins selected exceptional ownership states after reinitialization."""

import json
from pathlib import Path
import subprocess
import sys


repo = Path(__file__).resolve().parents[2]
compiler = Path(sys.argv[1]).resolve()
fixtures = repo / "tests/tooling/fixtures"
scratch = repo / "tmp/phase21-recover-reinit"
scratch.mkdir(parents=True, exist_ok=True)


def run(*args):
    return subprocess.run([str(arg) for arg in args], cwd=repo,
                          capture_output=True, text=True, timeout=180)


positive = fixtures / "phase21_recover_reinit_positive.moss"
checked = run(compiler, "check", positive, "--json")
assert checked.returncode == 0, (checked.stdout, checked.stderr)
generated = scratch / "positive.rs"
lowered = run(compiler, positive, "-o", generated)
assert lowered.returncode == 0, (lowered.stdout, lowered.stderr)
for opt in ("0", "3"):
    binary = scratch / f"positive-o{opt}"
    compiled = run("rustc", "--edition=2021", "-D", "warnings",
                   f"-Copt-level={opt}", generated, "-o", binary)
    assert compiled.returncode == 0, (opt, compiled.stdout, compiled.stderr)
    native = run(binary)
    assert native.returncode == 0 and native.stdout == "3\n", (
        opt, native.returncode, native.stdout, native.stderr)
debug = run(compiler, "run", "--interp", positive)
assert debug.returncode == 0 and debug.stdout == "3\n", (
    debug.returncode, debug.stdout, debug.stderr)

negative = fixtures / "phase21_recover_reinit_negative.moss"
rejected = run(compiler, "check", negative, "--json")
assert rejected.returncode != 0, (rejected.stdout, rejected.stderr)
diagnostic = json.loads(rejected.stdout)
assert diagnostic["error"]["code"] == "OWNERSHIP_USE_AFTER_CONSUME", diagnostic

print("Phase 21 recovery reinitialization joins selected exceptional paths.")
