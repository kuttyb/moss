#!/usr/bin/env python3
"""Focused Phase 21 Float division and canonical text parity check."""

import json
import hashlib
import math
import struct
import subprocess
import sys
from decimal import Decimal
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

# These original-like external peer-review cases pin expression typing and
# formatting, which the literal bit-pattern corpus alone cannot exercise.
PEER_FLOAT = ROOT / "tests" / "tooling" / "fixtures" / "phase21_peer_float_reconstructed"
PEER_CASES = (
    ("computed_float_output", "-2.0\n-1.0\n-2.0\n-2.0\n-1.0\n-0.0\n-1e16\n"),
    ("sqrt_float_locals", "4.0 4.0 1.4142135623730951\n"),
    ("sqrt_float_forms", "4.0 3.0\n"),
)
peer_material = b"".join(
    (name + "\n").encode() + (PEER_FLOAT / f"{name}.moss").read_bytes() +
    b"\0" + peer_expected.encode() + b"\0"
    for name, peer_expected in PEER_CASES
)
peer_digest = hashlib.sha256(peer_material).hexdigest()
if peer_digest != "4a14b7bee3f4f391105be90ddda1e4c01eac629ac0f6184fc0e703c8c5e23c00":
    raise SystemExit(f"pinned Float expression corpus changed: {peer_digest}")
for name, peer_expected in PEER_CASES:
    peer_source = PEER_FLOAT / f"{name}.moss"
    checked = subprocess.run([str(COMPILER), "check", str(peer_source), "--json"],
                             check=True, capture_output=True, text=True)
    if not json.loads(checked.stdout).get("ok"):
        raise SystemExit(f"{name} did not pass moss check: {checked.stdout}")
    peer_rust = SCRATCH / f"{name}.rs"
    subprocess.run([str(COMPILER), str(peer_source), "-o", str(peer_rust)], check=True)
    for optimization in ("O0", "O3"):
        peer_native = SCRATCH / f"{name}_{optimization}"
        subprocess.run(["rustc", "-C", f"opt-level={optimization[-1]}",
                        "-D", "warnings", "--check-cfg", "cfg(moss_perf)",
                        "--check-cfg", "cfg(test)", str(peer_rust), "-o",
                        str(peer_native)], check=True)
        peer_output = subprocess.run([str(peer_native)], check=True,
                                     capture_output=True, text=True).stdout
        if peer_output != peer_expected:
            raise SystemExit(f"{name} native {optimization} mismatch: {peer_output!r}")
    peer_debug = subprocess.run([str(COMPILER), "run", "--interp", str(peer_source)],
                                check=True, capture_output=True, text=True).stdout
    if peer_debug != peer_expected:
        raise SystemExit(f"{name} Fast Debug mismatch: {peer_debug!r}")
    if name == "sqrt_float_locals":
        queried = subprocess.run([str(COMPILER), "effects", "sqrt_typed", "--source",
                                  str(peer_source), "--json"], check=True,
                                 capture_output=True, text=True)
        effects = json.loads(queried.stdout)["result"]["observable_effects"]
        if effects["unresolved"] or effects["may_panic"] or effects["raise_set"]:
            raise SystemExit(f"sqrt_typed must be effect-free: {effects}")

# Python's shortest-round-trip conversion is independent of both the pinned
# native Ryū core and Fast Debug's C++ to_chars implementation. Freeze the
# resulting corpus digest as well, so a change in the Python oracle cannot
# silently redefine the expected Moss output on a future test host.
def reference_float_text(value):
    if value == 0.0:
        return "-0.0" if math.copysign(1.0, value) < 0 else "0.0"
    negative = value < 0
    decimal = Decimal(repr(abs(value)))
    digits = "".join(str(digit) for digit in decimal.as_tuple().digits)
    exponent = decimal.as_tuple().exponent + len(digits) - 1
    digits = digits.rstrip("0")
    if -4 <= exponent < 16:
        decimal_at = exponent + 1
        if decimal_at <= 0:
            rendered = "0." + "0" * -decimal_at + digits
        elif decimal_at >= len(digits):
            rendered = digits + "0" * (decimal_at - len(digits)) + ".0"
        else:
            rendered = digits[:decimal_at] + "." + digits[decimal_at:]
    else:
        rendered = digits[0]
        if len(digits) > 1:
            rendered += "." + digits[1:]
        rendered += "e" + str(exponent)
    return "-" + rendered if negative else rendered


bit_patterns = [0, 1 << 63, 1, (1 << 63) | 1,
                0x000fffffffffffff, 0x0010000000000000,
                0x7fefffffffffffff, 0xffefffffffffffff]
