"""Phase 25 Team S S3: key-protocol-v1 scalar semantics and sort-key laws.

Usage: python3 tests/tooling/check_phase25_s3_key_protocol.py path/to/moss

Extends the L7 ordering-law and purity regressions to:

- named key functions imported from another module and another package,
  source-backed and through a compiled-only (source-free) provider;
- one untyped key function specialized at several element types;
- non-Copy (String and record) elements and keys;
- NaN values from several Moss computations, plus native-only checks over
  NaN sign/payload bit patterns that Moss source cannot construct;
- adversarial keys: user `less`/`isequal`/`hash` functions and methods that
  violate the laws, degenerate but pure keys, and impure keys
  (`SORT_KEY_IMPURE`);
- cross-engine hash stability of the built-in scalar hashes.

Every accepted program must give identical stdout in Fast Debug, Native
Debug, and Native Release; most also pin the exact expected stdout.
"""

import json
import os
from pathlib import Path
import re
import struct
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]
MOSS = Path(sys.argv[1] if len(sys.argv) > 1 else "./moss").resolve()
MARGO = MOSS.parent / "margo" if (MOSS.parent / "margo").exists() else ROOT / "margo"
SCRATCH = ROOT / "tmp"
SCRATCH.mkdir(exist_ok=True)
ENV = {k: v for k, v in os.environ.items() if k != "MOSS_STAGED_STRING_REPR"}
CASES = []


def run(cmd, cwd=ROOT, timeout=600):
    return subprocess.run(cmd, cwd=cwd, capture_output=True, text=True,
                          timeout=timeout, env=ENV)


def passed(name):
    CASES.append(name)
    print(f"  PASS: {name}")


def three_modes(code, name, expected=None):
    """Run standalone source in all three engines; require identical stdout."""
    with tempfile.TemporaryDirectory(prefix="p25-s3-", dir=SCRATCH) as td:
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
        passed(name)
        return outputs["fast-debug"]


def diagnostic_codes(stdout):
    payload = json.loads(stdout)
    codes = [(payload.get("error") or {}).get("code")]
    codes += [d.get("code") for d in (payload.get("result") or {}).get("diagnostics", []) or []]
    return [c for c in codes if c]


def rejected(code, name, code_expected):
    """`moss check` rejects with the code; Fast Debug and lowering also refuse."""
    with tempfile.TemporaryDirectory(prefix="p25-s3-", dir=SCRATCH) as td:
        source = Path(td) / "case.moss"
        source.write_text(code)
        result = run([str(MOSS), "check", str(source), "--json"])
        assert result.returncode != 0, f"[{name}] accepted:\n{result.stdout}"
        codes = diagnostic_codes(result.stdout)
        assert code_expected in codes, f"[{name}] expected {code_expected}, got {codes}"
        interp = run([str(MOSS), "run", "--interp", str(source)])
        assert interp.returncode != 0 and code_expected in interp.stderr, (
            f"[{name}] Fast Debug did not refuse:\n{interp.stdout}{interp.stderr}")
        gen = run([str(MOSS), str(source), "--debug", "-o", str(Path(td) / "case.rs")])
        assert gen.returncode != 0, f"[{name}] native lowering accepted"
        passed(f"{name} -> {code_expected}")


def write_tree(base, files):
    for rel, text in files.items():
        path = base / rel
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(text)


def margo_three_modes(project, name, expected):
    outputs = {}
    for mode, args in (("native-debug", ["run"]), ("native-release", ["--release", "run"]),
                       ("fast-debug", ["debug"])):
        result = run([str(MARGO), *args], cwd=project)
        assert result.returncode == 0, f"[{name}] {mode} failed:\n{result.stdout}{result.stderr}"
        outputs[mode] = result.stdout
    assert set(outputs.values()) == {expected}, f"[{name}] {outputs}"
    passed(name)


# --- Imported named key functions -------------------------------------------

