#!/usr/bin/env python3
"""Regression for enforcing compact canonical indexing and collection type syntax during Moss formatting."""
import subprocess
import sys
import tempfile
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
COMPILER = Path(sys.argv[1] if len(sys.argv) > 1 else ROOT / "moss").resolve()
SOURCE = """fn make_values() -> Vector [Int]:
  return [1, 2]

fn main():
  var xs = Vector [Int]()
  xs.push(42)
  var i = 0
  x = xs [i]
  xs [i] = x + 1

  row0 = [10, 20]
  row1 = [30, 40]
  matrix = [row0, row1]
  var j = 1
  y = matrix [i] [j]

  z = make_values () [i]

  echo xs [0]
  echo y
  echo z
"""
EXPECTED = """fn make_values() -> Vector[Int]:
  return [1, 2]

fn main():
  var xs = Vector[Int]()
  xs.push(42)
  var i = 0
  x = xs[i]
  xs[i] = x + 1

  row0 = [10, 20]
  row1 = [30, 40]
  matrix = [row0, row1]
  var j = 1
  y = matrix[i][j]

  z = make_values()[i]

  echo xs[0]
  echo y
  echo z
"""


def run(args, **kwargs):
    completed = subprocess.run(args, text=True, capture_output=True, **kwargs)
    assert completed.returncode == 0, (args, completed.stdout, completed.stderr)
    return completed


with tempfile.TemporaryDirectory(dir=ROOT / "tmp") as directory:
    source = Path(directory) / "indexing_syntax.moss"
    source.write_text(SOURCE)
    run([str(COMPILER), "fmt", str(source)])
    actual = source.read_text()
    assert actual == EXPECTED, f"Expected:\n{EXPECTED}\nActual:\n{actual}"
    assert "Vector [Int]" not in actual, "Found unwanted 'Vector [Int]'"
    assert "xs [i]" not in actual, "Found unwanted 'xs [i]'"
    assert "xs [0]" not in actual, "Found unwanted 'xs [0]'"
    assert "matrix[i] [j]" not in actual, "Found unwanted 'matrix[i] [j]'"
    assert "matrix [i]" not in actual, "Found unwanted 'matrix [i]'"
    assert "make_values() [i]" not in actual, "Found unwanted 'make_values() [i]'"
    assert "make_values ()" not in actual, "Found unwanted 'make_values ()'"
    assert "return[1, 2]" not in actual, "Found unwanted 'return[1, 2]'"
    assert "Vector[Int]" in actual, "Expected 'Vector[Int]'"
    assert "xs[i]" in actual, "Expected 'xs[i]'"
    assert "matrix[i][j]" in actual, "Expected 'matrix[i][j]'"
    assert "make_values()[i]" in actual, "Expected 'make_values()[i]'"
    assert "return [1, 2]" in actual, "Expected 'return [1, 2]'"
    run([str(COMPILER), "fmt", str(source)])
    assert source.read_text() == EXPECTED, "Formatter was not idempotent"
    run([str(COMPILER), "check", str(source), "--json"])
    rust = Path(directory) / "indexing_syntax.rs"
    run([str(COMPILER), str(source), "-o", str(rust)])
    binary = Path(directory) / "indexing_syntax"
    run(["rustc", "-D", "warnings", str(rust), "-o", str(binary)])
    assert run([str(binary)]).stdout == "43\n20\n1\n"

print("Moss compact indexing formatter regression passed")
