"""Phase 25 Team S S1.1/S2: equality operands and the final Char/String contract.

L7 decision for this cycle: Char is a distinct scalar. There is no transparent
Char == String equality and no mixed isequal/assertEqual; a String is obtained
with an explicit c.to_string(). Records have no == / != (no synthesized
equality). The checker rejects these before any engine runs.

Accepted programs run in Fast Debug, Native Debug, and Native Release (and,
for the randomized Unicode properties, in the staged indexed-v1 String
representation) with identical stdout, compared with a Python oracle.
"""

import json
import os
from pathlib import Path
import random
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]
MOSS = Path(sys.argv[1] if len(sys.argv) > 1 else "./moss").resolve()
MARGO = ROOT / "margo"
SCRATCH = ROOT / "tmp"
SCRATCH.mkdir(exist_ok=True)
SEED = int(os.environ.get("MOSS_S2_SEED", "2025"))


def run(cmd, cwd=ROOT, timeout=600, env=None):
    return subprocess.run(cmd, cwd=cwd, capture_output=True, text=True, timeout=timeout,
                          env=env)


def native(source, td, mode, staged=False):
    env = dict(os.environ)
    if staged:
        env["MOSS_STAGED_STRING_REPR"] = "indexed-v1"
    flag, rustc_flags = ("--debug", []) if mode == "debug" else ("-O", ["-O"])
    tag = f"{mode}{'-indexed' if staged else ''}"
    rust = Path(td) / f"{tag}.rs"
    binary = Path(td) / tag
    gen = run([str(MOSS), str(source), flag, "-o", str(rust)], env=env)
    assert gen.returncode == 0, f"{tag} lowering failed:\n{gen.stderr}"
    build = run(["rustc", *rustc_flags, "--crate-name", "case", str(rust), "-o", str(binary)])
    assert build.returncode == 0, f"{tag} rustc failed:\n{build.stderr[:4000]}"
    return run([str(binary)])


def engines(code, name, expected=None, staged=False):
    """Run all engines; require identical stdout and success."""
    with tempfile.TemporaryDirectory(prefix="p25-s2-", dir=SCRATCH) as td:
        source = Path(td) / "case.moss"
        source.write_text(code)
        check = run([str(MOSS), "check", str(source), "--json"])
        assert json.loads(check.stdout)["ok"], f"[{name}] check rejected:\n{check.stdout[:2000]}"
        outputs = {}
        interp = run([str(MOSS), "run", "--interp", str(source)])
        assert interp.returncode == 0, f"[{name}] Fast Debug failed:\n{interp.stdout}{interp.stderr}"
        outputs["fast-debug"] = interp.stdout
        modes = [("debug", False), ("release", False)]
        if staged:
            modes += [("debug", True), ("release", True)]
        for mode, indexed in modes:
            result = native(source, td, mode, indexed)
            assert result.returncode == 0, f"[{name}] {mode} failed:\n{result.stderr}"
            outputs[f"native-{mode}{'-indexed' if indexed else ''}"] = result.stdout
        assert len(set(outputs.values())) == 1, f"[{name}] engine disagreement: " + json.dumps(
            {k: v[:400] for k, v in outputs.items()}, indent=1)
        if expected is not None:
            assert outputs["fast-debug"] == expected, (
                f"[{name}] expected {expected!r}, got {outputs['fast-debug']!r}")
        print(f"  PASS: {name} ({len(outputs)} engines)")
        return outputs["fast-debug"]


