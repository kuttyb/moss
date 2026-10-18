#!/usr/bin/env python3
"""Gate G2: Declaration Type Authority Verification Suite.

Verifies:
1. Exactly one checked type authority for declarations in src/ast.hpp
   (Param, Method, TraitMethod, Handler, Function).
2. Fail-closed rejection of contradictory string vs term projections via assert_consistent().
3. Unknown nominals cannot be silently interned as primitives when TypeContext::current() is absent.
4. Comprehensive verification across C++ invariants and live compiler behavior.
"""

import json
from pathlib import Path
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]
MOSS = Path(sys.argv[1]).resolve() if len(sys.argv) > 1 else ROOT / "moss"
(ROOT / "tmp").mkdir(exist_ok=True)

# 1. Run direct C++ unit tests for ast.hpp and type_ir.hpp invariants
cpp_test = ROOT / "tests" / "tooling" / "check_phase25_g1_g2_authority.cpp"
cpp_bin = ROOT / "tmp" / "check_phase25_g1_g2_authority_bin"
subprocess.run([
    "g++", "-std=c++17", "-O2", "-Wall", "-Wextra", "-Werror", "-pedantic",
    "-I" + str(ROOT / "src"), str(cpp_test), "-o", str(cpp_bin)
], check=True)
run_cpp = subprocess.run([str(cpp_bin)], capture_output=True, text=True, check=True)
print(run_cpp.stdout.strip())
print("PASS G2 C++ declaration authority & consistency assertions", flush=True)

# Inject erased checked projections at the real compiler export/checking boundaries.
boundary_test = ROOT / "tests" / "tooling" / "check_phase25_declaration_boundaries.cpp"
boundary_bin = ROOT / "tmp" / "check_phase25_declaration_boundaries_bin"
subprocess.run([
    "g++", "-std=c++17", "-O0", "-g0",
    str(boundary_test), "-o", str(boundary_bin)
], check=True)
subprocess.run([str(boundary_bin)], check=True)


def check_moss(source_text: str) -> subprocess.CompletedProcess:
    with tempfile.NamedTemporaryFile("w", suffix=".moss", dir=ROOT / "tmp", delete=False) as f:
        f.write(source_text)
        f.flush()
        return subprocess.run([str(MOSS), "check", f.name, "--json"], capture_output=True, text=True)

# 2. Unknown nominal cannot be silently interned as primitive
res_unknown = check_moss("""
fn process(item: UnknownNominalType):
  echo 1

fn main():
  echo 0
""")
assert res_unknown.returncode != 0, "Unknown nominal must fail checking"
doc_unknown = json.loads(res_unknown.stdout)
assert not doc_unknown["ok"], doc_unknown
err_msg = doc_unknown["error"]["message"].lower()
assert "unknown" in err_msg or "cannot find" in err_msg or "malformed" in err_msg, doc_unknown
print("PASS G2 unknown nominal cannot be silently interned as primitive", flush=True)

# 3. Known nominal resolves and binds authoritative TypeRef
res_known = check_moss("""
type User:
  name: String
  age: Int

fn greet(u: User) -> String:
  return u.name

fn main():
  u = User(name: "Alice", age: 30)
  echo greet(u)
""")
assert res_known.returncode == 0, (res_known.stdout, res_known.stderr)
doc_known = json.loads(res_known.stdout)
assert doc_known["ok"], doc_known
print("PASS G2 registered nominal resolves and unifies term-driven", flush=True)

# 4. Trait declarations bind authoritative structural constraints
res_trait = check_moss("""
trait Identifiable:
  fn id() -> Int

type Item:
  item_id: Int

  fn id() -> Int:
    return item_id

fn print_id(val: Identifiable):
  echo val.id()

fn main():
  it = Item(item_id: 42)
  print_id(it)
""")
assert res_trait.returncode == 0, (res_trait.stdout, res_trait.stderr)
doc_trait = json.loads(res_trait.stdout)
assert doc_trait["ok"], doc_trait
print("PASS G2 trait declaration binds authoritative structural constraint", flush=True)

# 5. Handler reply type authority
res_handler = check_moss("""
domain Counter:
  count: Int = 0

  fn Get() -> Int:
    reply count

  fn Bad() -> Int:
    reply "wrong_type"

fn main():
  c = Counter()
  echo message c.Get()
""")
assert res_handler.returncode != 0, "Handler contradictory reply must fail checking"
doc_handler = json.loads(res_handler.stdout)
assert not doc_handler["ok"], doc_handler
assert "type" in doc_handler["error"]["message"].lower() or "reply" in doc_handler["error"]["message"].lower(), doc_handler
print("PASS G2 handler reply type enforces term authority fail-closed", flush=True)

print("Gate G2 Declaration Type Authority: PASS")