KEYS_MODULE = """module keys

export type Item:
  label: String
  weight: Float

export fn by_weight(i: Item) -> Float:
  return i.weight

export fn by_label(i: Item) -> String:
  return i.label

export fn loose(x):
  return x

export fn negated(x):
  return 0 - x

export fn size(x: String) -> Int:
  return x.length()

export fn less(a: Int, b: Int) -> Bool:
  return b < a

export fn order_items(v: Vector[Item]):
  sort(v, by: by_weight)
"""

KEYS_CONSUMER = """module app
import keys

fn show(v: Vector[keys.Item]) -> String:
  var parts = Vector[String]()
  for k in range(0, v |> count):
    parts.push(v[k].label)
  return ",".join(parts)

fn main():
  let z = 0.0
  var items = [keys.Item(label: "nan", weight: z / z), keys.Item(label: "pz", weight: 0.0), keys.Item(label: "nz", weight: -0.0), keys.Item(label: "neg", weight: -1.0), keys.Item(label: "pz2", weight: 0.0), keys.Item(label: "nan2", weight: 0.0 - z / z)]
  sort(items, by: keys.by_weight)
  echo show(items)
  sort(items, by: Reverse(keys.by_label))
  echo show(items)
  keys.order_items(items)
  echo show(items)
  var ints = [3, 1, 2]
  sort(ints, by: keys.loose)
  echo ints[0]
  sort(ints, by: keys.negated)
  echo ints[0]
  var words = ["bbb", "a", "cc"]
  sort(words, by: keys.loose)
  echo ",".join(words)
  sort(words, by: Reverse(keys.size))
  echo ",".join(words)
  var fs = [1.5, -0.0, 0.0]
  sort(fs, by: keys.negated)
  echo fs[0]
  echo isequal(fs[1], 0.0)
  echo keys.less(1, 2)
  echo less(1, 2)
"""

KEYS_EXPECTED = ("neg,nz,pz,pz2,nan,nan2\n"   # NaNs last, stable; -0.0 before +0.0
                 "pz2,pz,nz,neg,nan2,nan\n"    # labels descending
                 "neg,nz,pz2,pz,nan2,nan\n"    # stable re-sort by weight
                 "1\n3\na,bbb,cc\nbbb,cc,a\n1.5\nfalse\nfalse\ntrue\n")  # 0 - (+/-0.0) is +0.0 for both zeros: a stable tie


def test_imported_key_functions():
    print("Imported named key functions (module, package, source-free)")
    with tempfile.TemporaryDirectory(prefix="p25-s3-imp-", dir=SCRATCH) as td:
        project = Path(td) / "imp"
        write_tree(project, {
            "Moss.toml": '[package]\nname = "imp"\nversion = "0.1.0"\n',
            "src/keys.moss": KEYS_MODULE,
            "src/main.moss": KEYS_CONSUMER,
        })
        margo_three_modes(project, "same-package module key functions", KEYS_EXPECTED)

    with tempfile.TemporaryDirectory(prefix="p25-s3-pkg-", dir=SCRATCH) as td:
        base = Path(td)
        write_tree(base, {
            "keys/Moss.toml": '[package]\nname = "keys"\nversion = "0.1.0"\n\n[build]\nsource = "src"\n',
            "keys/src/main.moss": KEYS_MODULE,
            "app/Moss.toml": ('[package]\nname = "keyapp"\nversion = "0.1.0"\n\n[build]\nsource = "src"\n\n'
                              '[dependencies]\nkeys = { path = "../keys" }\n'),
            "app/src/main.moss": KEYS_CONSUMER,
        })
        margo_three_modes(base / "app", "path-dependency package key functions", KEYS_EXPECTED)

    with tempfile.TemporaryDirectory(prefix="p25-s3-sf-", dir=SCRATCH) as td:
        project = Path(td) / "sf"
        write_tree(project, {
            "Moss.toml": '[package]\nname = "sfkeys"\nversion = "0.1.0"\n',
            "src/keys.moss": KEYS_MODULE,
            "src/main.moss": KEYS_CONSUMER,
        })
        first = run([str(MOSS), "build", "--json"], cwd=project)
        assert first.returncode == 0, first.stdout + first.stderr
        (project / "src" / "keys.moss").unlink()
        second = run([str(MOSS), "build", "--json"], cwd=project)
        assert second.returncode == 0, second.stdout + second.stderr
        artifact = json.loads(second.stdout)["result"]["artifacts"]["executable"]
        result = run([artifact])
        assert result.returncode == 0, result.stderr
        assert result.stdout == KEYS_EXPECTED, result.stdout
        passed("compiled-only provider key functions (source removed)")


