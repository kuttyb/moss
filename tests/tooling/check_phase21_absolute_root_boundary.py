#!/usr/bin/env python3
"""Every admitted Root is a total typed-error boundary in every ingress mode."""

import json
from pathlib import Path
import subprocess
import sys


ROOT = Path(__file__).resolve().parents[2]
COMPILER = Path(sys.argv[1] if len(sys.argv) > 1 else ROOT / "moss").resolve()
MARGO = COMPILER.with_name("margo")
SCRATCH = ROOT / "tmp" / "phase21_absolute_root_boundary"
SCRATCH.mkdir(parents=True, exist_ok=True)


def run(*args, cwd=ROOT, expected=0):
    result = subprocess.run([str(arg) for arg in args], cwd=cwd,
                            capture_output=True, text=True, timeout=180)
    if expected is not None and result.returncode != expected:
        raise AssertionError((args, result.returncode, result.stdout,
                              result.stderr))
    return result


def check_source(name, source, error=None, output=None):
    path = SCRATCH / f"{name}.moss"
    path.write_text(source)
    checked = run(COMPILER, "check", path, "--json",
                  expected=0 if error is None else 1)
    record = json.loads(checked.stdout)
    if error is not None:
        assert record["error"]["code"] == error, (name, record)
        return
    assert record["ok"], (name, record)
    for profile, moss_flag, rust_flag in (
            ("debug", "-O0", "-Copt-level=0"),
            ("optimized", "-Oshared-memory", "-Copt-level=3")):
        rust = SCRATCH / f"{name}-{profile}.rs"
        executable = SCRATCH / f"{name}-{profile}"
        run(COMPILER, path, moss_flag, "-o", rust)
        run("rustc", "--edition=2021", "-Dwarnings", rust_flag, rust,
            "-o", executable)
        assert run(executable).stdout == output, (name, profile)
    assert run(COMPILER, "run", "--interp", path).stdout == output, name


error_decl = '''enum E:
  Bad
  Other
'''

# A lexical recover in main cannot catch an escaped synchronous Root result.
check_source("sync_nested_main_recover", error_decl + '''domain Worker:
  fn Fetch() -> Int:
    raise E.Bad
fn main():
  worker = Worker()
  if true:
    try:
      echo message worker.Fetch()
    recover E.Bad:
      echo 99
''', error="ROOT_UNHANDLED_RAISE")

# Executor admission is equally total, including transitive typed effects.
check_source("executor_generic_unhandled", error_decl + '''fn generic(value):
  if value == 0:
    raise E.Bad
  return value
domain Worker:
  fn Run():
    echo generic(0)
fn main():
  worker = Worker()
  executor = Executor().threads(1).start()
  executor.invoke(worker.Run())
  executor.join()
''', error="ROOT_UNHANDLED_RAISE")

# A nested handler's on_fail does not run at that nested message. The outer
# Root must have its own coverage for errors propagated from nested handlers.
nested = error_decl + '''domain Leaf:
  fn Fail() -> Int:
    raise E.Bad
  on_fail E.Bad:
    reply 99
domain Outer:
  domainroutes(leaf: Leaf)
  fn Run() -> Int:
    reply message leaf.Fail()
'''
check_source("nested_on_fail_not_root", nested + '''fn main():
  leaf = Leaf()
  outer = Outer(leaf: leaf)
  try:
    echo message outer.Run()
  recover E.Bad:
    echo 99
''', error="ROOT_UNHANDLED_RAISE")
check_source("nested_root_covered", nested.replace(
    "    reply message leaf.Fail()\n",
    "    reply message leaf.Fail()\n  on_fail E.Bad:\n    reply 7\n") + '''fn main():
  leaf = Leaf()
  outer = Outer(leaf: leaf)
  echo message outer.Run()
''', output="7\n")

# The failure trailer itself has an empty escaping raise set, even if its new
# typed error comes from an ordinary helper. It may recover that new error
# lexically and then satisfy the normal reply contract.
trailer = error_decl + '''fn fail_in_trailer() -> Int:
  raise E.Other
domain Worker:
  fn Run() -> Int:
    raise E.Bad
  on_fail E.Bad:
    reply fail_in_trailer()
fn main():
  worker = Worker()
  echo message worker.Run()
'''
check_source("trailer_transitive_raise", trailer,
             error="ON_FAIL_MUST_HANDLE_ERRORS")
check_source("trailer_recovered_raise", trailer.replace(
    "    reply fail_in_trailer()\n",
    "    try:\n      reply fail_in_trailer()\n"
    "    recover E.Other:\n      reply 8\n"), output="8\n")

# A compiled provider handler without an on_fail remains unsafe as a Root even
# if the consumer's main attempts lexical recovery. Source removal must not
# reset the provider's checked effect summary or create an ingress exception.
project = SCRATCH / "source_free"
source_dir = project / "src"
source_dir.mkdir(parents=True, exist_ok=True)
(project / "Moss.toml").write_text(
    '[project]\nname = "phase21_absolute_root_source_free"\nversion = "0.1.0"\n')
provider = source_dir / "provider.moss"
consumer = source_dir / "main.moss"
provider.write_text('''module provider
export enum E:
  Bad
export domain Leaf:
  fn Fail() -> Int:
    raise E.Bad
  on_fail E.Bad:
    reply 99
export domain Bare:
  fn Fail() -> Int:
    raise E.Bad
export fn generic(value) -> Int:
  if value == 0:
    raise E.Bad
  return value
''')


def project_check(expected=None):
    result = run(COMPILER, "check", consumer, "--json", cwd=project,
                 expected=0 if expected is None else 1)
    record = json.loads(result.stdout)
    if expected is None:
        assert record["ok"], record
    else:
        assert record["error"]["code"] == expected, record


consumer.write_text('''module app
import provider
fn main():
  leaf = provider.Leaf()
  try:
    echo message leaf.Fail()
  recover provider.E.Bad:
    echo 88
''')
project_check()
built = json.loads(run(MARGO, "build", "--json", cwd=project).stdout)
assert run(built["artifacts"]["artifacts"]["executable"],
           cwd=project).stdout == "99\n"
# Keep package source available for R2 interface freshness validation.
project_check()
built = json.loads(run(MARGO, "build", "--json", cwd=project).stdout)
assert run(built["artifacts"]["artifacts"]["executable"],
           cwd=project).stdout == "99\n"

# The provider's handler has no trailer. A consumer-side lexical recover is
# not a Root-level exception channel, even after the provider source is gone.
consumer.write_text('''module app
import provider
fn main():
  bare = provider.Bare()
  try:
    echo message bare.Fail()
  recover provider.E.Bad:
    echo 88
''')
project_check("ROOT_UNHANDLED_RAISE")

# A handler that calls a source-free generic function must cover its precise
# imported raise. This is a Root check, rather than a rustc/linker failure.
consumer.write_text('''module app
import provider
domain Worker:
  fn Run() -> Int:
    reply provider.generic(0)
fn main():
  worker = Worker()
  try:
    echo message worker.Run()
  recover provider.E.Bad:
    echo 99
''')
project_check("ROOT_UNHANDLED_RAISE")
consumer.write_text(consumer.read_text().replace(
    "    reply provider.generic(0)\n",
    "    reply provider.generic(0)\n"
    "  on_fail provider.E.Bad:\n    reply 6\n"))
project_check()
built = json.loads(run(MARGO, "build", "--json", cwd=project).stdout)
assert run(built["artifacts"]["artifacts"]["executable"],
           cwd=project).stdout == "6\n"

print("Phase 21 absolute Root typed-error boundary passed")
