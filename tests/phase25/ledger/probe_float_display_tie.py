#!/usr/bin/env python3
"""Minimal executable G1-07 parity regression; currently fails on Fast Debug."""

import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
SOURCE = Path(__file__).resolve().parent / "fixtures/g1_float_display_tie.moss"
MOSS = Path(sys.argv[1]).resolve() if len(sys.argv) > 1 else ROOT / "moss"
SCRATCH = ROOT / "tmp/phase25-g1-float-display-tie"
SCRATCH.mkdir(parents=True, exist_ok=True)
RUST = SCRATCH / "tie.rs"
NATIVE = SCRATCH / "tie"


def output(command):
    return subprocess.run(command, check=True, capture_output=True, text=True).stdout


debug = output([str(MOSS), "run", "--interp", str(SOURCE)])
output([str(MOSS), str(SOURCE), "-o", str(RUST)])
subprocess.run(["rustc", "-D", "warnings", str(RUST), "-o", str(NATIVE)],
               check=True, capture_output=True, text=True)
native = output([str(NATIVE)])
if debug != native:
    raise SystemExit(
        "G1-07 Float.to_string parity failure at f64 bits 0x4306a67789fbfd12: "
        f"Fast Debug {debug.strip()!r}, native Rust Display {native.strip()!r}")
print("G1-07 minimal Float.to_string tie parity passed")