SIZE_PROVIDER = "module keys\n\nexport fn width(x) -> Int:\n  return x.length()\n"
SIZE_CONSUMER = ("module app\nimport keys\n\nfn main():\n  var w = [\"bb\", \"a\"]\n"
                 "  sort(w, by: keys.width)\n  echo w[0]\n")


def test_untyped_member_key_across_packages():
    """Pins current behaviour; see TEAM_S_KEY_PROTOCOL_V1_SCALARS.md section 6.

    An untyped key whose body makes a member call (`x.length()`) is accepted
    from a same-package module but rejected, fail-closed, from a path
    dependency: the .mossi summary of an untyped function carries an
    unresolved observable effect. This is the open-summary (Team G G2) item,
    not a sort-specific rule; flip this case when summaries carry effects.
    """
    print("Untyped member-calling key: same package vs path dependency")
    with tempfile.TemporaryDirectory(prefix="p25-s3-width-", dir=SCRATCH) as td:
        base = Path(td)
        write_tree(base, {
            "same/Moss.toml": '[package]\nname = "same"\nversion = "0.1.0"\n',
            "same/src/keys.moss": SIZE_PROVIDER,
            "same/src/main.moss": SIZE_CONSUMER,
            "keys/Moss.toml": '[package]\nname = "keys"\nversion = "0.1.0"\n\n[build]\nsource = "src"\n',
            "keys/src/main.moss": SIZE_PROVIDER,
            "app/Moss.toml": ('[package]\nname = "widthapp"\nversion = "0.1.0"\n\n[build]\nsource = "src"\n\n'
                              '[dependencies]\nkeys = { path = "../keys" }\n'),
            "app/src/main.moss": SIZE_CONSUMER,
        })
        margo_three_modes(base / "same", "untyped member-calling key, same-package module", "a\n")
        result = run([str(MARGO), "build"], cwd=base / "app")
        output = result.stdout + result.stderr
        assert result.returncode != 0, output
        assert "SORT_KEY_IMPURE" in output and "unresolved effect" in output, output
    passed("untyped member-calling key from a path dependency fails closed (pinned limitation)")


# --- Concrete specializations of one untyped key ----------------------------

def test_concrete_specializations():
    print("One untyped key function specialized at several element types")
    three_modes("""
type Tag:
  name: String
  rank: Int

  fn label() -> String:
    return name

fn same(x):
  return x

fn flip(x):
  return 0 - x

fn width(x) -> Int:
  return x.length()

fn name_of(t) -> String:
  return t.label()

fn main():
  var ints = [3, -1, 2, 2]
  sort(ints, by: same)
  echo ints[0]
  echo ints[3]
  sort(ints, by: flip)
  echo ints[0]
  let z = 0.0
  var floats = [z / z, 2.5, 0.0, -0.0, -3.0]
  sort(floats, by: same)
  echo floats[0]
  echo isequal(floats[1], -0.0)
  echo is_nan(floats[4])
  sort(floats, by: flip)
  echo is_nan(floats[4])
  echo floats[0]
  echo isequal(floats[1], -0.0)
  echo isequal(floats[2], 0.0)
  echo floats[3]
  var chars = ['é', 'a', 'Z', '😀']
  sort(chars, by: same)
  echo chars[0]
  echo chars[3]
  var words = ["ccc", "é", "bb", "a"]
  sort(words, by: same)
  echo ",".join(words)
  sort(words, by: width)
  echo ",".join(words)
  var flags = [true, false, true]
  sort(flags, by: same)
  echo flags[0]
  var tags = [Tag(name: "b", rank: 1), Tag(name: "a", rank: 2)]
  sort(tags, by: name_of)
  echo tags[0].rank
  echo same(7)
  echo same("x")
""", "untyped same/flip/width/name_of at Int, Float, Char, String, Bool, record",
        # flip maps both zeros to +0.0, so they tie and keep their order
        "-1\n3\n3\n-3.0\ntrue\ntrue\ntrue\n2.5\ntrue\ntrue\n-3.0\n"
        "Z\n😀\na,bb,ccc,é\na,é,bb,ccc\nfalse\n2\n7\nx\n")


