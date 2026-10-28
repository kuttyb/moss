"""Phase 25 Team S workstream S4: adversarial Fast Debug parity regressions.

Each defect found by the S4 adversarial pass (P25-S-S4-NNN) is pinned here.
Accepted programs must produce identical stdout in Fast Debug, Native Debug,
and Native Release; programs that are invalid must be rejected by `moss check`
with a structured diagnostic before any engine runs them.

Usage: python3 tests/tooling/check_phase25_s4_fast_debug_adversarial.py ./moss
"""

import json
from pathlib import Path
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]
MOSS = Path(sys.argv[1] if len(sys.argv) > 1 else "./moss").resolve()
SCRATCH = ROOT / "tmp"
SCRATCH.mkdir(exist_ok=True)


def run(cmd, cwd=ROOT, timeout=600):
    return subprocess.run(cmd, cwd=cwd, capture_output=True, text=True, timeout=timeout)


def three_modes(code, name, expected):
    """Run standalone source in all three engines; require identical stdout."""
    with tempfile.TemporaryDirectory(prefix="p25-s4-", dir=SCRATCH) as td:
        source = Path(td) / "case.moss"
        source.write_text(code)
        check = run([str(MOSS), "check", str(source), "--json"])
        assert check.returncode == 0, f"[{name}] check rejected:\n{check.stdout}{check.stderr}"
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
        assert outputs["fast-debug"] == expected, (
            f"[{name}] expected {expected!r}, got {outputs['fast-debug']!r}")
        print(f"  PASS: {name}")


def rejected(code, name, code_expected):
    with tempfile.TemporaryDirectory(prefix="p25-s4-", dir=SCRATCH) as td:
        source = Path(td) / "case.moss"
        source.write_text(code)
        result = run([str(MOSS), "check", str(source), "--json"])
        assert result.returncode != 0, f"[{name}] accepted:\n{result.stdout}"
        payload = json.loads(result.stdout)
        codes = [payload.get("error", {}).get("code")]
        codes += [d.get("code") for d in payload.get("result", {}).get("diagnostics", [])]
        assert code_expected in codes, f"[{name}] expected {code_expected}, got {codes}"
        interp = run([str(MOSS), "run", "--interp", str(source)])
        assert interp.returncode != 0 and interp.stdout == "", (
            f"[{name}] Fast Debug ran a rejected program:\n{interp.stdout}")
        print(f"  PASS: {name} -> {code_expected}")


def test_try_join_binding_after_raise():
    print("P25-S-S4-001: try/recover join binding first assigned by a raising call")
    three_modes("""
enum F:
  Bad

fn boom(b: Bool) -> Int:
  if b:
    raise F.Bad
  return 1

fn maybe(b: Bool):
  if b:
    raise F.Bad

fn pick(b: Bool) -> Int:
  try:
    let v = boom(b)
  recover F.Bad:
    let v = 50
  return v

fn late(b: Bool) -> String:
  if b:
    try:
      maybe(b)
      let s = "body"
    recover F.Bad:
      let s = "arm"
  else:
    let s = "else"
  return s

fn main():
  echo pick(true)
  echo pick(false)
  echo late(true)
  echo late(false)
""", "join binding initialized after the raise point", "50\n1\narm\nelse\n")


def test_branch_bindings_shadowing_state():
    print("P25-S-S4-002/003: branch-local bindings that shadow state, parameters, or later names")
    three_modes("""
domain Leading:
  count = 5
  total = 1

  fn Bump(flag: Bool) -> Int:
    if flag:
      let count = 99
      echo count
    count = count + 1
    reply count

  fn Get(flag: Bool) -> Int:
    if flag:
      let count = 99
      echo count
    reply count

  fn Param(n: Int) -> Int:
    if n > 0:
      let n = 1000
      echo n
    total = total + n
    reply total

  fn Later(flag: Bool) -> Int:
    if flag:
      let t = 10
      echo t
    let t = 3
    total = total + t
    reply total

  fn Joined(flag: Bool) -> Int:
    if flag:
      let r = count * 10
    else:
      let r = total * 100
    reply r

domain Joins:
  total = 1

  fn Run(flag: Bool) -> Int:
    echo "s"
    if flag:
      let total = 10
      echo total
    else:
      let total = 20
      echo total
    total = total + 100
    reply total

  fn Peek() -> Int:
    reply total

fn main():
  d = Leading()
  j = Joins()
  echo message d.Bump(true)
  echo message d.Bump(false)
  echo message d.Get(true)
  echo message d.Param(5)
  echo message d.Later(true)
  echo message d.Joined(true)
  echo message d.Joined(false)
  echo message j.Run(true)
  echo message j.Run(false)
  echo message j.Peek()
""", "leading split and join scoping", "99\n6\n7\n99\n7\n1000\n6\n10\n9\n70\n900\n"
        "s\n10\n101\ns\n20\n201\n201\n")