def rejected(code, name, code_expected, line=None):
    with tempfile.TemporaryDirectory(prefix="p25-s2-", dir=SCRATCH) as td:
        source = Path(td) / "case.moss"
        source.write_text(code)
        result = run([str(MOSS), "check", str(source), "--json"])
        assert result.returncode != 0, f"[{name}] accepted:\n{result.stdout}"
        error = json.loads(result.stdout).get("error") or {}
        assert error.get("code") == code_expected, (
            f"[{name}] expected {code_expected}, got {error.get('code')}: {error.get('message')}")
        if line is not None:
            assert error.get("line") == line, f"[{name}] expected line {line}, got {error.get('line')}"
        # The same diagnostic stops every engine before execution.
        interp = run([str(MOSS), "run", "--interp", str(source)])
        assert interp.returncode != 0 and code_expected in interp.stderr and not interp.stdout, (
            f"[{name}] Fast Debug did not stop before execution:\n{interp.stdout}{interp.stderr}")
        gen = run([str(MOSS), str(source), "--debug", "-o", str(Path(td) / "case.rs")])
        assert gen.returncode != 0 and code_expected in gen.stderr, (
            f"[{name}] native lowering did not reject:\n{gen.stderr}")
        print(f"  PASS: {name} -> {code_expected}")


RECORD = """
type Point:
  x: Int
  y: Int

"""


def test_record_equality_rejected():
    print("S1.1: record == / != is rejected before native compilation")
    rejected(RECORD + "fn main():\n  a = Point(x: 1, y: 2)\n  b = Point(x: 1, y: 2)\n"
             "  echo a == b\n", "record ==", "RECORD_EQUALITY_UNSUPPORTED", 9)
    rejected(RECORD + "fn main():\n  a = Point(x: 1, y: 2)\n  b = Point(x: 3, y: 2)\n"
             "  if a != b:\n    echo 1\n", "record != in a condition", "RECORD_EQUALITY_UNSUPPORTED")
    rejected(RECORD + "fn main():\n  a = Point(x: 1, y: 2)\n  assert(a == a)\n",
             "record == in assert", "RECORD_EQUALITY_UNSUPPORTED")
    rejected(RECORD + "fn main():\n  v = [Point(x: 1, y: 2)]\n  w = [Point(x: 1, y: 2)]\n"
             "  echo v == w\n", "Vector of records ==", "RECORD_EQUALITY_UNSUPPORTED")
    rejected(RECORD + "type Line:\n  a: Point\n  b: Point\n\n"
             "fn main():\n  l = Line(a: Point(x: 1, y: 2), b: Point(x: 1, y: 2))\n"
             "  echo l.a == l.b\n", "nested record fields ==", "RECORD_EQUALITY_UNSUPPORTED")
    rejected(RECORD + "fn same(left, right):\n  return left == right\n\n"
             "fn main():\n  echo same(Point(x: 1, y: 2), Point(x: 1, y: 2))\n",
             "record == through an untyped specialization", "RECORD_EQUALITY_UNSUPPORTED")
    rejected(RECORD + "fn main():\n  a = Point(x: 1, y: 2)\n  while a == a:\n    echo 1\n",
             "record == in while", "RECORD_EQUALITY_UNSUPPORTED")
    rejected(RECORD + "fn main():\n  assertEqual(Point(x: 1, y: 2), Point(x: 1, y: 2))\n",
             "record assertEqual", "RECORD_EQUALITY_UNSUPPORTED")
    rejected(RECORD + "fn main():\n  assertEqual([Point(x: 1, y: 2)], [Point(x: 1, y: 2)])\n",
             "Vector of records assertEqual", "RECORD_EQUALITY_UNSUPPORTED")
    # Field-wise comparison is the supported spelling.
    engines(RECORD + "fn same_point(a: Point, b: Point) -> Bool:\n"
            "  return a.x == b.x and a.y == b.y\n\n"
            "fn main():\n  echo same_point(Point(x: 1, y: 2), Point(x: 1, y: 2))\n"
            "  echo same_point(Point(x: 1, y: 2), Point(x: 1, y: 3))\n",
            "explicit field comparison", "true\nfalse\n")