# --- Non-Copy elements and keys ---------------------------------------------

def test_non_copy_keys():
    print("Non-Copy String/record elements and keys")
    three_modes("""
type Doc:
  title: String
  body: String
  score: Float
  tags: Vector[String]

fn title_of(d: Doc) -> String:
  return d.title

fn score_of(d: Doc) -> Float:
  return d.score

fn tag_count(d: Doc) -> Int:
  var n = 0
  for t in d.tags:
    n = n + 1
  return n

fn whole(s: String) -> String:
  return s

fn lowered(s: String) -> String:
  return s.ascii_lower()

fn show(v: Vector[Doc]) -> String:
  var parts = Vector[String]()
  for k in range(0, v |> count):
    parts.push(v[k].body)
  return ",".join(parts)

fn main():
  let z = 0.0
  var docs = Vector[Doc]()
  docs.push(Doc(title: "b", body: "1", score: 0.0, tags: ["x"]))
  docs.push(Doc(title: "a", body: "2", score: z / z, tags: Vector[String]()))
  docs.push(Doc(title: "b", body: "3", score: -0.0, tags: ["x", "y"]))
  docs.push(Doc(title: "a", body: "4", score: -0.0, tags: ["y"]))
  docs.push(Doc(title: "c", body: "5", score: 1.0, tags: ["x", "y", "z"]))
  sort(docs, by: title_of)
  echo show(docs)
  sort(docs, by: score_of)
  echo show(docs)
  sort(docs, by: Reverse(tag_count))
  echo show(docs)
  sort(docs, by: Reverse(title_of))
  echo show(docs)
  echo docs[0].tags[2]
  var words = ["b", "B", "a", "A", "é"]
  sort(words, by: lowered)
  echo ",".join(words)
  sort(words, by: whole)
  echo ",".join(words)
  sort(words)
  echo ",".join(words)
""", "String/record elements with String/Float/Int keys, stable ties",
        "2,4,1,3,5\n4,3,1,5,2\n5,3,4,1,2\n5,3,1,4,2\nz\na,A,b,B,é\nA,B,a,b,é\nA,B,a,b,é\n")


# --- NaN values and payloads -------------------------------------------------

def test_nan_values():
    print("NaN values from several computations")
    three_modes("""
type Sample:
  label: String
  value: Float

fn value_of(s: Sample) -> Float:
  return s.value

fn hash_of(x: Float) -> Int:
  return hash(x)

fn main():
  let zero = 0.0
  let inf = 1.0 / zero
  let a = zero / zero
  let b = 0.0 - a
  let c = inf - inf
  let d = inf * zero
  let e = a + 1.0
  let f = b * -2.0
  let nans = [a, b, c, d, e, f]
  var failures = 0
  for x in nans:
    if not is_nan(x):
      failures = failures + 1
    if x == x:
      failures = failures + 1
    for y in nans:
      if not isequal(x, y):
        failures = failures + 1
      if less(x, y):
        failures = failures + 1
      if hash(x) != hash(y):
        failures = failures + 1
      if not less(inf, y):
        failures = failures + 1
  echo failures
  var samples = [Sample(label: "a", value: a), Sample(label: "one", value: 1.0), Sample(label: "b", value: b), Sample(label: "c", value: c), Sample(label: "inf", value: inf), Sample(label: "d", value: d), Sample(label: "ninf", value: 0.0 - inf), Sample(label: "e", value: e), Sample(label: "f", value: f)]
  sort(samples, by: value_of)
  var labels = Vector[String]()
  for k in range(0, samples |> count):
    labels.push(samples[k].label)
  echo ",".join(labels)
  sort(samples, by: Reverse(value_of))
  var reversed = Vector[String]()
  for k in range(0, samples |> count):
    reversed.push(samples[k].label)
  echo ",".join(reversed)
  var hashes = [a, -0.0, b, 0.0]
  sort(hashes, by: hash_of)
  echo isequal(hashes[0], -0.0)
  echo isequal(hashes[1], 0.0)
  echo is_nan(hashes[2])
  echo hash(a) == hash(0.0)
""", "computed NaNs are isequal, hash-equal, unordered among themselves, last",
        "0\nninf,one,inf,a,b,c,d,e,f\na,b,c,d,e,f,inf,one,ninf\ntrue\ntrue\ntrue\nfalse\n")


