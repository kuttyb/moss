#!/usr/bin/env python3
"""Compiler-owned standard errors keep their identity across package boundaries.

This is an acceptance gate for the L7-approved Phase 21 standard namespace. A
provider may own unrelated enums named FileError, ParseError, and
ConversionError. The consumer's unqualified names still denote the standard
errors, and qualified provider names retain their separate nominal identity.
"""

import json
from pathlib import Path
import subprocess
import sys


ROOT = Path(__file__).resolve().parents[2]
COMPILER = Path(sys.argv[1] if len(sys.argv) > 1 else ROOT / "moss").resolve()
MARGO = COMPILER.with_name("margo")
SCRATCH = ROOT / "tmp" / "phase21_standard_error_namespace"
SCRATCH.mkdir(parents=True, exist_ok=True)


def run(*args, cwd=ROOT, ok=True):
    result = subprocess.run([str(arg) for arg in args], cwd=cwd,
                            capture_output=True, text=True, timeout=180)
    if ok and result.returncode != 0:
        raise AssertionError((args, result.returncode, result.stdout,
                              result.stderr))
    if not ok and result.returncode == 0:
        raise AssertionError((args, "unexpected success", result.stdout,
                              result.stderr))
    return result


def check(source, expected_code=None):
    checked = run(COMPILER, "check", source, "--json", ok=expected_code is None)
    parsed = json.loads(checked.stdout)
    if expected_code is None:
        assert parsed["ok"], parsed
    else:
        assert parsed["error"]["code"] == expected_code, parsed
    return parsed


# No application declaration or import is needed for all three exact standard
# error types, including direct constructors and the built-in operations.
standalone = SCRATCH / "standalone.moss"
standalone.write_text('''fn main():
  try:
    echo parse_int("bad")
  recover ParseError.Invalid:
    echo 1
  try:
    echo to_int(0.0 / 0.0)
  recover ConversionError.NonFinite:
    echo 2
  try:
    raise FileError.Full
  recover FileError.Full:
    echo 3
  try:
    echo parse_int("bad")
  recover std.ParseError.Invalid:
    echo 4
  try:
    echo to_int(0.0 / 0.0)
  recover std.ConversionError.NonFinite:
    echo 5
  try:
    raise std.FileError.Full
  recover std.FileError.Full:
    echo 6
''')
check(standalone)
for profile, moss_flag, rust_flag in (
        ("debug", "-O0", "-Copt-level=0"),
        ("optimized", "-Oshared-memory", "-Copt-level=3")):
    rust = SCRATCH / f"standalone-{profile}.rs"
    executable = SCRATCH / f"standalone-{profile}"
    run(COMPILER, standalone, moss_flag, "-o", rust)
    run("rustc", "--edition=2021", "-Dwarnings", rust_flag, rust,
        "-o", executable)
    assert run(executable).stdout == "1\n2\n3\n4\n5\n6\n"
assert run(COMPILER, "run", "--interp", standalone).stdout == (
    "1\n2\n3\n4\n5\n6\n")


# Exact legacy declarations migrate to the canonical types, keeping old
# source valid. A malformed declaration cannot replace the built-in contract.
legacy = SCRATCH / "legacy.moss"
legacy_prefix = '''enum FileError:
  NotFound
  PermissionDenied
  NotRegularFile
  InUse
  Full
  IO
enum ParseError:
  Invalid
  Overflow
enum ConversionError:
  NonFinite
  Overflow
'''
legacy.write_text(legacy_prefix + standalone.read_text())
check(legacy)
legacy.write_text(legacy_prefix.replace("  Invalid\n", "  Invalid(code: Int)\n") +
                  standalone.read_text())
check(legacy, "BUILTIN_ERROR_TYPE_SHAPE")


# Provider-owned namesakes deliberately reuse variant names and carry payloads.
# The consumer's unqualified standard arms must never select a provider error.
project = SCRATCH / "namesake_project"
source_dir = project / "src"
source_dir.mkdir(parents=True, exist_ok=True)
(project / "Moss.toml").write_text(
    '[project]\nname = "phase21_standard_error_namesake"\nversion = "0.1.0"\n')
provider = source_dir / "provider.moss"
consumer = source_dir / "main.moss"
provider.write_text('''module provider
export enum FileError:
  Full(code: Int)
export enum ParseError:
  Invalid(code: Int)
export enum ConversionError:
  NonFinite(code: Int)
export fn file_error() -> Int:
  raise FileError.Full(code: 41)
export fn parse_error() -> Int:
  raise ParseError.Invalid(code: 42)
export fn conversion_error() -> Int:
  raise ConversionError.NonFinite(code: 43)
''')
consumer.write_text('''module app
import provider
fn main():
  try:
    echo parse_int("bad")
  recover ParseError.Invalid:
    echo 1
  try:
    echo to_int(0.0 / 0.0)
  recover ConversionError.NonFinite:
    echo 2
  try:
    file = FileIO.open("tmp/phase21-standard-error-no-such-file", ro)
    file.close()
  recover std.FileError.NotFound:
    echo 3
  try:
    echo provider.file_error()
  recover std.FileError.Full:
    echo 99
  recover provider.FileError.Full(code):
    echo code
  try:
    echo provider.parse_error()
  recover std.ParseError.Invalid:
    echo 98
  recover provider.ParseError.Invalid(code):
    echo code
  try:
    echo provider.conversion_error()
  recover std.ConversionError.NonFinite:
    echo 97
  recover provider.ConversionError.NonFinite(code):
    echo code
''')
expected = "1\n2\n3\n41\n42\n43\n"


def build_and_run():
    check(consumer)
    built = json.loads(run(MARGO, "build", "--json", cwd=project).stdout)
    executable = Path(built["artifacts"]["artifacts"]["executable"])
    assert run(executable, cwd=project).stdout == expected


build_and_run()
provider.rename(provider.with_suffix(".removed"))
build_and_run()

print("Phase 21 standard error namespace and source-free identity passed")
