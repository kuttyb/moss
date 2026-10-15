"""Phase 25 Team S workstream S1 (items 2-3): checker authority regressions.

S1.2  Named message/function arguments.  Moss v0.1 message and ordinary
      function arguments are positional; labels belong only to constructor,
      enum-case, route, and builtin (e.g. ``sort(v, by: key)``) surfaces.
      ``message c.Bump(step: 2)`` and ``f(a: 1)`` used to pass ``moss check``
      and then fail in Fast Debug ("unsupported expression") and native
      lowering/rustc.  They are now rejected before execution with
      ``MESSAGE_NAMED_ARGUMENT_UNSUPPORTED`` / ``CALL_NAMED_ARGUMENT_UNSUPPORTED``.

S1.3  Fast Debug FileIO preflight.  (a) Test entry points are preflighted per
      test: a test whose reachable closure reaches FileIO is reported
      ``FAIL <name>: [FAST_DEBUG_FILEIO] ...`` without executing any of its
      statements, while other tests run.  (b) Same-named locals, parameters,
      fields, labels, enum cases, members, and same-named handlers of other
      domains no longer cause conservative rejection; every program that can
      reach FileIO is still rejected before any statement executes.

Accepted programs run in Fast Debug, Native Debug, and Native Release with
identical stdout.  Usage: check_phase25_s1_checker_authority.py ./moss
"""

import json
from pathlib import Path
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


def three_modes(code, name, expected):
    """Run standalone source in all three engines; require identical stdout."""
    with tempfile.TemporaryDirectory(prefix="p25-s1-", dir=SCRATCH) as td:
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
            result = run([str(binary)], cwd=td)
            assert result.returncode == 0, f"[{name}] {mode} run failed:\n{result.stderr}"
            outputs[mode] = result.stdout
        assert len(set(outputs.values())) == 1, f"[{name}] engine disagreement: {outputs}"
        assert outputs["fast-debug"] == expected, (
            f"[{name}] expected {expected!r}, got {outputs['fast-debug']!r}")
        print(f"  PASS: {name}")


def rejected(code, name, code_expected, line_expected=None):
    """Require a structured checker diagnostic, and that no engine runs it."""
    with tempfile.TemporaryDirectory(prefix="p25-s1-", dir=SCRATCH) as td:
        source = Path(td) / "case.moss"
        source.write_text(code)
        result = run([str(MOSS), "check", str(source), "--json"])
        assert result.returncode != 0, f"[{name}] accepted:\n{result.stdout}"
        payload = json.loads(result.stdout)
        error = payload.get("error", {})
        assert error.get("code") == code_expected, f"[{name}] expected {code_expected}, got {error}"
        if line_expected is not None:
            assert error.get("line") == line_expected, f"[{name}] line {error.get('line')}"
        interp = run([str(MOSS), "run", "--interp", str(source)])
        assert interp.returncode != 0 and interp.stdout == "", f"[{name}] Fast Debug ran: {interp}"
        assert code_expected in interp.stderr, f"[{name}] Fast Debug: {interp.stderr}"
        gen = run([str(MOSS), str(source), "--debug", "-o", str(Path(td) / "case.rs")])
        assert gen.returncode != 0 and code_expected in gen.stderr, f"[{name}] lowering: {gen}"
        print(f"  PASS: {name} -> {code_expected} at line {error.get('line')}")


def fast_debug_rejected(code, name):
    """moss check accepts; Fast Debug rejects with FAST_DEBUG_FILEIO before any output."""
    with tempfile.TemporaryDirectory(prefix="p25-s1-", dir=SCRATCH) as td:
        source = Path(td) / "case.moss"
        source.write_text(code)
        check = run([str(MOSS), "check", str(source), "--json"])
        assert check.returncode == 0, f"[{name}] moss check rejected:\n{check.stdout}"
        for cmd in (["run", "--interp"], ["debug"]):
            result = run([str(MOSS), *cmd, str(source)])
            assert result.returncode == 1 and result.stdout == "", f"[{name}] {cmd} ran: {result}"
            assert "FAST_DEBUG_FILEIO" in result.stderr, f"[{name}] {cmd}: {result.stderr}"
        print(f"  PASS: {name} -> FAST_DEBUG_FILEIO before any output")


