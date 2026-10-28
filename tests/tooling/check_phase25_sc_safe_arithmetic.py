"""Phase 25 Team S S-C: proven-safe Int division/remainder and key purity.

Usage: python3 tests/tooling/check_phase25_sc_safe_arithmetic.py path/to/moss

The observable-effect analysis (`observable_expression_effects` in
`src/moss.cpp`) classifies Int `/` and `%` as panic-free when the divisor is a
proven-safe constant: an Int literal, optionally parenthesized or signed,
whose value is neither 0 nor -1. Every other Int divisor stays `may_panic`.
That one analysis feeds `moss effects`/`inspect`, `.mossi` effect summaries,
`SORT_KEY_IMPURE`, domain-initializer purity, and fusion. This suite checks:

- effect classification for safe and unsafe divisors (and that `char_at` and
  indexing stay conservative);
- sort keys: `x % 10` and `x / 10` accepted; `x % y`, `x % 0`, `x / -1`
  remain `SORT_KEY_IMPURE` in check, Fast Debug, and native lowering;
- lawful nominal `isequal`/`hash` methods using `%` and `/` are pure and
  give identical law-check results in all three engines;
- the Int.MIN / -1 and Int.MIN % -1 runtime rule (wraps to Int.MIN and 0)
  and division/remainder by zero (fatal) agree across engines;
- a compiled-only provider's `.mossi` summary carries `may_panic=0` for the
  safe functions and methods and `may_panic=1` for `x / -1`;
- fusion now fuses through a proven-safe `%` callback, with equal output.
"""

import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]
MOSS = Path(sys.argv[1] if len(sys.argv) > 1 else "./moss").resolve()
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


def native(source, td, mode):
    moss_flag, rustc_flags = ("--debug", []) if mode == "native-debug" else ("-O", ["-O"])
    rust = Path(td) / f"{mode}.rs"
    binary = Path(td) / mode
    gen = run([str(MOSS), str(source), moss_flag, "-o", str(rust)])
    assert gen.returncode == 0, f"{mode} lowering failed:\n{gen.stderr}"
    build = run(["rustc", *rustc_flags, "--crate-name", "case", str(rust), "-o", str(binary)])
    assert build.returncode == 0, f"{mode} rustc failed:\n{build.stderr[:4000]}"
    return run([str(binary)])


def three_modes(code, name, expected=None):
    """Run standalone source in all three engines; require identical stdout."""
    with tempfile.TemporaryDirectory(prefix="p25-sc-", dir=SCRATCH) as td:
        source = Path(td) / "case.moss"
        source.write_text(code)
        outputs = {}
        interp = run([str(MOSS), "run", "--interp", str(source)])
        assert interp.returncode == 0, f"[{name}] Fast Debug failed:\n{interp.stdout}{interp.stderr}"
        outputs["fast-debug"] = interp.stdout
        for mode in ("native-debug", "native-release"):
            result = native(source, td, mode)
            assert result.returncode == 0, f"[{name}] {mode} run failed:\n{result.stderr}"
            outputs[mode] = result.stdout
        assert len(set(outputs.values())) == 1, f"[{name}] engine disagreement: {outputs}"
        if expected is not None:
            assert outputs["fast-debug"] == expected, (
                f"[{name}] expected {expected!r}, got {outputs['fast-debug']!r}")
        passed(name)
        return outputs["fast-debug"]


def three_modes_fail(code, name, before):
    """All three engines print `before` and then fail at run time."""
    with tempfile.TemporaryDirectory(prefix="p25-sc-", dir=SCRATCH) as td:
        source = Path(td) / "case.moss"
        source.write_text(code)
        check = run([str(MOSS), "check", str(source), "--json"])
        assert check.returncode == 0, f"[{name}] check failed:\n{check.stdout}"
        interp = run([str(MOSS), "run", "--interp", str(source)])
        assert interp.returncode != 0 and interp.stdout == before, (
            f"[{name}] Fast Debug: rc={interp.returncode} {interp.stdout!r} {interp.stderr}")
        for mode in ("native-debug", "native-release"):
            result = native(source, td, mode)
            assert result.returncode != 0 and result.stdout == before, (
                f"[{name}] {mode}: rc={result.returncode} {result.stdout!r}")
        passed(name)


