#!/usr/bin/env python3
"""Cross-crate standard-error WRITE must survive typed failure and Root recovery."""

import json
import os
from pathlib import Path
import shutil
import subprocess
import sys


repo = Path(__file__).resolve().parents[2]
compiler = Path(sys.argv[1]).resolve()
margo = repo / "margo"
project = repo / "tmp/phase21-source-free-standard-write-recovery"
if project.exists():
    shutil.rmtree(project)
(project / "src").mkdir(parents=True)
(project / "moss.toml").write_text(
    '[project]\nname="phase21_standard_write_recovery"\nversion="0.1.0"\n')
provider = project / "src/provider.moss"
provider.write_text('''module provider
export enum Failure:
  Bad
export type Carrier:
  fn Reset(error: std.ParseError) -> Int:
    error = std.ParseError.Overflow
    raise Failure.Bad
''')
consumer = project / "src/main.moss"


def run(*args, expected=0):
    environment = os.environ.copy()
    environment["MOSS"] = str(compiler)
    result = subprocess.run([str(arg) for arg in args], cwd=project,
                            env=environment, capture_output=True, text=True,
                            timeout=180)
    assert result.returncode == expected, (
        args, result.returncode, result.stdout, result.stderr)
    return result


def check_effects():
    checked = json.loads(run(compiler, "check", consumer, "--json").stdout)
    assert checked["ok"], checked
    effects = json.loads(run(
        compiler, "effects", "entity-v1:method:provider__Carrier.Reset",
        "--source", consumer, "--json").stdout)["result"]
    assert effects["ownership"][1]["effect"] == "WRITE", effects
    assert effects["observable_effects"]["raise_set"] == [
        "enum:provider__Failure:Bad"], effects


def build_and_run(output, *, release=False):
    options = ("build", "--release", "--json") if release else ("build", "--json")
    built = json.loads(run(margo, *options).stdout)
    executable = built["artifacts"]["artifacts"]["executable"]
    assert run(executable).stdout == output


# First materialize the provider artifact while the consumer has no imported
# WRITE call. Subsequent builds exercise the actual cross-crate bridge.
consumer.write_text('''module app
import provider
fn main():
  echo 0
''')
build_and_run("0\n")

consumer.write_text('''module app
import provider
domain Worker:
  fn Run() -> Int:
    carrier = provider.Carrier()
    var error = std.ParseError.Invalid
    var answer = 0
    try:
      answer = carrier.Reset(error)
    recover provider.Failure.Bad:
      match error:
        case Invalid:
          answer = 1
        case Overflow:
          answer = 2
    reply answer
fn main():
  worker = Worker()
  echo message worker.Run()
''')
check_effects()
build_and_run("2\n")
build_and_run("2\n", release=True)
assert run(margo, "debug").stdout == "2\n"

provider.rename(provider.with_suffix(".removed"))
# Force a fresh consumer build against the paired .mossi/.rlib contract.
consumer.write_text(consumer.read_text().replace(
    "  echo message worker.Run()\n",
    "  echo message worker.Run()\n  echo message worker.Run()\n"))
check_effects()
build_and_run("2\n2\n")
build_and_run("2\n2\n", release=True)
print("Phase 21 source-free standard-error WRITE Root recovery passed.")
