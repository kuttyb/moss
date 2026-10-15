"""Phase 25 Team S S-A: numeric literal specification (P25-S-ARCH-018).

One shared literal specification (src/numeric_literal.hpp) is used by the
lexer/checker, Fast Debug literal evaluation, and native lowering. Accepted
literal forms (moss-0.1):

  decimal Int    1000, 1_000, 007
  hex Int        0x10, 0xff_ff, 0xAbC          (prefix lowercase `0x` only)
  decimal Float  2.5, 1_0.5, 1., .5, 1.e5, 1e5, 1E5, 1e-5, 1.5e+3

`_` separates digits only between two digits of the same group. Int literal
magnitudes are 0 ... 2^63-1; magnitude 2^63 is accepted only as the operand of
unary minus (Int minimum, decimal or hex). Float literals that round to
infinity are rejected; underflow rounds (possibly to 0.0). Binary/octal
prefixes, `0X`, hexadecimal fractions, and type suffixes are rejected.

Every accepted literal is compared across Fast Debug, Native Debug, and Native
Release against a Python oracle. Every rejected literal must be refused by
`moss check --json` with a stable code, and the same diagnostic must stop Fast
Debug and native lowering before execution.
"""

import json
from pathlib import Path
import re
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]
MOSS = Path(sys.argv[1] if len(sys.argv) > 1 else "./moss").resolve()
SCRATCH = ROOT / "tmp"
SCRATCH.mkdir(exist_ok=True)
COUNTS = {"accepted": 0, "rejected": 0, "programs": 0}


def run(cmd, cwd=ROOT, timeout=600):
    return subprocess.run(cmd, cwd=cwd, capture_output=True, text=True, timeout=timeout)


def native(source, td, mode):
    flag, rustc_flags = ("--debug", []) if mode == "debug" else ("-O", ["-O"])
    rust = Path(td) / f"{mode}.rs"
    binary = Path(td) / mode
    gen = run([str(MOSS), str(source), flag, "-o", str(rust)])
    assert gen.returncode == 0, f"{mode} lowering failed:\n{gen.stderr}"
    build = run(["rustc", *rustc_flags, "--crate-name", "case", str(rust), "-o", str(binary)])
    assert build.returncode == 0, f"{mode} rustc failed:\n{build.stderr[:4000]}"
    return run([str(binary)]), rust.read_text()


def engines(code, name, expected):
    """Run all three engines; require identical stdout equal to `expected`."""
    COUNTS["programs"] += 1
    with tempfile.TemporaryDirectory(prefix="p25-sa-", dir=SCRATCH) as td:
        source = Path(td) / "case.moss"
        source.write_text(code)
        check = run([str(MOSS), "check", str(source), "--json"])
        assert json.loads(check.stdout)["ok"], f"[{name}] check rejected:\n{check.stdout[:2000]}"
        outputs = {}
        interp = run([str(MOSS), "run", "--interp", str(source)])
        assert interp.returncode == 0, f"[{name}] Fast Debug failed:\n{interp.stdout}{interp.stderr}"
        outputs["fast-debug"] = interp.stdout
        rust_sources = []
        for mode in ("debug", "release"):
            result, rust = native(source, td, mode)
            assert result.returncode == 0, f"[{name}] {mode} failed:\n{result.stderr}"
            outputs[f"native-{mode}"] = result.stdout
            rust_sources.append(rust)
        assert len(set(outputs.values())) == 1, f"[{name}] engine disagreement: " + json.dumps(
            {k: v[:600] for k, v in outputs.items()}, indent=1)
        assert outputs["fast-debug"] == expected, (
            f"[{name}] expected {expected!r}, got {outputs['fast-debug']!r}")
        print(f"  PASS: {name} ({len(outputs)} engines)")
        return rust_sources


def rejected(code, name, code_expected, line=None, message=None):
    COUNTS["rejected"] += 1
    with tempfile.TemporaryDirectory(prefix="p25-sa-", dir=SCRATCH) as td:
        source = Path(td) / "case.moss"
        source.write_text(code)
        result = run([str(MOSS), "check", str(source), "--json"])
        assert result.returncode != 0, f"[{name}] accepted:\n{result.stdout}"
        error = json.loads(result.stdout).get("error") or {}
        assert error.get("code") == code_expected, (
            f"[{name}] expected {code_expected}, got {error.get('code')}: {error.get('message')}")
        if line is not None:
            assert error.get("line") == line, f"[{name}] expected line {line}, got {error.get('line')}"
        if message is not None:
            assert message in (error.get("message") or ""), (
                f"[{name}] message lacks {message!r}: {error.get('message')}")
        interp = run([str(MOSS), "run", "--interp", str(source)])
        assert interp.returncode != 0 and code_expected in interp.stderr and not interp.stdout, (
            f"[{name}] Fast Debug did not stop before execution:\n{interp.stdout}{interp.stderr}")
        gen = run([str(MOSS), str(source), "--debug", "-o", str(Path(td) / "case.rs")])
        assert gen.returncode != 0 and code_expected in gen.stderr, (
            f"[{name}] native lowering did not reject:\n{gen.stderr}")
        print(f"  PASS: {name} -> {code_expected}")