def diagnostic_codes(stdout):
    payload = json.loads(stdout)
    codes = [(payload.get("error") or {}).get("code")]
    codes += [d.get("code") for d in (payload.get("result") or {}).get("diagnostics", []) or []]
    return [c for c in codes if c]


def rejected(code, name, code_expected):
    """`moss check` rejects with the code; Fast Debug and lowering also refuse."""
    with tempfile.TemporaryDirectory(prefix="p25-sc-", dir=SCRATCH) as td:
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


def accepted(code, name):
    with tempfile.TemporaryDirectory(prefix="p25-sc-", dir=SCRATCH) as td:
        source = Path(td) / "case.moss"
        source.write_text(code)
        result = run([str(MOSS), "check", str(source), "--json"])
        assert result.returncode == 0 and json.loads(result.stdout)["ok"], (
            f"[{name}] rejected:\n{result.stdout}")
        passed(name)


def effects_of(source, target):
    result = run([str(MOSS), "effects", target, "--source", str(source), "--json"])
    assert result.returncode == 0, f"effects {target} failed:\n{result.stdout}{result.stderr}"
    payload = json.loads(result.stdout)["result"]
    summary = payload["effect_summary"]
    assert summary["status"] == "available", summary
    return payload["observable_effects"], summary["transitive"]


# --- Effect classification ------------------------------------------------------

CLASSIFY = """
fn mod10(x: Int) -> Int:
  return x % 10

fn div10(x: Int) -> Int:
  return x / 10

fn nested(x: Int) -> Int:
  return (x / 10) % 10 + x % 100 / 7 * 3

fn parens(x: Int) -> Int:
  return x % (10) + x / (-7) + x % -2 + x / +3

fn big(x: Int) -> Int:
  return x / 9223372036854775807 + x % -9223372036854775807

fn fdiv(x: Float) -> Float:
  return x / 0.0

fn by_var(x: Int) -> Int:
  let y = 7
  return x % y

fn by_param(x: Int, y: Int) -> Int:
  return x / y

fn by_zero(x: Int) -> Int:
  return x % 0

fn by_paren_zero(x: Int) -> Int:
  return x / (0)

fn by_neg_one(x: Int) -> Int:
  return x / -1

fn rem_neg_one(x: Int) -> Int:
  return x % -1

fn by_paren_neg_one(x: Int) -> Int:
  return x % (-1)

fn by_folded_sum(x: Int) -> Int:
  return x / (5 + 5)

fn by_call(x: Int) -> Int:
  return x % mod10(13)

fn inner_unsafe(x: Int) -> Int:
  return (x / 0) % 10

fn char_first(s: String) -> Char:
  return s.char_at(0)

fn first(xs: Vector[Int]) -> Int:
  return xs[0]

fn main():
  echo mod10(13)
"""

SAFE = ("mod10", "div10", "nested", "parens", "big", "fdiv")
UNSAFE = ("by_var", "by_param", "by_zero", "by_paren_zero", "by_neg_one",
          "rem_neg_one", "by_paren_neg_one", "by_folded_sum", "by_call",
          "inner_unsafe", "char_first", "first")


def test_effect_classification():
    print("Effect classification of Int / and % (moss effects JSON)")
    with tempfile.TemporaryDirectory(prefix="p25-sc-eff-", dir=SCRATCH) as td:
        source = Path(td) / "case.moss"
        source.write_text(CLASSIFY)
        for name in SAFE + UNSAFE:
            direct, transitive = effects_of(source, f"fn:{name}")
            expect_panic = name in UNSAFE
            for label, facts in (("observable_effects", direct), ("transitive", transitive)):
                assert facts["may_panic"] is expect_panic, (
                    f"{name} {label}: may_panic={facts['may_panic']}, expected {expect_panic}")
                assert not facts["unresolved"] and not facts["raise_set"], (name, facts)
                assert not facts["may_diverge"] and not facts["external_io"], (name, facts)
        passed(f"proven-safe divisors are panic-free ({len(SAFE)} functions)")
        passed(f"unknown, zero, -1, and unfolded divisors stay may_panic ({len(UNSAFE) - 2} functions)")
        passed("char_at and indexing stay may_panic")


