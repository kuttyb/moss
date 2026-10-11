#!/usr/bin/env python3
"""Exercise O1 and O8 through a compiled provider with its source removed."""

import json
import os
from pathlib import Path
import shutil
import subprocess
import sys


repo = Path(__file__).resolve().parents[2]
compiler = Path(sys.argv[1]).resolve()
margo = repo / "margo"
project = repo / "tmp/phase21-source-free-proof-edges"
if project.exists():
    shutil.rmtree(project)
(project / "src").mkdir(parents=True)
(project / "moss.toml").write_text(
    '[project]\nname="phase21_source_free_proof_edges"\nversion="0.1.0"\n')
provider = project / "src/provider.moss"
provider.write_text('''module provider
export enum Failure:
  Bad
export type Counter:
  current: Int
  fn next() -> option[Int]:
    if current == 1:
      raise Failure.Bad
    if current <= 0:
      return None
    value = current
    current = current - 1
    Some(value)
fn fail() -> Int:
  raise Failure.Bad
fn consume_after(number: Int, data: String) -> String:
  return data
export domain Worker:
  fn Run() -> Int:
    var data = "abc"
    result = consume_after(fail(), data)
    reply 0
  on_fail Failure.Bad:
    reply data.length()
''')
consumer = project / "src/main.moss"


def run(*args, expected=0):
    environment = os.environ.copy()
    environment["MOSS"] = str(compiler)
    result = subprocess.run([str(arg) for arg in args], cwd=project,
                            env=environment, text=True,
                            capture_output=True, timeout=180)
    assert result.returncode == expected, (
        args, result.returncode, result.stdout, result.stderr)
    return result


consumer.write_text('''module app
import provider
fn main():
  worker = provider.Worker()
  echo message worker.Run()
''')
first = json.loads(run(margo, "build", "--json").stdout)
assert run(first["artifacts"]["artifacts"]["executable"]).stdout == "3\n"
interface = project / "build/debug/provider.mossi"
assert interface.is_file()
provider.rename(provider.with_suffix(".removed"))

# Alter the consumer to force a fresh source-free compilation. The provider's
# early argument raise leaves its owned String available to the Root trailer.
consumer.write_text('''module app
import provider
fn main():
  worker = provider.Worker()
  echo message worker.Run()
  echo message worker.Run()
''')
checked = json.loads(run(compiler, "check", consumer, "--json").stdout)
assert checked["ok"], checked
rebuilt = json.loads(run(margo, "build", "--json").stdout)
assert run(rebuilt["artifacts"]["artifacts"]["executable"]).stdout == "3\n3\n"

# An implicit imported next() call has a typed raise and cannot use the
# ordinary unchecked iterator lowering. Its precise summary must survive the
# source-free boundary before the checker rejects the loop.
consumer.write_text('''module app
import provider
fn main():
  counter = provider.Counter(current: 2)
  for value in counter:
    echo value
''')
checked = run(compiler, "check", consumer, "--json", expected=1)
diagnostic = json.loads(checked.stdout)
assert diagnostic["error"]["code"] == "RAISING_ITERATOR_UNSUPPORTED", diagnostic
print("Phase 21 source-free O1/O8 proof edges passed.")
