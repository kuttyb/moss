#!/usr/bin/env python3
"""Field-move diagnostics and owned reply lowering across both engines."""
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile

repo = Path(__file__).resolve().parents[2]
compiler = Path(sys.argv[1]).resolve()
out = Path(sys.argv[2]).resolve() if len(sys.argv) > 2 else repo / "tmp/check_moss_field_moves"
out.mkdir(parents=True, exist_ok=True)


def run(args, expected=0):
    result = subprocess.run([str(arg) for arg in args], cwd=repo,
                            capture_output=True, text=True, timeout=120)
    assert result.returncode == expected, (
        args, result.returncode, result.stdout, result.stderr)
    return result


invalid = [
    ("vector_assignment", """type Holder:
  frontier: Vector[Int]

fn main():
  s = Holder(frontier: [1, 2, 3])
  b = s.frontier
  echo b |> count
""", "'s.frontier' is a field; write take(s.frontier)"),
    ("record_assignment", """type Point:
  x: Int

type Holder:
  point: Point

fn main():
  s = Holder(point: Point(x: 9))
  b = s.point
  echo b.x
""", "'s.point' is a field; write copy(s.point)"),
    ("constructor_field", """type Point:
  x: Int

type Envelope:
  point: Point

fn main():
  s = Envelope(point: Point(x: 9))
  e = Envelope(point: s.point)
  echo e.point.x
""", "'s.point' is a field; write copy(s.point)"),
    ("collection_insert", """type Point:
  x: Int

type Holder:
  point: Point

fn main():
  s = Holder(point: Point(x: 9))
  values = Vector[Point]()
  values.push(s.point)
  echo values[0].x
""", "'s.point' is a field; write copy(s.point)"),
    ("map_insert", """type Point:
  x: Int

type Holder:
  point: Point

fn main():
  s = Holder(point: Point(x: 9))
  values = Map()
  values[\"point\"] = s.point
  echo values[\"point\"].x
""", "'s.point' is a field; write copy(s.point)"),
    ("function_return", """type Holder:
  items: Vector[Int]

fn extract(s: Holder) -> Vector[Int]:
  return s.items

fn main():
  s = Holder(items: [1, 2])
  echo extract(s) |> count
""", "'s.items' is a field; write take(s.items)"),
    ("method_return", """type Holder:
  payload: Point
  fn Raw() -> Point:
    return payload

type Point:
  value: Int

fn main():
  s = Holder(payload: Point(value: 9))
  echo s.Raw().value
""", "'payload' is a field; write copy(payload)"),
    ("executor_argument", """type Holder:
  items: Vector[Int]

domain Worker:
  total = 0
  fn P(items: Vector[Int]):
    total = total + (items |> count)

fn main():
  worker = Worker()
  var s = Holder(items: [1, 2])
  executor = Executor().threads(1).start()
  executor.invoke(worker.P(s.items))
  executor.join()
""", "'s.items' is a field; write take(s.items)"),
]

for name, source, message in invalid:
    path = out / f"{name}.moss"
    path.write_text(source)
    result = run([compiler, "check", path, "--json"], expected=1)
    diagnostic = json.loads(result.stdout)["error"]
    assert diagnostic["code"] == "IMPLICIT_FIELD_MOVE", (name, diagnostic)
    assert message in diagnostic["message"], (name, diagnostic)
    print(f"PASS reject {name}: IMPLICIT_FIELD_MOVE")


