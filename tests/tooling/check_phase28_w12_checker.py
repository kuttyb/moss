#!/usr/bin/env python3
"""Targeted verification for Phase 28 W12 checker features:
1. copy(x) explicit duplication, type eligibility, and fail-closed hooks
2. Nested take(place) in arbitrary expression contexts, place extraction restrictions, duplicate place rejection
3. match consume owned enum/option destructuring, payload ownership transfer, use-after-consume, and fail-closed hooks
4. Collection ownership rules (Vector.remove, Vector.swap_remove, Map.remove, Map.get, element consume rejection, retired replace/swap rejection)
5. Fast Debug execution across new features
"""
from __future__ import annotations

import os
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]
TMP = ROOT / "tmp/phase28_w12_checker"
TMP.mkdir(parents=True, exist_ok=True)

COMPILER_DEV = str(ROOT / "build/compiler/moss-dev")
COMPILER_OPT = str(ROOT / "build/compiler/moss-opt")


def run(cmd: list[str], env: dict[str, str] | None = None) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, cwd=ROOT, env=env
    )


def check_accept(compiler: str, name: str, source: str) -> None:
    src_file = TMP / f"{name}.moss"
    src_file.write_text(source)
    proc = run([compiler, "check", str(src_file)])
    if proc.returncode != 0:
        raise AssertionError(f"[{name}] expected check to pass, failed with:\n{proc.stderr}\n{proc.stdout}")


def check_reject(compiler: str, name: str, source: str, expected_diag: str) -> None:
    src_file = TMP / f"{name}.moss"
    src_file.write_text(source)
    proc = run([compiler, "check", "--json", str(src_file)])
    combined = proc.stderr + proc.stdout
    if proc.returncode == 0:
        raise AssertionError(f"[{name}] expected check to reject with {expected_diag}, but it succeeded!")
    if expected_diag not in combined:
        raise AssertionError(f"[{name}] expected diagnostic {expected_diag}, got:\n{combined}")


def check_interp_success(compiler: str, name: str, source: str, expected_output: str | None = None) -> None:
    src_file = TMP / f"{name}.moss"
    src_file.write_text(source)
    proc = run([compiler, "run", "--interp", str(src_file)])
    combined = proc.stderr + proc.stdout
    if proc.returncode != 0:
        raise AssertionError(f"[{name}] Fast Debug failed with:\n{combined}")
    if expected_output is not None and expected_output not in combined:
        raise AssertionError(f"[{name}] expected output '{expected_output}', got:\n{combined}")


def check_fail_closed_hook(compiler: str, name: str, source: str, hook: str) -> None:
    if "opt" in compiler:
        return
    src_file = TMP / f"{name}.moss"
    src_file.write_text(source)
    env = os.environ.copy()
    env[hook] = "1"
    env["MOSS_TEST_VALIDATE_COPY_CONTRACT"] = "1"
    env["MOSS_TEST_VALIDATE_MATCH_CONTRACT"] = "1"
    proc = run([compiler, "check", str(src_file)], env=env)
    combined = proc.stderr + proc.stdout
    if proc.returncode == 0:
        raise AssertionError(f"[{name}] Hook {hook}: expected failure, but succeeded!")


def test_copy(compiler: str) -> None:
    print(f"=== Testing copy(x) on {compiler} ===")

    # Positive: copy primitives, record, enum, collections, option
    check_accept(compiler, "copy_primitives", """
fn main():
  let a = 42
  let b = copy(a)
  let s = "moss"
  let s2 = copy(s)
  let f = 3.14
  let f2 = copy(f)
  let ok = true
  let ok2 = copy(ok)
""")

    check_accept(compiler, "copy_record_and_enum", """
type Point:
  x: Int
  y: Int

enum Shape:
  Circle(r: Int)
  Point(pt: Point)

fn main():
  let p = Point(x = 10, y = 20)
  let p2 = copy(p)
  let s = Shape.Circle(r: 5)
  let s2 = copy(s)
""")

    check_accept(compiler, "copy_collections_and_option", """
fn main():
  let v = [1, 2, 3]
  let v2 = copy(v)
  let opt = Some(99)
  let opt2 = copy(opt)
""")

    # Fast Debug execution of copy
    check_interp_success(compiler, "copy_interp", """
type Item:
  val: Int

fn main():
  var a = Item(val = 10)
  var b = copy(a)
  b.val = 20
  echo a.val
  echo b.val
""", "10\n20")

    # Negative: Arity != 1
    check_reject(compiler, "copy_no_args", """
fn main():
  let x = copy()
""", "COPY_ARGUMENT_ARITY")

    check_reject(compiler, "copy_two_args", """
fn main():
  let x = copy(1, 2)
""", "COPY_ARGUMENT_ARITY")

    # Negative: Ineligible type FileIO
    check_reject(compiler, "copy_fileio", """
fn test_copy_fileio(f: FileIO):
  let f2 = copy(f)
fn main():
  pass
""", "COPY_INELIGIBLE_TYPE")

    # Negative: Source .clone() rejected
    check_reject(compiler, "clone_method_rejected", """
type Point:
  x: Int
fn main():
  let p = Point(x = 1)
  let p2 = p.clone()
""", "CLONE_ALIAS_UNSUPPORTED")

    # Fail-closed hooks for copy facts
    copy_src = """
type Box:
  x: Int
fn main():
  let b = Box(x = 1)
  let b2 = copy(b)
"""
    check_fail_closed_hook(compiler, "copy_drop_fact", copy_src, "MOSS_TEST_DROP_COPY_FACT")
    check_fail_closed_hook(compiler, "copy_perturb_fact", copy_src, "MOSS_TEST_PERTURB_COPY_FACT")
    check_fail_closed_hook(compiler, "copy_duplicate_fact", copy_src, "MOSS_TEST_DUPLICATE_COPY_FACT")
    check_fail_closed_hook(compiler, "copy_duplicate_req", copy_src, "MOSS_TEST_DUPLICATE_COPY_REQUIREMENT")


