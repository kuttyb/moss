#!/usr/bin/env python3
"""Typed numeric parsing and conversion parity across both execution engines."""

import json
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
COMPILER = Path(sys.argv[1]).resolve() if len(sys.argv) > 1 else ROOT / "moss"
SCRATCH = ROOT / "tmp" / "phase21_numeric_parse"
SCRATCH.mkdir(parents=True, exist_ok=True)
SOURCE = SCRATCH / "numeric.moss"
RUST = SCRATCH / "numeric.rs"
NATIVE = SCRATCH / "numeric"

header = """enum ParseError:
  Invalid
  Overflow

enum ConversionError:
  NonFinite
  Overflow

fn read_int(text: String) -> Int:
  var result = 0
  try:
    result = parse_int(text)
  recover ParseError.Invalid:
    result = -17
  recover ParseError.Overflow:
    result = -18
  return result

fn read_float(text: String) -> Float:
  var result = 0.0
  try:
    result = parse_float(text)
  recover ParseError.Invalid:
    result = -17.0
  return result

fn checked_int(value: Float) -> Int:
  var result = 0
  try:
    result = to_int(value)
  recover ConversionError.NonFinite:
    result = -17
  recover ConversionError.Overflow:
    result = -18
  return result

fn main():
"""
integer_cases = [
    ("0", "0"), (" \t+1_234\r ", "1234"), ("-0", "0"),
    ("9223372036854775807", "9223372036854775807"),
    ("-9223372036854775808", "-9223372036854775808"),
    ("9223372036854775808", "-18"),
    ("-9223372036854775809", "-18"),
    ("", "-17"), ("1__2", "-17"), ("1_", "-17"),
    ("_1", "-17"), ("1 2", "-17"), ("١", "-17"),
    ("\u00a01", "-17"), ("0x10", "-17"),
]
float_cases = [
    (".5", "0.5"), ("1.", "1.0"), ("1E3", "1000.0"),
    ("00.5", "0.5"), ("1e+3", "1000.0"),
    ("1e-3", "0.001"), ("-0", "-0.0"), ("+0", "0.0"),
    ("1_2.3_4E+2", "1234.0"),
    ("1e309", "inf"), ("1e-400", "0.0"),
    ("5e-324", "5e-324"),
    ("inf", "inf"), ("-inf", "-inf"), ("NaN", "NaN"),
    ("+inf", "-17.0"), ("nan", "-17.0"),
    ("INF", "-17.0"), ("-nan", "-17.0"),
    ("Infinity", "-17.0"), ("1e", "-17.0"),
    ("1._0", "-17.0"), ("1_.0", "-17.0"),
    ("1e+_3", "-17.0"), ("١.0", "-17.0"),
    ("\u00a01.0", "-17.0"), ("1 2.0", "-17.0"),
]
conversion_cases = [
    ("1.9", "1"), ("-1.9", "-1"),
    ("-9223372036854775808.0", "-9223372036854775808"),
    ("9223372036854775808.0", "-18"),
    ("1.0 / 0.0", "-17"), ("0.0 / 0.0", "-17"),
]

lines = [header]
expected = []
for value, output in integer_cases:
    lines.append(f"  echo read_int({json.dumps(value, ensure_ascii=False)})\n")
    expected.append(output)
for value, output in float_cases:
    lines.append(f"  echo read_float({json.dumps(value, ensure_ascii=False)})\n")
    expected.append(output)
for value, output in conversion_cases:
    lines.append(f"  echo checked_int({value})\n")
    expected.append(output)
SOURCE.write_text("".join(lines))

subprocess.run([str(COMPILER), str(SOURCE), "-o", str(RUST)], check=True)
subprocess.run(["rustc", "-D", "warnings", str(RUST), "-o", str(NATIVE)],
               check=True)
native = subprocess.run([str(NATIVE)], check=True, capture_output=True,
                        text=True).stdout.splitlines()
debug = subprocess.run([str(COMPILER), "run", "--interp", str(SOURCE)],
                       check=True, capture_output=True, text=True).stdout.splitlines()
if native != expected or debug != expected:
    raise SystemExit(f"numeric parity mismatch:\nexpected={expected}\nnative={native}\ndebug={debug}")