valid = [
    ("take_vector", """type Holder:
  frontier: Vector[Int]

fn main():
  var s = Holder(frontier: [1, 2, 3])
  old = take(s.frontier)
  echo old |> count, s.frontier |> count
""", "3 0"),
    ("field_remove_pop", """type Holder:
  items: Vector[Int]

fn main():
  var s = Holder(items: [1, 2, 3])
  try:
    x = s.items.remove(0)
    y = s.items.pop()
    echo x, y, s.items |> count
  recover CollectionError.InvalidIndex:
    echo "invalid"
""", "1 3 1"),
    ("copy_record", """type Point:
  x: Int

type Holder:
  point: Point

fn main():
  var s = Holder(point: Point(x: 1))
  b = copy(s.point)
  b.x = 9
  echo s.point.x, b.x
""", "1 9"),
    ("copy_function_return", """type Holder:
  items: Vector[Int]

fn extract(s: Holder) -> Vector[Int]:
  return copy(s.items)

fn main():
  s = Holder(items: [1, 2])
  b = extract(s)
  echo b |> count, s.items |> count
""", "2 2"),
    ("copy_method_return", """type Point:
  value: Int

type Holder:
  payload: Point
  fn Get() -> Point:
    return copy(payload)

fn main():
  s = Holder(payload: Point(value: 9))
  echo s.Get().value
""", "9"),
    ("copy_executor_argument", """type Holder:
  items: Vector[Int]

domain Worker:
  total = 0
  fn P(items: Vector[Int]):
    total = total + (items |> count)
  fn Read() -> Int:
    reply total

fn main():
  worker = Worker()
  var s = Holder(items: [1, 2])
  executor = Executor().threads(1).start()
  executor.invoke(worker.P(copy(s.items)))
  executor.join()
  result = message worker.Read()
  echo result, s.items |> count
""", "2 2"),
    ("take_executor_argument", """type Holder:
  items: Vector[Int]

domain Worker:
  total = 0
  fn P(items: Vector[Int]):
    total = total + (items |> count)
  fn Read() -> Int:
    reply total

fn main():
  worker = Worker()
  var s = Holder(items: [1, 2])
  executor = Executor().threads(1).start()
  executor.invoke(worker.P(take(s.items)))
  executor.join()
  result = message worker.Read()
  echo result, s.items |> count
""", "2 0"),
    ("allowed_reads", """type Holder:
  n: Int
  frontier: Vector[Int]

fn peek(values: Vector[Int]) -> Int:
  return values |> count

domain Worker:
  fn Read(values: Vector[Int]) -> Int:
    reply values |> count

fn main():
  worker = Worker()
  s = Holder(n: 7, frontier: [2, 4])
  x = s.n
  a = peek(s.frontier)
  b = s.frontier |> count
  c = message worker.Read(s.frontier)
  echo x, a, b, c
""", "7 2 2 2"),
    ("fresh_reply", """type Payload:
  value: Int

domain Worker:
  fn Make() -> Payload:
    reply Payload(value: 9)

fn main():
  worker = Worker()
  result = message worker.Make()
  echo result.value
""", "9"),
]

for name, source, expected in valid:
    moss_source = out / f"{name}.moss"
    rust_source = out / f"{name}.rs"
    binary = out / name
    moss_source.write_text(source)
    run([compiler, "check", moss_source, "--json"])
    run([compiler, moss_source, "-o", rust_source])
    if name == "fresh_reply":
        generated = rust_source.read_text()
        match = re.search(r"// Moss line \d+: reply Payload\(value: 9\)\n([^\n]+)",
                          generated)
        assert match and ".clone()" not in match.group(1), generated
    run(["rustc", "-D", "warnings", rust_source, "-o", binary])
    native = run([binary]).stdout.strip()
    fast_debug = run([compiler, "run", "--interp", moss_source]).stdout.strip()
    assert native == expected, (name, "native", expected, native)
    assert fast_debug == expected, (name, "Fast Debug", expected, fast_debug)
    assert native == fast_debug, (name, native, fast_debug)
    print(f"PASS {name}: native = Fast Debug = {expected!r}")

print("PASS all field-move checks")


def write_package(path, name, source, dependencies=""):
    (path / "src").mkdir(parents=True, exist_ok=True)
    (path / "Moss.toml").write_text(
        f'[package]\nname = "{name}"\nversion = "0.1.0"\n\n'
        '[build]\nsource = "src"\n' + dependencies)
    (path / "src" / "main.moss").write_text(source)