def test_map_keys_fail_closed():
    print("S1: record and Float-containing Map keys fail closed (P25-S3-D2)")
    tag = "type Tag:\n  label: String\n\n"
    rejected(tag + "fn main():\n  var m = Map()\n  m[Tag(label: \"a\")] = 1\n  echo 1\n",
             "record key assignment", "RECORD_MAP_KEY_UNSUPPORTED", 6)
    rejected(tag + "fn main():\n  var m = Map()\n  m[[Tag(label: \"a\")]] = 1\n  echo 1\n",
             "Vector-of-record key", "RECORD_MAP_KEY_UNSUPPORTED", 6)
    rejected("fn main():\n  var m = Map()\n  m[[1.5]] = 1\n  echo 1\n",
             "Vector[Float] key", "FLOAT_MAP_KEY_UNSUPPORTED", 3)
    rejected(tag + "fn size(m: Map[Tag, Int]) -> Int:\n  return 0\n\nfn main():\n  echo 1\n",
             "declared Map[Record, V] parameter", "UNKNOWN_SYMBOL_OR_TYPE")
    engines(tag + "fn main():\n  var m = Map()\n  m[\"a\"] = Tag(label: \"x\")\n"
            "  echo m[\"a\"].label\n  var k = Map()\n  k[[1, 2]] = 3\n  echo k.get([1, 2], 0)\n",
            "record values and Vector[Int] keys still work", "x\n3\n")


def test_architect_next_cycle_findings():
    print("Architect findings P25-S-ARCH-013..017 at b6d45cf")
    for body, label in (("c = \"abc\".char_at(0)\n  echo c + \"x\"", "013 Char + String"),
                        ("echo 'a' + 'b'", "013 Char + Char"),
                        ("echo true + 1", "013 Bool + Int"),
                        ("echo \"a\" - \"b\"", "013 String - String"),
                        ("echo [1] + [2]", "013 Vector + Vector"),
                        ("echo \"ab\" * 2", "013 String * Int"),
                        ("c = 'a'\n  echo -c", "013 unary minus on Char"),
                        ("echo -true", "013 unary minus on Bool")):
        rejected(f"fn main():\n  {body}\n", label, "TYPE_MISMATCH")
    with tempfile.TemporaryDirectory(prefix="p25-s2-", dir=SCRATCH) as td:
        source = Path(td) / "case.moss"
        source.write_text('fn main():\n  echo "a" % "b"\n')
        error = json.loads(run([str(MOSS), "check", str(source), "--json"]).stdout)["error"]
        assert "integer remainder operands must have type 'Int'" in error["message"], error
        print("  PASS: 013 '%' keeps its Int-only remainder diagnostic")
    engines("fn main():\n  x = 3\n  echo 1 + 2.5 * x - 4 / 2\n  echo 5 - -x\n"
            "  echo \"a\" + \"b\" + 'c'.to_string()\n",
            "013 numeric and String + String still valid", "6.5\n8\nabc\n")
    engines("type P:\n  n: Int\n\n  fn get() -> Int:\n    return n\n\n"
            "fn big() -> Int:\n  return 7\n\n"
            "fn main():\n  s = \"abc\"\n  c = 'a'\n  v = [4, 5]\n  p = P(n: 4)\n"
            "  echo -s.length()\n  echo -c.codepoint()\n  echo -s.find(\"b\")\n"
            "  echo 10 + -s.length()\n  if -s.length() < 0:\n    echo \"neg\"\n"
            "  echo -p.get()\n  echo -big()\n  echo -(big() + 1)\n  echo -v[0]\n  echo - -3\n",
            "021/020 prefix minus negates the whole postfix operand",
            "-3\n-97\n-1\n7\nneg\n-4\n-7\n-8\n-4\n3\n")
    color = "enum Color:\n  Red\n  Blue\n\n"
    rejected(color + "fn main():\n  var inner = Map()\n  inner[\"a\"] = 1\n  var m = Map()\n"
             "  m[inner] = 1\n  echo 1\n", "014 Map as a Map key", "MAP_KEY_TYPE_UNSUPPORTED")
    rejected(color + "fn main():\n  var m = Map()\n  m[[Color.Red]] = 1\n  echo 1\n",
             "014 Vector[enum] as a Map key", "ENUM_MAP_KEY_UNSUPPORTED")
    engines("fn main():\n  var m = Map()\n  m['a'] = 1\n  var b = Map()\n  b[true] = 2\n  echo m['a'] + b[true]\n"
            "  var q = Map()\n  q[[\"x\", \"y\"]] = 3\n  echo q.get([\"x\", \"y\"], 0)\n",
            "014 hashable builtin keys still valid", "3\n3\n")
    rejected(color + "fn main():\n  assertEqual([Color.Red], [Color.Red])\n",
             "015 assertEqual on Vector[enum]", "ENUM_OPERATOR_UNSUPPORTED")
    rejected("fn main():\n  echo Char(codepoint: 65)\n", "016 labelled Char(Int)",
             "CALL_NAMED_ARGUMENT_UNSUPPORTED")
    rejected("fn main():\n  echo range(0, 3) == range(0, 3)\n", "017 range as a value",
             "RANGE_OUTSIDE_FOR")
    rejected("fn main():\n  r = range(0, 3)\n  echo 1\n", "017 range bound to a local",
             "RANGE_OUTSIDE_FOR")
    engines("fn main():\n  var t = 0\n  for i in range(0, 5, 2):\n    t = t + i\n  echo t\n",
            "017 range as a for source still valid", "6\n")