def test_joined_bindings_remain_assignable():
    three_modes("""
enum E:
  A
  B

fn f(flag: Bool) -> Int:
  if flag:
    var r = 10
  else:
    var r = 20
  r = r + 1
  return r

domain D:
  a = 7

  fn Run(e: E) -> Int:
    echo "x"
    match e:
      case A:
        let a = 1
      case B:
        let a = 2
    a = a + 10
    reply a

fn main():
  d = D()
  echo f(true)
  echo message d.Run(E.A)
  echo message d.Run(E.B)
""", "joined var and state shadowed on every arm stay assignable", "11\nx\n17\nx\n27\n")


def test_wrapping_unary_minus():
    print("P25-S-S4-005/012: unary minus wraps, infers its operand type, and negated literals are Int")
    three_modes("""
type P:
  v: Int

domain D:
  n = -9223372036854775807 - 1

  fn Flip() -> Int:
    n = -n
    reply n

fn f(x: Int) -> Int:
  return x

fn neg(x):
  return -x

fn main():
  d = D()
  let lo = -9223372036854775807 - 1
  let p = P(v: lo)
  echo -lo
  echo -p.v
  echo 1 - -lo
  echo -lo * 1
  echo - lo
  echo - 3000000000
  echo -  9223372036854775808
  echo f(-9223372036854775808)
  let fl = 2.5
  echo -fl
  echo message d.Flip()
  echo neg(lo)
  echo neg(2.5)
  let v = [-fl, 1.0]
  echo v[0]
""", "Int minimum negation", "-9223372036854775808\n-9223372036854775808\n"
        "-9223372036854775807\n-9223372036854775808\n-9223372036854775808\n"
        "-3000000000\n-9223372036854775808\n-9223372036854775808\n-2.5\n"
        "-9223372036854775808\n-9223372036854775808\n-2.5\n-2.5\n")


def test_int_literal_range():
    print("P25-S-S4-006: Int literals outside the 64-bit range are rejected")
    for literal in ("9223372036854775808", "[9223372036854775808]", "5 - 9223372036854775808",
                    "5 -9223372036854775808", "99999999999999999999"):
        rejected(f"fn main():\n  echo {literal}\n", f"literal {literal}", "INT_LITERAL_OUT_OF_RANGE")
    three_modes("""
fn main():
  echo 9223372036854775807
  echo -9223372036854775808
  echo (-9223372036854775808)
  echo 1 + -9223372036854775808
  echo 0009223372036854775807
  echo 9223372036854775808.5
""", "boundary literals", "9223372036854775807\n-9223372036854775808\n"
        "-9223372036854775808\n-9223372036854775807\n9223372036854775807\n"
        "9.223372036854776e18\n")


def test_message_result_writes_state():
    print("P25-S-S4-007/008: `name = message ...` assigns the existing binding")
    three_modes("""
enum F:
  Worse(code: Int)

type P:
  x: Int
  name: String

domain W:
  n = 0

  fn Num(k: Int) -> Int:
    n = n + k
    reply n

  fn Risky(k: Int) -> Int:
    n = n + k
    if k > 5:
      raise F.Worse(code: k)
    reply n

  fn Text(k: Int) -> String:
    reply "t" + k.to_string()

  fn Point(k: Int) -> P:
    reply P(x: k, name: "p")

  fn Items(k: Int) -> Vector[Int]:
    reply [k, k + 1]

domain Boss:
  domainroutes(w: W)
  seen = 0
  label = ""
  pt = P(x: 0, name: "")
  items = Vector[Int]()

  fn Go(k: Int):
    seen = message w.Num(k)
    label = message w.Text(k)
    pt = message w.Point(k)
    items = message w.Items(k)

  fn GoIf(k: Int) -> Int:
    if k > 0:
      seen = message w.Num(k)
    reply seen

  fn Guarded(k: Int) -> Int:
    try:
      seen = message w.Risky(k)
    recover F.Worse(code):
      seen = 0 - code
    reply seen

  fn Show() -> String:
    reply seen.to_string() + label + pt.x.to_string() + pt.name + (items |> count).to_string()

fn main():
  w = W()
  b = Boss(w: w)
  message b.Go(4)
  echo message b.Show()
  echo message b.GoIf(1)
  echo message b.Guarded(1)
  echo message b.Guarded(9)
  echo message b.Show()
""", "message results written to domain state", "4t44p2\n5\n6\n-9\n-9t44p2\n")
    with tempfile.TemporaryDirectory(prefix="p25-s4-", dir=SCRATCH) as td:
        source = Path(td) / "payload.moss"
        source.write_text("""domain W:
  fn Num(k: Int) -> Int:
    reply k + 1

domain B:
  domainroutes(w: W)

  fn Go(k: Int) -> Int:
    k = message w.Num(k)
    reply k

fn main():
  w = W()
  b = B(w: w)
  echo message b.Go(3)
""")
        result = run([str(MOSS), "check", str(source), "--json"])
        assert result.returncode != 0 and "cannot WRITE incoming message payload" in result.stdout, result.stdout
    print("  PASS: message result into an incoming payload is rejected")


