"""Re-run the Phase 25 G1 defect-ledger reproducers against one or more compilers.

Usage: python3 tests/phase25/ledger/run_ledger.py LABEL=path/to/moss [LABEL=...]

Each reproducer states the correct Moss behaviour: the stdout every engine must
produce, or the checker diagnostic code that must reject it. A compiler passes a
reproducer only if `moss check`, Fast Debug, and native debug all agree with it.
Package reproducers (#10, #14) use that compiler's sibling `margo`.
"""

import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[3]
SCRATCH = ROOT / "tmp"
SCRATCH.mkdir(exist_ok=True)
ENV = {k: v for k, v in os.environ.items() if k != "MOSS_STAGED_STRING_REPR"}

SOURCES = {
    "#1 Int ordering above 2^53": ("""
fn main():
  let a = 9007199254740992
  let b = 9007199254740993
  echo b > a
  echo a < b
  echo b >= a + 1
  echo a == b
""", "true\ntrue\ntrue\nfalse\n"),
    "#2 ordering on user types rejected": ("""
type Point:
  x: Int

fn main():
  let p = Point(x: 1)
  let q = Point(x: 2)
  if p < q:
    echo "bad"
""", "reject"),
    "#3 one type per binding": ("""
fn main():
  var x = 42
  x = "hello"
  echo x
""", "reject"),
    "#4 parameter-based indexed assignment": ("""
type Matrix:
  rows: Int
  cols: Int
  data: Vector[Int]

fn set_at(m: Matrix, r: Int, c: Int, v: Int):
  m.data[r * m.cols + c] = v

fn main():
  var m = Matrix(rows: 2, cols: 3, data: [0, 0, 0, 0, 0, 0])
  set_at(m, 1, 2, 42)
  echo m.data[5]
""", "42\n"),
    "#5 repeated discard": ("""
fn twice(x: Int) -> Int:
  return x * 2

fn main():
  _ = twice(1)
  _ = twice(2)
  echo "ok"
""", "ok\n"),
    "#6 assert text with braces": ("""
fn main():
  assert("{json: key}" != "")
  assertEqual("{a: 1}", "{a: 1}")
  echo "ok"
""", "ok\n"),
    "#7 Float.to_string": ("""
fn main():
  echo 1.5.to_string()
  let x = 2.0
  echo x.to_string()
""", "1.5\n2\n"),
    "#8 for traversal": ("""
fn main():
  var total = 0
  for i in range(0, 4):
    total = total + i
  for x in [10, 20]:
    total = total + x
  echo total
""", "36\n"),
    "#12d local collection member call": ("""
fn main():
  var v = Vector[Int]()
  v.push(4)
  v.push(5)
  echo v[1]
""", "5\n"),
    "#12 open receiver (joint Team G G2)": ("""
fn main():
  var v = []
  v.push(1)
  echo v[0]
""", "1\n"),
    "#15 local field member call": ("""
type Holder:
  data: Vector[Int]

fn main():
  var h = Holder(data: Vector[Int]())
  h.data.push(7)
  echo h.data[0]
""", "7\n"),
}


def run(cmd, cwd=ROOT, timeout=600):
    try:
        return subprocess.run(cmd, cwd=cwd, env=ENV, capture_output=True, text=True, timeout=timeout)
    except subprocess.TimeoutExpired:
        return subprocess.CompletedProcess(cmd, 124, "", "timeout")


def source_case(moss, code, expected, td):
    source = Path(td) / "case.moss"
    source.write_text(code)
    checked = run([moss, "check", str(source), "--json"])
    if expected == "reject":
        return "pass" if checked.returncode != 0 else "accepted (should reject)"
    if checked.returncode != 0:
        try:
            message = json.loads(checked.stdout)["error"]["message"]
        except Exception:
            message = checked.stderr.strip()[:80]
        return f"check rejects: {message[:70]}"
    interp = run([moss, "run", "--interp", str(source)])
    rust, binary = Path(td) / "case.rs", Path(td) / "case"
    gen = run([moss, str(source), "--debug", "-o", str(rust)])
    native = "lowering failed"
    if gen.returncode == 0:
        built = run(["rustc", "--edition=2021", "--crate-name", "case", str(rust), "-o", str(binary)])
        native = run([str(binary)]).stdout if built.returncode == 0 else "rustc failed"
    problems = []
    if interp.returncode != 0 or interp.stdout != expected:
        problems.append("Fast Debug " + (interp.stdout.strip().replace("\n", ",") or
                                          interp.stderr.strip().split("\n")[0][:60]))
    if native != expected:
        problems.append("native " + native.strip().replace("\n", ","))
    return "pass" if not problems else "; ".join(problems)


