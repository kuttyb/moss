"""Phase 25 Team S regressions for the L7 review of 983acb2.

Each case runs in Fast Debug, Native Debug, and Native Release and requires
identical stdout, or asserts a structured checker diagnostic before lowering.
The cases are the original L7 findings and ledger reproducers, not a
re-statement of the earlier G1/G3/G4 suites.
"""

import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]
MOSS = Path(sys.argv[1] if len(sys.argv) > 1 else "./moss").resolve()
MARGO = ROOT / "margo"
SCRATCH = ROOT / "tmp"
SCRATCH.mkdir(exist_ok=True)


def run(cmd, cwd=ROOT, timeout=600):
    return subprocess.run(cmd, cwd=cwd, capture_output=True, text=True, timeout=timeout)


def three_modes(code, name, expected=None):
    """Run standalone source in all three engines; require identical stdout."""
    with tempfile.TemporaryDirectory(prefix="p25-l7-", dir=SCRATCH) as td:
        source = Path(td) / "case.moss"
        source.write_text(code)
        outputs = {}
        interp = run([str(MOSS), "run", "--interp", str(source)])
        assert interp.returncode == 0, f"[{name}] Fast Debug failed:\n{interp.stdout}{interp.stderr}"
        outputs["fast-debug"] = interp.stdout
        for mode, moss_flag, rustc_flags in (("native-debug", "--debug", []),
                                             ("native-release", "-O", ["-O"])):
            rust = Path(td) / f"{mode}.rs"
            binary = Path(td) / mode
            gen = run([str(MOSS), str(source), moss_flag, "-o", str(rust)])
            assert gen.returncode == 0, f"[{name}] {mode} lowering failed:\n{gen.stderr}"
            build = run(["rustc", *rustc_flags, "--crate-name", "case", str(rust), "-o", str(binary)])
            assert build.returncode == 0, f"[{name}] {mode} rustc failed:\n{build.stderr[:4000]}"
            result = run([str(binary)])
            assert result.returncode == 0, f"[{name}] {mode} run failed:\n{result.stderr}"
            outputs[mode] = result.stdout
        assert len(set(outputs.values())) == 1, f"[{name}] engine disagreement: {outputs}"
        if expected is not None:
            assert outputs["fast-debug"] == expected, (
                f"[{name}] expected {expected!r}, got {outputs['fast-debug']!r}")
        print(f"  PASS: {name}")
        return outputs["fast-debug"]


def rejected(code, name, code_expected):
    with tempfile.TemporaryDirectory(prefix="p25-l7-", dir=SCRATCH) as td:
        source = Path(td) / "case.moss"
        source.write_text(code)
        result = run([str(MOSS), "check", str(source), "--json"])
        assert result.returncode != 0, f"[{name}] accepted:\n{result.stdout}"
        payload = json.loads(result.stdout)
        codes = [payload.get("error", {}).get("code")]
        codes += [d.get("code") for d in payload.get("result", {}).get("diagnostics", [])]
        assert code_expected in codes, f"[{name}] expected {code_expected}, got {codes}"
        print(f"  PASS: {name} -> {code_expected}")


# --- Finding 1: IEEE Float ==/!= independent of isequal --------------------

def test_float_ieee_equality():
    print("Finding 1: IEEE Float ==/!= vs isequal")
    three_modes("""
fn main():
  let zero = 0.0
  let nan = zero / zero
  let negative_nan = 0.0 - nan
  let positive_zero = 0.0
  let negative_zero = -0.0
  echo nan == nan
  echo nan != nan
  echo negative_nan == nan
  echo nan == 1.0
  echo positive_zero == negative_zero
  echo positive_zero != negative_zero
  echo [nan] == [nan]
  echo [positive_zero] == [negative_zero]
  echo isequal(nan, nan)
  echo isequal(nan, negative_nan)
  echo isequal(positive_zero, negative_zero)
  echo nan.isequal(nan)
  echo Float.isequal(negative_zero, positive_zero)
  echo hash(nan) == hash(negative_nan)
  echo hash(positive_zero) == hash(negative_zero)
  assertEqual(positive_zero, negative_zero)
  assert(nan != nan)
""", "NaN and signed-zero operator vs total equality",
        "false\ntrue\nfalse\nfalse\ntrue\nfalse\nfalse\ntrue\n"
        "true\ntrue\nfalse\ntrue\nfalse\ntrue\nfalse\n")