# --- S1.2 named arguments ---------------------------------------------------

DOMAINS = """domain Ledger:
  total: Int

  fn Add(amount: Int, times: Int) -> Int:
    total = total + amount * times
    reply total

  fn Note(amount: Int):
    total = total + amount

domain Front:
  domainroutes(ledger: Ledger)

  fn Submit(amount: Int) -> Int:
    reply message ledger.Add({route})

fn helper(a: Int, b: Int) -> Int:
  return a - b

fn main():
  ledger = Ledger(total: 0)
  front = Front(ledger: ledger)
{body}"""


def program(body, route="amount, 1"):
    return DOMAINS.format(route=route, body=body)


def test_named_arguments():
    print("S1.2: named message and function arguments")
    # The positional contract, including labels that remain legal elsewhere.
    three_modes(program(
        "  message ledger.Note(3)\n"
        "  let r = message ledger.Add(2, 3)\n"
        "  echo r\n"
        "  x = message ledger.Add(1, 1)\n"
        "  echo x\n"
        "  echo message front.Submit(4)\n"
        "  echo helper(5, 2)\n"
        "  var v = [3, 1, 2]\n"
        "  sort(v, by: negate)\n"
        "  echo v[0]\n"
        "  echo Pair(left: 1, right: 2).right\n",
    ).replace("fn helper(", "type Pair:\n  left: Int\n  right: Int\n\n"
              "fn negate(x: Int) -> Int:\n  return 0 - x\n\nfn helper("),
        "positional messages/calls with constructor and builtin labels",
        "9\n10\n14\n3\n3\n2\n")
    line = 23
    cases = [
        ("no-value handler, statement", "  message ledger.Note(amount: 3)\n", "MESSAGE"),
        ("value handler, statement", "  message ledger.Add(amount: 2, times: 3)\n", "MESSAGE"),
        ("value handler, let expression, mixed", "  let r = message ledger.Add(2, times: 3)\n  echo r\n", "MESSAGE"),
        ("value handler, echo expression, reordered", "  echo message ledger.Add(times: 3, amount: 2)\n", "MESSAGE"),
        ("value handler, assignment, '=' labels", "  x = message ledger.Add(amount = 2, times = 3)\n  echo x\n", "MESSAGE"),
        ("wrong label", "  message ledger.Note(nope: 3)\n", "MESSAGE"),
        ("function statement", "  helper(a: 1, b: 2)\n", "CALL"),
        ("function mixed", "  echo helper(1, b: 2)\n", "CALL"),
        ("function wrong labels", "  echo helper(x: 1, y: 2)\n", "CALL"),
        ("function nested in call", "  echo helper(helper(a: 3, b: 1), 1)\n", "CALL"),
        ("function nested in message", "  echo message ledger.Add(helper(a: 3, b: 1), 1)\n", "CALL"),
    ]
    for name, body, kind in cases:
        rejected(program(body), f"{kind.lower()} {name}", f"{kind}_NAMED_ARGUMENT_UNSUPPORTED", line)
    rejected(program("  echo message front.Submit(4)\n", route="amount: amount, times: 1"),
             "message handler-to-domain reply expression", "MESSAGE_NAMED_ARGUMENT_UNSUPPORTED", 15)
    rejected("fn twice(x: Int) -> Int:\n  return x * 2\n\nfn apply(f, x):\n  return f(x: x)\n\n"
             "fn main():\n  echo apply(twice, 4)\n",
             "callable parameter call", "CALL_NAMED_ARGUMENT_UNSUPPORTED", 5)


# --- S1.3 FileIO preflight ---------------------------------------------------

FILE_ERRORS = "enum FileError:\n" + "".join(
    f"  {v}\n" for v in ("NotFound", "PermissionDenied", "NotRegularFile", "InUse", "Full", "IO"))