def write_record_projects(base):
    provider = base / "geometry"
    (provider / "src").mkdir(parents=True)
    (provider / "Moss.toml").write_text(
        '[project]\nname = "geometry"\nversion = "0.1.0"\n\n[build]\nsource = "src"\n')
    (provider / "src" / "geometry.moss").write_text(
        "module geometry\n\nexport type Point:\n  x: Int\n  y: Int\n\n"
        "export fn origin() -> Point:\n  return Point(x: 0, y: 0)\n")
    app = base / "app"
    (app / "src").mkdir(parents=True)
    (app / "Moss.toml").write_text(
        '[project]\nname = "app"\nversion = "0.1.0"\n\n[build]\nsource = "src"\n\n'
        '[dependencies]\ngeometry = { path = "../geometry" }\n')
    (app / "src" / "main.moss").write_text(
        "module app\nimport geometry\n\nfn main():\n  a = geometry.origin()\n"
        "  b = geometry.origin()\n  echo a == b\n")
    return app


def test_imported_record_equality_rejected():
    print("S1.1: imported record == is rejected in the package build")
    with tempfile.TemporaryDirectory(prefix="p25-s2-pkg-", dir=SCRATCH) as td:
        app = write_record_projects(Path(td))
        result = run([str(MARGO), "build", "--json"], cwd=app)
        text = result.stdout + result.stderr
        assert result.returncode != 0, f"imported record == accepted:\n{text[-2000:]}"
        assert "RECORD_EQUALITY_UNSUPPORTED" in text, text[-2000:]
        assert "error[E" not in text, f"reached rustc:\n{text[-2000:]}"
        print("  PASS: imported record == -> RECORD_EQUALITY_UNSUPPORTED")


def test_mixed_operands_rejected():
    print("S1.1: mismatched builtin equality operands are rejected")
    for left, right, label in (("1", '"a"', "Int == String"),
                               ("true", "1", "Bool == Int"),
                               ("[1]", "[1.5]", "Vector[Int] == Vector[Float]"),
                               ('"a"', "1.0", "String == Float")):
        rejected(f"fn main():\n  echo {left} == {right}\n", label, "TYPE_MISMATCH", 2)
    engines("fn main():\n  echo 1 == 1.0\n  echo 2.5 != 2\n  echo [1, 2] == [1, 2]\n"
            "  echo [[1], [2]] != [[1], [3]]\n  var m = Map()\n  m[\"k\"] = 1\n"
            "  var n = Map()\n  n[\"k\"] = 1\n  echo m == n\n  var q = Queue()\n"
            "  q.push('a')\n  var r = Queue()\n  r.push('a')\n  echo q == r\n",
            "same-type and Int/Float equality", "true\ntrue\ntrue\ntrue\ntrue\ntrue\n")


