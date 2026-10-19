#!/usr/bin/env python3
"""Known nominal template with an inferred field specializes at each call."""
from pathlib import Path
import subprocess
import sys

root = Path(__file__).resolve().parents[2]
compiler = Path(sys.argv[1]).resolve() if len(sys.argv) > 1 else root / "moss"
source = root / "tests/phase25/g0/conformance/known_open_nominal_field.moss"
out = root / "tmp" / "phase25-known-open-nominal"
out.parent.mkdir(exist_ok=True)
subprocess.run([str(compiler), "check", str(source), "--json"], check=True, capture_output=True)
interp = subprocess.run([str(compiler), "run", "--interp", str(source)], check=True,
                        capture_output=True, text=True).stdout
subprocess.run([str(compiler), str(source), "-o", str(out) + ".rs"],
               check=True, capture_output=True)
subprocess.run(["rustc", "-D", "warnings", str(out) + ".rs", "-o", str(out)],
               check=True, capture_output=True)
native = subprocess.run([str(out)], check=True, capture_output=True, text=True).stdout
assert interp == native == "42\nhello\n", (interp, native)
print("Phase 25 known-open nominal: PASS")