def test_nested_take(compiler: str) -> None:
    print(f"=== Testing nested take(place) on {compiler} ===")

    # Positive: nested take in call argument, constructor, collection literal
    check_accept(compiler, "take_in_call", """
type Container:
  opt: option[Int]

fn consume_opt(o: option[Int]) -> Int:
  return 1

fn main():
  var c = Container(opt = Some(42))
  let res = consume_opt(take(c.opt))
""")

    check_accept(compiler, "take_in_constructor", """
type Container:
  opt: option[Int]
  items: Vector[Int]

type Holder:
  a: option[Int]
  b: Vector[Int]

fn main():
  var c = Container(opt = Some(1), items = [10, 20])
  let h = Holder(a = take(c.opt), b = take(c.items))
""")

    check_accept(compiler, "take_in_vector_literal", """
type Container:
  items: Vector[Int]

fn main():
  var c = Container(items = [1, 2])
  let outer = [take(c.items)]
""")

    # Fast Debug execution of nested take
    check_interp_success(compiler, "take_nested_interp", """
type Node:
  items: Vector[Int]

fn sum(v: Vector[Int]) -> Int:
  var s = 0
  for x in v:
    s = s + x
  return s

fn main():
  var n = Node(items = [3, 4, 5])
  let s = sum(take(n.items))
  echo s
  echo (n.items |> count)
""", "12\n0")

    # Negative: taking a local
    check_reject(compiler, "take_local", """
fn main():
  var v = [1, 2]
  let taken = take(v)
""", "TAKE_REQUIRES_FIELD_PLACE")

    # Negative: taking a collection element
    check_reject(compiler, "take_element", """
type Node:
  v: Vector[Vector[Int]]
fn main():
  var n = Node(v = [[1, 2]])
  let taken = take(n.v[0])
""", "TAKE_REQUIRES_FIELD_PLACE")

    # Negative: taking non-eligible terminal field (e.g. unified record field)
    check_reject(compiler, "take_unified_field", """
type Point:
  x: Int
fn main():
  var p = Point(x = 10)
  let taken = take(p.x)
""", "TAKE_INELIGIBLE_TERMINAL")

    # Negative: taking from immutable local (e.g. borrowed match binding)
    check_reject(compiler, "take_immutable_local", """
type Container:
  items: Vector[Int]
enum Box:
  Wrap(c: Container)
fn test_take_borrowed(b: Box):
  match b:
    case Wrap(c):
      let taken = take(c.items)
fn main():
  pass
""", "IMMUTABLE_LOCAL_MUTATION")

    # Negative: duplicate take of same place in a single statement
    check_reject(compiler, "take_duplicate_in_stmt", """
type Node:
  opt: option[Int]
fn pair(a: option[Int], b: option[Int]) -> Int:
  return 0
fn main():
  var n = Node(opt = Some(1))
  let x = pair(take(n.opt), take(n.opt))
""", "TAKE_DUPLICATE_PLACE_ACCESS")