RUST_NAN_PAYLOADS = r"""
fn main() {
    let bits = [0x7ff8000000000000u64, 0xfff8000000000000, 0x7ff0000000000001,
                0xfff0000000000001, 0x7ff4000000000000, 0x7fffffffffffffff,
                0xffffffffffffffff, 0x7ff8000000000001];
    let nans: Vec<f64> = bits.iter().map(|b| f64::from_bits(*b)).collect();
    let mut failures = 0;
    for x in &nans {
        assert!(x.is_nan());
        if moss_float_hash(*x) != 0x7ff8000000000000_u64 as i64 { failures += 1; }
        if moss_float_less(*x, *x) { failures += 1; }
        if moss_float_less(f64::INFINITY, *x) == false { failures += 1; }
        for y in &nans {
            if !moss_float_isequal(*x, *y) { failures += 1; }
            if moss_float_total_cmp(*x, *y) != std::cmp::Ordering::Equal { failures += 1; }
        }
    }
    if moss_float_hash(0.0) == moss_float_hash(-0.0) { failures += 1; }
    if moss_float_isequal(0.0, -0.0) { failures += 1; }
    if !moss_float_less(-0.0, 0.0) { failures += 1; }
    // Stable sort over mixed payloads keeps NaNs last in input order.
    let mut tagged: Vec<(f64, usize)> = vec![(nans[3], 0), (1.0, 1), (nans[0], 2), (-0.0, 3),
        (nans[5], 4), (0.0, 5), (f64::NEG_INFINITY, 6), (nans[6], 7)];
    tagged.sort_by(|a, b| moss_float_total_cmp(a.0, b.0));
    let order: Vec<usize> = tagged.iter().map(|t| t.1).collect();
    if order != vec![6, 3, 5, 1, 0, 2, 4, 7] { failures += 1; }
    println!("{}", failures);
}
"""


def test_native_nan_payload_bits():
    print("Native-only: NaN sign/payload bit patterns (not constructible in Moss)")
    header = (ROOT / "src" / "char_string_ordering_runtime.hpp").read_text()
    runtime = re.search(r'R"RUST\((.*?)\)RUST"', header, re.S).group(1)
    with tempfile.TemporaryDirectory(prefix="p25-s3-nan-", dir=SCRATCH) as td:
        source = Path(td) / "nan.rs"
        source.write_text("#![allow(dead_code)]\n" + runtime + RUST_NAN_PAYLOADS)
        binary = Path(td) / "nan"
        for flags in ([], ["-O"]):
            build = run(["rustc", *flags, str(source), "-o", str(binary)])
            assert build.returncode == 0, build.stderr
            result = run([str(binary)])
            assert result.returncode == 0 and result.stdout == "0\n", result.stdout + result.stderr
    passed("eight NaN payloads: one hash, isequal, unordered, stable-last (debug and -O)")


# --- Adversarial keys ---------------------------------------------------------