# --- Finding 5: Fast Debug ranges, iteration limits, block scope -----------

def test_fast_debug_loops():
    print("Finding 5: Fast Debug range overflow, iteration limits, scoping")
    three_modes("""
fn main():
  let top = 9223372036854775807
  var a = 0
  for i in range(top - 2, top):
    a = a + 1
  echo a
  var b = 0
  for j in range(top - 5, top, 2):
    b = b + 1
  echo b
  var c = 0
  for k in range(top - 1, top, 1000):
    c = c + 1
  echo c
  var d = 0
  for q in range(5, 5):
    d = d + 1
  echo d
""", "range bounds at Int max never wrap", "2\n3\n1\n0\n")

    three_modes("""
fn main():
  var total = 0
  for i in range(0, 10000001):
    total = total + 1
  echo total
""", "for loop beyond the former 10M interpreter cap", "10000001\n")

    three_modes("""
enum Shape:
  Circle(radius: Int)
  Square(side: Int)

fn pick(flag: Bool) -> Shape:
  if flag:
    return Shape.Circle(radius: 1)
  return Shape.Square(side: 2)

fn main():
  let i = 100
  for i in range(0, 2):
    echo i
  echo i
  let y = 1
  for k in [5]:
    let y = 2
    echo y
  echo y
  if y == 1:
    let y = 3
    echo y
  else:
    echo "wrong branch"
  echo y
  var w = 0
  while w < 2:
    let y = 4
    w = w + 1
  echo y
  match pick(true):
    case Circle(radius):
      let y = 5
      echo radius
    case Square(side):
      echo side
  echo y
  var outer = 0
  for n in range(0, 3):
    outer = outer + n
  echo outer
""", "block-scoped shadowing restores outer bindings",
        "0\n1\n100\n2\n1\n3\n1\n1\n1\n1\n3\n")


# --- Ledger #4: parameter-based indexed assignment ------------------------

def test_parameter_indexed_assignment():
    print("Ledger #4: parameter-based indexed assignment")
    three_modes("""
type Matrix:
  rows: Int
  cols: Int
  data: Vector[Int]

fn set_at(m: Matrix, r: Int, c: Int, v: Int):
  m.data[r * m.cols + c] = v

fn get_at(m: Matrix, r: Int, c: Int) -> Int:
  return m.data[r * m.cols + c]

fn bump(v: Vector[Int], i: Int):
  v[i + v[1] - 2] = v[i] + 10

fn main():
  var m = Matrix(rows: 2, cols: 3, data: [0, 0, 0, 0, 0, 0])
  set_at(m, 1, 2, 42)
  set_at(m, 0, 1, 7)
  echo get_at(m, 1, 2)
  echo get_at(m, 0, 1)
  var v = [1, 2, 3]
  bump(v, 0)
  echo v[0]
""", "indexed write through a parameter reads the same parameter", "42\n7\n11\n")


# --- Ledger #10: cross-package constructor-label collision ----------------

PROVIDER = """module net

export type Table:
  row_count: Int
  name: String

export enum TableError:
  Mismatch(column_count: Int)

export fn row_count(t: Table) -> Int:
  return t.row_count

export fn column_count(x: Int) -> Int:
  return x + 1

export fn make(n: Int) -> Table:
  return Table(row_count: n, name: "t")

export fn mismatch(n: Int) -> TableError:
  return TableError.Mismatch(column_count: column_count(n))

export fn describe(e: TableError) -> Int:
  match e:
    case Mismatch(column_count):
      return column_count
"""

CONSUMER = """module app

import net

fn main():
  let t = net.make(5)
  echo net.row_count(t)
  echo t.row_count
  let t2 = net.Table(row_count: 7, name: "u")
  echo net.row_count(t2)
  echo net.describe(net.mismatch(2))
  echo net.describe(net.TableError.Mismatch(column_count: 9))
"""


def write_label_projects(base):
    (base / "net" / "src").mkdir(parents=True)
    (base / "app" / "src").mkdir(parents=True)
    (base / "net" / "Moss.toml").write_text(
        '[package]\nname = "net"\nversion = "0.1.0"\n\n[build]\nsource = "src"\n')
    (base / "net" / "src" / "main.moss").write_text(PROVIDER)
    (base / "app" / "Moss.toml").write_text(
        '[package]\nname = "labelapp"\nversion = "0.1.0"\n\n[build]\nsource = "src"\n\n'
        '[dependencies]\nnet = { path = "../net" }\n')
    (base / "app" / "src" / "main.moss").write_text(CONSUMER)


