#!/usr/bin/env python3
"""Focused Phase 21 IEEE Float division parity check."""

import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
COMPILER = Path(sys.argv[1]).resolve() if len(sys.argv) > 1 else ROOT / "moss"
SCRATCH = ROOT / "tmp" / "phase21_float_ieee"
SCRATCH.mkdir(parents=True, exist_ok=True)
SOURCE = SCRATCH / "float_ieee.moss"
RUST = SCRATCH / "float_ieee.rs"
NATIVE = SCRATCH / "float_ieee"

SOURCE.write_text("""fn main():
  echo 1.0 / 0.0
  echo -1.0 / 0.0
  echo 0.0 / 0.0
  echo -0.0
""")

subprocess.run([str(COMPILER), str(SOURCE), "-o", str(RUST)], check=True)
subprocess.run(["rustc", str(RUST), "-o", str(NATIVE)], check=True)
native = subprocess.run([str(NATIVE)], check=True, capture_output=True, text=True).stdout
debug = subprocess.run([str(COMPILER), "run", "--interp", str(SOURCE)],
                       check=True, capture_output=True, text=True).stdout
expected = "inf\n-inf\nNaN\n-0.0\n"
if native != expected:
    raise SystemExit(f"native output mismatch: {native!r} != {expected!r}")
if debug != expected:
    raise SystemExit(f"Fast Debug output mismatch: {debug!r} != {expected!r}")
print("Phase 21 Float IEEE division parity passed")