# ---------------------------------------------------------------------------
# Python oracle: Python's own literal parsers, independent of Moss.

def oracle_value(spelling):
    text = spelling.strip()
    sign = 1
    if text[0] in "+-":
        sign = -1 if text[0] == "-" else 1
        text = text[1:]
    plain = text.replace("_", "")
    if plain.startswith("0x"):
        return sign * int(plain[2:], 16)
    if any(c in plain for c in ".eE"):
        value = float(plain)
        return -value if sign < 0 else value
    return sign * int(plain, 10)


def moss_text(value):
    """Moss echo text: Int in decimal; Float as shortest round-trip repr with a
    `.0` for integral values and an exponent without `+` or leading zeros."""
    if isinstance(value, int):
        return str(value)
    text = repr(value)
    return re.sub(r"e([+-]?)0*(\d+)", lambda m: "e" + ("-" if m.group(1) == "-" else "") + m.group(2), text.replace("e+", "e"))


ACCEPTED_INT = [
    "0", "7", "007", "1_000", "1_2_3", "3_000_000_000", "9223372036854775807",
    "9_223_372_036_854_775_807", "0009223372036854775807",
    "0x0", "0x10", "0xff", "0xFF", "0xAbC", "0xff_ff", "0x1f32", "0xDEAD_BEEF",
    "0x7fffffffffffffff", "0x7FFF_FFFF_FFFF_FFFF",
    "-1_000", "-0x10", "+5", "+0x1_0", "-0",
    "-9223372036854775808", "-9_223_372_036_854_775_808", "-0x8000000000000000",
    "-0x8000_0000_0000_0000", "-0009223372036854775808",
]

ACCEPTED_FLOAT = [
    "2.5", "0.1", "1_0.5", "1_000.000_5", ".5", "1.", "1.e5", "1.E-3", "1e5", "1E5",
    "1e+5", "1e-5", "1.5e3", "1.5E-3", "1.5e+10", "1_0e1_0", "0.0", "-0.0", "-2.5",
    "-.5", "-1.", "-1e-3", "+2.5", "1e22", "1e300", "1e308", "1.7976931348623157e308",
    "1.7976931348623158e308", "5e-324", "2.5e-310", "1e-400", "9223372036854775808.5",
    "9_223_372_036_854_775_808.0", "123456789012345678901234567890.0",
    "0.30000000000000004", "4.35", "1e-7", "1e15", "1e16",
]


def literal_program(literals):
    return "fn main():\n" + "".join(f"  echo {literal}\n" for literal in literals)


def test_accepted_literals():
    print("S-A: every accepted literal has one value in all three engines")
    for label, literals in (("Int literals", ACCEPTED_INT), ("Float literals", ACCEPTED_FLOAT)):
        expected = "".join(moss_text(oracle_value(literal)) + "\n" for literal in literals)
        rust_sources = engines(literal_program(literals), label, expected)
        COUNTS["accepted"] += len(literals)
        # Native lowering emits canonical suffixed Rust literals from the
        # decoded value, never Moss spelling.
        for rust in rust_sources:
            for line in rust.splitlines():
                if line.lstrip().startswith("//") or "println!" not in line:
                    continue
                for spelling in ("0x", "1_0", "_000", "1E5", "1.e5", "1.E"):
                    assert spelling not in line, f"[{label}] Moss spelling {spelling!r} in Rust: {line}"
        assert any("16_i64" in rust for rust in rust_sources) or label != "Int literals"
        assert any("2.5_f64" in rust for rust in rust_sources) or label != "Float literals"


