#!/usr/bin/env python3
"""Checker regressions for primitive Vector element ownership in P28-R7-009."""
from __future__ import annotations

import json
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]
COMPILER = Path(sys.argv[1]).resolve() if len(sys.argv) > 1 else ROOT / "moss"
TMP = ROOT / "tmp/phase28_r7_009"
TMP.mkdir(parents=True, exist_ok=True)

CASES = {
    "primitive_index_in_loop": '''fn main():
  values = [1, 2, 3]
  var result = Vector[Int]()
  for i in range(0, 3):
    result.push(values[i])
  echo values[0]
''',
    "primitive_index_reused": '''fn main():
  values = [1, 2]
  var result = Vector[Int]()
  result.push(values[0])
  echo values[0]
''',
    "primitive_index_method_write": '''type Box:
  values: Vector[Int]
  fn Set(index: Int, value: Int) -> Int:
    self.values[index] = value
    self.values[index] = value + 1
    return self.values[index]

fn main():
  box = Box(values = [1, 2])
  echo box.Set(0, 9)
  echo box.values[0]
''',
    "nonprimitive_index_consume_rejected": '''type Resource:
  tag: String

fn consume_resource(value: Resource) -> Resource:
  return value

fn main():
  values = [Resource(tag = "x")]
  moved = consume_resource(values[0])
  echo moved.tag
''',
    "nonprimitive_index_return_rejected": '''type Resource:
  tag: String

fn first(items: Vector):
  return items[0]

fn main():
  items = [Resource(tag = "x")]
  value = first(items)
  echo value.tag
''',
    "nonprimitive_index_reply_rejected": '''domain Store:
  strings: Vector[String]
  fn Read() -> String:
    reply strings[0]

fn main():
  store = Store(strings = ["indexed"])
  echo message store.Read()
''',
    "nonprimitive_index_reply_copy": '''domain Store:
  strings: Vector[String]
  fn Read() -> String:
    reply copy(strings[0])

fn main():
  store = Store(strings = ["indexed"])
  echo message store.Read()
''',
    "nonprimitive_index_alias_reply_rejected": '''domain Store:
  strings: Vector[String]
  fn Read() -> String:
    value = strings[0]
    reply value

fn main():
  pass
''',
    "primitive_index_reply_read": '''domain Store:
  values: Vector[Int]
  fn Read() -> Int:
    reply values[0]

fn main():
  store = Store(values = [7])
  echo message store.Read()
''',
    "option_index_reply_rejected": '''domain Store:
  values: Vector[option[Int]]
  fn Read() -> option[Int]:
    reply values[0]

fn main():
  pass
''',
    "option_parenthesized_index_reply_rejected": '''domain Store:
  values: Vector[option[Int]]
  fn Read() -> option[Int]:
    reply (values[0])

fn main():
  pass
''',
    "option_index_alias_reply_rejected": '''domain Store:
  values: Vector[option[Int]]
  fn Read() -> option[Int]:
    value = values[0]
    reply value

fn main():
  pass
''',
    "resource_index_alias_reply_rejected": '''type Resource:
  tag: String

domain Store:
  values: Vector[Resource]
  fn Read() -> Resource:
    value = values[0]
    reply value

fn main():
  pass
''',
    "option_index_return_rejected": '''fn first(items: Vector[option[Int]]) -> option[Int]:
  return items[0]

fn main():
  pass
''',
    "option_iteration_return_rejected": '''fn first(items: Vector[option[Int]]) -> option[Int]:
  for item in items:
    return item
  return None

fn main():
  pass
''',
    "option_index_alias_return_rejected": '''fn first(items: Vector[option[Int]]) -> option[Int]:
  value = items[0]
  return value

fn main():
  pass
''',
    "option_iteration_alias_return_rejected": '''fn first(items: Vector[option[Int]]) -> option[Int]:
  for item in items:
    value = item
    return value
  return None

fn main():
  pass
''',
}


