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
SOURCE.write_text('fn main():\n  echo "FileIO.open( parse_int( to_int("\n')
subprocess.run([str(COMPILER), "check", str(SOURCE), "--json"],
               check=True, capture_output=True, text=True)

# A provider may export a distinct user-defined FileError. Until FileIO error
# identity is resolved across qualified modules, this ambiguity fails closed.
package = SCRATCH / "unrelated_import"
(package / "src").mkdir(parents=True, exist_ok=True)
(package / "Moss.toml").write_text(
    '[project]\nname="phase21_error_contract_modules"\nversion="0.1.0"\n')
(package / "src/provider.moss").write_text(
    'module provider\nexport enum FileError:\n  Full(code: Int)\n')
(package / "src/main.moss").write_text(
    'module app\nimport provider\n' + file_error + '\n' +
    'fn main():\n  try:\n'
    '    file = FileIO.open("phase21-error-contract-absent.txt", ro)\n'
    '    file.close()\n'
    '  recover FileError.NotFound:\n    echo 1\n'
    '  recover:\n    echo 99\n')
checked = subprocess.run([str(COMPILER), "build", "--json"], cwd=package,
                         capture_output=True, text=True)
diagnostic = json.loads(checked.stdout)
if checked.returncode == 0 or diagnostic.get("error", {}).get("code") != "FILEIO_ERROR_IDENTITY_UNSUPPORTED":
    raise SystemExit(f"ambiguous module FileError identity accepted: {diagnostic}")

(package / "src/main.moss").write_text(
    'module app\nimport provider\nfn main():\n'
    '  try:\n    file = FileIO.open("phase21-error-contract-absent.txt", ro)\n'
    '    file.close()\n  recover:\n    echo 1\n')
checked = subprocess.run([str(COMPILER), "build", "--json"], cwd=package,
                         capture_output=True, text=True)
diagnostic = json.loads(checked.stdout)
if checked.returncode == 0 or diagnostic.get("error", {}).get("code") != "BUILTIN_ERROR_TYPE_SHAPE":
    raise SystemExit(f"imported payload FileError accepted for FileIO: {diagnostic}")

qualified = SCRATCH / "qualified_recovery"
(qualified / "src").mkdir(parents=True, exist_ok=True)
(qualified / "Moss.toml").write_text(
    '[project]\nname="phase21_qualified_error_recovery"\nversion="0.1.0"\n')
(qualified / "src/main.moss").write_text(
    'module app\n' + file_error + '\n' +
    'fn main():\n  try:\n'
    '    file = FileIO.open("phase21-error-contract-absent.txt", ro)\n'
    '    file.close()\n'
    '  recover FileError.NotFound:\n    echo 1\n'
    '  recover:\n    echo 99\n')
checked = subprocess.run([str(COMPILER), "build", "--json"], cwd=qualified,
                         capture_output=True, text=True)
diagnostic = json.loads(checked.stdout)
if checked.returncode == 0 or diagnostic.get("error", {}).get("code") != "FILEIO_ERROR_IDENTITY_UNSUPPORTED":
    raise SystemExit(f"module-qualified FileIO recovery accepted: {diagnostic}")

print("Phase 21 declared built-in error contracts passed")