def test_literal_contexts():
    print("S-A: literals in expressions, unary signs, and collection/index contexts")
    code = """
fn scale(value: Int) -> Int:
  return value * 0x2

fn main():
  v = [0x10, 1_000, 7]
  echo v[0x1]
  echo v[0] + 1_0
  echo scale(1_000)
  echo 5 - 0x10
  echo 5 -0x10
  echo 1_000 * 2
  echo 0x10 + 1_0.5
  echo 3 * 1_0.5
  echo 1.0 + 1e-3
  echo 2.0 - 1.5e-3
  echo 1.0 - -1e-3
  echo 1e-3 + 1
  echo 1.5e-3 - 2
  echo - 1_000
  echo -(0x10)
  echo 1 + -9_223_372_036_854_775_808
  echo -0x8000000000000000 + 1
  echo (-0x8000000000000000)
  echo 9_223_372_036_854_775_807 + 1
  echo 0x10 == 16
  echo .5 < 1.
  echo 1. / -0
  echo 2.5 * -0
  x = 1_000_000
  var i = 0
  while i < 0x3:
    i = i + 1
  echo i
  echo x
  s = "0x10 1__0 3i64 1e400"
  echo s
  echo 1 # 0b101 1__0 trailing comment
"""
    expected = "\n".join([
        "1000", "26", "2000", "-11", "-11", "2000", "26.5", "31.5", "1.001", "1.9985", "1.001",
        "1.001", "-1.9985", "-1000", "-16", "-9223372036854775807", "-9223372036854775807",
        "-9223372036854775808", "-9223372036854775808", "true", "true", "inf", "0.0", "3",
        "1000000", "0x10 1__0 3i64 1e400", "1",
    ]) + "\n"
    engines(code, "literal contexts", expected)


REJECTED = [
    # (literal, code, message fragment)
    ("1__0", "INVALID_DIGIT_SEPARATOR", "'1__0'"),
    ("1_", "INVALID_DIGIT_SEPARATOR", "between two digits"),
    ("1_.5", "INVALID_DIGIT_SEPARATOR", "'1_.5'"),
    ("1._5", "INVALID_DIGIT_SEPARATOR", "'1._5'"),
    ("0x_1", "INVALID_DIGIT_SEPARATOR", "'0x_1'"),
    ("0x1_", "INVALID_DIGIT_SEPARATOR", "'0x1_'"),
    ("0xff__ff", "INVALID_DIGIT_SEPARATOR", "'0xff__ff'"),
    ("1e_5", "INVALID_NUMERIC_LITERAL", "exponent marker without exponent digits"),
    ("1e5_", "INVALID_DIGIT_SEPARATOR", "'1e5_'"),
    ("1_e5", "INVALID_DIGIT_SEPARATOR", "'1_e5'"),
    ("1.5_", "INVALID_DIGIT_SEPARATOR", "'1.5_'"),
    ("1.0_e5", "INVALID_DIGIT_SEPARATOR", "'1.0_e5'"),
    ("1_000__000", "INVALID_DIGIT_SEPARATOR", "'1_000__000'"),
    ("0x", "INVALID_NUMERIC_LITERAL", "no hexadecimal digits"),
    ("0x_", "INVALID_NUMERIC_LITERAL", "no hexadecimal digits"),
    ("0xG", "INVALID_NUMERIC_LITERAL", "'G', which is not a hexadecimal digit"),
    ("0x1G", "INVALID_NUMERIC_LITERAL", "'G', which is not a hexadecimal digit"),
    ("0X10", "INVALID_NUMERIC_LITERAL", "lowercase '0x'"),
    ("0XFF", "INVALID_NUMERIC_LITERAL", "lowercase '0x'"),
    ("1e", "INVALID_NUMERIC_LITERAL", "exponent marker without exponent digits"),
    ("1E", "INVALID_NUMERIC_LITERAL", "exponent marker without exponent digits"),
    ("1e+", "INVALID_NUMERIC_LITERAL", "'1e+'"),
    ("1e-", "INVALID_NUMERIC_LITERAL", "'1e-'"),
    ("1.5e", "INVALID_NUMERIC_LITERAL", "'1.5e'"),
    (".5e", "INVALID_NUMERIC_LITERAL", "'.5e'"),
    ("2abc", "INVALID_NUMERIC_LITERAL", "cannot be joined to a name"),
    ("1x10", "INVALID_NUMERIC_LITERAL", "cannot be joined to a name"),
    ("3i64", "UNSUPPORTED_NUMERIC_LITERAL", "type suffix"),
    ("3_i64", "UNSUPPORTED_NUMERIC_LITERAL", "type suffix"),
    ("1u8", "UNSUPPORTED_NUMERIC_LITERAL", "type suffix"),
    ("7usize", "UNSUPPORTED_NUMERIC_LITERAL", "type suffix"),
    ("1.5f32", "UNSUPPORTED_NUMERIC_LITERAL", "type suffix"),
    ("2f64", "UNSUPPORTED_NUMERIC_LITERAL", "type suffix"),
    ("1e5f64", "UNSUPPORTED_NUMERIC_LITERAL", "type suffix"),
    ("0xffi64", "UNSUPPORTED_NUMERIC_LITERAL", "type suffix"),
    ("0x10u8", "UNSUPPORTED_NUMERIC_LITERAL", "type suffix"),
    ("0b101", "UNSUPPORTED_NUMERIC_LITERAL", "binary prefix"),
    ("0B1", "UNSUPPORTED_NUMERIC_LITERAL", "binary prefix"),
    ("0o7", "UNSUPPORTED_NUMERIC_LITERAL", "octal prefix"),
    ("0O7", "UNSUPPORTED_NUMERIC_LITERAL", "octal prefix"),
    ("0x1.5", "UNSUPPORTED_NUMERIC_LITERAL", "hexadecimal fraction"),
    ("9223372036854775808", "INT_LITERAL_OUT_OF_RANGE", "does not fit in Int"),
    ("9_223_372_036_854_775_808", "INT_LITERAL_OUT_OF_RANGE", "does not fit in Int"),
    ("0x8000000000000000", "INT_LITERAL_OUT_OF_RANGE", "'0x8000000000000000'"),
    ("0x8000_0000_0000_0000", "INT_LITERAL_OUT_OF_RANGE", "does not fit in Int"),
    ("0xFFFFFFFFFFFFFFFFFFFF", "INT_LITERAL_OUT_OF_RANGE", "does not fit in Int"),
    ("99999999999999999999", "INT_LITERAL_OUT_OF_RANGE", "does not fit in Int"),
    ("-9223372036854775809", "INT_LITERAL_OUT_OF_RANGE", "does not fit in Int"),
    ("-0x8000000000000001", "INT_LITERAL_OUT_OF_RANGE", "does not fit in Int"),
    ("5 - 9223372036854775808", "INT_LITERAL_OUT_OF_RANGE", "does not fit in Int"),
    ("5 - 0x8000000000000000", "INT_LITERAL_OUT_OF_RANGE", "does not fit in Int"),
    ("[0x8000000000000000]", "INT_LITERAL_OUT_OF_RANGE", "does not fit in Int"),
    ("1e400", "FLOAT_LITERAL_OUT_OF_RANGE", "too large for Float"),
    ("1.5e400", "FLOAT_LITERAL_OUT_OF_RANGE", "'1.5e400'"),
    ("-1e400", "FLOAT_LITERAL_OUT_OF_RANGE", "too large for Float"),
    ("1e309", "FLOAT_LITERAL_OUT_OF_RANGE", "too large for Float"),
    ("1_000e306", "FLOAT_LITERAL_OUT_OF_RANGE", "too large for Float"),
    ("1.7976931348623159e308", "FLOAT_LITERAL_OUT_OF_RANGE", "too large for Float"),
    ("2.5 * 1e400", "FLOAT_LITERAL_OUT_OF_RANGE", "too large for Float"),
]


