#!/usr/bin/env python3
"""Source-free imported helpers retain no-op recovery write warnings."""

import json
from pathlib import Path
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]
MOSS = Path(sys.argv[1] if len(sys.argv) > 1 else ROOT / "moss").resolve()
MARGO = MOSS.with_name("margo")
PROJECT = ROOT / "tmp/phase21-sourcefree-noop-warning"
SOURCE = PROJECT / "src"
if PROJECT.exists():
    shutil.rmtree(PROJECT)
SOURCE.mkdir(parents=True, exist_ok=True)
(PROJECT / "moss.toml").write_text(
    '[project]\nname="phase21_sourcefree_noop_warning"\nversion="0.1.0"\n')
PROVIDER = SOURCE / "provider.moss"
PROVIDER.write_text('''module provider
export enum Error:
  Bad
export fn swallow(target: Int):
  try:
    target = 2
    raise Error.Bad
  recover:
    pass
fn inner(target: Int):
  try:
    target = 3
    raise Error.Bad
  recover:
    pass
export fn outer(target: Int):
  inner(target)
export fn generic_swallow(target):
  try:
    target = 9
    raise Error.Bad
  recover:
    pass
export type Box:
  value: Int
  fn swallow():
    try:
      value = 5
      raise Error.Bad
    recover:
      pass
export domain Faulty:
  value = 0
  fn Run():
    value = 2
    raise Error.Bad
  on_fail:
    pass
''')
CONSUMER = SOURCE / "main.moss"
CONSUMER.write_text('''module app
import provider
domain Sample:
  value = 1
  other = 4
  generic_value = 1
  cell = provider.Box(value = 1)
  fn Run():
    provider.swallow(value)
    provider.outer(other)
    provider.generic_swallow(generic_value)
    cell.swallow()
    echo value
    echo other
    echo generic_value
    echo cell.value
fn main():
  sample = Sample()
  faulty = provider.Faulty()
  message sample.Run()
  message faulty.Run()
  local = 1
  provider.swallow(local)
  local_box = provider.Box(value = 1)
  local_box.swallow()
''')


def run(*args):
    return subprocess.run([str(arg) for arg in args], cwd=PROJECT,
                          text=True, capture_output=True, timeout=180)


def warning_codes():
    result = run(MOSS, "check", CONSUMER, "--json")
    assert result.returncode == 0, (result.stdout, result.stderr)
    return [item["code"] for item in
            json.loads(result.stdout)["result"]["diagnostics"]]


assert warning_codes().count("RECOVER_EMPTY_AFTER_STATE_WRITE") == 4
assert "ON_FAIL_EMPTY_AFTER_STATE_WRITE" in warning_codes()
source_build = run(MARGO, "build", "--json")
assert source_build.returncode == 0, (source_build.stdout, source_build.stderr)
source_exe = json.loads(source_build.stdout)["artifacts"]["artifacts"][
    "executable"]
source_run = run(source_exe)
assert (source_run.returncode, source_run.stdout) == (0, "2\n3\n9\n5\n"), source_run

PROVIDER.rename(PROVIDER.with_suffix(".removed"))
CONSUMER.write_text(CONSUMER.read_text() + '  echo "rebuilt"\n')
sourcefree_build = run(MARGO, "build", "--json")
assert sourcefree_build.returncode == 0, (
    sourcefree_build.stdout, sourcefree_build.stderr)
sourcefree_exe = json.loads(sourcefree_build.stdout)["artifacts"]["artifacts"][
    "executable"]
sourcefree_run = run(sourcefree_exe)
assert (sourcefree_run.returncode, sourcefree_run.stdout) == (
    0, "2\n3\n9\n5\nrebuilt\n"), sourcefree_run
assert warning_codes().count("RECOVER_EMPTY_AFTER_STATE_WRITE") == 4, warning_codes()
assert "ON_FAIL_EMPTY_AFTER_STATE_WRITE" in warning_codes(), warning_codes()

print("Phase 21 source-free imported no-op recovery warning passed.")