def check(name: str, source: str, expected_code: str | None) -> None:
    path = TMP / f"{name}.moss"
    path.write_text(source, encoding="utf-8")
    proc = subprocess.run([str(COMPILER), "check", str(path), "--json"],
                          cwd=ROOT, capture_output=True, text=True, timeout=60)
    try:
        result = json.loads(proc.stdout)
    except json.JSONDecodeError as exc:
        raise AssertionError(f"{name}: invalid checker JSON: {proc.stdout}\n{proc.stderr}") from exc
    if expected_code is None:
        if proc.returncode != 0 or not result.get("ok"):
            raise AssertionError(f"{name}: expected acceptance, got {result}")
    else:
        error = result.get("error") or {}
        if proc.returncode == 0 or result.get("ok") or error.get("code") != expected_code:
            raise AssertionError(f"{name}: expected {expected_code}, got {result}")


def main() -> int:
    for name in ("primitive_index_in_loop", "primitive_index_reused",
                 "primitive_index_method_write"):
        check(name, CASES[name], None)
        print(f"PASS {name}", flush=True)
    check("nonprimitive_index_consume_rejected",
          CASES["nonprimitive_index_consume_rejected"],
          "OWNERSHIP_COLLECTION_ELEMENT_CONSUME")
    print("PASS nonprimitive_index_consume_rejected", flush=True)
    check("nonprimitive_index_return_rejected",
          CASES["nonprimitive_index_return_rejected"],
          "OWNERSHIP_COLLECTION_ELEMENT_CONSUME")
    print("PASS nonprimitive_index_return_rejected", flush=True)
    check("nonprimitive_index_reply_rejected",
          CASES["nonprimitive_index_reply_rejected"],
          "OWNERSHIP_COLLECTION_ELEMENT_CONSUME")
    print("PASS nonprimitive_index_reply_rejected", flush=True)
    check("nonprimitive_index_reply_copy",
          CASES["nonprimitive_index_reply_copy"], None)
    print("PASS nonprimitive_index_reply_copy", flush=True)
    check("nonprimitive_index_alias_reply_rejected",
          CASES["nonprimitive_index_alias_reply_rejected"],
          "OWNERSHIP_COLLECTION_ELEMENT_CONSUME")
    print("PASS nonprimitive_index_alias_reply_rejected", flush=True)
    check("primitive_index_reply_read",
          CASES["primitive_index_reply_read"], None)
    print("PASS primitive_index_reply_read", flush=True)
    check("option_index_return_rejected",
          CASES["option_index_return_rejected"],
          "OWNERSHIP_COLLECTION_ELEMENT_CONSUME")
    print("PASS option_index_return_rejected", flush=True)
    check("option_index_reply_rejected",
          CASES["option_index_reply_rejected"],
          "OWNERSHIP_COLLECTION_ELEMENT_CONSUME")
    print("PASS option_index_reply_rejected", flush=True)
    check("option_parenthesized_index_reply_rejected",
          CASES["option_parenthesized_index_reply_rejected"],
          "OWNERSHIP_COLLECTION_ELEMENT_CONSUME")
    print("PASS option_parenthesized_index_reply_rejected", flush=True)
    check("option_index_alias_reply_rejected",
          CASES["option_index_alias_reply_rejected"],
          "OWNERSHIP_COLLECTION_ELEMENT_CONSUME")
    print("PASS option_index_alias_reply_rejected", flush=True)
    check("resource_index_alias_reply_rejected",
          CASES["resource_index_alias_reply_rejected"],
          "OWNERSHIP_COLLECTION_ELEMENT_CONSUME")
    print("PASS resource_index_alias_reply_rejected", flush=True)
    check("option_iteration_return_rejected",
          CASES["option_iteration_return_rejected"],
          "OWNERSHIP_VECTOR_ELEMENT_CONSUME")
    print("PASS option_iteration_return_rejected", flush=True)
    check("option_index_alias_return_rejected",
          CASES["option_index_alias_return_rejected"],
          "OWNERSHIP_ILLEGAL_CONSUME")
    print("PASS option_index_alias_return_rejected", flush=True)
    check("option_iteration_alias_return_rejected",
          CASES["option_iteration_alias_return_rejected"],
          "OWNERSHIP_VECTOR_ELEMENT_CONSUME")
    print("PASS option_iteration_alias_return_rejected", flush=True)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
