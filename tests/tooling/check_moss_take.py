#!/usr/bin/env python3
"""Directed take(field) semantics checks across native and Fast Debug."""
import json
import os
from pathlib import Path
import subprocess
import sys

repo = Path(__file__).resolve().parents[2]
compiler = Path(sys.argv[1]).resolve()
out = Path(sys.argv[2]).resolve() if len(sys.argv) > 2 else repo / "tmp/check_moss_take"
out.mkdir(parents=True, exist_ok=True)


def run(args, expected=0):
    result = subprocess.run([str(arg) for arg in args], cwd=repo,
                            capture_output=True, text=True, timeout=120)
    assert result.returncode == expected, (
        args, result.returncode, result.stdout, result.stderr)
    return result


valid = [
    ("vector_field", """type Holder:
  f: Vector[Int]

fn main():
  var s = Holder(f: [1, 2, 3])
  old = take(s.f)
  echo old |> count, s.f |> count
""", "3 0"),
    ("map_field", """type Holder:
  f: Map[String, Int]

fn main():
  m = Map()
  m["a"] = 10
  m["b"] = 20
  var s = Holder(f: m)
  old = take(s.f)
  echo old.keys() |> count, s.f.keys() |> count
""", "2 0"),
    ("option_field", """type Holder:
  f: option[Int]

type OptIter:
  val: option[Int]
  done: Bool
  fn next() -> option[Int]:
    if done:
      return None
    done = true
    return copy(val)

fn show_opt(v: option[Int]) -> String:
  for x in OptIter(val: v, done: false):
    return "Some"
  return "None"

fn show_val(v: option[Int]) -> Int:
  for x in OptIter(val: v, done: false):
    return x
  return -1

fn main():
  var s = Holder(f: Some(42))
  old = take(s.f)
  echo show_val(old), show_opt(s.f)
""", "42 None"),
    ("nested_field_path", """type Inner:
  f: Vector[Int]
  opt: option[Int]

type Outer:
  a: Inner

type OptIter:
  val: option[Int]
  done: Bool
  fn next() -> option[Int]:
    if done:
      return None
    done = true
    return copy(val)

fn show_opt(v: option[Int]) -> String:
  for x in OptIter(val: v, done: false):
    return "Some"
  return "None"

fn show_val(v: option[Int]) -> Int:
  for x in OptIter(val: v, done: false):
    return x
  return -1

fn main():
  var s = Outer(a: Inner(f: [1, 2, 3], opt: Some(7)))
  old_vec = take(s.a.f)
  old_opt = take(s.a.opt)
  echo old_vec |> count, s.a.f |> count
  echo show_val(old_opt), show_opt(s.a.opt)
""", "3 0\n7 None"),
    ("method_and_handler_named_take", """type Line:
  n: Int

  fn take() -> Int:
    return n

domain Worker:
  fn take() -> Int:
    reply 9

fn main():
  worker = Worker()
  line = Line(n: 7)
  echo line.take(), message worker.take()
""", "7 9"),
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
    print(f"PASS {name}: native = Fast Debug = {expected!r}")

invalid = [
    ("record_field", """type Inner:
  x: Int

type Holder:
  f: Inner

fn main():
  var s = Holder(f: Inner(x: 1))
  old = take(s.f)
""", ("take requires a Vector, Map, or Option field", "Inner", "TAKE_UNSUPPORTED_FIELD_TYPE")),
    ("enum_field", """enum Color:
  Red
  Blue

type Holder:
  f: Color

fn main():
  var s = Holder(f: Color.Red)
  old = take(s.f)
""", ("take requires a Vector, Map, or Option field", "Color", "TAKE_UNSUPPORTED_FIELD_TYPE")),
    ("string_field", """type Holder:
  f: String

fn main():
  var s = Holder(f: "hello")
  old = take(s.f)
""", ("take requires a Vector, Map, or Option field", "string", "TAKE_UNSUPPORTED_FIELD_TYPE")),
    ("primitive_field", """type Holder:
  f: Int

fn main():
  var s = Holder(f: 10)
  old = take(s.f)
""", ("take requires a Vector, Map, or Option field", "int", "TAKE_UNSUPPORTED_FIELD_TYPE")),
    ("queue_field", """type Holder:
  f: Queue[Int]

fn main():
  q = Queue()
  q.push(1)
  var s = Holder(f: q)
  old = take(s.f)
""", ("take requires a Vector, Map, or Option field", "queue[int]", "TAKE_UNSUPPORTED_FIELD_TYPE")),
    ("vector_element", """fn main():
  var v = [[1, 2], [3]]
  old = take(v[0])
""", ("cannot take from an element or Map value", "TAKE_REQUIRES_FIELD_PATH")),
    ("map_value", """fn main():
  var m = Map()
  m["k"] = [1, 2]
  old = take(m["k"])
""", ("cannot take from an element or Map value", "TAKE_REQUIRES_FIELD_PATH")),
    ("local_variable", """fn main():
  var a = [1, 2, 3]
  old = take(a)
""", ("cannot take from local", "TAKE_REQUIRES_FIELD_PATH")),
    ("path_containing_index", """type Holder:
  f: Vector[Int]

fn main():
  var v = [Holder(f: [1, 2])]
  old = take(v[0].f)
""", ("containing an index", "TAKE_PATH_CONTAINS_INDEX")),
    ("call_result_field", """type Holder:
  f: Vector[Int]

fn make_holder() -> Holder:
  return Holder(f: [1, 2])

fn main():
  old = take(make_holder().f)
""", ("call result", "TAKE_REQUIRES_FIELD_PATH")),
    ("call_result_direct", """fn make_vec() -> Vector[Int]:
  return [1, 2]

fn main():
  old = take(make_vec())
""", ("call result", "TAKE_REQUIRES_FIELD_PATH")),
    ("readonly_payload_field", """type Payload:
  f: Vector[Int]

domain Worker:
  fn Process(p: Payload) -> Int:
    old = take(p.f)
    reply old |> count

fn main():
  w = Worker()
  echo message w.Process(Payload(f: [1, 2]))
""", ("cannot WRITE incoming message payload",)),
    ("borrowed_enum_payload_field", """type Holder:
  f: Vector[Int]

enum Boxed:
  Wrap(s: Holder)
  Empty

fn main():
  b = Boxed.Wrap(s: Holder(f: [1, 2]))
  match b:
    case Wrap(s):
      old = take(s.f)
    case Empty:
      pass
""", ("payload 's' is a READ view", "BORROWED_ENUM_PAYLOAD_ACCESS")),
    ("consumed_match_immutable_payload_field", """type Holder:
  f: Vector[Int]

enum Boxed:
  Wrap(s: Holder)
  Empty

fn main():
  b = Boxed.Wrap(s: Holder(f: [1, 2]))
  match consume b:
    case Wrap(s):
      old = take(s.f)
    case Empty:
      pass
""", ("cannot mutate immutable local 's'", "IMMUTABLE_LOCAL_MUTATION")),
    ("reserved_take_binding", """fn main():
  take = 1
""", ("'take' is a reserved builtin name",)),
    ("reserved_take_function", """fn take(x: Int) -> Int:
  return x

fn main():
  echo take(1)
""", ("'take' is a reserved builtin name",)),
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
    run([compiler, moss_source, "-o", out / f"{name}.rs"], expected=1)
    debug = run([compiler, "run", "--interp", moss_source], expected=1)
    debug_text = debug.stdout + debug.stderr
    for part in expected_parts:
        if not part.isupper():
            assert part in debug_text, (name, part, debug_text)
    print(f"PASS {name}: rejected by native and Fast Debug checks")

hook_source = out / "take_target_fact.moss"
hook_source.write_text("""type Holder:
  f: Vector[Int]

fn main():
  var s = Holder(f: [1, 2, 3])
  old = take(s.f)
  echo old |> count, s.f |> count
""")
for kind, variable in (("drop", "MOSS_TEST_DROP_FACT"),
                       ("perturb", "MOSS_TEST_PERTURB")):
    env = os.environ.copy()
    env[variable] = "call-targets"
    result = subprocess.run(
        [str(compiler), str(hook_source), "-o",
         str(out / f"take_target_{kind}.rs")], cwd=repo,
        capture_output=True, text=True, timeout=120, env=env)
    assert result.returncode != 0 and "MOSS_INTERNAL_INVARIANT" in (
        result.stdout + result.stderr), (kind, result.stdout, result.stderr)
print("PASS Native take target drop/perturb fails closed")
print("PASS all directed take(field) tests")