def test_char_string_distinct():
    print("S2: Char and String are distinct types")
    cases = (
        ("fn main():\n  c = 'a'\n  echo c == \"a\"\n", "Char == String", "TYPE_MISMATCH"),
        ("fn main():\n  c = 'a'\n  echo \"a\" == c\n", "String == Char", "TYPE_MISMATCH"),
        ("fn main():\n  echo \"aé\".char_at(1) != \"é\"\n", "char_at != String", "TYPE_MISMATCH"),
        ("fn main():\n  echo \"ab\".chars() == [\"a\", \"b\"]\n", "Vector[Char] == Vector[String]",
         "TYPE_MISMATCH"),
        ("fn main():\n  assertEqual(\"é\".char_at(0), \"é\")\n", "mixed assertEqual", "TEST_ASSERTION_CONFIGURATION_ERROR"),
        ("fn main():\n  assertEqual(\"é\", 'é')\n", "mixed assertEqual reversed", "TEST_ASSERTION_CONFIGURATION_ERROR"),
        ("fn main():\n  echo isequal('a', \"a\")\n", "mixed isequal", "KEY_PROTOCOL_TYPE"),
        ("fn main():\n  echo less(\"a\", 'b')\n", "mixed less", "KEY_PROTOCOL_TYPE"),
        ("fn main():\n  n = \"abc\".chars() |> filter(_ == \"a\") |> count\n  echo n\n",
         "placeholder Char == String", "TYPE_MISMATCH"),
        ("fn main():\n  b = \"abc\".chars() |> any(_ == \"b\")\n  echo b\n",
         "any placeholder Char == String", "TYPE_MISMATCH"),
        ("fn is_slash(c: Char) -> Bool:\n  return c == \"/\"\n\nfn main():\n  echo is_slash('/')\n",
         "typed Char parameter == String", "TYPE_MISMATCH"),
        ("fn eq(a, b):\n  return a == b\n\nfn main():\n  echo eq('a', \"a\")\n",
         "untyped specialization Char == String", "TYPE_MISMATCH"),
    )
    for code, label, diagnostic in cases:
        rejected(code, label, diagnostic)
    engines("""
fn is_slash(c: Char) -> Bool:
  return c == '/'

fn main():
  let s = "a/é/😀"
  let parts = s.chars()
  echo parts[1] == '/'
  echo is_slash(s.char_at(3))
  echo s.char_at(2) == 'é'
  echo s.char_at(2).to_string() == "é"
  echo "😀" == parts[4].to_string()
  echo parts |> filter(_ == '/') |> count
  echo parts |> any(_ == '😀')
  echo isequal('é', s.char_at(2))
  echo 'a' < 'b'
  assertEqual(s.char_at(4), '😀')
  assertEqual(s.char_at(4).to_string(), "😀")
""", "Char literal and explicit to_string comparisons",
            "true\ntrue\ntrue\ntrue\ntrue\n2\ntrue\ntrue\ntrue\n")


def test_invalid_expression():
    print("S1: operands with no operator between them are rejected")
    for text in ("x = if 1 == 1: 1 else: 2", "x = foo bar", "x = 1 2", "echo if true",
                 "x = (while true)", "x = return 1", "x = [1 2]"):
        rejected(f"fn main():\n  {text}\n  echo 0\n", text, "INVALID_EXPRESSION", 2)
    for text in ('x = "a" in m', 'x = "a" not in m'):
        rejected(f"fn main():\n  var m = Map()\n  m[\"a\"] = 1\n  {text}\n  echo 0\n", text,
                 "UNSUPPORTED_EXPRESSION_OPERATOR", 4)
    engines("fn first(values: Vector[Int]) -> Int:\n  return values [0]\n\n"
            "fn main():\n  v = [4, 5]\n  echo v [1]\n  echo first (v)\n  echo 1.5e-3 < 2\n"
            "  echo not true and false\n  echo \"a b\" + \"c\"\n",
            "postfix forms after whitespace stay valid", "5\n4\ntrue\nfalse\na bc\n")


