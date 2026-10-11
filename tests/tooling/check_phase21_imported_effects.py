#!/usr/bin/env python3
"""Source-free Phase 21 effects: preserve provider summaries and fail closed."""

import json
from pathlib import Path
import re
import subprocess
import sys


compiler = Path(sys.argv[1]).resolve()
margo = compiler.with_name("margo")
out = Path(sys.argv[2]).resolve()
project = out / "provider-consumer"
(project / "src").mkdir(parents=True, exist_ok=True)
(project / "moss.toml").write_text(
    '[project]\nname="phase21_imported_effects"\nversion="0.1.0"\n')
provider = project / "src/provider.moss"
consumer = project / "src/main.moss"
provider.write_text('''module provider
export enum Fault:
  Bad(code: Int)
  Other(code: Int)
export fn fail() -> Int:
  raise Fault.Bad(code: 17)
export fn generic(value) -> Int:
  if value == 0:
    raise Fault.Other(code: 23)
  return value
export fn panic(value: Int) -> Int:
  return value / 0
export type Gauge:
  value: Int
  fn fail() -> Int:
    raise Fault.Bad(code: value)
  fn panic() -> Int:
    return 1 / 0
  fn panic_map(item: Int) -> Int:
    return item / 0
''')


def run(args, expected=0):
    result = subprocess.run([str(a) for a in args], cwd=project,
                            text=True, capture_output=True, timeout=180)
    assert result.returncode == expected, (
        args, result.returncode, result.stdout, result.stderr)
    return result


def check():
    return json.loads(run([compiler, "check", consumer, "--json"]).stdout)


def effects(identity):
    result = run([compiler, "effects", identity, "--source", consumer,
                  "--json"])
    return json.loads(result.stdout)["result"]["observable_effects"]


def build_and_run(expected):
    built = json.loads(run([margo, "build", "--json"]).stdout)
    binary = built["artifacts"]["artifacts"]["executable"]
    assert run([binary]).stdout == expected


source_backed = '''module app
import provider
fn main():
  g = provider.Gauge(value: 19)
  try:
    echo provider.fail()
  recover provider.Fault.Bad(code):
    echo code
  try:
    echo provider.generic(0)
  recover provider.Fault.Other(code):
    echo code
  try:
    echo g.fail()
  recover provider.Fault.Bad(code):
    echo code
'''
consumer.write_text(source_backed)
assert check()["ok"]
assert effects("entity-v1:function:provider__fail")["raise_set"] == [
    "enum:provider__Fault:Bad"]
assert effects("entity-v1:function:provider__generic")["raise_set"] == [
    "enum:provider__Fault:Other"]
assert effects("entity-v1:method:provider__Gauge.fail")["raise_set"] == [
    "enum:provider__Fault:Bad"]
assert effects("entity-v1:method:provider__Gauge.panic")["may_panic"]
assert effects("entity-v1:function:provider__panic")["may_panic"]
build_and_run("17\n23\n19\n")
assert run([margo, "debug"]).stdout == "17\n23\n19\n"

provider.rename(provider.with_suffix(".removed"))
source_free = '''module app
import provider
fn main():
  try:
    echo provider.fail()
  recover provider.Fault.Bad(code):
    echo code
  try:
    echo provider.generic(0)
  recover provider.Fault.Other(code):
    echo code
'''
consumer.write_text(source_free)
assert check()["ok"]
assert effects("entity-v1:function:provider__fail")["raise_set"] == [
    "enum:provider__Fault:Bad"]
assert effects("entity-v1:function:provider__generic")["raise_set"] == [
    "enum:provider__Fault:Other"]
assert effects("entity-v1:function:provider__panic")["may_panic"]
build_and_run("17\n23\n")

consumer.write_text('''module app
import provider
domain Worker:
  fn Run() -> Int:
    reply provider.fail()
  on_fail provider.Fault.Bad(code):
    reply code
fn main():
  worker = Worker()
  echo message worker.Run()
''')
assert check()["ok"]
build_and_run("17\n")

for call in ("provider.fail()", "provider.generic(0)"):
    consumer.write_text('''module app
import provider
fn main():
  echo ''' + call + "\n")
    rejected = json.loads(run([compiler, "check", consumer, "--json"],
                              expected=1).stdout)
    assert not rejected["ok"] and "main may not let typed errors escape" in (
        rejected["error"]["message"]), rejected

# ABI v8 has no observable-effect record for object methods. Until an
# approved interface revision, a source-free consumer must fail at Moss check.
consumer.write_text('''module app
import provider
fn main():
  g = provider.Gauge(value: 19)
  echo g.fail()
''')
rejected = json.loads(run([compiler, "check", consumer, "--json"],
                          expected=1).stdout)
assert rejected["error"]["code"] == "IMPORTED_METHOD_EFFECTS_UNAVAILABLE"

consumer.write_text('''module app
import provider
fn main():
  g = provider.Gauge(value: 19)
  echo [1, 2] |> map(g.panic_map) |> sum
''')
rejected = json.loads(run([compiler, "check", consumer, "--json"],
                          expected=1).stdout)
assert rejected["error"]["code"] in (
    "IMPORTED_METHOD_EFFECTS_UNAVAILABLE", "FUNCTIONAL_CALLABLE_UNRESOLVED"), rejected

interface = project / "build/debug/provider.mossi"
original = interface.read_text()
assert "object_method fail" in original
assert "object_method fail" in original and not re.search(
    r"object_method fail[^\n]*effects=", original)

# A missing required concrete-function record is rejected even when a forged
# interface rehashes its own sections. The rlib remains present and unchanged.
tampered = original
line = next(s for s in tampered.splitlines() if s.startswith("export fn fail "))
changed = re.sub(r" effects=[^ ]+", "", line, count=1)
assert changed != line
tampered = tampered.replace(line, changed, 1)
contract, semantic = tampered.split("\ncontract\n", 1)[1].split(
    "\nsemantic_exports\n", 1)
def stable_hash(section):
    value = 14695981039346656037
    for byte in section.encode():
        value = ((value ^ byte) * 1099511628211) & ((1 << 64) - 1)
    return f"{value:016x}"
for key, section in (("concrete_interface_hash", contract + "\n"),
                     ("interface_hash", contract + "\n"),
                     ("generic_semantic_hash", semantic)):
    tampered = re.sub(rf"(?m)^{key} [0-9a-f]{{16}}$",
                      f"{key} {stable_hash(section)}", tampered)
interface.write_text(tampered)
consumer.write_text(source_free)
rejected = json.loads(run([compiler, "check", consumer, "--json"],
                          expected=1).stdout)
assert "lacks Phase 21 observable effects" in rejected["error"]["message"]
interface.write_text(original)

print("Phase 21 imported function effects and method fail-closed checks passed")
