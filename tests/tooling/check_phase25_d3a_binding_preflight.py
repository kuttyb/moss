#!/usr/bin/env python3
"""Focused proof of implicit mutable-local semantics for D3a retention."""

import json
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]
MOSS = Path(sys.argv[1] if len(sys.argv) > 1 else "./moss").resolve()
SCRATCH = ROOT / "tmp"
SCRATCH.mkdir(exist_ok=True)

IMPLICIT = """fn main():
  n = 1
  n = 2
  echo n
"""

EXPLICIT_VAR = """fn main():
  var n = 1
  n = 2
  echo n
"""

BRANCH_LOCAL_ESCAPE = """fn main():
  if true:
    n = 1
  echo n
"""


def run(args):
    return subprocess.run(args, cwd=ROOT, capture_output=True, text=True)


def check_rejects(code, expected_code, stem):
    source = SCRATCH / f"{stem}.moss"
    source.write_text(code)
    try:
        result = run([str(MOSS), "check", str(source), "--json"])
        assert result.returncode != 0, f"{stem} unexpectedly passed checker"
        diagnostic = json.loads(result.stdout)
        codes = [d.get("code") for d in diagnostic.get("diagnostics", [])]
        if diagnostic.get("error"):
            codes.append(diagnostic["error"].get("code"))
        assert expected_code in codes, f"{stem}: expected {expected_code}, got {codes}"
    finally:
        source.unlink(missing_ok=True)


def execute_all_modes(code, stem):
    source = SCRATCH / f"{stem}.moss"
    source.write_text(code)
    generated = SCRATCH / f"{stem}.rs"
    binary = SCRATCH / stem
    try:
        checked = run([str(MOSS), "check", str(source), "--json"])
        assert checked.returncode == 0, f"checker rejected {stem}: {checked.stdout}\n{checked.stderr}"

        interpreted = run([str(MOSS), "run", "--interp", str(source)])
        assert interpreted.returncode == 0, f"interpreter failed {stem}: {interpreted.stderr}"

        emitted = run([str(MOSS), "--debug", str(source), "-o", str(generated)])
        assert emitted.returncode == 0, f"native emission failed {stem}: {emitted.stderr}"
        rustc = run(["rustc", "--crate-name", stem, str(generated), "-o", str(binary)])
        assert rustc.returncode == 0, f"rustc failed {stem}: {rustc.stderr}"
        native = run([str(binary)])
        assert native.returncode == 0, f"native execution failed {stem}: {native.stderr}"

        assert interpreted.stdout == native.stdout, (
            f"{stem}: interpreter/native outputs differ: "
            f"{interpreted.stdout!r} != {native.stdout!r}"
        )
        return interpreted.stdout
    finally:
        source.unlink(missing_ok=True)
        generated.unlink(missing_ok=True)
        binary.unlink(missing_ok=True)


implicit_output = execute_all_modes(IMPLICIT, "d3a_implicit_mutable")
var_output = execute_all_modes(EXPLICIT_VAR, "d3a_explicit_var")
assert implicit_output == var_output, (
    f"implicit assignment and var differ: {implicit_output!r} != {var_output!r}"
)
assert implicit_output.strip() == "2", f"expected reassigned value 2, got {implicit_output!r}"

check_rejects(BRANCH_LOCAL_ESCAPE, "UNKNOWN_SYMBOL_OR_TYPE", "d3a_branch_local")

print("D3a implicit mutable binding preflight: PASS")