# --- Sort keys ------------------------------------------------------------------

def test_sort_keys():
    print("Sort keys using Int / and %")
    three_modes("""
fn digit(x: Int) -> Int:
  return x % 10

fn tens(x) -> Int:
  return x / 10

fn show(v: Vector[Int]) -> String:
  var parts = Vector[String]()
  for i in range(0, v |> count):
    parts.push(v[i].to_string())
  return ",".join(parts)

fn main():
  var v = [42, 17, 93, 5, 120, -13, 31]
  sort(v, by: digit)
  echo show(v)
  sort(v, by: Reverse(tens))
  echo show(v)
""", "sort by x % 10 and untyped Reverse(x / 10) accepted, all engines",
        "-13,120,31,42,93,5,17\n120,93,42,31,17,5,-13\n")
    for body, label in (("let y = 7\n  return x % y", "x % y"),
                        ("return x % 0", "x % 0"),
                        ("return x / -1", "x / -1"),
                        ("return x % -1", "x % -1"),
                        ("return x / (5 + 5)", "x / (5 + 5)")):
        rejected(f"""
fn key(x: Int) -> Int:
  {body}

fn main():
  var v = [3, 1, 2]
  sort(v, by: key)
""", f"sort key {label}", "SORT_KEY_IMPURE")
    rejected("""
fn key(s: String) -> Char:
  return s.char_at(0)

fn main():
  var v = ["b", "a"]
  sort(v, by: key)
""", "sort key s.char_at(0) stays conservative", "SORT_KEY_IMPURE")


# --- Nominal key methods -----------------------------------------------------------

LAW_CHECKER = """
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
  let values = [{T}(code: 0), {T}(code: 10), {T}(code: 3), {T}(code: 13), {T}(code: -7), {T}(code: 5), {T}(code: 23), {T}(code: -9223372036854775807 - 1)]
  echo law_failures(values)
"""


def test_nominal_key_methods():
    print("Nominal isequal/hash methods using proven-safe % and /")
    lawful = LAW_CHECKER.replace("{T}", "Digit").replace(
        "{EQ}", "code % 10 == other.code % 10").replace(
        "{HASH}", "code % 10 + (code % 10) / 10")
    lawless = LAW_CHECKER.replace("{T}", "Bucket").replace(
        "{EQ}", "code / 10 == other.code / 10").replace("{HASH}", "code % 10")
    with tempfile.TemporaryDirectory(prefix="p25-sc-key-", dir=SCRATCH) as td:
        for text, type_name in ((lawful, "Digit"), (lawless, "Bucket")):
            source = Path(td) / f"{type_name}.moss"
            source.write_text(text)
            for method in ("isequal", "hash"):
                direct, transitive = effects_of(source, f"method:{type_name}.{method}")
                for facts in (direct, transitive):
                    assert facts == {
                        "local_capture_read": False, "local_mutation": False,
                        "domain_read": False, "domain_write": False, "message": False,
                        "external_io": False, "may_panic": False, "raise_set": [],
                        "may_diverge": False, "unresolved": False}, (type_name, method, facts)
            # inspect agrees with effects at the method declaration line.
            line = text.splitlines().index("  fn isequal(other: %s) -> Bool:" % type_name) + 1
            inspect = run([str(MOSS), "inspect", f"{source}:{line}", "--json"])
            payload = json.loads(inspect.stdout)["result"]
            assert payload["target"]["semantic_identity"].startswith(f"method:{type_name}.isequal"), payload["target"]
            assert payload["observable_effects"]["may_panic"] is False, payload["observable_effects"]
            ownership = {p["name"]: p["effect"] for p in payload["ownership"]}
            assert ownership == {"self": "READ", "other": "READ"}, ownership
    passed("isequal/hash with code % 10 and code / 10 are pure and READ-only (effects, inspect)")
    three_modes(lawful, "lawful % key: 0 law failures in all engines", "0\n")
    out = three_modes(lawless, "lawless / vs % key: identical failure count in all engines")
    assert out.strip() != "0", out


