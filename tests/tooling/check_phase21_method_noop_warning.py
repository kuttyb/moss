#!/usr/bin/env python3
"""A method's no-op catch-all must warn when it writes protected domain state."""

import json
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / "tests/tooling/fixtures/phase21_method_noop_warning.moss"
MOSS = Path(sys.argv[1] if len(sys.argv) > 1 else ROOT / "moss").resolve()
OUT = ROOT / "tmp/phase21-method-noop-warning"
OUT.mkdir(parents=True, exist_ok=True)


def run(*args):
    return subprocess.run([str(arg) for arg in args], cwd=ROOT, text=True,
                          capture_output=True, timeout=120)


checked = run(MOSS, "check", SOURCE, "--json")
assert checked.returncode == 0, checked.stderr
diagnostics = json.loads(checked.stdout)["result"]["diagnostics"]
warnings = [item for item in diagnostics if item["code"] ==
            "RECOVER_EMPTY_AFTER_STATE_WRITE"]
assert len(warnings) == 1, diagnostics
assert warnings[0]["severity"] == "warning", warnings
assert "cell.value" in warnings[0]["message"], warnings

effects = run(MOSS, "effects", "handler:Sample.Run", "--source", SOURCE,
              "--json")
assert effects.returncode == 0, effects.stderr
facts = json.loads(effects.stdout)["result"]
assert facts["observable_effects"]["domain_write"], facts["observable_effects"]
handlers = facts["synchronization"][
    "instances"][0]["handlers"]
assert any(handler["write_set"] == ["cell.value"] for handler in handlers), handlers

generated = OUT / "method_noop.rs"
binary = OUT / "method_noop"
assert run(MOSS, SOURCE, "-o", generated).returncode == 0
compiled = run("rustc", "--edition=2021", "-D", "warnings", generated,
               "-o", binary)
assert compiled.returncode == 0, compiled.stderr
native = run(binary)
debug = run(MOSS, "run", "--interp", SOURCE)
assert (native.returncode, native.stdout) == (0, "2\n"), native
assert (debug.returncode, debug.stdout) == (0, "2\n"), debug

print("Phase 21 method no-op recovery warning regression passed.")
