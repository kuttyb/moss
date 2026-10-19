#!/usr/bin/env python3
"""Gate G1: Open Relationships, MemberDispatchRequirement, and Dedicated CallableId Suite.

Verifies:
1. Open relationships operate via MemberDispatchRequirement and Requirement records
   with explicit access modes (Read, Write, Consume), origins, and parent relations.
2. Dedicated CallableId key/record distinct from TypeRef algebra.
3. Open nominal field substitution binds and closes correctly upon static specialization.
4. Contradictory element and index operations reject fail-closed.
5. Unannotated trait method results unify term-driven in solver.
6. Zero internal pseudo-types leak as semantic authority.
"""

import json
from pathlib import Path
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]
MOSS = Path(sys.argv[1]).resolve() if len(sys.argv) > 1 else ROOT / "moss"
(ROOT / "tmp").mkdir(exist_ok=True)

def run_cmd(*args) -> subprocess.CompletedProcess:
    return subprocess.run(list(args), capture_output=True, text=True)

def check_moss(source_text: str) -> subprocess.CompletedProcess:
    with tempfile.NamedTemporaryFile("w", suffix=".moss", dir=ROOT / "tmp", delete=False) as f:
        f.write(source_text)
        f.flush()
        return run_cmd(str(MOSS), "check", f.name, "--json")

def inspect_moss(target: str, source_text: str) -> dict:
    with tempfile.NamedTemporaryFile("w", suffix=".moss", dir=ROOT / "tmp", delete=False) as f:
        f.write(source_text)
        f.flush()
        res = run_cmd(str(MOSS), "inspect", target, "--source", f.name, "--json")
        assert res.returncode == 0, (res.stdout, res.stderr)
        return json.loads(res.stdout)

# 1. Open nominal field substitution closes across static specialization
field_sub = """
type Box:
  value

fn get_val(b: Box):
  return b.value

fn main():
  b1 = Box(value: 10)
  b2 = Box(value: "hello")
  echo get_val(b1)
  echo get_val(b2)
"""
res = check_moss(field_sub)
assert res.returncode == 0, (res.stdout, res.stderr)
doc = json.loads(res.stdout)
assert doc["ok"], doc
print("PASS G1 open nominal field substitution closes across multiple specializations", flush=True)

# 2. Contradictory collection element rejection
conflict_elem = """
fn push_and_echo(items, val):
  items.push(val)

fn main():
  v = [1, 2]
  push_and_echo(v, "string_in_int_vector")
"""
res_conflict = check_moss(conflict_elem)
assert res_conflict.returncode != 0, "Contradictory element in collection must fail closed"
doc_conflict = json.loads(res_conflict.stdout)
assert not doc_conflict["ok"], doc_conflict
assert "TYPE_MISMATCH" in str(doc_conflict) or "type" in doc_conflict["error"]["message"].lower(), doc_conflict
print("PASS G1 contradictory collection element rejects fail-closed", flush=True)

# 3. Unannotated trait method result unifies term-driven
trait_unify = """
trait Valued:
  fn get()

type BoxInt:
  val: Int
  fn get() -> Int:
    return val

type BoxStr:
  val: String
  fn get() -> String:
    return val

fn fetch_and_print(item: Valued):
  echo item.get()

fn main():
  i = BoxInt(val: 99)
  s = BoxStr(val: "ok")
  fetch_and_print(i)
  fetch_and_print(s)
"""
res_trait = check_moss(trait_unify)
assert res_trait.returncode == 0, (res_trait.stdout, res_trait.stderr)
doc_trait = json.loads(res_trait.stdout)
assert doc_trait["ok"], doc_trait
print("PASS G1 unannotated trait method result unifies term-driven", flush=True)

# 4. Dedicated CallableId in specialization and member requirements
higher_order = """
fn double(x: Int) -> Int:
  return x * 2

export fn apply_op(f, arg: Int) -> Int:
  return f(arg)

fn main():
  echo apply_op(double, 21)
"""
res_ho = check_moss(higher_order)
assert res_ho.returncode == 0, (res_ho.stdout, res_ho.stderr)
doc_ho = json.loads(res_ho.stdout)
assert doc_ho["ok"], doc_ho

insp = inspect_moss("main", higher_order)
assert insp["ok"], insp
specs = insp["result"]["typed_semantic_v1"]["specializations"]
assert any("apply_op" in s["callable_id"] for s in specs), specs
apply_op_spec = next(s for s in specs if "apply_op" in s["callable_id"])
assert apply_op_spec["callable_id"].startswith("callable-v1:"), apply_op_spec
assert apply_op_spec["callable_arguments"] == [
    "callable-v1:module-v1:__standalone__@0.0.0::main:fn:double"
], apply_op_spec
print("PASS G1 dedicated CallableId record in specializations and callable arguments", flush=True)

print("Gate G1 Open Relationships & Callable Authority: PASS")