LABEL_PROVIDER = """module net

export type Table:
  row_count: Int

export fn row_count(t: Table) -> Int:
  return t.row_count

export fn make(n: Int) -> Table:
  return Table(row_count: n)
"""


def package_label_case(moss, td):
    margo = str(Path(moss).parent / "margo")
    base = Path(td)
    (base / "net" / "src").mkdir(parents=True)
    (base / "app" / "src").mkdir(parents=True)
    (base / "net" / "Moss.toml").write_text('[package]\nname = "net"\nversion = "0.1.0"\n\n[build]\nsource = "src"\n')
    (base / "net" / "src" / "main.moss").write_text(LABEL_PROVIDER)
    (base / "app" / "Moss.toml").write_text(
        '[package]\nname = "ledgerapp"\nversion = "0.1.0"\n\n[build]\nsource = "src"\n\n'
        '[dependencies]\nnet = { path = "../net" }\n')
    (base / "app" / "src" / "main.moss").write_text(
        "module app\n\nimport net\n\nfn main():\n  echo net.row_count(net.make(5))\n")
    problems = []
    for name, args in (("native", ["run"]), ("Fast Debug", ["debug"])):
        result = run([margo, *args], cwd=base / "app")
        if result.returncode != 0 or result.stdout != "5\n":
            detail = result.stdout + result.stderr
            marker = '"message": "'
            if marker in detail:
                detail = detail[detail.index(marker) + len(marker):].split('"')[0]
            problems.append(f"{name}: {detail.strip()[:70]}")
    return "pass" if not problems else "; ".join(problems)


def package_bench_case(moss, td):
    margo = str(Path(moss).parent / "margo")
    work = Path(td)
    (work / "src").mkdir(parents=True)
    (work / "benches").mkdir(parents=True)
    (work / "Moss.toml").write_text('[package]\nname = "moss.calc"\nversion = "0.1.0"\n')
    (work / "src" / "main.moss").write_text(
        "module calc\n\nexport enum CalcError:\n  InvalidInput\n\n"
        "export fn risky(x: Int) -> Int:\n  if x < 0:\n    raise CalcError.InvalidInput\n  return x + 1\n")
    (work / "benches" / "bench_main.moss").write_text(
        "import calc\n\nbench \"risky\":\n  try:\n    let v = calc.risky(10)\n    assert(v == 11)\n"
        "  recover calc.CalcError.InvalidInput:\n    assert(false)\n")
    result = run([margo, "bench"], cwd=work, timeout=900)
    return "pass" if result.returncode == 0 else "margo bench failed"


def main():
    compilers = [arg.split("=", 1) for arg in sys.argv[1:]]
    rows = {}
    for label, moss in compilers:
        moss = str(Path(moss).resolve())
        for name, (code, expected) in SOURCES.items():
            with tempfile.TemporaryDirectory(prefix="p25-ledger-", dir=SCRATCH) as td:
                rows.setdefault(name, {})[label] = source_case(moss, code, expected, td)
        with tempfile.TemporaryDirectory(prefix="p25-ledger-", dir=SCRATCH) as td:
            rows.setdefault("#10 cross-package constructor label", {})[label] = package_label_case(moss, td)
        with tempfile.TemporaryDirectory(prefix="p25-ledger-", dir=SCRATCH) as td:
            rows.setdefault("#14 raising call in bench", {})[label] = package_bench_case(moss, td)
    labels = [label for label, _ in compilers]
    print("| reproducer | " + " | ".join(labels) + " |")
    print("|---|" + "---|" * len(labels))
    for name, results in rows.items():
        print(f"| {name} | " + " | ".join(results[label] for label in labels) + " |")


if __name__ == "__main__":
    main()
