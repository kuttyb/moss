#!/usr/bin/env python3
"""Imported effect summaries use current source-backed provider interfaces."""

import json
import hashlib
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys


compiler = Path(sys.argv[1]).resolve()
margo = compiler.with_name("margo")
out = Path(sys.argv[2]).resolve()
project = out / "provider-consumer"
if project.exists():
    shutil.rmtree(project)
(project / "provider/src").mkdir(parents=True)
(project / "provider/moss.toml").write_text(
    '[project]\nname="phase21_imported_effects"\nversion="0.1.0"\n')
consumer_project = project / "consumer"
(consumer_project / "src").mkdir(parents=True)
(consumer_project / "moss.toml").write_text(
    '[project]\nname="phase21_effects_consumer"\nversion="0.1.0"\n'
    '\n[dependencies]\nphase21_imported_effects = { path = "../provider" }\n')
provider = project / "provider/src/provider.moss"
consumer = consumer_project / "src/main.moss"
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


provider_env = dict(os.environ)
provider_env["MOSS"] = str(compiler)
consumer_env = dict(provider_env)
consumer_env["MOSS_MODULE_PATH"] = str(project / "provider/build/debug")


def run(args, expected=0, *, cwd=consumer_project, env=consumer_env):
    result = subprocess.run([str(a) for a in args], cwd=cwd, env=env,
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


run([margo, "build", "--json"], cwd=project / "provider", env=provider_env)

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
# Plant an old local output before the first build. It is neither an imported
# provider nor a contract for the current app module.
stale_app = consumer_project / "build/debug/app.mossi"
stale_app.parent.mkdir(parents=True)
stale_app.write_text("moss-module-interface-v1\nnative_abi 8\n")
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
assert stale_app.read_text().startswith(
    "moss-module-interface-v2\nprovider_contract_digest sha256:")
assert run([margo, "debug"]).stdout == "17\n23\n19\n"

# Keep the provider package source available so its interface freshness hash
# remains valid under the Phase 26.1 R2 contract.
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

# ABI v9 carries the exact checked method effect summary across the source-free
# boundary, including typed variants and separate panic potential.
consumer.write_text('''module app
import provider
fn main():
  g = provider.Gauge(value: 19)
  try:
    echo g.fail()
  recover provider.Fault.Bad(code):
    echo code
''')
assert check()["ok"]
assert effects("entity-v1:method:provider__Gauge.fail")["raise_set"] == [
    "enum:provider__Fault:Bad"]
assert effects("entity-v1:method:provider__Gauge.panic")["may_panic"]
build_and_run("19\n")

consumer.write_text('''module app
import provider
fn main():
  g = provider.Gauge(value: 19)
  echo [1, 2] |> map(g.panic_map) |> sum
''')
assert check()["ok"]

interface = project / "provider/build/debug/provider.mossi"
original = interface.read_text()
assert "object_method fail" in original
assert re.search(r"object_method fail[^\n]*effects=", original)

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
material = re.sub(r"(?m)^provider_contract_digest sha256:[0-9a-f]{64}\n",
                  "", tampered, count=1)
material = re.sub(r"(?m)^package_source_hash sha256:[0-9a-f]{64}\n",
                  "", material, count=1)
tampered = re.sub(r"(?m)^provider_contract_digest sha256:[0-9a-f]{64}$",
                  "provider_contract_digest sha256:" +
                  hashlib.sha256(material.encode()).hexdigest(), tampered)
consumer.write_text(source_free)
build_and_run("17\n23\n")
# Keep the consumer source unchanged so this is a true warm-cache forgery.
build_and_run("17\n23\n")
interface.write_text(tampered)
checked = json.loads(run([compiler, "check", consumer, "--json"],
                          expected=1).stdout)
assert checked["error"]["code"] == "MOSS_COMPILE_ERROR" and \
    "lacks Phase 21 observable effects" in checked["error"]["message"], checked
rebuilt = json.loads(run([margo, "build", "--json"]).stdout)
assert rebuilt["ok"], rebuilt
assert interface.read_text() == original, "Margo must restore tampered provider output"

# Old local output metadata must not be mistaken for a compiled provider when
# a no-export application is rebuilt in the same directory.
stale_app.write_text("moss-module-interface-v1\nnative_abi 8\n")
built = json.loads(run([margo, "build", "--json"]).stdout)
assert stale_app.read_text().startswith(
    "moss-module-interface-v2\nprovider_contract_digest sha256:"), built
assert run([built["artifacts"]["artifacts"]["executable"]]).stdout == "17\n23\n"

print("Phase 21 ABI-v9 imported function/method effects and artifact binding passed")