def test_constructor_label_collision():
    print("Ledger #10: constructor/enum-payload labels vs same-module functions")
    expected = "5\n5\n7\n3\n9\n"
    with tempfile.TemporaryDirectory(prefix="p25-l7-labels-", dir=SCRATCH) as td:
        base = Path(td)
        write_label_projects(base)
        app = base / "app"
        outputs = {}
        for mode, args in (("native-debug", ["run"]), ("native-release", ["--release", "run"]),
                           ("fast-debug", ["debug"])):
            result = run([str(MARGO), *args], cwd=app)
            assert result.returncode == 0, f"[{mode}] failed:\n{result.stdout}{result.stderr}"
            outputs[mode] = result.stdout
        assert set(outputs.values()) == {expected}, outputs
        print("  PASS: provider labels row_count/column_count in all three engines")


# --- G3: sort, Reverse, and key purity -----------------------------------

def test_sort_semantics():
    print("G3: sort / sort by: / Reverse")
    three_modes("""
fn negate(x: Int) -> Int:
  return 0 - x

fn size(s: String) -> Int:
  return s.length()

fn order(v: Vector[Int]):
  sort(v)

fn order_desc(v: Vector[Int]):
  sort(v, by: negate)

fn main():
  var v = [3, 1, 2]
  order(v)
  echo v[0]
  order_desc(v)
  echo v[0]
  var words = ["ccc", "a", "bb", "dd"]
  sort(words, by: size)
  echo words[0]
  echo words[1]
  echo words[2]
  sort(words, by: Reverse(size))
  echo words[0]
  echo words[1]
  echo words[2]
  var letters = ['z', 'a', 'é']
  sort(letters)
  echo letters[0]
  echo letters[2]
  var flags = [true, false]
  sort(flags)
  echo flags[0]
  let zero = 0.0
  var floats = [zero / zero, 1.0, 0.0, -0.0, -10.0]
  sort(floats)
  echo floats[0]
  echo isequal(floats[1], -0.0)
  echo isequal(floats[2], 0.0)
  echo floats[3]
  echo is_nan(floats[4])
""", "stable total-order sort across scalar keys and call positions",
        "1\n3\na\nbb\ndd\nccc\nbb\ndd\na\né\nfalse\n-10.0\ntrue\ntrue\n1.0\ntrue\n")

    rejected("""
fn noisy(x: Int) -> Int:
  echo "side effect"
  return x

fn main():
  var v = [2, 1]
  sort(v, by: noisy)
""", "impure sort key", "SORT_KEY_IMPURE")
    rejected("""
fn key(x: Int) -> Int:
  return x

fn main():
  var v = [2, 1]
  sort(v, using: key)
""", "unlabelled sort key", "SORT_SIGNATURE")
    rejected("""
type P:
  x: Int

fn main():
  var v = [P(x: 1)]
  sort(v)
""", "sort without a total order", "SORT_KEY_UNORDERED")
    rejected("""
fn key(x: Int) -> Int:
  return x

fn main():
  let r = Reverse(key)
""", "Reverse outside sort", "SORT_REVERSE_POSITION")


def test_scalar_protocol_types():
    print("key-protocol-v1 scalar operand types")
    rejected("fn main():\n  echo less(1, 2.0)\n", "less across Int/Float", "KEY_PROTOCOL_TYPE")
    rejected("fn main():\n  echo isequal(1, \"a\")\n", "isequal across Int/String", "KEY_PROTOCOL_TYPE")
    rejected("fn main():\n  echo Int.less(1, 2.5)\n", "static less with Float", "KEY_PROTOCOL_TYPE")
    rejected("fn main():\n  echo hash([1])\n", "hash of a Vector", "KEY_PROTOCOL_TYPE")
    rejected("fn main():\n  var m = Map()\n  m[1.5] = 1\n", "Float Map key", "FLOAT_MAP_KEY_UNSUPPORTED")
    three_modes("""
fn hash(x: Int) -> Int:
  return 7

fn main():
  echo hash(3)
""", "user fn hash is not hijacked by the builtin", "7\n")


# --- Ordering laws ---------------------------------------------------------