def test_try_raise_edge_identities():
    print("P25-S-S4-009/010: raise edges keep state and Copy outer bindings available")
    three_modes("""
enum F:
  Bad

fn boom(b: Bool) -> Int:
  if b:
    raise F.Bad
  return 1

domain S:
  n = 0

  fn Run(b: Bool) -> Int:
    try:
      n = n + 1
      if b:
        raise F.Bad
      reply n
    recover F.Bad:
      n = n + 10
    reply n * 100

fn main():
  s = S()
  echo message s.Run(false)
  echo message s.Run(true)
  var x = 1
  try:
    x = 5
    let x = 2
    echo x
    let y = boom(true)
    echo y
  recover F.Bad:
    echo x
    let x = 3
    echo x
  echo x
""", "state write and Copy shadow before a raise", "1\n1200\n2\n5\n3\n5\n")


def checker_rejected(code, name, code_expected):
    """Source the checker rejects before any engine runs (S-B display rule)."""
    with tempfile.TemporaryDirectory(prefix="p25-s4-", dir=SCRATCH) as td:
        source = Path(td) / "case.moss"
        source.write_text(code)
        check = run([str(MOSS), "check", str(source), "--json"])
        error = json.loads(check.stdout).get("error") or {}
        assert check.returncode != 0 and error.get("code") == code_expected, (
            f"[{name}] check: {check.stdout}")
        interp = run([str(MOSS), "run", "--interp", str(source)])
        assert interp.returncode != 0 and code_expected in interp.stderr and not interp.stdout, (
            f"[{name}] Fast Debug: {interp.stdout}{interp.stderr}")
        for flag in ("--debug", "-O"):
            gen = run([str(MOSS), str(source), flag, "-o", str(Path(td) / "case.rs")])
            assert gen.returncode != 0 and code_expected in gen.stderr, (
                f"[{name}] {flag} lowering: {gen.returncode} {gen.stderr}")
        print(f"  PASS: {name} -> {code_expected}")


def test_echo_display():
    # History: P25-S-S4-011 found that native echo of a Vector, Map, or record
    # gave rustc E0277, and S4 reported it as the native-lowering diagnostic
    # NATIVE_ECHO_DISPLAY_UNSUPPORTED. That made project-mode check (which ran
    # lowering) disagree with standalone check (P25-S-ARCH-019). S-B replaced
    # it: a Vector of displayable elements has one contract in every engine,
    # and every other type is a checker error, ECHO_DISPLAY_UNSUPPORTED.
    print("P25-S-S4-011 / S-B: echo of a Vector runs; Map and record are checker errors")
    three_modes("fn main():\n  let v = [1, 2]\n  echo v\n", "echo Vector", "[1, 2]\n")
    checker_rejected("fn main():\n  var m = Map()\n  m[\"a\"] = 1\n  echo m\n", "echo Map",
                     "ECHO_DISPLAY_UNSUPPORTED")
    checker_rejected("type P:\n  x: Int\n\nfn main():\n  let p = P(x: 1)\n  echo p\n",
                     "echo record", "ECHO_DISPLAY_UNSUPPORTED")


def main():
    print("=== Phase 25 Team S S4 adversarial Fast Debug parity regressions ===")
    test_try_join_binding_after_raise()
    test_branch_bindings_shadowing_state()
    test_joined_bindings_remain_assignable()
    test_wrapping_unary_minus()
    test_int_literal_range()
    test_message_result_writes_state()
    test_try_raise_edge_identities()
    test_echo_display()
    print("=== All Phase 25 S4 regressions passed ===")


if __name__ == "__main__":
    main()
