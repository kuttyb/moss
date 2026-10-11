#!/usr/bin/env python3
"""Raising chunk placeholders stay inside their callback Result before recover."""

import json
from pathlib import Path
import re
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]
COMPILER = Path(sys.argv[1] if len(sys.argv) > 1 else ROOT / "moss").resolve()
SOURCE = ROOT / "tests/tooling/fixtures/phase21_pipeline_try_placeholder.moss"
OUT = ROOT / "tmp/phase21-pipeline-try-placeholder"
OUT.mkdir(parents=True, exist_ok=True)
(OUT / "p4.bin").write_bytes(b"ABABC")


def run(*args, expected=0):
    result = subprocess.run([str(arg) for arg in args], cwd=ROOT,
                            capture_output=True, text=True, timeout=180)
    assert result.returncode == expected, (
        args, result.returncode, result.stdout, result.stderr)
    return result


checked = json.loads(run(COMPILER, "check", SOURCE, "--json").stdout)
assert checked["ok"], checked
for handler in ("Reader.Eligible", "Reader.Sequential", "Reader.SeedBeforeMap"):
    effects = json.loads(run(COMPILER, "effects", handler, "--source",
                             SOURCE, "--json").stdout)
    assert effects["ok"] and not effects["result"]["observable_effects"]["unresolved"]
    if handler == "Reader.SeedBeforeMap":
        raises = effects["result"]["observable_effects"]["raise_set"]
        assert "enum:SetupError:Bad" in raises and "enum:FormatError:Bad" in raises, raises

rust = OUT / "pipeline_try_placeholder.rs"
ir = run(COMPILER, SOURCE, "--dump-functional-ir", "-o", rust).stdout
assert re.search(r"handler:Reader.Eligible[\s\S]*?chunk-plan:[^\n]*eligible=yes", ir), ir
assert re.search(r"handler:Reader.Sequential[\s\S]*?chunk-plan:[^\n]*eligible=no", ir), ir
assert re.search(r"handler:Reader.SeedBeforeMap[\s\S]*?chunk-plan:[^\n]*eligible=yes", ir), ir
for level in (0, 3):
    native = OUT / f"pipeline_try_placeholder_O{level}"
    run("rustc", "--edition=2021", "-Dwarnings", "-C", f"opt-level={level}",
        rust, "-o", native)
    assert run(native).stdout == "-1\n-1\n-3\n", level

print("Phase 21 lexical recovery of checked chunk placeholders passed.")
