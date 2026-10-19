#!/usr/bin/env python3
"""Focused checker and execution coverage for unsupported unary !."""

import json
from pathlib import Path
import subprocess
import tempfile


ROOT = Path(__file__).resolve().parents[2]
MOSS = ROOT / "moss"
WORK = ROOT / "tmp" / "unsupported-bang-operator"
WORK.mkdir(parents=True, exist_ok=True)


def source(name, text):
    path = WORK / name
    path.write_text(text)
    return path


def run(*args, expected=0):
    result = subprocess.run([str(MOSS), *map(str, args)], cwd=ROOT,
                            text=True, capture_output=True)
    assert result.returncode == expected, (
        args, result.returncode, result.stdout, result.stderr)
    return result


def check(text, accepted, name):
    path = source(name, text)
    result = run("check", path, "--json", expected=0 if accepted else 1)
    payload = json.loads(result.stdout)
    diagnostics = payload.get("result", {}).get("diagnostics", [])
    if accepted:
        assert payload["ok"] and not diagnostics, (name, payload)
    else:
        error_code = payload.get("error", {}).get("code")
        assert not payload["ok"] and (
            error_code == "UNSUPPORTED_EXPRESSION_OPERATOR" or any(
                diagnostic.get("code") == "UNSUPPORTED_EXPRESSION_OPERATOR"
                for diagnostic in diagnostics)), (name, payload)
    return path


check("""fn main():
  available = false
  if !available:
    echo "down"
""", False, "bang-if.moss")

not_path = check("""fn main():
  available = false
  if not available:
    echo "down"
""", True, "not-if.moss")
interp = run("run", "--interp", not_path)
with tempfile.TemporaryDirectory(dir=ROOT / "tmp") as directory:
    generated = Path(directory) / "not.rs"
    compiled = run(not_path, "-O0", "-o", generated)
    native_compile = subprocess.run(
        ["rustc", "--edition=2021", "-C", "opt-level=0", "-D", "warnings",
         str(generated), "-o", str(Path(directory) / "not-bin")],
        cwd=ROOT, text=True, capture_output=True)
    assert native_compile.returncode == 0, native_compile.stderr
    native = subprocess.run([str(Path(directory) / "not-bin")], cwd=ROOT,
                            text=True, capture_output=True)
    assert native.returncode == 0, native.stderr
assert interp.stdout == native.stdout == "down\n", (interp.stdout, native.stdout)

check("""fn main():
  a = 1
  b = 2
  if a != b:
    echo "different"
""", True, "not-equal.moss")

check("""fn main():
  a = 1
  b = 2
  if !(a == b):
    echo "different"
""", False, "nested-bang.moss")

check("""fn main():
  echo "!"
""", True, "bang-string.moss")

check("""fn invert(value: Bool) -> Bool:
  return !value

fn main():
  echo invert(false)
""", False, "bang-function-return.moss")

check("""domain Worker:
  fn Check(available: Bool):
    if !available:
      echo "down"

fn main():
  worker = Worker()
  message worker.Check(false)
""", False, "bang-handler.moss")

print("unsupported unary ! focused checks passed")