def test_adversarial_lawless_user_protocol():
    print("Adversarial: user less/isequal/hash that violate the laws")
    three_modes("""
type Key:
  code: Int

  fn less(other: Key) -> Bool:
    return true

  fn isequal(other: Key) -> Bool:
    return code != other.code

  fn hash() -> Int:
    return code

fn less(a: Int, b: Int) -> Bool:
  return true

fn isequal(a: Int, b: Int) -> Bool:
  return false

fn hash(a: Int) -> Int:
  return 0 - a

fn code_of(k: Key) -> Int:
  return k.code

fn main():
  echo less(2, 1)
  echo less(1, 1)
  echo isequal(3, 3)
  echo hash(4)
  echo Int.less(2, 1)
  echo Int.isequal(3, 3)
  let k = Key(code: 1)
  let j = Key(code: 1)
  echo k.less(k)
  echo k.isequal(j)
  echo k.hash() == j.hash()
  var v = [3, 1, 2]
  sort(v)
  echo v[0]
  echo v[2]
  var keys = [Key(code: 2), Key(code: 1), Key(code: 2), Key(code: 0)]
  sort(keys, by: code_of)
  echo keys[0].code
  echo keys[3].code
""", "user functions shadow less/isequal/hash; sort keeps the builtin total order",
        "true\ntrue\nfalse\n-4\nfalse\ntrue\ntrue\nfalse\ntrue\n1\n3\n0\n2\n")

    # A reusable law checker over a nominal key's structural methods: a lawful
    # key reports 0 failures, a lawless key reports the same count everywhere.
    law_checker = """
type {T}:
  code: Int

  fn isequal(other: {T}) -> Bool:
    return {EQ}

  fn hash() -> Int:
    return {HASH}

fn law_failures(values: Vector[{T}]) -> Int:
  var failures = 0
  for x in values:
    if not x.isequal(x):
      failures = failures + 1
    for y in values:
      if x.isequal(y) != y.isequal(x):
        failures = failures + 1
      if x.isequal(y) and x.hash() != y.hash():
        failures = failures + 1
      for z in values:
        if x.isequal(y) and y.isequal(z) and not x.isequal(z):
          failures = failures + 1
  return failures

fn main():
  let values = [{T}(code: 0), {T}(code: 10), {T}(code: 3), {T}(code: 13), {T}(code: -7), {T}(code: 5)]
  echo law_failures(values)
"""
    lawful = law_checker.replace("{T}", "Digit").replace(
        "{EQ}", "code - (code / 10) * 10 == other.code - (other.code / 10) * 10").replace(
        "{HASH}", "code - (code / 10) * 10")
    three_modes(lawful, "lawful nominal key methods satisfy the law suite", "0\n")
    lawless = law_checker.replace("{T}", "Near").replace(
        "{EQ}", "code - other.code < 4 and other.code - code < 4").replace("{HASH}", "code")
    out = three_modes(lawless, "lawless nominal key methods: identical failure count")
    assert out.strip() != "0", out


def test_adversarial_degenerate_pure_keys():
    print("Adversarial: degenerate but pure keys behave identically (stable)")
    three_modes("""
fn constant(x: String) -> Int:
  return 1

fn big(x: Int) -> Bool:
  return x > 4

fn to_nan(x: Float) -> Float:
  let zero = 0.0
  if x < 0.0:
    return zero / zero
  return x * 0.0

fn signed_zero(x: Int) -> Float:
  if x > 2:
    return -0.0
  return 0.0

fn uses_protocol(x: Float) -> Int:
  if less(x, 0.0) or isequal(x, -0.0):
    return hash(1)
  return hash(0)

fn show(v: Vector[Int]) -> String:
  var parts = Vector[String]()
  for x in v:
    parts.push(x.to_string())
  return ",".join(parts)

fn main():
  var w = ["d", "b", "c", "a"]
  sort(w, by: constant)
  echo ",".join(w)
  sort(w, by: Reverse(constant))
  echo ",".join(w)
  var v = [5, 3, 9, 1, 4, 7, 6]
  sort(v, by: big)
  echo show(v)
  sort(v, by: Reverse(big))
  echo show(v)
  sort(v, by: signed_zero)
  echo show(v)
  var fs = [2.0, -1.0, 0.0, -0.0, -5.0, 3.0]
  sort(fs, by: to_nan)
  echo fs[0]
  echo fs[1]
  echo fs[3]
  echo fs[5]
  sort(fs, by: uses_protocol)
  echo fs[0]
  echo fs[5]
""", "constant, Bool, NaN-producing, signed-zero and protocol-calling keys",
        # signed_zero: -0.0 (x > 2) sorts before +0.0, each group stable
        "d,b,c,a\nd,b,c,a\n3,1,4,5,9,7,6\n5,9,7,6,3,1,4\n5,9,7,6,3,4,1\n"
        "-0.0\n2.0\n3.0\n-5.0\n2.0\n-5.0\n")