for boundary in (1e-4, 1e16):
    for value in (math.nextafter(boundary, 0.0), boundary,
                  math.nextafter(boundary, math.inf)):
        bit_patterns.append(struct.unpack(">Q", struct.pack(">d", value))[0])
state = 0x21d5a17e5eedcafe
while len(bit_patterns) < 270:
    state = (state * 6364136223846793005 + 1442695040888963407) & ((1 << 64) - 1)
    value = struct.unpack(">d", state.to_bytes(8, "big"))[0]
    if math.isfinite(value):
        bit_patterns.append(state)
values = [struct.unpack(">d", bits.to_bytes(8, "big"))[0]
          for bits in bit_patterns]
expected_corpus = [reference_float_text(value) for value in values]
for bits, rendered in zip(bit_patterns, expected_corpus):
    parsed_bits = struct.unpack(">Q", struct.pack(">d", float(rendered)))[0]
    if parsed_bits != bits:
        raise SystemExit(f"reference Float text did not round-trip 0x{bits:016x}")
corpus_hash = hashlib.sha256(("\n".join(expected_corpus) + "\n").encode()).hexdigest()
if corpus_hash != "91553f7482c35cfef6c108c429c7bdb54fba4cc8d4959c4d0a7bd3faf1ca28ef":
    raise SystemExit(f"independent Float corpus changed: {corpus_hash}")
corpus_source = SCRATCH / "float_corpus.moss"
corpus_rust = SCRATCH / "float_corpus.rs"
corpus_native = SCRATCH / "float_corpus"
corpus_native_opt = SCRATCH / "float_corpus_optimized"
corpus_source.write_text("fn main():\n" + "".join(
    f"  echo {repr(value)}\n" for value in values))
subprocess.run([str(COMPILER), str(corpus_source), "-o", str(corpus_rust)], check=True)
subprocess.run(["rustc", "-D", "warnings", str(corpus_rust), "-o",
                str(corpus_native)], check=True)
subprocess.run(["rustc", "-C", "opt-level=3", "-D", "warnings",
                str(corpus_rust), "-o", str(corpus_native_opt)], check=True)
for engine, command in (("native", [str(corpus_native)]),
                        ("optimized native", [str(corpus_native_opt)]),
                        ("Fast Debug", [str(COMPILER), "run", "--interp",
                                        str(corpus_source)])):
    actual = subprocess.run(command, check=True, capture_output=True,
                            text=True).stdout.splitlines()
    if len(actual) != len(expected_corpus):
        raise SystemExit(f"{engine} Float corpus printed {len(actual)} values, "
                         f"expected {len(expected_corpus)}")
    if actual != expected_corpus:
        first = next(index for index, pair in enumerate(zip(actual, expected_corpus))
                     if pair[0] != pair[1])
        raise SystemExit(
            f"{engine} Float corpus mismatch at bits 0x{bit_patterns[first]:016x}: "
            f"{actual[first]!r} != {expected_corpus[first]!r}")

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

division_source = SCRATCH / "float_division_effects.moss"
division_source.write_text("""fn divide(x: Float, y: Float) -> Float:
  return x / y
fn nested(x: Float, y: Float) -> Float:
  return (x / y) + 1.0
fn int_divide(x: Int, y: Int) -> Int:
  return x / y
""")
for function, expected_panic in (("divide", False), ("nested", False),
                                 ("int_divide", True)):
    queried = subprocess.run([str(COMPILER), "effects", function, "--source",
                              str(division_source), "--json"], check=True,
                             capture_output=True, text=True)
    effects = json.loads(queried.stdout)["result"]["observable_effects"]
    if (effects["may_panic"] != expected_panic or effects["raise_set"] or
            effects["unresolved"]):
        raise SystemExit(f"{function} division effects mismatch: {effects}")
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
# Direct `moss -o` Rust is a standalone executable even when the source
# declares an explicit module. Project-only cfg selectors must not hide main
# or trip plain rustc's -D warnings check below.
if "moss_process_root" in generic_rust.read_text():
    raise SystemExit("standalone explicit-module Rust used project-only process-root cfg")
subprocess.run(["rustc", "-D", "warnings", "--check-cfg", "cfg(moss_perf)",
                "--check-cfg", "cfg(test)", str(generic_rust), "-o",
                str(generic_native)], check=True)
for command in ((str(generic_native),),
                (str(COMPILER), "run", "--interp", str(generic_source))):
    result = subprocess.run(command, check=True, capture_output=True, text=True)
    if result.stdout != "false\n":
        raise SystemExit(f"generic Float predicate mismatch: {result.stdout!r}")
print("Phase 21 Float IEEE and canonical text parity passed")