# --- Runtime rule ---------------------------------------------------------------------

def test_int_min_runtime():
    print("Int.MIN / -1, Int.MIN % -1, truncation, and zero divisors at run time")
    three_modes("""
fn neg1() -> Int:
  return 0 - 1

fn div(a: Int, b: Int) -> Int:
  return a / b

fn rem(a: Int, b: Int) -> Int:
  return a % b

fn halve(x: Int) -> Int:
  return x / -1

fn main():
  let m = -9223372036854775807 - 1
  let d = neg1()
  echo m / d
  echo m % d
  echo div(m, -1)
  echo rem(m, -1)
  echo m / -1
  echo m % -1
  echo halve(m)
  echo -7 / 2, -7 % 2, 7 / -2, 7 % -2, -7 % 10, m % 10, m / 10
  let xs = [m, 5, -5]
  echo xs |> map(halve) |> sum
""", "Int.MIN / -1 wraps to Int.MIN and Int.MIN % -1 is 0 in every engine",
        "-9223372036854775808\n0\n-9223372036854775808\n0\n-9223372036854775808\n0\n"
        "-9223372036854775808\n-3 -1 -3 1 -7 -8 -922337203685477580\n"
        "-9223372036854775808\n")
    for op, label in (("/", "division"), ("%", "remainder")):
        three_modes_fail(f"""
fn zero() -> Int:
  return 0

fn main():
  echo 1
  echo 7 {op} zero()
  echo 2
""", f"Int {label} by zero is fatal in every engine", "1\n")


# --- Domain initializers ------------------------------------------------------------------

def test_domain_initializer():
    print("Domain state initializers use the same classification")
    three_modes("""
fn seed() -> Int:
  return 1234 % 100 + 1234 / 100

domain Counter:
  value = seed()

  fn Get() -> Int:
    reply value

fn main():
  c = Counter()
  echo message c.Get()
""", "initializer calling a % 100 / 100 helper accepted", "46\n")
    with tempfile.TemporaryDirectory(prefix="p25-sc-init-", dir=SCRATCH) as td:
        source = Path(td) / "case.moss"
        source.write_text("""
fn seed() -> Int:
  return 1234 / -1

domain Counter:
  value = seed()

  fn Get() -> Int:
    reply value

fn main():
  c = Counter()
  echo message c.Get()
""")
        result = run([str(MOSS), "check", str(source), "--json"])
        assert result.returncode != 0, result.stdout
        assert "side-effect-free" in result.stdout and "failing" in result.stdout, result.stdout
    passed("initializer calling an x / -1 helper still rejected")


# --- Fusion ----------------------------------------------------------------------------

def test_fusion():
    print("Fusion through a proven-safe % callback")
    code = """
fn digit(x: Int) -> Int:
  return x % 10

fn odd(x: Int) -> Bool:
  return x % 2 == 1

fn risky(x: Int) -> Int:
  return x / -1

fn main():
  let v = [12, 7, 33, 41, -19, 120]
  echo v |> map(digit) |> filter(odd) |> sum
  echo v |> map(risky) |> filter(odd) |> sum
"""
    with tempfile.TemporaryDirectory(prefix="p25-sc-fuse-", dir=SCRATCH) as td:
        source = Path(td) / "case.moss"
        source.write_text(code)
        explain = run([str(MOSS), str(source), "-O", "--explain-fusion", "-o", str(Path(td) / "o.rs")])
        assert explain.returncode == 0, explain.stderr
        text = explain.stdout + explain.stderr
        blocks = text.split("Pipeline %")
        safe = next(b for b in blocks if "line 13\n" in b)
        risky = next(b for b in blocks if "line 14\n" in b)
        assert "decision: fused stages 1-3" in safe, safe
        assert "possible panic ordering" in risky, risky
    passed("proven-safe % pipeline fuses; x / -1 pipeline still stops at panic ordering")
    three_modes(code, "fused and unfused pipelines agree in all engines", "11\n19\n")