def test_impure_keys_rejected():
    print("Adversarial: impure keys are rejected before lowering")
    rejected("""
fn inner(x: Int) -> Int:
  echo x
  return x

fn outer(x: Int) -> Int:
  return inner(x) + 1

fn main():
  var v = [2, 1]
  sort(v, by: outer)
""", "transitively printing key", "SORT_KEY_IMPURE")
    rejected("""
fn key(x: Int) -> Int:
  var file = FileIO.open("missing", ro)
  file.close()
  return x

fn main():
  var v = [2, 1]
  sort(v, by: key)
""", "key opening FileIO", "SORT_KEY_IMPURE")
    # A handler can send a domain message, but it is not a named sort key
    # function. Keep this distinction visible; it does not prove the frozen
    # SORT_KEY_IMPURE diagnostic for a message-sending selector.
    rejected("""
domain Worker:
  fn Get() -> Int:
    reply 1

domain Runner:
  domainroutes(worker: Worker)

  fn Key(x: Int) -> Int:
    reply message worker.Get()

  fn Run():
    var v = [2, 1]
    sort(v, by: Key)

fn main():
  let w = Worker()
  let r = Runner(worker: w)
  message r.Run()
""", "message-sending handler is not a sort selector", "SORT_KEY_CALLABLE")
    rejected("""
enum Bad:
  Oops

fn key(x: Int) -> Int:
  if x > 5:
    raise Bad.Oops
  return x

fn main():
  var v = [2, 1]
  try:
    sort(v, by: key)
  recover:
    echo 0
""", "raising key", "SORT_KEY_IMPURE")
    rejected("""
fn key(xs: Vector[Int]) -> Int:
  return xs[5]

fn main():
  var v = [[2], [1]]
  sort(v, by: key)
""", "possibly panicking key", "SORT_KEY_IMPURE")
    rejected("""
fn key(xs: Vector[Int]) -> Int:
  xs.push(1)
  return 0

fn main():
  var v = [[2], [1]]
  sort(v, by: key)
""", "key writing its non-Copy argument", "SORT_KEY_IMPURE")
    rejected("""
fn key(x: Int) -> Int:
  var i = 0
  while true:
    i = i + 1
  return x

fn main():
  var v = [2, 1]
  sort(v, by: key)
""", "divergent key", "SORT_KEY_IMPURE")
    rejected("""
fn shout(s: String) -> String:
  echo s
  return s

fn main():
  var v = ["b", "a"]
  sort(v, by: Reverse(shout))
""", "impure key under Reverse", "SORT_KEY_IMPURE")
    rejected("""
type K:
  code: Int

fn wrap(x: Int) -> K:
  return K(code: x)

fn main():
  var v = [2, 1]
  sort(v, by: wrap)
""", "key returning a record", "SORT_KEY_UNORDERED")
    rejected("""
type Key:
  code: Int

  fn less(other: Key) -> Bool:
    return code < other.code

fn main():
  var v = [Key(code: 2), Key(code: 1)]
  sort(v)
""", "structural less method does not make a record sortable", "SORT_KEY_UNORDERED")
    rejected("""
fn key(x: Int) -> Int:
  return x

fn main():
  var v = [2, 1, 3]
  sort(v, by: Reverse(Reverse(key)))
""", "nested Reverse", "SORT_KEY_CALLABLE")
    rejected("""
type K:
  code: Int

fn main():
  echo hash(K(code: 1))
""", "builtin hash of a record", "KEY_PROTOCOL_TYPE")
    rejected("""
fn main():
  echo isequal('a', "a")
""", "isequal across Char/String", "KEY_PROTOCOL_TYPE")