with tempfile.TemporaryDirectory(prefix="field-moves-source-free-", dir=repo / "tmp") as td:
    scratch = Path(td)
    provider = scratch / "provider"
    provider_source = """module FieldOps
export type Box:
  value
export type Store:
  items: Vector[Int]
export fn fresh_box(value):
  return Box(value: value)
export fn duplicate(item: Box):
  return copy(item.value)
export fn drain(store: Store):
  return take(store.items)
"""
    write_package(provider, "field_ops_provider", provider_source)
    env = os.environ.copy()
    for key in list(env):
        if key == "MOSS_STAGED_ABI" or key.startswith("MOSS_PHASE25_"):
            env.pop(key, None)
    env.update(MOSS=str(compiler), MOSS_STAGED_ABI="v10",
               TMPDIR=str(repo / "tmp"),
               MARGO_HOME=str(scratch / "margo-home"))
    built = subprocess.run([str(repo / "margo"), "build"], cwd=provider,
                           env=env, capture_output=True, text=True, timeout=300)
    assert built.returncode == 0, (built.stdout, built.stderr)
    artifacts = scratch / "provider-artifacts"
    shutil.copytree(provider, artifacts)
    artifact_dir = artifacts / "build" / "debug"
    consumer_source = """module Main
import FieldOps
fn main():
  parcel = FieldOps.fresh_box(9)
  value = FieldOps.duplicate(parcel)
  store = FieldOps.Store(items: [1, 2, 3])
  items = FieldOps.drain(store)
  echo value, items |> count
"""
    expected = "9 3"
    visible = scratch / "visible-consumer"
    write_package(visible, "field_ops_visible_consumer", consumer_source,
                  f'\n[dependencies]\nfield_ops_provider = {{ path = "{provider}" }}\n')
    visible_env = env | {"MARGO_HOME": str(scratch / "visible-home")}
    visible_build = subprocess.run([str(repo / "margo"), "build"], cwd=visible,
                                   env=visible_env, capture_output=True,
                                   text=True, timeout=300)
    assert visible_build.returncode == 0, (visible_build.stdout,
                                            visible_build.stderr)
    visible_native = subprocess.run([str(repo / "margo"), "run"], cwd=visible,
                                    env=visible_env, capture_output=True,
                                    text=True, timeout=300)
    assert visible_native.returncode == 0, (visible_native.stdout,
                                             visible_native.stderr)
    visible_debug = subprocess.run([str(repo / "margo"), "debug"], cwd=visible,
                                   env=visible_env, capture_output=True,
                                   text=True, timeout=300)
    assert visible_debug.returncode == 0, (visible_debug.stdout,
                                            visible_debug.stderr)
    source_visible_outputs = (visible_native.stdout.strip(),
                             visible_debug.stdout.strip())
    assert source_visible_outputs == (expected, expected), source_visible_outputs

    source_free = scratch / "source-free-consumer"
    write_package(source_free, "field_ops_source_free_consumer", consumer_source)
    source_free_env = env | {
        "MOSS_MODULE_PATH": str(artifact_dir),
        "MARGO_HOME": str(scratch / "source-free-home")}
    free_build = subprocess.run([str(repo / "margo"), "build"], cwd=source_free,
                                env=source_free_env, capture_output=True,
                                text=True, timeout=300)
    assert free_build.returncode == 0, (free_build.stdout, free_build.stderr)
    free_native = subprocess.run([str(repo / "margo"), "run"], cwd=source_free,
                                 env=source_free_env, capture_output=True,
                                 text=True, timeout=300)
    assert free_native.returncode == 0, (free_native.stdout, free_native.stderr)
    assert free_native.stdout.strip() == expected, free_native.stdout
    print("PASS source-free copy(item.value) and take(store.items): "
          "source-visible Native/Fast Debug and source-free Native agree")
