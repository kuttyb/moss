#!/usr/bin/env python3
"""A method's no-op catch-all must warn when it writes protected domain state."""

import json
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]
FIXTURES = ROOT / "tests/tooling/fixtures"
MOSS = Path(sys.argv[1] if len(sys.argv) > 1 else ROOT / "moss").resolve()
OUT = ROOT / "tmp/phase21-method-noop-warning"
OUT.mkdir(parents=True, exist_ok=True)


def run(*args):
    return subprocess.run([str(arg) for arg in args], cwd=ROOT, text=True,
                          capture_output=True, timeout=120)


for stem in ("phase21_method_noop_warning",
             "phase21_helper_method_noop_warning"):
    source = FIXTURES / f"{stem}.moss"
    checked = run(MOSS, "check", source, "--json")
    assert checked.returncode == 0, (stem, checked.stderr)
    diagnostics = json.loads(checked.stdout)["result"]["diagnostics"]
    warnings = [item for item in diagnostics if item["code"] ==
                "RECOVER_EMPTY_AFTER_STATE_WRITE"]
    assert len(warnings) == 1, (stem, diagnostics)
    assert warnings[0]["severity"] == "warning", (stem, warnings)
    assert "cell.value" in warnings[0]["message"], (stem, warnings)

    effects = run(MOSS, "effects", "handler:Sample.Run", "--source", source,
                  "--json")
    assert effects.returncode == 0, (stem, effects.stderr)
    facts = json.loads(effects.stdout)["result"]
    assert facts["observable_effects"]["domain_write"], (
        stem, facts["observable_effects"])
    handlers = facts["synchronization"]["instances"][0]["handlers"]
    assert any(handler["write_set"] == ["cell.value"] for handler in handlers), (
        stem, handlers)

    generated = OUT / f"{stem}.rs"
    binary = OUT / stem
    assert run(MOSS, source, "-o", generated).returncode == 0
    compiled = run("rustc", "--edition=2021", "-D", "warnings", generated,
                   "-o", binary)
    assert compiled.returncode == 0, (stem, compiled.stderr)
    native = run(binary)
    debug = run(MOSS, "run", "--interp", source)
    assert (native.returncode, native.stdout) == (0, "2\n"), (stem, native)
    assert (debug.returncode, debug.stdout) == (0, "2\n"), (stem, debug)

local_source = OUT / "method_local_receiver.moss"
local_source.write_text('''enum Error:
  Bad
type Box:
  value: Int
  fn swallow():
    try:
      value = 2
      raise Error.Bad
    recover:
      pass
domain Sample:
  unrelated = 0
  fn Run():
    local = Box(value = 1)
    local.swallow()
    echo local.value
fn main():
  sample = Sample()
  message sample.Run()
''')
local_check = run(MOSS, "check", local_source, "--json")
assert local_check.returncode == 0, local_check.stderr
local_diagnostics = json.loads(local_check.stdout)["result"]["diagnostics"]
assert not [item for item in local_diagnostics if item["code"] ==
            "RECOVER_EMPTY_AFTER_STATE_WRITE"], local_diagnostics
local_effects = run(MOSS, "effects", "handler:Sample.Run", "--source",
                    local_source, "--json")
assert local_effects.returncode == 0, local_effects.stderr
assert not json.loads(local_effects.stdout)["result"]["observable_effects"][
    "domain_write"]

print("Phase 21 method no-op recovery warning regression passed.")