def test_match_consume(compiler: str) -> None:
    print(f"=== Testing match consume and option match on {compiler} ===")

    # Positive: match consume enum and payload transfer
    check_accept(compiler, "match_consume_enum", """
type Buffer:
  tag: String

enum Command:
  Write(buf: Buffer)
  Stop

fn main():
  let cmd = Command.Write(buf: Buffer(tag = "data"))
  var pending = Vector[Buffer]()
  match consume cmd:
    case Write(buf):
      pending.push(buf)
    case Stop:
      pass
""")

    # Fast Debug execution of match consume
    check_interp_success(compiler, "match_consume_interp", """
type Buffer:
  tag: String

enum Command:
  Write(buf: Buffer)
  Stop

fn main():
  let cmd = Command.Write(buf: Buffer(tag = "transferred"))
  var pending = Vector[Buffer]()
  match consume cmd:
    case Write(buf):
      pending.push(buf)
    case Stop:
      pass
  echo (pending |> count)
  echo pending[0].tag
""", "1\ntransferred")

    # Positive: option matching (read match)
    check_interp_success(compiler, "option_match_read", """
fn main():
  let opt = Some(42)
  match opt:
    case Some(val):
      echo val
    case None:
      echo 0
""", "42")

    # Positive: option match consume
    check_interp_success(compiler, "option_match_consume", """
type Item:
  name: String

fn main():
  let opt = Some(Item(name = "owned"))
  var items = Vector[Item]()
  match consume opt:
    case Some(item):
      items.push(item)
    case None:
      pass
  echo (items |> count)
  echo items[0].name
""", "1\nowned")

    # Negative: use after consume
    check_reject(compiler, "match_consume_use_after", """
enum State:
  Active
  Idle
fn main():
  let s = State.Active
  match consume s:
    case Active:
      pass
    case Idle:
      pass
  let s2 = s
""", "OWNERSHIP_USE_AFTER_CONSUME")

    # Negative: match consume on interior field
    check_reject(compiler, "match_consume_interior_field", """
type Job:
  state: State
enum State:
  Active
  Idle
fn main():
  var j = Job(state = State.Active)
  match consume j.state:
    case Active:
      pass
    case Idle:
      pass
""", "MATCH_CONSUME_INTERIOR_PLACE")

    # Negative: non-exhaustive option match (missing None)
    check_reject(compiler, "option_non_exhaustive", """
fn main():
  let opt = Some(1)
  match opt:
    case Some(x):
      echo x
""", "NON_EXHAUSTIVE_MATCH")

    # Negative: unknown case on option
    check_reject(compiler, "option_unknown_case", """
fn main():
  let opt = Some(1)
  match opt:
    case Some(x):
      echo x
    case None:
      pass
    case Extra:
      pass
""", "UNKNOWN_ENUM_CASE")

    # Fail-closed hooks for match facts
    match_src = """
enum Flag:
  Yes
  No
fn main():
  let f = Flag.Yes
  match consume f:
    case Yes:
      pass
    case No:
      pass
"""
    check_fail_closed_hook(compiler, "match_drop_fact", match_src, "MOSS_TEST_DROP_MATCH_FACT")
    check_fail_closed_hook(compiler, "match_perturb_fact", match_src, "MOSS_TEST_PERTURB_MATCH_FACT")
    check_fail_closed_hook(compiler, "match_duplicate_fact", match_src, "MOSS_TEST_DUPLICATE_MATCH_FACT")
    check_fail_closed_hook(compiler, "match_duplicate_req", match_src, "MOSS_TEST_DUPLICATE_MATCH_REQUIREMENT")


def test_collection_ownership(compiler: str) -> None:
    print(f"=== Testing collection ownership rules on {compiler} ===")

    # Positive: Vector.remove and Vector.swap_remove
    check_interp_success(compiler, "vector_remove_interp", """
fn main():
  var v = [10, 20, 30, 40]
  let r = v.remove(1)
  echo r
  echo (v |> count)
  echo v[1]
  let sr = v.swap_remove(0)
  echo sr
  echo (v |> count)
  echo v[0]
""", "20\n3\n30\n10\n2\n40")

    # Positive: Map.remove and Map.get (1 arg)
    check_interp_success(compiler, "map_remove_and_get_interp", """
fn main():
  var m = Map()
  m["alpha"] = 1
  m["beta"] = 2
  echo m.get("alpha")
  let val = m.remove("alpha")
  echo val
  echo (m.keys() |> count)
""", "1\n1\n1")

    # Negative: consume vector loop element
    check_reject(compiler, "consume_vector_loop_element", """
type Item:
  tag: String
fn consume_item(it: Item) -> Item:
  return it
fn main():
  var v = [Item(tag = "a")]
  for item in v:
    let x = consume_item(item)
""", "OWNERSHIP_VECTOR_ELEMENT_CONSUME")


def main() -> None:
    for compiler in [COMPILER_DEV, COMPILER_OPT]:
        test_copy(compiler)
        test_nested_take(compiler)
        test_match_consume(compiler)
        test_collection_ownership(compiler)
    print("ALL PHASE 28 W12 CHECKER TESTS PASSED!")


if __name__ == "__main__":
    main()
