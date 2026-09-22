#!/usr/bin/env python3
"""Regression for preserving unary minus during Moss formatting."""
import subprocess
import sys
import tempfile
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
COMPILER = Path(sys.argv[1] if len(sys.argv) > 1 else ROOT / "moss").resolve()
SOURCE = """fn main():
  x=-4
  assertEqual(x,-4)
  values=[-4,-1]
  y=3*-4
  ready=false
  assert(not ready)
  assert(not (3>5))
  echo x
  echo values[0]
  echo values[1]
  echo y
"""
EXPECTED = """fn main():
  x = -4
  assertEqual(x, -4)
  values = [-4, -1]
  y = 3 * -4
  ready = false
  assert(not ready)
  assert(not (3 > 5))
  echo x
  echo values [0]
  echo values [1]
  echo y
"""


def run(args, **kwargs):
    completed = subprocess.run(args, text=True, capture_output=True, **kwargs)
    assert completed.returncode == 0, (args, completed.stdout, completed.stderr)
    return completed


with tempfile.TemporaryDirectory(dir=ROOT / "tmp") as directory:
    source = Path(directory) / "unary_minus.moss"
    source.write_text(SOURCE)
    run([str(COMPILER), "fmt", str(source)])
    assert source.read_text() == EXPECTED, source.read_text()
    run([str(COMPILER), "fmt", str(source)])
    assert source.read_text() == EXPECTED, source.read_text()
    run([str(COMPILER), "check", str(source), "--json"])
    rust = Path(directory) / "unary_minus.rs"
    run([str(COMPILER), str(source), "-o", str(rust)])
    binary = Path(directory) / "unary_minus"
    run(["rustc", "-D", "warnings", str(rust), "-o", str(binary)])
    assert run([str(binary)]).stdout == "-4\n-4\n-1\n-12\n"

print("SWARM-003 unary-minus formatter regression passed")
