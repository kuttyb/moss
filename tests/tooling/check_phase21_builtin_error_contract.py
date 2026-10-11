#!/usr/bin/env python3
"""The declared Phase 21 built-in error enums have exact, tag-only shapes."""

import json
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
COMPILER = Path(sys.argv[1]).resolve() if len(sys.argv) > 1 else ROOT / "moss"
SCRATCH = ROOT / "tmp" / "phase21_builtin_error_contract"
SCRATCH.mkdir(parents=True, exist_ok=True)
SOURCE = SCRATCH / "contract.moss"
RUST = SCRATCH / "contract.rs"
NATIVE = SCRATCH / "contract"

file_error = """enum FileError:
  NotFound
  PermissionDenied
  NotRegularFile
  InUse
  Full
  IO
"""
parse_error = """enum ParseError:
  Invalid
  Overflow
"""
conversion_error = """enum ConversionError:
  NonFinite
  Overflow
"""
body = """fn main():
  try:
    file = FileIO.open("tmp/phase21-error-contract-absent.txt", ro)
    file.close()
  recover FileError.NotFound:
    echo 1
  recover:
    echo 99
  try:
    echo parse_int("bad")
  recover ParseError.Invalid:
    echo 2
  recover ParseError.Overflow:
    echo 98
  try:
    echo to_int(0.0 / 0.0)
  recover ConversionError.NonFinite:
    echo 3
  recover ConversionError.Overflow:
    echo 97
"""


def check_error(source, expected_code):
    SOURCE.write_text(source)
    result = subprocess.run([str(COMPILER), "check", str(SOURCE), "--json"],
                            capture_output=True, text=True)
    decoded = json.loads(result.stdout)
    actual = decoded.get("error", {}).get("code")
    if result.returncode == 0 or actual != expected_code:
        raise SystemExit(f"expected {expected_code}, got {actual}: {decoded}")


valid = file_error + "\n" + parse_error + "\n" + conversion_error + "\n" + body
SOURCE.write_text(valid)
subprocess.run([str(COMPILER), "check", str(SOURCE), "--json"],
               check=True, capture_output=True, text=True)
subprocess.run([str(COMPILER), str(SOURCE), "-o", str(RUST)], check=True,
               capture_output=True, text=True)
subprocess.run(["rustc", "-D", "warnings", str(RUST), "-o", str(NATIVE)],
               check=True, capture_output=True, text=True)
result = subprocess.run([str(NATIVE)], check=True, capture_output=True, text=True)
if result.stdout != "1\n2\n3\n":
    raise SystemExit(f"native error contract output: {result.stdout!r}")

# Fast Debug still delegates FileIO to the native backend. Numeric recovery
# must agree with native execution for the operations it supports.
file_block = body[body.index("  try:\n"):body.index("  try:\n    echo parse_int")]
SOURCE.write_text(valid.replace(file_block, ""))
result = subprocess.run([str(COMPILER), "run", "--interp", str(SOURCE)],
                        check=True, capture_output=True, text=True)
if result.stdout != "2\n3\n":
    raise SystemExit(f"Fast Debug numeric contract output: {result.stdout!r}")

for malformed in (
        file_error.replace("  IO\n", "  IO(code: Int)\n"),
        file_error.replace("  IO\n", ""),
        file_error + "  Extra\n"):
    check_error(valid.replace(file_error, malformed), "BUILTIN_ERROR_TYPE_SHAPE")
for malformed in (
        parse_error.replace("  Invalid\n", "  Invalid(code: Int)\n"),
        parse_error.replace("  Overflow\n", ""),
        parse_error + "  Extra\n"):
    check_error(valid.replace(parse_error, malformed), "BUILTIN_ERROR_TYPE_SHAPE")
for malformed in (
        conversion_error.replace("  NonFinite\n", "  NonFinite(code: Int)\n"),
        conversion_error.replace("  Overflow\n", ""),
        conversion_error + "  Extra\n"):
    check_error(valid.replace(conversion_error, malformed),
                "BUILTIN_ERROR_TYPE_SHAPE")

check_error(valid.replace(file_error + "\n", ""), "UNKNOWN_ERROR_TYPE")
check_error(valid.replace(parse_error + "\n", ""), "UNKNOWN_ERROR_TYPE")
check_error(valid.replace(conversion_error + "\n", ""), "UNKNOWN_ERROR_TYPE")

# An enum with the same name can be a distinct user type when no built-in
# operation uses that error family. Source-free providers rely on this.
SOURCE.write_text("enum FileError:\n  Full(code: Int)\nfn main():\n  pass\n")
subprocess.run([str(COMPILER), "check", str(SOURCE), "--json"],
               check=True, capture_output=True, text=True)

print("Phase 21 declared built-in error contracts passed")
