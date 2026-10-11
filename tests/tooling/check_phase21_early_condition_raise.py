#!/usr/bin/env python3
"""A typed raise in a leading condition preserves Root handling and 2PL placement."""

import json
from pathlib import Path
import subprocess
import sys


repo = Path(__file__).resolve().parents[2]
compiler = Path(sys.argv[1]).resolve()
source = repo / "tests/tooling/fixtures/phase21_early_condition_raise.moss"
scratch = repo / "tmp/phase21-early-condition-raise"
scratch.mkdir(parents=True, exist_ok=True)


def run(*args):
    return subprocess.run([str(arg) for arg in args], cwd=repo,
                          capture_output=True, text=True, timeout=180)


checked = run(compiler, "check", source, "--json")
assert checked.returncode == 0, (checked.stdout, checked.stderr)
effects = run(compiler, "effects", "handler:Worker.Run", "--source", source,
              "--json")
assert effects.returncode == 0, (effects.stdout, effects.stderr)
facts = json.loads(effects.stdout)["result"]
assert facts["observable_effects"]["raise_set"] == ["enum:Failure:Bad"], facts
assert facts["observable_effects"]["may_panic"] is False, facts

generated = scratch / "condition.rs"
lowered = run(compiler, source, "-o", generated)
assert lowered.returncode == 0, (lowered.stdout, lowered.stderr)
for opt in ("0", "3"):
    binary = scratch / f"condition-o{opt}"
    compiled = run("rustc", "--edition=2021", "-D", "warnings",
                   f"-Copt-level={opt}", generated, "-o", binary)
    assert compiled.returncode == 0, (opt, compiled.stdout, compiled.stderr)
    native = run(binary)
    assert native.returncode == 0 and native.stdout == "1\n4\n2\n", (
        opt, native.returncode, native.stdout, native.stderr)

debug = run(compiler, "run", "--interp", source)
assert debug.returncode == 0 and debug.stdout == "1\n4\n2\n", (
    debug.returncode, debug.stdout, debug.stderr)

print("Phase 21 raising leading condition preserves checked Root recovery.")
