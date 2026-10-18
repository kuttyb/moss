#!/usr/bin/env python3
"""Check compiler-retained body semantic hashes and explicit unsupported status."""

import json
from pathlib import Path
import re
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]
MOSS = Path(sys.argv[1]).resolve() if len(sys.argv) > 1 else ROOT / "moss"
(ROOT / "tmp").mkdir(exist_ok=True)

# 1. Run C++ executable unit tests covering expression nodes, statements, control flow,
# deterministic encoding, exact identities, malformed IR rejection, and dependency closure validation.
cpp_test = ROOT / "tests" / "tooling" / "check_phase25_checked_body_ir.cpp"
cpp_bin = ROOT / "tmp" / "phase25-checked-body-ir-cpp-test"
subprocess.run(["g++", "-std=c++17", "-O0", "-Wall", "-Wextra", "-Werror", "-pedantic",
                str(cpp_test), "-o", str(cpp_bin)], check=True)
subprocess.run([str(cpp_bin)], check=True)


def summary(source: Path, contents: str) -> dict:
    source.write_text(contents)
    checked = subprocess.run([str(MOSS), "inspect", "main", "--source",
                              str(source), "--json"], capture_output=True, text=True)
    assert checked.returncode == 0, (checked.stdout, checked.stderr)
    result = json.loads(checked.stdout)
    assert result["ok"], result
    summaries = result["result"]["typed_semantic_v1"]["open_summaries"]
    return next(entry for entry in summaries
                if entry["callable_id"].endswith(":fn:inc"))


with tempfile.TemporaryDirectory(prefix="phase25-checked-body-", dir=ROOT / "tmp") as scratch:
    first = summary(Path(scratch) / "first.moss", """fn inc(value):
  return value + 1

fn main():
  echo inc(10)
""")
    relocated = summary(Path(scratch) / "relocated.moss", """fn inc(value):
    return  value+1

fn main():
    echo inc(10)
""")
    changed = summary(Path(scratch) / "changed.moss", """fn inc(value):
  return value + 2

fn main():
  echo inc(10)
""")
    field = summary(Path(scratch) / "field.moss", """type Box:
  value

fn inc(item: Box):
  return item.value

fn main():
  echo inc(Box(value: 10))
""")
    other_field = summary(Path(scratch) / "other_field.moss", """type Cell:
  value

fn inc(item: Cell):
  return item.value

fn main():
  echo inc(Cell(value: 10))
""")
    vector = summary(Path(scratch) / "vector.moss", """fn inc(unused):
  return [1, 2]

fn main():
  echo inc(10)
""")
    reversed_vector = summary(Path(scratch) / "reversed_vector.moss", """fn inc(unused):
  return [2, 1]

fn main():
  echo inc(10)
""")
    direct_call = summary(Path(scratch) / "direct_call.moss", """fn helper(value: Int) -> Int:
  return value + 1

fn inc(unused):
  return helper(1)

fn main():
  echo inc(10)
""")
    nested_call = summary(Path(scratch) / "nested_call.moss", """fn helper(value: Int) -> Int:
  return value + 1

fn inc(unused):
  return helper(helper(1))

fn main():
  echo inc(10)
""")
    digest = first["body_semantic_hash"]
    assert re.fullmatch(r"body-semantic-v1:sha256:[0-9a-f]{64}", digest), first
    assert first["checked_body_ir_available"], first
    assert first["body_semantic_status"] == "available_checked_body_v1", first
    assert relocated["body_semantic_hash"] == digest, (first, relocated)
    assert changed["body_semantic_hash"] != digest, (first, changed)
    assert field["checked_body_ir_available"], field
    assert other_field["checked_body_ir_available"], other_field
    assert field["body_semantic_hash"] != other_field["body_semantic_hash"]
    assert vector["checked_body_ir_available"], vector
    assert vector["body_semantic_hash"] != reversed_vector["body_semantic_hash"]
    assert direct_call["checked_body_ir_available"], direct_call
    assert direct_call["semantic_dependencies"] == [
        "callable-v1:module-v1:__standalone__@0.0.0::main:fn:helper"
    ], direct_call
    assert not nested_call["checked_body_ir_available"], nested_call
    assert nested_call["body_semantic_hash"] is None, nested_call
    assert nested_call["body_semantic_status"] == "unsupported_checked_body_form", nested_call

print("checked body IR hash: literal, nominal field, vector order, path, and fail-closed cases passed")
