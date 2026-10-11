#!/usr/bin/env python3
"""A cross-crate WRITE bridge must retain Moss argument evaluation order."""

import json
import os
from pathlib import Path
import shutil
import subprocess
import sys


repo = Path(__file__).resolve().parents[2]
compiler = Path(sys.argv[1]).resolve()
margo = repo / "margo"
scratch = repo / "tmp/phase21-standard-write-order"
if scratch.exists():
    shutil.rmtree(scratch)
scratch.mkdir(parents=True)

method = '''  fn Flip(before: Int, error: std.ParseError) -> Int:
    var invalid = false
    match error:
      case Invalid:
        invalid = true
      case Overflow:
        pass
    if invalid:
      error = std.ParseError.Overflow
    else:
      error = std.ParseError.Invalid
    return 0
'''
function = '''fn FlipFn(before: Int, error: std.ParseError) -> Int:
  var invalid = false
  match error:
    case Invalid:
      invalid = true
    case Overflow:
      pass
  if invalid:
    error = std.ParseError.Overflow
  else:
    error = std.ParseError.Invalid
  return 0
'''
consumer_body = '''fn set_overflow(error: std.ParseError) -> Int:
  error = std.ParseError.Overflow
  return 0
fn main():
  carrier = CARRIER()
  var error = std.ParseError.Invalid
  ignored = carrier.Flip(set_overflow(error), error)
  match error:
    case Invalid:
      echo 1
    case Overflow:
      echo 2
  var function_error = std.ParseError.Invalid
  function_result = FLIP_FUNCTION(set_overflow(function_error), function_error)
  match function_error:
    case Invalid:
      echo 3
    case Overflow:
      echo 4
  var statement_error = std.ParseError.Invalid
  carrier.Flip(set_overflow(statement_error), statement_error)
  match statement_error:
    case Invalid:
      echo 5
    case Overflow:
      echo 6
'''


def run(*args, cwd=scratch):
    env = os.environ.copy()
    env["MOSS"] = str(compiler)
    result = subprocess.run([str(arg) for arg in args], cwd=cwd, env=env,
                            capture_output=True, text=True, timeout=180)
    assert result.returncode == 0, (
        args, result.returncode, result.stdout, result.stderr)
    return result


# The same-module result is the semantic oracle: first set Overflow, then
# Flip observes it and writes Invalid. The final match must therefore print 1.
local = scratch / "local.moss"
local.write_text("type Carrier:\n" + method + function +
                 consumer_body.replace("CARRIER()", "Carrier()")
                              .replace("FLIP_FUNCTION", "FlipFn"))
run(compiler, "check", local, "--json")
generated = scratch / "local.rs"
run(compiler, local, "-o", generated)
for optimization in ("0", "3"):
    executable = scratch / f"local-o{optimization}"
    run("rustc", "--edition=2021", "-D", "warnings",
        f"-Copt-level={optimization}", generated, "-o", executable)
    assert run(executable).stdout == "1\n3\n5\n"
assert run(compiler, "run", "--interp", local).stdout == "1\n3\n5\n"

project = scratch / "project"
(project / "src").mkdir(parents=True)
(project / "moss.toml").write_text(
    '[project]\nname="phase21_standard_write_order"\nversion="0.1.0"\n')
provider = project / "src/provider.moss"
provider.write_text("module provider\nexport type Carrier:\n" + method +
                    "export " + function)
consumer = project / "src/main.moss"
consumer.write_text("module app\nimport provider\nfn main():\n  echo 0\n")


def build_and_run(expected, *, release=False):
    options = ("build", "--release", "--json") if release else ("build", "--json")
    built = json.loads(run(margo, *options, cwd=project).stdout)
    executable = built["artifacts"]["artifacts"]["executable"]
    assert run(executable, cwd=project).stdout == expected


# Materialize both provider artifacts before removing source. The consumer
# then proves the bridge preserves left-to-right source behavior at O0/O3.
build_and_run("0\n")
build_and_run("0\n", release=True)
consumer.write_text("module app\nimport provider\n" +
                    consumer_body.replace("CARRIER()", "provider.Carrier()")
                                 .replace("FLIP_FUNCTION", "provider.FlipFn"))
run(compiler, "check", consumer, "--json", cwd=project)
build_and_run("1\n3\n5\n")
build_and_run("1\n3\n5\n", release=True)
assert run(margo, "debug", cwd=project).stdout == "1\n3\n5\n"

provider.rename(provider.with_suffix(".removed"))
consumer.write_text(consumer.read_text().replace(
    "      echo 1\n", "      echo 1\n      echo 1\n"))
run(compiler, "check", consumer, "--json", cwd=project)
build_and_run("1\n1\n3\n5\n")
build_and_run("1\n1\n3\n5\n", release=True)
print("Phase 21 standard-error WRITE argument order passed.")
