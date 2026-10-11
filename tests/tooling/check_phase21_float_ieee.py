#!/usr/bin/env python3
"""Focused Phase 21 Float division and canonical text parity check."""

import json
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
NATIVE_OPT = SCRATCH / "float_ieee_optimized"

# The final three finite cases are exact binary64 halfway values. Their
# shortest decimal candidates differ in the last digit, so ties choose even.
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
  echo 2.9802322387695312e-8
  echo 4.940656e-318
  echo 9060801153433600.0
  echo 9007199254740991.0
  echo 9007199254740992.0
  echo -2.109808898695963e16
  echo 86621683325507.375
  echo 821236155936890.25
  echo 1285892674357227.75
  echo is_nan(0.0 / 0.0)
  echo is_nan(1.0)
  echo is_finite(1.0)
  echo is_finite(1.0 / 0.0)
  echo is_finite(-0.0)
""")

subprocess.run([str(COMPILER), str(SOURCE), "-o", str(RUST)], check=True)
subprocess.run(["rustc", "-D", "warnings", "--check-cfg", "cfg(moss_perf)",
                "--check-cfg", "cfg(test)", str(RUST), "-o", str(NATIVE)], check=True)
native = subprocess.run([str(NATIVE)], check=True, capture_output=True, text=True).stdout
subprocess.run(["rustc", "-C", "opt-level=3", "-D", "warnings",
                "--check-cfg", "cfg(moss_perf)", "--check-cfg", "cfg(test)",
                str(RUST), "-o", str(NATIVE_OPT)], check=True)
native_optimized = subprocess.run(
    [str(NATIVE_OPT)], check=True, capture_output=True, text=True).stdout
debug = subprocess.run([str(COMPILER), "run", "--interp", str(SOURCE)],
                       check=True, capture_output=True, text=True).stdout
expected = ("inf\n-inf\nNaN\n-0.0\n1.0\n0.30000000000000004\n"
            "0.0001\n1e-7\n1e16\n1e300\n1000.0\n1000.0\n"
            "1e-5\n5e-324\n2.2250738585072014e-308\n"
            "1.7976931348623157e308\n2.9802322387695312e-8\n"
            "4.940656e-318\n9060801153433600.0\n"
            "9007199254740991.0\n9007199254740992.0\n"
            "-2.109808898695963e16\n86621683325507.38\n"
            "821236155936890.2\n1285892674357227.8\n"
            "true\nfalse\ntrue\nfalse\ntrue\n")
if native != expected:
    raise SystemExit(f"native output mismatch: {native!r} != {expected!r}")
if native_optimized != expected:
    raise SystemExit(
        f"optimized native output mismatch: {native_optimized!r} != {expected!r}")
if debug != expected:
    raise SystemExit(f"Fast Debug output mismatch: {debug!r} != {expected!r}")

for expression, code in (("is_nan(1)", "TYPE_MISMATCH"),
                         ("is_finite()", "INVALID_ARITY")):
    SOURCE.write_text(f"fn main():\n  echo {expression}\n")
    checked = subprocess.run([str(COMPILER), "check", str(SOURCE), "--json"],
                             capture_output=True, text=True)
    diagnostic = json.loads(checked.stdout)
    if checked.returncode == 0 or diagnostic.get("error", {}).get("code") != code:
        raise SystemExit(f"{expression} should report {code}: {diagnostic}")

pure_source = SCRATCH / "float_predicate_effects.moss"
pure_source.write_text("""fn check_nan(x: Float) -> Bool:
  return is_nan(x)
fn check_finite(x: Float) -> Bool:
  return is_finite(x)
""")
for function in ("check_nan", "check_finite"):
    queried = subprocess.run([str(COMPILER), "effects", function, "--source",
                              str(pure_source), "--json"], check=True,
                             capture_output=True, text=True)
    effects = json.loads(queried.stdout)["result"]["observable_effects"]
    if effects["unresolved"] or effects["may_panic"] or effects["raise_set"]:
        raise SystemExit(f"{function} must be pure: {effects}")
bootstrap = subprocess.run([str(COMPILER), "agent", "bootstrap", "--json"],
                           check=True, capture_output=True, text=True)
predicates = json.loads(bootstrap.stdout)["result"]["source_surface"]["numeric"]["float_predicates"]
if predicates != ["is_nan(Float) -> Bool", "is_finite(Float) -> Bool"]:
    raise SystemExit(f"numeric discovery omitted Float predicates: {predicates}")

generic_source = SCRATCH / "float_predicate_generic.moss"
generic_rust = SCRATCH / "float_predicate_generic.rs"
generic_native = SCRATCH / "float_predicate_generic"
generic_source.write_text("""module app
fn generic_nan(x) -> Bool:
  return is_nan(x)
fn main():
  echo generic_nan(0.0)
""")
subprocess.run([str(COMPILER), str(generic_source), "-o", str(generic_rust)], check=True)
subprocess.run(["rustc", "-D", "warnings", "--check-cfg", "cfg(moss_perf)",
                "--check-cfg", "cfg(test)", str(generic_rust), "-o",
                str(generic_native)], check=True)
for command in ((str(generic_native),),
                (str(COMPILER), "run", "--interp", str(generic_source))):
    result = subprocess.run(command, check=True, capture_output=True, text=True)
    if result.stdout != "false\n":
        raise SystemExit(f"generic Float predicate mismatch: {result.stdout!r}")
print("Phase 21 Float IEEE and canonical text parity passed")
