"""Phase 25 Team S S-B: `moss check` modes and the echo display contract.

P25-S-ARCH-019: standalone `moss check` accepted `echo v` for a Vector while
project-mode check of the identical source rejected it with
NATIVE_ECHO_DISPLAY_UNSUPPORTED, because project check ran native lowering.
Project check now stops at semantics exactly like standalone check, and the
display rule is a checker rule.

Echo display contract (docs/FAST_DEBUG.md, "Echo display contract"):
  Int     decimal, leading '-' when negative
  Float   canonical Moss Float text (NaN, inf, -inf, -0.0, 1e21, 1e-7, 0.1)
  Bool    true | false
  Char    the character itself, unquoted
  String  the text itself, unquoted and unescaped
  Vector  "[" + element texts joined by ", " + "]"; empty is "[]"; nested
          Vectors use the same rule recursively
Map, Queue, records, enums, function references, Unit, and Vectors of any of
them have no display form and are rejected with ECHO_DISPLAY_UNSUPPORTED by the
checker, so Fast Debug and native report the same code before execution.

Every source is checked standalone and as a project `src/main.moss`; the two
diagnostics must be identical. Accepted sources must print identical stdout in
Fast Debug, Native Debug, Native Release, `moss debug .`, `margo run`, and
`margo run --release`, equal to the documented expectation.
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
MARGO = ROOT / "margo"
SCRATCH = ROOT / "tmp"
SCRATCH.mkdir(exist_ok=True)
MANIFEST = '[project]\nname = "{name}"\nversion = "0.1.0"\n\n[build]\nsource = "src"\n'
CODE = "ECHO_DISPLAY_UNSUPPORTED"
COUNT = {"accepted": 0, "rejected": 0}


def run(cmd, cwd=ROOT, timeout=600):
    return subprocess.run(cmd, cwd=cwd, capture_output=True, text=True, timeout=timeout)


def check_verdict(source, cwd):
    """The comparable part of a `moss check --json` result."""
    result = run([str(MOSS), "check", str(source), "--json"], cwd=cwd)
    data = json.loads(result.stdout)
    if data["ok"]:
        diagnostics = [(d.get("code"), d.get("message"), d.get("line"))
                       for d in data["result"].get("diagnostics", [])]
        return {"ok": True, "diagnostics": diagnostics}, result.returncode
    error = data["error"]
    return {"ok": False, "code": error.get("code"), "message": error.get("message"),
            "line": error.get("line")}, result.returncode


def materialize(td, code, name="case"):
    """Write one source both as a standalone file and as a project main."""
    standalone = Path(td) / "standalone" / f"{name}.moss"
    standalone.parent.mkdir(parents=True)
    standalone.write_text(code)
    project = Path(td) / "project"
    (project / "src").mkdir(parents=True)
    (project / "Moss.toml").write_text(MANIFEST.format(name="sbcase"))
    (project / "src" / "main.moss").write_text(code)
    return standalone, project


def same_check(standalone, project, name):
    alone, alone_rc = check_verdict(standalone, standalone.parent)
    proj, proj_rc = check_verdict(project / "src" / "main.moss", project)
    assert alone == proj and alone_rc == proj_rc, (
        f"[{name}] standalone and project check disagree:\n"
        f"  standalone={alone} rc={alone_rc}\n  project={proj} rc={proj_rc}")
    return alone


def native(source, td, mode):
    flag, rustc_flags = ("--debug", []) if mode == "debug" else ("-O", ["-O"])
    rust = Path(td) / f"{mode}.rs"
    binary = Path(td) / mode
    gen = run([str(MOSS), str(source), flag, "-o", str(rust)])
    assert gen.returncode == 0, f"{mode} lowering failed:\n{gen.stderr}"
    text = rust.read_text()
    build = run(["rustc", *rustc_flags, "--crate-name", "case", str(rust), "-o", str(binary)])
    assert build.returncode == 0, f"{mode} rustc failed:\n{build.stderr[:4000]}"
    return run([str(binary)]), text


def accepted(code, name, expected):
    with tempfile.TemporaryDirectory(prefix="p25-sb-", dir=SCRATCH) as td:
        standalone, project = materialize(td, code)
        verdict = same_check(standalone, project, name)
        assert verdict["ok"], f"[{name}] check rejected: {verdict}"
        outputs = {}
        interp = run([str(MOSS), "run", "--interp", str(standalone)])
        assert interp.returncode == 0, f"[{name}] Fast Debug failed:\n{interp.stderr}"
        outputs["fast-debug"] = interp.stdout
        for mode in ("debug", "release"):
            result, rust = native(standalone, td, mode)
            assert result.returncode == 0, f"[{name}] native {mode} failed:\n{result.stderr}"
            outputs[f"native-{mode}"] = result.stdout
            # The display contract is Moss's, never Rust's Debug formatting.
            echo_lines = [line for line in rust.splitlines() if "println!(" in line]
            assert not any("{:?}" in line for line in echo_lines), (
                f"[{name}] native echo uses Rust Debug formatting")
            # Vector display is static helper calls, not a runtime trait.
            runtime = rust.split("Begin Moss Echo Display Runtime", 1)[1].split(
                "End Moss Echo Display Runtime", 1)[0]
            assert "trait " not in runtime and "dyn " not in runtime and "impl" not in runtime, (
                f"[{name}] display runtime introduced trait machinery")
            assert not [line for line in rust.splitlines()
                        if "__moss_display" in line and "dyn " in line], (
                f"[{name}] display call uses dynamic dispatch")
            if "[" in code:
                assert "__moss_display_vector(" in rust, f"[{name}] no display helper call"
        project_debug = run([str(MOSS), "debug", "."], cwd=project)
        assert project_debug.returncode == 0, (
            f"[{name}] moss debug . failed:\n{project_debug.stderr}")
        outputs["project-fast-debug"] = project_debug.stdout
        for label, extra in (("margo-run", []), ("margo-run-release", ["--release"])):
            result = run([str(MARGO), "run", *extra], cwd=project)
            assert result.returncode == 0, f"[{name}] {label} failed:\n{result.stderr[-3000:]}"
            outputs[label] = result.stdout
        assert len(set(outputs.values())) == 1, f"[{name}] engine disagreement: " + json.dumps(
            outputs, indent=1)
        assert outputs["fast-debug"] == expected, (
            f"[{name}] expected {expected!r}, got {outputs['fast-debug']!r}")
        COUNT["accepted"] += 1
        print(f"  PASS: {name} ({len(outputs)} engines, both check modes)")


def rejected(code, name, code_expected=CODE, line=None, build_project=False):
    with tempfile.TemporaryDirectory(prefix="p25-sb-", dir=SCRATCH) as td:
        standalone, project = materialize(td, code)
        verdict = same_check(standalone, project, name)
        assert not verdict["ok"] and verdict["code"] == code_expected, (
            f"[{name}] expected {code_expected}, got {verdict}")
        if line is not None:
            assert verdict["line"] == line, f"[{name}] expected line {line}, got {verdict}"
        interp = run([str(MOSS), "run", "--interp", str(standalone)])
        assert interp.returncode != 0 and code_expected in interp.stderr and not interp.stdout, (
            f"[{name}] Fast Debug did not stop before execution:\n{interp.stdout}{interp.stderr}")
        project_debug = run([str(MOSS), "debug", "."], cwd=project)
        assert (project_debug.returncode != 0 and code_expected in project_debug.stderr
                and not project_debug.stdout), (
            f"[{name}] moss debug . did not stop:\n{project_debug.stdout}{project_debug.stderr}")
        for flag in ("--debug", "-O"):
            gen = run([str(MOSS), str(standalone), flag, "-o", str(Path(td) / "case.rs")])
            assert gen.returncode != 0 and code_expected in gen.stderr, (
                f"[{name}] native {flag} lowering did not reject:\n{gen.stderr}")
        if build_project:
            result = run([str(MARGO), "build"], cwd=project)
            text = result.stdout + result.stderr
            assert result.returncode != 0 and code_expected in text, text[-2000:]
            assert "error[E" not in text, f"[{name}] reached rustc:\n{text[-2000:]}"
        COUNT["rejected"] += 1
        print(f"  PASS: {name} -> {code_expected} (both check modes, all engines)")


def float_text_via_engines(values):
    """Echo of a Float Vector equals '[' + scalar echo texts joined + ']'."""
    lines = "".join(f"  echo v[{i}]\n" for i in range(len(values)))
    return "fn main():\n  let z = 0.0\n  let v = [" + ", ".join(values) + "]\n  echo v\n" + lines


def test_arch_019_repro():
    print("P25-S-ARCH-019: one verdict for the checked-in echo_vector project")
    project = ROOT / "examples/swarm/modules/algo_chain/repro/echo_vector/single"
    with tempfile.TemporaryDirectory(prefix="p25-sb-", dir=SCRATCH) as td:
        copy = Path(td) / "single"
        shutil.copytree(project, copy, ignore=shutil.ignore_patterns("build", ".moss"))
        standalone = Path(td) / "case.moss"
        standalone.write_text((copy / "src" / "main.moss").read_text())
        alone, _ = check_verdict(standalone, Path(td))
        proj, _ = check_verdict(copy / "src" / "main.moss", copy)
        assert alone == proj == {"ok": True, "diagnostics": []}, (alone, proj)
        debug = run([str(MOSS), "debug", "."], cwd=copy)
        margo = run([str(MARGO), "run"], cwd=copy)
        assert debug.stdout == margo.stdout == "[1, 2, 3]\n", (debug.stdout, margo.stderr)
    print("  PASS: standalone check, project check, moss debug ., margo run agree")


def test_vector_contract():
    print("S-B: Vector display contract across every engine and both check modes")
    accepted("fn main():\n  let v = [1, -2, 3]\n  echo v\n"
             "  echo [9223372036854775807, -9223372036854775807 - 1]\n"
             "  echo [true, false]\n",
             "Int and Bool elements",
             "[1, -2, 3]\n[9223372036854775807, -9223372036854775808]\n[true, false]\n")
    accepted(float_text_via_engines(
                 ["1.5", "-0.0", "0.0", "1e21", "1.0e-7", "0.1", "100.0", "1e300",
                  "0.0 / z", "1.0 / z", "-1.0 / z", "1.0 / 3.0"]),
             "Float elements use scalar Float text",
             "[1.5, -0.0, 0.0, 1e21, 1e-7, 0.1, 100.0, 1e300, NaN, inf, -inf, "
             "0.3333333333333333]\n1.5\n-0.0\n0.0\n1e21\n1e-7\n0.1\n100.0\n1e300\nNaN\n"
             "inf\n-inf\n0.3333333333333333\n")
    accepted("fn main():\n  let s = \"h\u00e9\u4e16!\"\n  echo s.chars()\n"
             "  echo ['a', '\\u{1F600}']\n",
             "Char elements are unquoted characters",
             "[h, \u00e9, \u4e16, !]\n[a, \U0001F600]\n")
    accepted("fn main():\n  echo [\"a\", \"\", \"x, y\", \"q\\\"\", \"\u00e9\"]\n"
             "  echo \"a,b\".split(\",\")\n",
             "String elements are raw text (unquoted, unescaped)",
             "[a, , x, y, q\", \u00e9]\n[a, b]\n")
    accepted("fn grid() -> Vector[Vector[Float]]:\n  return [[0.5], [], [-0.0, 1e21]]\n\n"
             "fn main():\n  echo [[1], [2, 3]]\n  echo [[], [1]]\n"
             "  let d = [[[1, 2]], [[3]]]\n  echo d\n  echo d[0]\n  echo [[\"a\"], []]\n"
             "  let e = [[], [true]]\n  echo e\n  echo grid()\n",
             "nested Vectors (an empty [] element takes its siblings' type)",
             "[[1], [2, 3]]\n[[], [1]]\n[[[1, 2]], [[3]]]\n[[1, 2]]\n[[a], []]\n[[], [true]]\n"
             "[[0.5], [], [-0.0, 1e21]]\n")
    accepted("fn main():\n  var a = Vector[Int]()\n  echo a\n  var s = Vector[String]()\n"
             "  echo s\n  var p = [1]\n  p.pop()\n  echo p\n  var n = [[1]]\n  n.pop()\n"
             "  echo n\n",
             "empty Vectors", "[]\n[]\n[]\n[]\n")
    accepted("fn show(x):\n  echo x\n\nfn main():\n  show(Vector[Int]())\n",
             "typed empty Vector through specialization", "[]\n")
    accepted("fn show(x):\n  echo x\n\nfn forward(y):\n  show(y)\n\n"
             "fn main():\n  forward([1])\n",
             "concrete Vector through two specializations", "[1]\n")
    accepted("type Bag:\n  xs: Vector[Int]\n\n"
             "domain Store:\n  v = [1, 2]\n\n  fn Show():\n    echo v\n\n"
             "  fn Get() -> Vector[Float]:\n    reply [0.5, -0.0]\n\n"
             "fn show(x):\n  echo x\n\n"
             "fn make() -> Vector[Bool]:\n  return [true]\n\n"
             "fn main():\n  store = Store()\n  message store.Show()\n"
             "  echo message store.Get()\n  let b = Bag(xs: [3, 4])\n  echo b.xs\n"
             "  echo make()\n  show(7)\n  show([8, 9])\n  show([\"s\"])\n"
             "  echo \"v=\", [1], 2.5, 'c', [[0.25]]\n",
             "state, reply, field, return, specialization, and multi-argument echo",
             "[1, 2]\n[0.5, -0.0]\n[3, 4]\n[true]\n7\n[8, 9]\n[s]\nv= [1] 2.5 c [[0.25]]\n")


RECORD = "type P:\n  x: Int\n\n"
ENUM = "enum C:\n  Red\n  Blue\n\n"


def test_rejected_types():
    print("S-B: types without a display contract are rejected by the checker")
    rejected("fn main():\n  var m = Map()\n  m[\"a\"] = 1\n  echo m\n", "Map", line=4,
             build_project=True)
    rejected("fn main():\n  var q = Queue()\n  q.push(1)\n  echo q\n", "Queue", line=4)
    rejected(RECORD + "fn main():\n  let p = P(x: 1)\n  echo p\n", "record", line=6,
             build_project=True)
    rejected(ENUM + "fn main():\n  let c = C.Red\n  echo c\n", "enum", line=7)
    rejected(RECORD + "fn main():\n  echo [P(x: 1)]\n", "Vector of records", line=5)
    rejected(RECORD + "fn main():\n  echo [[P(x: 1)]]\n", "nested Vector of records", line=5)
    rejected(ENUM + "fn main():\n  let c = [C.Red]\n  echo c\n", "Vector of enums", line=7)
    rejected("fn main():\n  var m = Map()\n  m[1] = 2\n  echo [m]\n", "Vector of Maps", line=4)
    rejected("fn main():\n  var q = Queue()\n  q.push(1)\n  echo [q]\n", "Vector of Queues",
             line=4)
    rejected("fn f(x: Int) -> Int:\n  return x\n\nfn main():\n  echo f\n",
             "function reference", line=5)
    rejected("fn g():\n  pass\n\nfn main():\n  echo g()\n", "Unit call result", line=5)
    rejected(RECORD + "fn show(x):\n  echo x\n\nfn main():\n  show(1)\n  show(P(x: 1))\n",
             "record through an untyped specialization", line=5)
    rejected(RECORD + "fn main():\n  echo 1, P(x: 2)\n", "record as a later echo argument",
             line=5)
    rejected("fn main():\n  var v = []\n  echo v\n",
             "untyped local Vector echo", line=3)
    rejected("fn show(x):\n  echo x\n\nfn main():\n  show([])\n",
             "untyped Vector generic argument", "TYPE_INFERENCE_FAILED", line=5)
    # Existing rules that already stop these values keep their own codes, and
    # both check modes agree on them.
    rejected("domain D:\n  n = 0\n  fn Get() -> Int:\n    reply n\n\n"
             "fn main():\n  d = D()\n  echo d\n", "domain handle", "MOSS_COMPILE_ERROR")
    rejected("fn main():\n  let r = range(0, 3)\n  echo r\n", "range value",
             "RANGE_OUTSIDE_FOR")


def test_no_back_door():
    print("S-B: no other path formats a collection with a different text")
    rejected("fn main():\n  let v = [1, 2]\n  echo v.to_string()\n", "Vector.to_string()",
             "TYPE_MISMATCH")
    rejected("fn main():\n  let v = [1, 2]\n  echo \"x\" + v\n", "String + Vector",
             "TYPE_MISMATCH")
    rejected("fn main():\n  var m = Map()\n  m[1] = 2\n  echo m.to_string()\n",
             "Map.to_string()", "TYPE_MISMATCH")


def test_cross_package_and_indexed_string():
    print("S-B: the display runtime across crates and under the staged indexed String")
    with tempfile.TemporaryDirectory(prefix="p25-sb-", dir=SCRATCH) as td:
        base = Path(td)
        provider = base / "lists"
        (provider / "src").mkdir(parents=True)
        (provider / "Moss.toml").write_text(MANIFEST.format(name="lists"))
        (provider / "src" / "lists.moss").write_text(
            "module lists\n\nexport fn words() -> Vector[String]:\n"
            "  echo [\"in\", \"provider\"]\n  return [\"x\", \"y z\"]\n\n"
            "export fn grid() -> Vector[Vector[Float]]:\n  return [[0.5], [-0.0, 1e21]]\n")
        app = base / "app"
        (app / "src").mkdir(parents=True)
        (app / "Moss.toml").write_text(
            MANIFEST.format(name="app") + '\n[dependencies]\nlists = { path = "../lists" }\n')
        (app / "src" / "main.moss").write_text(
            "module app\nimport lists\n\nfn main():\n  echo lists.words()\n"
            "  echo lists.grid()\n")
        expected = "[in, provider]\n[x, y z]\n[[0.5], [-0.0, 1e21]]\n"
        # A path dependency is resolved by Margo, not by bare `moss check`.
        outputs = {}
        for label, cmd in (("margo-run", ["run"]), ("margo-run-release", ["--release", "run"]),
                           ("margo-debug", ["debug"])):
            result = run([str(MARGO), *cmd], cwd=app)
            assert result.returncode == 0, f"{label} failed:\n{result.stderr[-3000:]}"
            outputs[label] = result.stdout
        assert set(outputs.values()) == {expected}, json.dumps(outputs, indent=1)
        print("  PASS: provider and consumer crates each format through their own helper")
    code = ("fn main():\n  let s = \"été\"\n  echo s.split(\"t\")\n"
            "  echo [s, \"plain\"]\n")
    with tempfile.TemporaryDirectory(prefix="p25-sb-", dir=SCRATCH) as td:
        source = Path(td) / "case.moss"
        source.write_text(code)
        env = dict(os.environ, MOSS_STAGED_STRING_REPR="indexed-v1")
        rust = Path(td) / "case.rs"
        gen = subprocess.run([str(MOSS), str(source), "-O", "-o", str(rust)], cwd=ROOT,
                             capture_output=True, text=True, env=env)
        assert gen.returncode == 0, gen.stderr
        assert "pub type MossStr = MossString;" in rust.read_text()
        build = run(["rustc", "-O", "--crate-name", "case", str(rust), "-o", str(Path(td) / "b")])
        assert build.returncode == 0, build.stderr[:3000]
        native_out = run([str(Path(td) / "b")]).stdout
        interp = run([str(MOSS), "run", "--interp", str(source)]).stdout
        assert native_out == interp == "[é, é]\n[été, plain]\n", (
            native_out, interp)
        print("  PASS: indexed-v1 MossString elements display as raw text")


def test_pinned_diagnostic_precedence():
    print("S-B: the display rule does not pre-empt an earlier pinned diagnostic")
    for name in ("functional_placeholder_noncopy_field", "functional_placeholder_noncopy_identity"):
        source = ROOT / "tests" / "negative" / f"{name}.moss"
        result = run([str(MOSS), "--check", str(source)])
        assert result.returncode != 0 and (
            "functional map result aliases nontrivial source storage through '_'"
            in result.stderr), f"[{name}] diagnostic changed:\n{result.stderr}"
        print(f"  PASS: {name} keeps FUNCTIONAL_SEMANTIC_ERROR")


def main():
    print("=== Phase 25 Team S S-B check modes and echo display ===")
    test_arch_019_repro()
    test_vector_contract()
    test_rejected_types()
    test_no_back_door()
    test_cross_package_and_indexed_string()
    test_pinned_diagnostic_precedence()
    print(f"=== All Phase 25 S-B checks passed ({COUNT['accepted']} accepted programs, "
          f"{COUNT['rejected']} rejected programs) ===")


if __name__ == "__main__":
    main()