# --- Compiled-only provider ------------------------------------------------------------

PROVIDER = """module keys

export type Key:
  code: Int

  fn isequal(other: Key) -> Bool:
    return code % 10 == other.code % 10

  fn hash() -> Int:
    return code % 10

export fn digit(x: Int) -> Int:
  return x % 10

export fn tens(x: Int) -> Int:
  return (x / 10) % 10

export fn risky(x: Int) -> Int:
  return x / -1
"""

CONSUMER = """module app
import keys

fn main():
  var v = [42, 17, 93, 5, 120]
  sort(v, by: keys.digit)
  echo v[0], v[1], v[2], v[3], v[4]
  sort(v, by: keys.tens)
  echo v[0], v[1], v[2], v[3], v[4]
  let a = keys.Key(code: 13)
  let b = keys.Key(code: 23)
  echo a.isequal(b), a.hash() == b.hash()
"""

CONSUMER_EXPECTED = "120 42 93 5 17\n5 17 120 42 93\ntrue true\n"


def mossi_effects(text, prefix):
    for line in text.splitlines():
        if line.strip().startswith(prefix):
            return dict(item.split("=", 1) for item in
                        line.split(" effects=", 1)[1].split(" ", 1)[0].split(";"))
    raise AssertionError(f"no .mossi record starting with {prefix!r}")


def test_compiled_only_provider():
    print("Compiled-only provider: .mossi summaries carry the classification")
    with tempfile.TemporaryDirectory(prefix="p25-sc-sf-", dir=SCRATCH) as td:
        project = Path(td) / "sf"
        (project / "src").mkdir(parents=True)
        (project / "Moss.toml").write_text('[package]\nname = "sfkeys"\nversion = "0.1.0"\n')
        (project / "src" / "keys.moss").write_text(PROVIDER)
        (project / "src" / "main.moss").write_text(CONSUMER)
        first = run([str(MOSS), "build", "--json"], cwd=project)
        assert first.returncode == 0, first.stdout + first.stderr
        mossi = (project / "build" / "debug" / "keys.mossi").read_text()
        for prefix, panic in (("export fn digit ", "0"), ("export fn tens ", "0"),
                              ("export fn risky ", "1"), ("object_method isequal ", "0"),
                              ("object_method hash ", "0")):
            facts = mossi_effects(mossi, prefix)
            assert facts["may_panic"] == panic, (prefix, facts)
            assert facts["unresolved"] == "0" and facts["raise_set"] == "-", (prefix, facts)
        passed("keys.mossi: digit/tens/isequal/hash may_panic=0, risky may_panic=1")
        # Keep package source available for R2 interface freshness validation.
        second = run([str(MOSS), "build", "--json"], cwd=project)
        assert second.returncode == 0, second.stdout + second.stderr
        artifact = json.loads(second.stdout)["result"]["artifacts"]["executable"]
        result = run([artifact])
        assert result.returncode == 0 and result.stdout == CONSUMER_EXPECTED, (
            result.stdout + result.stderr)
        passed("source-free consumer sorts by imported % and / keys")
        (project / "src" / "main.moss").write_text(
            "module app\nimport keys\n\nfn main():\n  var v = [2, 1]\n  sort(v, by: keys.risky)\n")
        third = run([str(MOSS), "build", "--json"], cwd=project)
        assert third.returncode != 0 and "SORT_KEY_IMPURE" in third.stdout + third.stderr, (
            third.stdout + third.stderr)
        passed("source-free imported x / -1 key -> SORT_KEY_IMPURE")


def main():
    print("=== Phase 25 Team S S-C proven-safe arithmetic suite ===")
    assert shutil.which("rustc"), "rustc is required"
    test_effect_classification()
    test_sort_keys()
    test_nominal_key_methods()
    test_int_min_runtime()
    test_domain_initializer()
    test_fusion()
    test_compiled_only_provider()
    assert len(CASES) == len(set(CASES)), "duplicate case names"
    print(f"=== All Phase 25 S-C safe-arithmetic cases passed ({len(CASES)} cases) ===")


if __name__ == "__main__":
    main()