def test_rejected_literals():
    print("S-A: invalid and unsupported literals are rejected before execution")
    for literal, code, message in REJECTED:
        rejected(f"fn main():\n  x = 1\n  echo {literal}\n", f"literal {literal}", code, 3, message)
    # Rejection is independent of position: function bodies, state, vectors.
    rejected("fn f() -> Int:\n  return 0b1\n\nfn main():\n  echo f()\n",
             "binary literal in a return", "UNSUPPORTED_NUMERIC_LITERAL", 2)
    rejected("fn main():\n  v = [1, 2, 1__0]\n  echo v[0]\n",
             "separator error in a vector literal", "INVALID_DIGIT_SEPARATOR", 2)
    rejected("domain D:\n  n = 0X1\n\n  on Get() -> Int:\n    reply n\n\nfn main():\n"
             "  d = D()\n  echo message d.Get()\n",
             "uppercase prefix in domain state", "INVALID_NUMERIC_LITERAL", 2)
    # `_1` is an identifier spelling, not a numeric literal.
    rejected("fn main():\n  echo _1\n", "_1 is a name", "UNKNOWN_SYMBOL_OR_TYPE", 2)


def main():
    test_accepted_literals()
    test_literal_contexts()
    test_rejected_literals()
    print(f"phase25 S-A numeric literal checks passed: {COUNTS['accepted']} accepted literals "
          f"(3 engines), {COUNTS['rejected']} rejected cases, {COUNTS['programs']} engine programs")


if __name__ == "__main__":
    main()