def test_char_conversion_and_failure():
    print("S2: Char(Int) construction, boundaries, and invalid dynamic values")
    engines("""
fn main():
  echo Char(0).codepoint()
  echo Char(65)
  echo Char(55295).codepoint()
  echo Char(57344).codepoint()
  echo Char(1114111).codepoint()
  echo Char(128512).to_string().length()
  var n = 0
  var total = 0
  while n < 1114112:
    if n < 55296 or n > 57343:
      total = total + Char(n).codepoint() % 7
    n = n + 4099
  echo total
""", "valid Char(Int) boundaries and a dynamic sweep",
            "0\nA\n55295\n57344\n1114111\n1\n" + str(sum(n % 7 for n in range(0, 1114112, 4099)
                                                          if n < 55296 or n > 57343)) + "\n")
    for value, label in (("55296", "surrogate low edge"), ("57343", "surrogate high edge"),
                         ("1114112", "above U+10FFFF"), ("0 - 1", "negative"),
                         ("4294967296 + 65", "above 2^32 (no u32 truncation)")):
        code = ("fn make(n: Int) -> Char:\n  return Char(n)\n\n"
                f"fn main():\n  echo \"before\"\n  let v = {value}\n  c = make(v)\n"
                "  echo \"after\"\n  echo c.codepoint()\n")
        with tempfile.TemporaryDirectory(prefix="p25-s2-", dir=SCRATCH) as td:
            source = Path(td) / "case.moss"
            source.write_text(code)
            check = run([str(MOSS), "check", str(source), "--json"])
            assert json.loads(check.stdout)["ok"], check.stdout[:1000]
            results = {"fast-debug": run([str(MOSS), "run", "--interp", str(source)])}
            for mode in ("debug", "release"):
                results[f"native-{mode}"] = native(source, td, mode)
            for engine, result in results.items():
                assert result.returncode != 0, f"[{label}] {engine} constructed an invalid Char"
                assert result.stdout == "before\n", (
                    f"[{label}] {engine} continued past the failed Char(Int): {result.stdout!r}")
            print(f"  PASS: dynamic Char({value}) fails in every engine before use ({label})")


# Escapes Rust and Moss both define; every engine must decode them identically.
ESCAPES = [("\\n", "\n"), ("\\t", "\t"), ("\\r", "\r"), ("\\0", "\0"), ("\\\\", "\\"),
           ("\\\"", "\""), ("\\'", "'"), ("\\x41", "A"), ("\\x7F", "\x7f"),
           ("\\u{E9}", "é"), ("\\u{1F600}", "😀"), ("\\u{10FFFF}", "\U0010ffff"),
           ("\\u{0}", "\0"), ("\\u{D7FF}", "\ud7ff")]


def test_escaped_literal_parity():
    print("S2: escaped String and Char literals agree across engines")
    lines, expected = ["fn main():"], []
    for escape, value in ESCAPES:
        lines.append(f'  echo "{escape}".length()')
        expected.append(str(len(value)))
        lines.append(f'  echo "{escape}".char_at(0).codepoint()')
        expected.append(str(ord(value)))
        lines.append(f"  echo '{escape}'.codepoint()")
        expected.append(str(ord(value)))
        lines.append(f"  echo '{escape}' == \"{escape}\".char_at(0)")
        expected.append("true")
    lines.append('  echo "a\\tb\\u{E9}\\x41".chars() |> count')
    expected.append("5")
    engines("\n".join(lines) + "\n", f"{len(ESCAPES)} escapes x 4 forms", "\n".join(expected) + "\n")


POOL = (list(range(0x20, 0x7F)) + [0xA0, 0xE9, 0xF1, 0x3A9, 0x5D0, 0x627, 0x301, 0x200D] +
        [0x4E16, 0x754C, 0xAC00, 0xFFFD, 0xD7FF, 0xE000] +
        [0x1F600, 0x1F30D, 0x1F468, 0x10348, 0x10FFFF])