TOUCH = """
fn save() -> Int:
  try:
    file = FileIO.open("p25-s1-missing.txt", ro)
    file.close()
    return 1
  recover:
    return 2
"""


def test_fileio_false_positives():
    print("S1.3b: same-named non-references do not trigger the FileIO preflight")
    base = FILE_ERRORS + TOUCH
    three_modes(base + """
type Rec:
  save: Int

  fn save_twice() -> Int:
    return save * 2

enum Kind:
  save
  FileIO

fn take(save: Int) -> Int:
  return save + 1

fn main():
  let save = 3
  echo save
  let r = Rec(save: 4)
  echo r.save
  echo r.save_twice()
  echo take(2)
  let k = Kind.save
  match k:
    case save:
      echo "case"
    case FileIO:
      echo "other"
  for save in [7]:
    echo save
  echo 'F'
  echo "save() FileIO"
""", "locals, params, fields, labels, enum cases, literals named save/FileIO",
        "3\n4\n8\n3\ncase\n7\nF\nsave() FileIO\n")
    three_modes(FILE_ERRORS + """
domain Disk:
  count: Int

  fn Flush() -> Int:
    try:
      file = FileIO.open("p25-s1-missing.txt", ro)
      file.close()
      reply 1
    recover:
      reply 2

domain Memory:
  count: Int

  fn Flush() -> Int:
    count = count + 1
    reply count

fn main():
  memory = Memory(count: 0)
  message memory.Flush()
  echo message memory.Flush()
  let n = message memory.Flush()
  echo n
""", "same-named handler of an unreachable FileIO domain", "2\n3\n")
    three_modes(FILE_ERRORS + """
type Rec:
  n: Int

  fn save() -> Int:
    return n + 1
""" + TOUCH + """
fn main():
  let r = Rec(n: 1)
  echo r.save()
""", "method named like a FileIO function", "2\n")


def test_fileio_still_rejected():
    print("S1.3: every program that can reach FileIO is still rejected first")
    base = FILE_ERRORS + TOUCH
    fast_debug_rejected(base + "\nfn main():\n  echo \"started\"\n  echo save()\n", "direct call")
    fast_debug_rejected(base + "\nfn outer() -> Int:\n  return save() + 1\n\n"
                        "fn main():\n  echo \"started\"\n  echo outer()\n", "helper chain")
    fast_debug_rejected(base + "\nfn wrap() = save()\n\nfn main():\n  echo \"started\"\n  echo wrap()\n",
                        "expression-bodied helper")
    fast_debug_rejected(base + "\nfn apply(f, x: Int) -> Int:\n  return f() + x\n\n"
                        "fn main():\n  echo \"started\"\n  echo apply(save, 1)\n", "callable value argument")
    fast_debug_rejected(base + "\nfn main():\n  echo \"started\"\n  if true:\n    let save = 3\n"
                        "    echo save\n  let f = save\n  echo f()\n",
                        "function value after a same-named block local ends")
    fast_debug_rejected(base + "\nfn main():\n  echo \"started\"\n  let save = save()\n  echo save\n",
                        "local initialized from the same-named FileIO call")
    fast_debug_rejected(FILE_ERRORS + """
type Rec:
  n: Int

  fn save() -> Int:
    try:
      file = FileIO.open("p25-s1-missing.txt", ro)
      file.close()
      return 1
    recover:
      return 2

fn run(x) -> Int:
  return x.save()

fn main():
  echo "started"
  echo run(Rec(n: 1))
""", "duck-typed method reaching FileIO")
    domain = FILE_ERRORS + """
domain Disk:
  count: Int

  fn Flush() -> Int:
    try:
      file = FileIO.open("p25-s1-missing.txt", ro)
      file.close()
      reply 1
    recover:
      reply 2

domain Front:
  domainroutes(disk: Disk)

  fn Go() -> Int:
    reply message disk.Flush()

fn main():
  disk = Disk(count: 0)
  front = Front(disk: disk)
  echo "started"
{body}"""
    fast_debug_rejected(domain.format(body="  message disk.Flush()\n"), "message statement")
    fast_debug_rejected(domain.format(body="  echo message disk.Flush()\n"), "message expression")
    fast_debug_rejected(domain.format(body="  echo message front.Go()\n"), "route chain")
    two_flush = domain.replace("domain Front:", """domain Memory:
  count: Int

  fn Flush() -> Int:
    count = count + 1
    reply count

domain Front:""").replace("  disk = Disk(count: 0)\n", "  disk = Disk(count: 0)\n  memory = Memory(count: 0)\n")
    fast_debug_rejected(two_flush.format(
        body="  echo message memory.Flush(), 1 + message disk.Flush()\n"),
        "same-named handlers on one line, one unrecorded by the checker")
    fast_debug_rejected(FILE_ERRORS + """
domain Store:
  file: FileIO

  fn Close():
    file.close()

  on_fail:
    echo "error"

fn main():
  store = Store()
  echo "started"
  message store.Close()
""", "domain with FileIO state")