def test_impure_imported_key_rejected():
    print("Adversarial: impure key imported from another module")
    with tempfile.TemporaryDirectory(prefix="p25-s3-impure-", dir=SCRATCH) as td:
        project = Path(td) / "noisy"
        write_tree(project, {
            "Moss.toml": '[package]\nname = "noisy"\nversion = "0.1.0"\n',
            "src/keys.moss": "module keys\n\nexport fn loud(x: Int) -> Int:\n  echo x\n  return x\n",
            "src/main.moss": "module app\nimport keys\n\nfn main():\n  var v = [2, 1]\n  sort(v, by: keys.loud)\n",
        })
        # `moss check` takes the entry file; given the project directory it
        # currently reports ok without checking anything (see the S3 doc).
        result = run([str(MOSS), "check", str(project / "src" / "main.moss"), "--json"])
        assert result.returncode != 0, result.stdout
        assert "SORT_KEY_IMPURE" in diagnostic_codes(result.stdout), result.stdout
        for args in (["build"], ["debug"]):
            build = run([str(MARGO), *args], cwd=project)
            assert build.returncode != 0 and "SORT_KEY_IMPURE" in build.stdout + build.stderr, build
            assert build.stdout.strip() == "" or "SORT_KEY_IMPURE" in build.stdout, build.stdout
    passed("imported printing key -> SORT_KEY_IMPURE (check, margo build, margo debug)")


# --- Hash stability ----------------------------------------------------------

def test_hash_stability():
    print("Built-in scalar hash values are identical in every engine")
    three_modes("""
fn main():
  let zero = 0.0
  echo hash(0)
  echo hash(-1)
  echo hash(9223372036854775807)
  echo hash(1.5)
  echo hash(-1.5)
  echo hash(0.0)
  echo hash(-0.0)
  echo hash(zero / zero)
  echo hash(1.0 / zero)
  echo hash(true)
  echo hash(false)
  echo hash('A')
  echo hash('😀')
  echo hash("")
  echo hash("moss")
  echo hash("é")
  echo "a".hash() == hash("a")
  echo String.hash("a") == hash("a")
  echo 1.5.hash() == hash(1.5)
""", "pinned hash values (Int identity, Float bits, Char code point, String FNV-1a)",
        "\n".join(str(v) for v in (
            0, -1, 9223372036854775807, float_bits(1.5), float_bits(-1.5),
            0, -(1 << 63), 0x7ff8000000000000, float_bits(float("inf")),
            1, 0, 65, 0x1F600, fnv1a(""), fnv1a("moss"), fnv1a("é"))) +
        "\ntrue\ntrue\ntrue\n")


def signed64(value):
    return value - (1 << 64) if value >= (1 << 63) else value


def float_bits(value):
    return struct.unpack("<q", struct.pack("<d", value))[0]


def fnv1a(text):
    """Independent reference for the String hash: FNV-1a 64 over UTF-8."""
    h = 0xcbf29ce484222325
    for byte in text.encode("utf-8"):
        h = ((h ^ byte) * 0x100000001b3) & ((1 << 64) - 1)
    return signed64(h)


def main():
    print("=== Phase 25 Team S S3 key-protocol scalar suite ===")
    test_imported_key_functions()
    test_untyped_member_key_across_packages()
    test_concrete_specializations()
    test_non_copy_keys()
    test_nan_values()
    test_native_nan_payload_bits()
    test_adversarial_lawless_user_protocol()
    test_adversarial_degenerate_pure_keys()
    test_impure_keys_rejected()
    test_impure_imported_key_rejected()
    test_hash_stability()
    assert len(CASES) == len(set(CASES)), "duplicate case names"
    print(f"=== All Phase 25 S3 key-protocol cases passed ({len(CASES)} cases) ===")


if __name__ == "__main__":
    main()