for expression, code in (("parse_int(1)", "TYPE_MISMATCH"),
                         ("parse_float()", "INVALID_ARITY"),
                         ("to_int(1)", "TYPE_MISMATCH")):
    return_type = "Float" if expression.startswith("parse_float") else "Int"
    SOURCE.write_text(header.split("fn read_int")[0] +
                      f"fn value() -> {return_type}:\n  return {expression}\n")
    checked = subprocess.run([str(COMPILER), "check", str(SOURCE), "--json"],
                             capture_output=True, text=True)
    diagnostic = json.loads(checked.stdout)
    if checked.returncode == 0 or diagnostic.get("error", {}).get("code") != code:
        raise SystemExit(f"{expression} should report {code}: {diagnostic}")

SOURCE.write_text(header.split("fn read_int")[0] + """
fn raw_int(text: String) -> Int:
  return parse_int(text)
fn raw_float(text: String) -> Float:
  return parse_float(text)
fn raw_conversion(value: Float) -> Int:
  return to_int(value)
""")
for name, variants in (
    ("raw_int", ["enum:ParseError:Invalid", "enum:ParseError:Overflow"]),
    ("raw_float", ["enum:ParseError:Invalid"]),
    ("raw_conversion", ["enum:ConversionError:NonFinite", "enum:ConversionError:Overflow"]),
):
    queried = subprocess.run([str(COMPILER), "effects", name, "--source",
                              str(SOURCE), "--json"], check=True,
                             capture_output=True, text=True)
    effects = json.loads(queried.stdout)["result"]["observable_effects"]
    actual = effects["raise_set"]
    if effects["may_panic"] or len(actual) != len(variants) or not all(
        any(variant in entry for entry in actual) for variant in variants
    ):
        raise SystemExit(f"{name} effect mismatch: {effects}")

bootstrap = subprocess.run([str(COMPILER), "agent", "bootstrap", "--json"],
                           check=True, capture_output=True, text=True)
numeric = json.loads(bootstrap.stdout)["result"]["source_surface"]["numeric"]
if len(numeric["parsing"]) != 2 or len(numeric["conversion"]) != 1:
    raise SystemExit(f"numeric discovery missing operations: {numeric}")

package = SCRATCH / "package"
(package / "src").mkdir(parents=True, exist_ok=True)
(package / "src/service.moss").unlink(missing_ok=True)
(package / "Moss.toml").write_text('''[project]
name = "numeric-package"
version = "0.1.0"

[build]
source = "src"
''')
(package / "src/main.moss").write_text('''module app

enum ParseError:
  Invalid
  Overflow

enum ConversionError:
  NonFinite
  Overflow

fn main():
  var parsed = 0
  try:
    parsed = parse_int("12")
  recover ParseError.Invalid:
    parsed = -1
  recover ParseError.Overflow:
    parsed = -2
  echo parsed
  var converted = 0
  try:
    converted = to_int(2.5)
  recover ConversionError.NonFinite:
    converted = -1
  recover ConversionError.Overflow:
    converted = -2
  echo converted
''')
MARGO = COMPILER.parent / "margo"
for command in ("run", "debug"):
    result = subprocess.run([str(MARGO), command], cwd=package,
                            capture_output=True, text=True)
    if result.returncode or result.stdout != "12\n2\n":
        raise SystemExit(f"package {command} mismatch: "
                         f"{result.returncode=} {result.stdout!r} "
                         f"{result.stderr!r}")

cross_package = SCRATCH / "cross_package"
(cross_package / "src").mkdir(parents=True, exist_ok=True)
(cross_package / "Moss.toml").write_text((package / "Moss.toml").read_text())
(cross_package / "src/service.moss").write_text('''module service

export enum ParseError:
  Invalid
  Overflow

export fn read(text: String) -> Int:
  return parse_int(text)
''')
(cross_package / "src/main.moss").write_text('''module app
import service

fn main():
  var value = 0
  try:
    value = service.read("34")
  recover service.ParseError.Invalid:
    value = -1
  recover service.ParseError.Overflow:
    value = -2
  echo value
  try:
    value = service.read("bad")
  recover service.ParseError.Invalid:
    value = -1
  recover service.ParseError.Overflow:
    value = -2
  echo value
''')
for command in ("run", "debug"):
    result = subprocess.run([str(MARGO), command], cwd=cross_package,
                            capture_output=True, text=True)
    if result.returncode or result.stdout != "34\n-1\n":
        raise SystemExit(f"cross-module {command} mismatch: "
                         f"{result.returncode=} {result.stdout!r} "
                         f"{result.stderr!r}")

print("Phase 21 numeric parsing and conversion parity passed")
