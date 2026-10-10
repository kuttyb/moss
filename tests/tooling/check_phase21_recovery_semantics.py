#!/usr/bin/env python3
"""Focused checker coverage for Phase 21 lexical recovery and raise inference."""

from pathlib import Path
import json
import subprocess
import sys


repo = Path(__file__).resolve().parents[2]
compiler = Path(sys.argv[1] if len(sys.argv) > 1 else repo / "moss").resolve()
source = repo / "tests/tooling/fixtures/phase21_recovery_semantics.moss"


def run(*args):
    return subprocess.run([str(item) for item in args], cwd=repo,
                          text=True, capture_output=True, timeout=60)


checked = run(compiler, "check", source, "--json")
assert checked.returncode == 0, (checked.stdout, checked.stderr)
check_json = json.loads(checked.stdout)
assert check_json["ok"], check_json
warnings = check_json["result"]["diagnostics"]
assert any(item.get("code") == "RECOVER_UNREACHABLE_VARIANT"
           for item in warnings), warnings

effects = run(compiler, "effects", "may_fail", "--source", source, "--json")
assert effects.returncode == 0, (effects.stdout, effects.stderr)
effect_json = json.loads(effects.stdout)
assert effect_json["ok"], effect_json
raises = effect_json["result"]["observable_effects"]["raise_set"]
assert raises == ["enum:WorkError:Failed", "enum:WorkError:Missing"], raises

handled = run(compiler, "effects", "handled", "--source", source, "--json")
assert handled.returncode == 0, (handled.stdout, handled.stderr)
handled_json = json.loads(handled.stdout)
assert handled_json["ok"], handled_json
assert handled_json["result"]["observable_effects"]["raise_set"] == [], handled_json

print("Phase 21 recovery checker and variant-precise effect probes passed.")
