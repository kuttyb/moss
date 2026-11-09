#!/usr/bin/env python3
"""Directed Moss::Copy semantics checks across native and Fast Debug."""
import json
import os
from pathlib import Path
import subprocess
import sys

repo = Path(__file__).resolve().parents[2]
compiler = Path(sys.argv[1]).resolve()
out = Path(sys.argv[2]).resolve() if len(sys.argv) > 2 else repo / "tmp/check_moss_copy"
out.mkdir(parents=True, exist_ok=True)


def run(args, expected=0):
    result = subprocess.run([str(arg) for arg in args], cwd=repo,
                            capture_output=True, text=True, timeout=120)
    assert result.returncode == expected, (
        args, result.returncode, result.stdout, result.stderr)
    return result


valid = [
    ("vector", """fn main():
  a = [1, 2, 3]
  b = copy(a)
  b.push(4)
  echo a |> count, b |> count
""", "3 4"),
    ("record", """type Point:
  x: Int
  y: Int

fn main():
  p = Point(x: 1, y: 2)
  q = copy(p)
  q.x = 9
  echo p.x, q.x
""", "1 9"),
    ("record_vector", """type Bucket:
  values: Vector[Int]

fn main():
  a = Bucket(values: [1, 2])
  b = copy(a)
  b.values.push(3)
  echo a.values |> count, b.values |> count
""", "2 3"),
    ("map", """fn main():
  a = Map()
  a[\"left\"] = 1
  b = copy(a)
  b[\"right\"] = 2
  echo a.keys() |> count, b.keys() |> count
""", "1 2"),
    ("string", """fn main():
  s = \"hi\"
  t = copy(s)
  echo s, t
""", "hi hi"),
    ("scalar", """fn main():
  echo copy(5)
""", "5"),
    ("enum_payload", """enum Reply:
  Value(payload: Int)
  Empty

fn main():
  a = Reply.Value(payload: 8)
  b = copy(a)
  match b:
    case Value(payload):
      echo payload
    case Empty:
      echo 0
""", "8"),
    ("call_argument", """fn append_four(values: Vector[Int]):
  values.push(4)

fn main():
  a = [1, 2, 3]
  append_four(copy(a))
  echo a |> count
""", "3"),
]

for name, source, expected in valid:
    moss_source = out / f"{name}.moss"
    rust_source = out / f"{name}.rs"
    binary = out / name
    moss_source.write_text(source)
    run([compiler, "check", moss_source, "--json"])
    run([compiler, moss_source, "-o", rust_source])
    run(["rustc", "-D", "warnings", rust_source, "-o", binary])
    native = run([binary]).stdout.strip()
    fast_debug = run([compiler, "run", "--interp", moss_source]).stdout.strip()
    assert native == expected, (name, "native", expected, native)
    assert fast_debug == expected, (name, "Fast Debug", expected, fast_debug)
    assert native == fast_debug, (name, native, fast_debug)
    print(f"PASS {name}: native = Fast Debug = {expected}")

invalid = [
    ("file_handle", """enum FileError:
  NotFound
  PermissionDenied
  NotRegularFile
  InUse
  Full
  IO

fn main():
  try:
    file = FileIO.open(\"tmp/moss-copy-test.data\", create)
    value = copy(file)
    file.close()
  recover:
    pass
""", ("FileIO", "Moss::Copy")),
    ("record_noncopy_field", """type Log:
  file

fn main():
  executor = Executor().threads(1).start()
  log = Log(file: executor)
  value = copy(log)
  executor.join()
""", ("Log", "field 'file'", "executor")),
    ("domain_handle", """domain Worker:
  fn Ping():
    pass

fn main():
  worker = Worker()
  value = copy(worker)
""", ("Worker", "Moss::Copy")),
    ("executor", """fn main():
  executor = Executor().threads(1).start()
  value = copy(executor)
  executor.join()
""", ("executor", "Moss::Copy")),
    ("copy_zero_args", """fn main():
  value = copy()
""", ("copy expects exactly one argument",)),
    ("copy_two_args", """fn main():
  a = [1]
  value = copy(a, a)
""", ("copy expects exactly one argument",)),
    ("reserved_copy_binding", """fn main():
  copy = 1
""", ("copy", "reserved")),
    ("reserved_copy_function", """fn copy(value: String) -> String:
  return value

fn main():
  echo copy("hi")
""", ("copy", "reserved")),
]

for name, source, expected_parts in invalid:
    moss_source = out / f"{name}.moss"
    moss_source.write_text(source)
    checked = run([compiler, "check", moss_source, "--json"], expected=1)
    payload = json.loads(checked.stdout)
    message = payload["error"]["message"]
    combined = message + " " + str(payload["error"].get("code", ""))
    for part in expected_parts:
        assert part in combined, (name, part, payload)
    # Both execution engines must refuse invalid programs before execution.
    run([compiler, moss_source, "-o", out / f"{name}.rs"], expected=1)
    debug = run([compiler, "run", "--interp", moss_source], expected=1)
    debug_text = debug.stdout + debug.stderr
    for part in expected_parts:
        assert part in debug_text, (name, part, debug_text)
    print(f"PASS {name}: rejected by native and Fast Debug checks")

# The checker-selected builtin target is required by both consumers.
hook_source = out / "copy_target_fact.moss"
hook_source.write_text("fn main():\n  echo copy(5)\n")
for kind, variable in (("drop", "MOSS_TEST_DROP_FACT"),
                       ("perturb", "MOSS_TEST_PERTURB")):
    env = os.environ.copy()
    env[variable] = "call-targets"
    for engine in ("native", "fast-debug"):
        if engine == "native":
            result = subprocess.run(
                [str(compiler), str(hook_source), "-o",
                 str(out / f"copy_target_{kind}.rs")], cwd=repo,
                capture_output=True, text=True, timeout=120, env=env)
        else:
            result = subprocess.run(
                [str(compiler), "run", "--interp", str(hook_source)], cwd=repo,
                capture_output=True, text=True, timeout=120, env=env)
        if engine == "native":
            assert result.returncode != 0 and "MOSS_INTERNAL_INVARIANT" in (
                result.stdout + result.stderr), (kind, engine, result.stdout,
                                                   result.stderr)
        else:
            assert result.returncode == 0 and result.stdout.strip() == "5", (
                kind, engine, result.stdout, result.stderr)
print("PASS Native copy target drop/perturb fails closed; Fast Debug uses the reserved builtin")
print("PASS all 13 directed Moss::Copy tests")
