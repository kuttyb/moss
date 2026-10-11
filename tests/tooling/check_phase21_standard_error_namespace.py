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
  recover ParseError.Invalid, ParseError.Overflow:
    echo 1
  try:
    echo to_int(0.0 / 0.0)
  recover ConversionError.NonFinite, ConversionError.Overflow:
    echo 2
  try:
    raise FileError.Full
  recover FileError.Full:
    echo 3
  try:
    echo parse_int("bad")
  recover std.ParseError.Invalid, std.ParseError.Overflow:
    echo 4
  try:
    echo to_int(0.0 / 0.0)
  recover std.ConversionError.NonFinite, std.ConversionError.Overflow:
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

# A Root trailer selects the canonical variant under native and Fast Debug.
root_source = SCRATCH / "standard_root.moss"
root_source.write_text('''domain StandardRoot:
  fn Run() -> Int:
    raise std.ParseError.Invalid
  on_fail std.ParseError.Invalid:
    reply 9
fn main():
  root = StandardRoot()
  echo message root.Run()
''')
check(root_source)
root_rust = SCRATCH / "standard_root.rs"
root_executable = SCRATCH / "standard_root"
run(COMPILER, root_source, "-O0", "-o", root_rust)
run("rustc", "--edition=2021", "-Dwarnings", root_rust,
    "-o", root_executable)
assert run(root_executable).stdout == "9\n"
assert run(COMPILER, "run", "--interp", root_source).stdout == "9\n"


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
export fn standard_raise() -> Int:
  raise std.ParseError.Invalid
export fn standard_value() -> std.ParseError:
  return std.ParseError.Overflow
export fn standard_code(error: std.ParseError) -> Int:
  match error:
    case Invalid:
      return 0
    case Overflow:
      return 10
export domain StandardRoot:
  fn Run() -> Int:
    raise std.ConversionError.NonFinite
  on_fail std.ConversionError.NonFinite:
    reply 8
''')
consumer.write_text('''module app
import provider
import std
fn main():
  standard_root = provider.StandardRoot()
  try:
    echo parse_int("bad")
  recover ParseError.Invalid, ParseError.Overflow:
    echo 1
  try:
    echo to_int(0.0 / 0.0)
  recover ConversionError.NonFinite, ConversionError.Overflow:
    echo 2
  try:
    file = FileIO.open("tmp/phase21-standard-error-no-such-file", ro)
    file.close()
  recover std.FileError.NotFound:
    echo 3
  recover:
    echo 99
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
  try:
    echo provider.standard_raise()
  recover std.ParseError.Invalid:
    echo 7
  echo provider.standard_code(provider.standard_value())
  echo message standard_root.Run()
''')
expected = "1\n2\n3\n41\n42\n43\n7\n10\n8\n"


def build_and_run():
    check(consumer)
    built = json.loads(run(MARGO, "build", "--json", cwd=project).stdout)
    executable = Path(built["artifacts"]["artifacts"]["executable"])
    assert run(executable, cwd=project).stdout == expected


build_and_run()
provider.rename(provider.with_suffix(".removed"))
build_and_run()

# Standard enum values cross the native method and message contracts too.
# A source-free consumer has its own Rust enum declaration; both boundaries
# must convert the payload before invoking the provider's compiled symbols.
transport = SCRATCH / "transport_project"
transport_source = transport / "src"
transport_source.mkdir(parents=True, exist_ok=True)
(transport / "Moss.toml").write_text(
    '[project]\nname = "phase21_standard_error_transport"\nversion = "0.1.0"\n')
transport_provider = transport_source / "provider.moss"
transport_consumer = transport_source / "main.moss"
transport_provider.write_text('''module provider
export type Carrier:
  fn Echo(error: std.ParseError) -> std.ParseError:
    return error
  fn Code(error: std.ParseError) -> Int:
    match error:
      case Invalid:
        return 5
      case Overflow:
        return 6
  fn Reset(error: std.ParseError) -> Int:
    error = std.ParseError.Overflow
    return 7
export domain Worker:
  fn Echo(error: std.ParseError) -> std.ParseError:
    reply error
''')
transport_consumer.write_text('''module app
import provider
fn main():
  worker = provider.Worker()
  carrier = provider.Carrier()
  value = carrier.Echo(std.ParseError.Invalid)
  match value:
    case Invalid:
      echo 1
    case Overflow:
      echo 2
  echo carrier.Code(std.ParseError.Invalid)
  result = message worker.Echo(std.ParseError.Overflow)
  match result:
    case Invalid:
      echo 3
    case Overflow:
      echo 4
''')


def build_transport():
    check(transport_consumer)
    built = json.loads(run(MARGO, "build", "--json", cwd=transport).stdout)
    executable = Path(built["artifacts"]["artifacts"]["executable"])
    assert run(executable, cwd=transport).stdout == "1\n5\n4\n"


build_transport()
# READ and CONSUME arguments bridge through the provider's Rust enum. WRITE
# requires a mutable owner across two crate-local enum types, so it is rejected
# at Moss checking while the identical local operation remains supported.
positive_transport = transport_consumer.read_text()
transport_consumer.write_text('''module app
import provider
fn main():
  carrier = provider.Carrier()
  error = std.ParseError.Invalid
  echo carrier.Reset(error)
''')
check(transport_consumer, "STANDARD_ERROR_CROSS_CRATE_WRITE_UNSUPPORTED")
transport_consumer.write_text(positive_transport)

local_write = SCRATCH / "local_standard_write.moss"
local_write.write_text('''type Carrier:
  fn Reset(error: std.ParseError) -> Int:
    error = std.ParseError.Overflow
    return 7
fn main():
  carrier = Carrier()
  error = std.ParseError.Invalid
  echo carrier.Reset(error)
  match error:
    case Invalid:
      echo 0
    case Overflow:
      echo 1
''')
check(local_write)
local_write_rust = SCRATCH / "local_standard_write.rs"
local_write_executable = SCRATCH / "local_standard_write"
run(COMPILER, local_write, "-O0", "-o", local_write_rust)
run("rustc", "--edition=2021", "-Dwarnings", local_write_rust,
    "-o", local_write_executable)
assert run(local_write_executable).stdout == "7\n1\n"

transport_provider.rename(transport_provider.with_suffix(".removed"))
build_transport()
transport_consumer.write_text('''module app
import provider
fn main():
  carrier = provider.Carrier()
  error = std.ParseError.Invalid
  echo carrier.Reset(error)
''')
check(transport_consumer, "STANDARD_ERROR_CROSS_CRATE_WRITE_UNSUPPORTED")
transport_consumer.write_text(positive_transport)

print("Phase 21 standard error namespace and source-free identity passed")