def make_project(base, name, files):
    project = base / name
    (project / "src").mkdir(parents=True)
    (project / "Moss.toml").write_text(
        f'[package]\nname = "{name}"\nversion = "0.1.0"\n\n[build]\nsource = "src"\n')
    for rel, text in files.items():
        path = project / rel
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(text)
    return project


def test_fileio_test_entry_points():
    print("S1.3a: per-test FileIO preflight for Fast Debug test runners")
    with tempfile.TemporaryDirectory(prefix="p25-s1-", dir=SCRATCH) as td:
        base = Path(td)
        project = make_project(base, "fiotests", {
            "src/main.moss": FILE_ERRORS + TOUCH + "\nfn pure() -> Int:\n  return 3\n\n"
                             "fn main():\n  echo pure()\n",
            "tests/t.moss": 'test "pure":\n  echo "pure-ran"\n  assertEqual(pure(), 3)\n\n'
                            'test "disk":\n  echo "disk-ran"\n  assertEqual(save(), 2)\n\n'
                            'test "shadow":\n  let save = 5\n  assertEqual(save, 5)\n',
        })
        for cmd in ([str(MOSS), "test", "--interp"], [str(MARGO), "test", "--interp"]):
            result = run(cmd, cwd=project)
            assert result.returncode == 1, result
            assert "disk-ran" not in result.stdout, result.stdout
            assert "pure-ran\nPASS pure\n" in result.stdout, result.stdout
            assert "FAIL disk: [FAST_DEBUG_FILEIO]" in result.stdout, result.stdout
            assert "PASS shadow\n" in result.stdout, result.stdout
            assert result.stdout.endswith("2 passed, 1 failed\n"), result.stdout
        result = run([str(MOSS), "test", "--interp", "pure"], cwd=project)
        assert result.returncode == 0 and result.stdout == "pure-ran\nPASS pure\n1 passed\n", result
        result = run([str(MOSS), "test", "--interp", "disk"], cwd=project)
        assert result.returncode == 1 and "disk-ran" not in result.stdout, result
        native = run([str(MOSS), "test"], cwd=project)
        assert native.returncode == 0, native.stdout + native.stderr
        print("  PASS: FileIO test reported FAIL before executing; other tests ran; native runs all")
        modular = make_project(base, "fiomod", {
            "src/disk.moss": "module disk\n\n" + FILE_ERRORS + TOUCH.replace("fn save", "export fn save"),
            "src/main.moss": "module app\nimport disk\n\nfn main():\n  echo \"started\"\n  echo disk.save()\n",
        })
        result = run([str(MOSS), "debug", str(modular)], cwd=modular)
        assert result.returncode == 1 and result.stdout == "" and "FAST_DEBUG_FILEIO" in result.stderr, result
        print("  PASS: module-qualified FileIO call rejected in project Fast Debug")


def main():
    print("=== Phase 25 Team S S1 checker-authority regressions ===")
    test_named_arguments()
    test_fileio_false_positives()
    test_fileio_still_rejected()
    test_fileio_test_entry_points()
    print("=== All Phase 25 S1 checker-authority regressions passed ===")


if __name__ == "__main__":
    main()