LAW_PROGRAM = """
fn check_laws(values: Vector[{T}]) -> Int:
  var failures = 0
  for x in values:
    if less(x, x):
      failures = failures + 1
    if not isequal(x, x):
      failures = failures + 1
    for y in values:
      if less(x, y) and less(y, x):
        failures = failures + 1
      var relations = 0
      if less(x, y):
        relations = relations + 1
      if less(y, x):
        relations = relations + 1
      if isequal(x, y):
        relations = relations + 1
      if relations != 1:
        failures = failures + 1
      if isequal(x, y) != isequal(y, x):
        failures = failures + 1
      if isequal(x, y) and hash(x) != hash(y):
        failures = failures + 1
      for z in values:
        if less(x, y) and less(y, z) and not less(x, z):
          failures = failures + 1
        if isequal(x, y) and isequal(y, z) and not isequal(x, z):
          failures = failures + 1
  return failures

fn main():
  {SETUP}
  echo check_laws(values)
"""

LAW_CASES = {
    "Float": ("Float", """let zero = 0.0
  let nan = zero / zero
  let inf = 1.0 / zero
  let values = [0.0 - inf, -1.0e308, -1.5, -5.0e-324, -0.0, 0.0, 5.0e-324, 1.5, 1.0e308, inf, nan, 0.0 - nan, 1.5]"""),
    "Int": ("Int", "let values = [-9223372036854775807 - 1, -1, 0, 0, 1, 9223372036854775807]"),
    "Char": ("Char", "let values = [Char(0), 'A', 'a', 'é', '世', '😀', Char(1114111), 'a']"),
    "String": ("String", 'let values = ["", "a", "ab", "b", "é", "😀", "a\\u{0}", "ab"]'),
    "Bool": ("Bool", "let values = [false, true, true]"),
}


def test_ordering_laws():
    print("G3: irreflexive/asymmetric/total/transitive less; isequal/hash laws")
    for name, (type_name, setup) in LAW_CASES.items():
        program = LAW_PROGRAM.replace("{T}", type_name).replace("{SETUP}", setup)
        three_modes(program, f"{name} ordering laws", "0\n")


RUST_LAW_MAIN = r"""
fn main() {
    let mut values: Vec<f64> = vec![
        f64::NEG_INFINITY, -1.0e308, -1.5, -5.0e-324, -0.0, 0.0, 5.0e-324, 1.5, 1.0e308,
        f64::INFINITY,
    ];
    for bits in [0x7ff8000000000000u64, 0xfff8000000000000, 0x7ff0000000000001,
                 0xfff0000000000001, 0x7ff4000000000000, 0x7fffffffffffffff] {
        let nan = f64::from_bits(bits);
        assert!(nan.is_nan());
        values.push(nan);
    }
    let mut failures = 0;
    for &x in &values {
        if moss_float_less(x, x) { failures += 1; }
        if !moss_float_isequal(x, x) { failures += 1; }
        for &y in &values {
            let relations = moss_float_less(x, y) as i32 + moss_float_less(y, x) as i32
                + moss_float_isequal(x, y) as i32;
            if relations != 1 { failures += 1; }
            if moss_float_isequal(x, y) != moss_float_isequal(y, x) { failures += 1; }
            if moss_float_isequal(x, y) && moss_float_hash(x) != moss_float_hash(y) { failures += 1; }
            if moss_float_total_cmp(x, y) != moss_float_total_cmp(y, x).reverse() { failures += 1; }
            for &z in &values {
                if moss_float_less(x, y) && moss_float_less(y, z) && !moss_float_less(x, z) { failures += 1; }
            }
        }
    }
    println!("{}", failures);
}
"""


def test_native_float_laws_with_nan_payloads():
    print("G3: native Float laws over NaN sign/payload bit patterns")
    header = (ROOT / "src" / "char_string_ordering_runtime.hpp").read_text()
    runtime = re.search(r'R"RUST\((.*)\)RUST"', header, re.S).group(1)
    with tempfile.TemporaryDirectory(prefix="p25-l7-laws-", dir=SCRATCH) as td:
        source = Path(td) / "laws.rs"
        source.write_text("#![allow(dead_code)]\n" + runtime + RUST_LAW_MAIN)
        binary = Path(td) / "laws"
        build = run(["rustc", "-O", str(source), "-o", str(binary)])
        assert build.returncode == 0, build.stderr
        result = run([str(binary)])
        assert result.returncode == 0 and result.stdout == "0\n", result.stdout + result.stderr
    print("  PASS: quiet/signaling/negative NaN payloads satisfy the Float laws")


