#!/usr/bin/env python3
"""ABI-v9 negative gate: self-consistent forged metadata must fail checking."""

import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys


ROOT = Path(__file__).resolve().parents[2]
COMPILER = Path(sys.argv[1] if len(sys.argv) > 1 else ROOT / "moss").resolve()
OUT = ROOT / "tmp/phase21-mossi-pattern-forgery"
shutil.rmtree(OUT, ignore_errors=True)
(OUT / "provider/src").mkdir(parents=True)
(OUT / "provider/moss.toml").write_text(
    '[project]\nname="phase21_pattern_forgery"\nversion="0.1.0"\n')
consumer_project = OUT / "consumer"
(consumer_project / "src").mkdir(parents=True)
(consumer_project / "moss.toml").write_text(
    '[project]\nname="phase21_pattern_consumer"\nversion="0.1.0"\n')
provider = OUT / "provider/src/service.moss"
provider.write_text('''module service
export enum E:
  A
export enum F:
  B
export domain Worker:
  count: Int
  fn Run(which: Int) -> Int:
    if which == 1:
      raise E.A
    raise F.B
  on_fail E.A:
    count = count + 1
    reply count
  on_fail F.B:
    count = count + 2
    reply count
''')
main = consumer_project / "src/main.moss"
main.write_text('''module app
import service
fn main():
  worker = service.Worker(count: 0)
  echo message worker.Run(1)
  echo message worker.Run(2)
''')


def run(*command, expected=0, cwd=consumer_project, env=None):
    result = subprocess.run(
        [str(part) for part in command], cwd=cwd, env=env, capture_output=True,
        text=True, timeout=180)
    assert result.returncode == expected, (command, result.returncode,
                                            result.stdout, result.stderr)
    return result


provider_build = json.loads(run(ROOT / "margo", "build", "--json",
                                cwd=OUT / "provider").stdout)
assert provider_build["ok"], provider_build
consumer_env = os.environ.copy()
consumer_env["MOSS"] = str(COMPILER)
consumer_env["MOSS_MODULE_PATH"] = str(OUT / "provider/build/debug")
built = json.loads(run(COMPILER, "build", "--json", env=consumer_env).stdout)
assert built["ok"], built
executable = Path(built["result"]["artifacts"]["executable"])
assert run(executable).stdout == "1\n3\n"
interface = OUT / "provider/build/debug/service.mossi"
text = interface.read_text()
patterns = {
    'handler_failure_arm "Run" 0 "enum:service__E:A"':
        'handler_failure_arm "Run" 0 "enum:service__F:B"',
    'handler_failure_arm "Run" 1 "enum:service__F:B"':
        'handler_failure_arm "Run" 1 "enum:service__E:A"',
}
for before, after in patterns.items():
    assert text.count(before) == 2, before
    text = text.replace(before, after)

contract, semantic = text.split("\ncontract\n", 1)[1].split(
    "\nsemantic_exports\n", 1)


def hash_section(section):
    value = 14695981039346656037
    for byte in section.encode():
        value = ((value ^ byte) * 1099511628211) & ((1 << 64) - 1)
    return f"{value:016x}"


for key, section in (("concrete_interface_hash", contract + "\n"),
                     ("interface_hash", contract + "\n"),
                     ("generic_semantic_hash", semantic)):
    text, count = re.subn(rf"(?m)^{key} [0-9a-f]{{16}}$",
                          f"{key} {hash_section(section)}", text)
    assert count == 1, (key, count)
material, count = re.subn(
    r"(?m)^provider_contract_digest sha256:[0-9a-f]{64}\n", "", text)
assert count == 1
material, count = re.subn(
    r"(?m)^package_source_hash sha256:[0-9a-f]{64}\n", "", material)
assert count == 1
text, count = re.subn(
    r"(?m)^provider_contract_digest sha256:[0-9a-f]{64}$",
    "provider_contract_digest sha256:" + hashlib.sha256(
        material.encode()).hexdigest(), text)
assert count == 1
interface.write_text(text)

for command in (("check", main, "--json"), ("build", "--json")):
    rejected = json.loads(run(COMPILER, *command, expected=1,
                              env=consumer_env).stdout)
    assert rejected["error"]["code"] == (
        "SOURCE_FREE_ARTIFACT_CONTRACT_MISMATCH"), rejected
print("ABI-v9 forged failure-arm metadata rejected before native execution")
