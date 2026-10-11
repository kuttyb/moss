#!/usr/bin/env python3
"""Field projection of a raising rvalue preserves its exact typed effect."""

import json
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]
FIXTURES = ROOT / "tests/tooling/fixtures"
MOSS = Path(sys.argv[1] if len(sys.argv) > 1 else ROOT / "moss").resolve()
OUT = ROOT / "tmp/phase21-field-projection-effects"
OUT.mkdir(parents=True, exist_ok=True)


def run(*args):
    return subprocess.run([str(arg) for arg in args], cwd=ROOT, text=True,
                          capture_output=True, timeout=120)


valid = FIXTURES / "phase21_field_projection_raise.moss"
checked = run(MOSS, "check", valid, "--json")
assert checked.returncode == 0, (checked.stdout, checked.stderr)
diagnostics = json.loads(checked.stdout)["result"]["diagnostics"]
assert not [item for item in diagnostics if item["code"] ==
            "RECOVER_UNREACHABLE_VARIANT"], diagnostics
effects = run(MOSS, "effects", "projected", "--source", valid, "--json")
assert effects.returncode == 0, (effects.stdout, effects.stderr)
summary = json.loads(effects.stdout)["result"]["observable_effects"]
assert summary["raise_set"] == ["enum:Error:Bad"], summary
assert not summary["unresolved"], summary

rust = OUT / "field_projection.rs"
binary = OUT / "field_projection"
lowered = run(MOSS, valid, "-o", rust)
assert lowered.returncode == 0, (lowered.stdout, lowered.stderr)
compiled = run("rustc", "--edition=2021", "-D", "warnings", rust,
               "-o", binary)
assert compiled.returncode == 0, compiled.stderr
native = run(binary)
debug = run(MOSS, "run", "--interp", valid)
assert (native.returncode, native.stdout) == (0, "caught\n"), native
assert (debug.returncode, debug.stdout) == (0, "caught\n"), debug

unhandled = FIXTURES / "phase21_field_projection_root.moss"
root = run(MOSS, "check", unhandled, "--json")
assert root.returncode != 0, root.stdout
error = json.loads(root.stdout)["error"]
assert error["code"] == "ROOT_UNHANDLED_RAISE", error
assert "enum:Error:Bad" in error["message"], error

print("Phase 21 field projection effect and Root totality regressions passed.")