# --- G4: Char surface and Unicode conversions ------------------------------

def test_char_surface():
    print("G4: Char as a declared type and Unicode conversions")
    three_modes("""
type Token:
  first: Char

fn code(c: Char) -> Int:
  return c.codepoint()

fn initial(s: String) -> Char:
  return s.char_at(0)

fn main():
  let t = Token(first: initial("éa"))
  echo code(t.first)
  echo t.first
  echo t.first.to_string()
  var cs = Vector[Char]()
  cs.push('😀')
  cs.push(Char(1114111))
  echo cs[0].codepoint()
  echo code(cs[1])
  echo Char(55295).codepoint()
  echo Char(57344).codepoint()
  let s = "aé世😀"
  echo s.length()
  let parts = s.chars()
  echo parts[3].codepoint()
  echo parts[1] == "é"
  echo "世" == parts[2]
  echo Char(65) == 'A'
""", "Char fields, parameters, returns, vectors, and conversions",
        "233\né\né\n128512\n1114111\n55295\n57344\n4\n128512\ntrue\ntrue\ntrue\n")

    for codepoint in (55296, 57343, 1114112, -1):
        with tempfile.TemporaryDirectory(prefix="p25-l7-", dir=SCRATCH) as td:
            source = Path(td) / "bad.moss"
            source.write_text(f"fn main():\n  echo Char({codepoint})\n")
            interp = run([str(MOSS), "run", "--interp", str(source)])
            rust = Path(td) / "bad.rs"
            assert run([str(MOSS), str(source), "--debug", "-o", str(rust)]).returncode == 0
            assert run(["rustc", "--crate-name", "bad", str(rust), "-o", str(Path(td) / "bad")]).returncode == 0
            native = run([str(Path(td) / "bad")])
            assert interp.returncode != 0 and native.returncode != 0, (codepoint, interp, native)
            assert interp.stdout == native.stdout == ""
    print("  PASS: surrogates and out-of-range scalars fail in every engine")


# --- Source-free consumer --------------------------------------------------

def test_source_free_semantics():
    print("Source-free: sort/less/hash/Char across a compiled-only provider")
    with tempfile.TemporaryDirectory(prefix="p25-l7-sf-", dir=SCRATCH) as td:
        proj = Path(td)
        (proj / "Moss.toml").write_text('[package]\nname = "sf"\nversion = "0.1.0"\n')
        src = proj / "src"
        src.mkdir()
        (src / "provider.moss").write_text("""module provider

fn negate(x: Int) -> Int:
  return 0 - x

export fn descending(v: Vector[Int]):
  sort(v, by: negate)

export fn initial(s: String) -> Char:
  return s.char_at(0)

export fn before(a: Float, b: Float) -> Bool:
  return less(a, b)

export fn key_hash(s: String) -> Int:
  return hash(s)
""")
        (src / "main.moss").write_text("""module app
import provider

fn main():
  var v = [1, 3, 2]
  provider.descending(v)
  echo v[0]
  echo provider.initial("世界")
  echo provider.before(-0.0, 0.0)
  echo provider.key_hash("a") == hash("a")
""")
        first = run([str(MOSS), "build", "--json"], cwd=proj)
        assert first.returncode == 0, first.stdout + first.stderr
        (src / "provider.moss").unlink()
        second = run([str(MOSS), "build", "--json"], cwd=proj)
        assert second.returncode == 0, second.stdout + second.stderr
        artifact = json.loads(second.stdout)["result"]["artifacts"]["executable"]
        result = run([artifact])
        assert result.returncode == 0 and result.stdout == "3\n世\ntrue\ntrue\n", result.stdout + result.stderr
    print("  PASS: compiled-only provider preserves sort/Char/less/hash semantics")


def main():
    print("=== Phase 25 Team S L7 repair regressions ===")
    test_float_ieee_equality()
    test_fast_debug_loops()
    test_parameter_indexed_assignment()
    test_constructor_label_collision()
    test_sort_semantics()
    test_scalar_protocol_types()
    test_ordering_laws()
    test_native_float_laws_with_nan_payloads()
    test_char_surface()
    test_source_free_semantics()
    print("=== All Phase 25 L7 repair regressions passed ===")


if __name__ == "__main__":
    main()
