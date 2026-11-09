#!/usr/bin/env python3
"""Directed tests for collection remove methods and collection panics."""
import json
import os
from pathlib import Path
import subprocess
import sys

repo = Path(__file__).resolve().parents[2]
compiler = Path(sys.argv[1]).resolve()
out = Path(sys.argv[2]).resolve() if len(sys.argv) > 2 else repo / "tmp/check_moss_collection_removes"
out.mkdir(parents=True, exist_ok=True)


def run(args, expected=0):
    result = subprocess.run([str(arg) for arg in args], cwd=repo,
                            capture_output=True, text=True, timeout=120)
    assert result.returncode == expected, (
        args, result.returncode, result.stdout, result.stderr)
    return result


valid = [
    # 1. v = [A, B, C] (records): x = v.remove(0) -> x is A, count is 2, v[0] is B.
    ("vector_remove", """type Rec:
  name: String

fn main():
  var v = [Rec(name: "A"), Rec(name: "B"), Rec(name: "C")]
  try:
    x = v.remove(0)
    echo x.name, v |> count, v[0].name
  recover CollectionError.InvalidIndex:
    echo "invalid"
""", "A 2 B"),

    # 2. x = v.swap_remove(0) -> x is A, count is 2, v[0] is C.
    ("vector_swap_remove", """type Rec:
  name: String

fn main():
  var v = [Rec(name: "A"), Rec(name: "B"), Rec(name: "C")]
  try:
    x = v.swap_remove(0)
    echo x.name, v |> count, v[0].name
  recover CollectionError.InvalidIndex:
    echo "invalid"
""", "A 2 C"),

    # 3. v.remove(5) inside try / recover CollectionError.InvalidIndex -> prints invalid.
    ("vector_remove_out_of_bounds", """fn main():
  var v = [10, 20]
  try:
    x = v.remove(5)
    echo "ok"
  recover CollectionError.InvalidIndex:
    echo "invalid"
""", "invalid"),

    # 3b. v.swap_remove(5) inside try / recover CollectionError.InvalidIndex -> prints invalid.
    ("vector_swap_remove_out_of_bounds", """fn main():
  var v = [10, 20]
  try:
    x = v.swap_remove(5)
    echo "ok"
  recover CollectionError.InvalidIndex:
    echo "invalid"
""", "invalid"),

    # 4. m.remove("a") -> returns the value, and the Map count drops by 1.
    ("map_remove", """fn main():
  var m = Map()
  m["a"] = 100
  m["b"] = 200
  try:
    val = m.remove("a")
    echo val, m.keys() |> count
  recover CollectionError.MissingKey:
    echo "missing"
""", "100 1"),

    # 5. m.remove("zz") inside try / recover CollectionError.MissingKey -> prints missing.
    ("map_remove_missing_key", """fn main():
  var m = Map()
  m["a"] = 100
  try:
    val = m.remove("zz")
    echo "ok"
  recover CollectionError.MissingKey:
    echo "missing"
""", "missing"),

    # 6. x = s.items.remove(0) through a field -> allowed, and correct.
    ("field_vector_remove", """type Container:
  items: Vector[Int]

fn main():
  var s = Container(items: [11, 22, 33])
  try:
    x = s.items.remove(0)
    echo x, s.items |> count, s.items[0]
  recover CollectionError.InvalidIndex:
    echo "invalid"
""", "11 2 22"),

    # NonCopy record in Vector remove and swap_remove
    ("noncopy_vector_remove", """type NonCopyRec:
  tag: String

fn main():
  var v = [NonCopyRec(tag: "first"), NonCopyRec(tag: "second")]
  try:
    x = v.remove(0)
    echo x.tag, v |> count, v[0].tag
  recover CollectionError.InvalidIndex:
    echo "invalid"
""", "first 1 second"),

    # NonCopy record in Map remove
    ("noncopy_map_remove", """type NonCopyVal:
  val: Int

fn main():
  var m = Map()
  m["k"] = NonCopyVal(val: 99)
  try:
    x = m.remove("k")
    echo x.val, m.keys() |> count
  recover CollectionError.MissingKey:
    echo "missing"
""", "99 0"),
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

# 7. v.remove(0) at Root with no try -> compile error (unhandled raise).
unhandled_sources = [
    ("unhandled_remove_root", """fn main():
  var v = [1, 2, 3]
  x = v.remove(0)
"""),
    ("unhandled_swap_remove_root", """fn main():
  var v = [1, 2, 3]
  x = v.swap_remove(0)
"""),
    ("unhandled_map_remove_root", """fn main():
  var m = Map()
  m["a"] = 1
  x = m.remove("a")
"""),
]

for name, source in unhandled_sources:
    moss_source = out / f"{name}.moss"
    moss_source.write_text(source)
    checked = run([compiler, "check", moss_source, "--json"], expected=1)
    payload = json.loads(checked.stdout)
    message = payload["error"]["message"]
    assert "main may not let typed errors escape" in message or "try/recover" in message, (name, payload)
    run([compiler, moss_source, "-o", out / f"{name}.rs"], expected=1)
    debug = run([compiler, "run", "--interp", moss_source], expected=1)
    assert "try/recover" in (debug.stdout + debug.stderr) or "escape" in (debug.stdout + debug.stderr), (name, debug)
    print(f"PASS {name}: unhandled raise at Root rejected by both engines")

# 8. pop() on an empty Vector -> both engines print pop from empty collection and exit non-zero.
pop_empty_source = """fn main():
  var v = Vector[Int]()
  x = v.pop()
"""
pop_empty_file = out / "pop_empty.moss"
pop_empty_file.write_text(pop_empty_source)
rust_source = out / "pop_empty.rs"
binary = out / "pop_empty"
run([compiler, pop_empty_file, "-o", rust_source])
run(["rustc", "-D", "warnings", rust_source, "-o", binary])

native_res = subprocess.run([str(binary)], capture_output=True, text=True)
assert native_res.returncode != 0, f"Native pop empty expected non-zero, got {native_res.returncode}"
native_msg = native_res.stdout + native_res.stderr
assert "pop from empty collection" in native_msg, f"Native expected 'pop from empty collection' in output, got: {native_msg}"

debug_res = subprocess.run([str(compiler), "run", "--interp", str(pop_empty_file)], capture_output=True, text=True)
assert debug_res.returncode != 0, f"Fast Debug pop empty expected non-zero, got {debug_res.returncode}"
debug_msg = debug_res.stdout + debug_res.stderr
assert "pop from empty collection" in debug_msg, f"Fast Debug expected 'pop from empty collection' in output, got: {debug_msg}"
print("PASS pop() on empty Vector: both engines print 'pop from empty collection' and exit non-zero")

print("PASS all directed collection remove tests")