def moss_literal(text):
    out = []
    for ch in text:
        cp = ord(ch)
        if ch in '"\\' or cp < 0x20 or cp == 0x7F or cp > 0x7E and random_escape.random() < 0.5:
            out.append("\\u{%X}" % cp)
        else:
            out.append(ch)
    return '"' + "".join(out) + '"'


random_escape = random.Random(SEED + 1)


def test_randomized_unicode_properties():
    print(f"S2: randomized Unicode String/Char properties (seed {SEED})")
    rng = random.Random(SEED)
    lines = ["fn main():"]
    expected = []
    checks = 0
    for index in range(36):
        size = rng.choice([0, 1, 2, 7, 63, 64, 65, 130, 200])
        text = "".join(chr(rng.choice(POOL)) for _ in range(size))
        name = f"s{index}"
        lines.append(f"  let {name} = {moss_literal(text)}")
        lines.append(f"  echo {name}.length()")
        expected.append(str(len(text)))
        lines.append(f"  echo {name}.chars() |> count")
        expected.append(str(len(text)))
        for _ in range(4 if text else 0):
            i = rng.randrange(len(text))
            lines.append(f"  echo {name}.char_at({i}).codepoint()")
            expected.append(str(ord(text[i])))
            lines.append(f"  echo {name}.chars()[{i}] == {name}.char_at({i})")
            expected.append("true")
            lines.append(f"  echo {name}.char_at({i}).to_string() == {name}.slice({i}, 1)")
            expected.append("true")
        for _ in range(3):
            start = rng.randrange(len(text) + 1)
            length = rng.randrange(len(text) - start + 1)
            piece = text[start:start + length]
            lines.append(f"  echo {name}.slice({start}, {length}).length()")
            expected.append(str(len(piece)))
            lines.append(f"  echo {name}.find({moss_literal(piece)})")
            expected.append(str(text.find(piece)))
        needle = "".join(chr(rng.choice(POOL)) for _ in range(rng.randrange(1, 3)))
        lines.append(f"  echo {name}.find({moss_literal(needle)})")
        expected.append(str(text.find(needle)))
        # Append construction keeps the code-point metadata of the whole value.
        cuts = sorted(rng.randrange(len(text) + 1) for _ in range(3))
        pieces = [text[a:b] for a, b in zip([0] + cuts, cuts + [len(text)])]
        lines.append(f"  var b{index} = \"\"")
        lines.append(f"  b{index}.reserve({rng.randrange(-2, 40)})")
        for piece in pieces:
            lines.append(f"  b{index}.append({moss_literal(piece)})")
        lines.append(f"  echo b{index} == {name}")
        expected.append("true")
        lines.append(f"  echo b{index}.length()")
        expected.append(str(len(text)))
        if text:
            j = len(text) - 1
            lines.append(f"  echo b{index}.char_at({j}).codepoint()")
            expected.append(str(ord(text[j])))
        lines.append(f"  let c{index} = b{index} + {name}")
        lines.append(f"  echo c{index}.length()")
        expected.append(str(2 * len(text)))
        if text:
            k = len(text) + rng.randrange(len(text))
            lines.append(f"  echo c{index}.char_at({k}).codepoint()")
            expected.append(str(ord((text + text)[k])))
        checks += 1
    engines("\n".join(lines) + "\n",
            f"{checks} random strings, {len(expected)} property checks vs Python oracle",
            "\n".join(expected) + "\n", staged=True)


def main():
    test_record_equality_rejected()
    test_imported_record_equality_rejected()
    test_mixed_operands_rejected()
    test_map_keys_fail_closed()
    test_architect_next_cycle_findings()
    test_char_string_distinct()
    test_invalid_expression()
    test_char_conversion_and_failure()
    test_escaped_literal_parity()
    test_randomized_unicode_properties()
    print("=== Phase 25 S1.1/S2 equality and Char/String checks passed ===")


if __name__ == "__main__":
    main()
