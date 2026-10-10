#!/usr/bin/env python3
"""Focused Phase 21 Float division and canonical text parity check."""

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
  echo 1.0
  echo 0.1 + 0.2
  echo 1e-4
  echo 1e-7
  echo 1e16
  echo 1e300
  echo 1E3
  echo 1e+3
  echo 1e-5
  echo 5e-324
  echo 2.2250738585072014e-308
  echo 1.7976931348623157e308
""")

subprocess.run([str(COMPILER), str(SOURCE), "-o", str(RUST)], check=True)
subprocess.run(["rustc", "-D", "warnings", str(RUST), "-o", str(NATIVE)], check=True)
native = subprocess.run([str(NATIVE)], check=True, capture_output=True, text=True).stdout
debug = subprocess.run([str(COMPILER), "run", "--interp", str(SOURCE)],
                       check=True, capture_output=True, text=True).stdout
expected = ("inf\n-inf\nNaN\n-0.0\n1.0\n0.30000000000000004\n"
            "0.0001\n1e-7\n1e16\n1e300\n1000.0\n1000.0\n"
            "1e-5\n5e-324\n2.2250738585072014e-308\n"
            "1.7976931348623157e308\n")
if native != expected:
    raise SystemExit(f"native output mismatch: {native!r} != {expected!r}")
if debug != expected:
    raise SystemExit(f"Fast Debug output mismatch: {debug!r} != {expected!r}")
print("Phase 21 Float IEEE and canonical text parity passed")
