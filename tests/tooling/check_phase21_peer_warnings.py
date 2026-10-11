#!/usr/bin/env python3
"""EXT-07: actual recovery lock inflation and state-writing no-op catch-alls."""

import json
from pathlib import Path
import subprocess
import sys


ROOT = Path(__file__).resolve().parents[2]
COMPILER = Path(sys.argv[1] if len(sys.argv) > 1 else ROOT / "moss").resolve()
FIXTURES = ROOT / "tests/tooling/fixtures"
SCRATCH = ROOT / "tmp/phase21-peer-warnings"
SCRATCH.mkdir(parents=True, exist_ok=True)


def run(*argv):
    return subprocess.run([str(arg) for arg in argv], cwd=ROOT,
                          text=True, capture_output=True, timeout=90)


def checked(path):
    result = run(COMPILER, "check", path, "--json")
    assert result.returncode == 0, (path, result.stdout, result.stderr)
    document = json.loads(result.stdout)
    assert document["ok"], document
    return document["result"]["diagnostics"]


def warning(diagnostics, code):
    matches = [item for item in diagnostics if item["code"] == code]
    assert len(matches) == 1, (code, diagnostics)
    assert matches[0]["severity"] == "warning", matches
    return matches[0]


lockfree = FIXTURES / "phase21_peer_lockfree_inflation.moss"
warnings = checked(lockfree)
inflation = warning(warnings, "RECOVER_LOCK_INFLATION")
assert "value NONE->EXCLUSIVE" in inflation["message"], inflation
assert "recovery provenance: line 14" in inflation["message"], inflation
assert inflation["line"] == 10, inflation

combined = FIXTURES / "phase21_peer_warnings.moss"
warnings = checked(combined)
assert {item["code"] for item in warnings} == {
    "RECOVER_LOCK_INFLATION",
    "RECOVER_EMPTY_AFTER_STATE_WRITE",
    "ON_FAIL_EMPTY_AFTER_STATE_WRITE",
}, warnings
assert "value SHARED->EXCLUSIVE" in warning(
    warnings, "RECOVER_LOCK_INFLATION")["message"]
assert "try at line 18 (value)" in warning(
    warnings, "RECOVER_EMPTY_AFTER_STATE_WRITE")["message"]
assert "abandoned Root attempt (value)" in warning(
    warnings, "ON_FAIL_EMPTY_AFTER_STATE_WRITE")["message"]

# A leading conditional can defer the recovery class to its selected path.
conditional = SCRATCH / "conditional.moss"
conditional.write_text('''enum Error:
  Bad
fn fail():
  raise Error.Bad
domain Sample:
  value = 10
  fn Run(flag: Bool):
    if flag:
      try:
        echo value
        fail()
      recover Error.Bad:
        value = 0
    else:
      echo 1
fn main():
  sample = Sample()
  message sample.Run(true)
''')
conditional_warning = warning(checked(conditional), "RECOVER_LOCK_INFLATION")
assert "then class" in conditional_warning["message"], conditional_warning
assert "recovery provenance: line 12" in conditional_warning["message"]

for source, expected in ((lockfree, "10\n"), (combined, "10\n")):
    rust = SCRATCH / (source.stem + ".rs")
    binary = SCRATCH / source.stem
    generated = run(COMPILER, source, "-o", rust)
    assert generated.returncode == 0, generated.stderr
    compiled = run("rustc", "--edition=2021", "-D", "warnings",
                   rust, "-o", binary)
    assert compiled.returncode == 0, compiled.stderr
    native = run(binary)
    debug = run(COMPILER, "run", "--interp", source)
    assert (native.returncode, native.stdout) == (0, expected), native
    assert (debug.returncode, debug.stdout) == (0, expected), debug

# Read-only attempts and explicit cleanup have no partial-mutation warning.
negative_source = SCRATCH / "no_false_positive.moss"
negative_source.write_text('''enum Error:
  Bad
fn fail():
  raise Error.Bad
domain Sample:
  value = 1
  fn ReadOnly():
    try:
      echo value
      fail()
    recover:
      pass
  fn Repaired():
    try:
      value = 2
      fail()
    recover:
      value = 0
  fn RootRepaired():
    value = 3
    fail()
  on_fail:
    value = 0
fn main():
  sample = Sample()
  message sample.ReadOnly()
  message sample.Repaired()
  message sample.RootRepaired()
''')
negative = checked(negative_source)
assert not ({"RECOVER_EMPTY_AFTER_STATE_WRITE",
             "ON_FAIL_EMPTY_AFTER_STATE_WRITE"} &
            {item["code"] for item in negative}), negative

# A write after the only raising call cannot have occurred on the error edge.
post_failure_write = SCRATCH / "post_failure_write.moss"
post_failure_write.write_text('''enum Error:
  Bad
fn maybe_fail(flag: Bool):
  if flag:
    raise Error.Bad
domain Sample:
  value = 1
  fn Run(flag: Bool):
    try:
      maybe_fail(flag)
      value = 2
    recover:
      pass
  fn Root(flag: Bool):
    maybe_fail(flag)
    value = 3
  on_fail:
    pass
fn main():
  sample = Sample()
  message sample.Run(true)
  message sample.Root(true)
''')
post_failure = checked(post_failure_write)
assert not ({"RECOVER_EMPTY_AFTER_STATE_WRITE",
             "ON_FAIL_EMPTY_AFTER_STATE_WRITE"} &
            {item["code"] for item in post_failure}), post_failure

# Repeated loops can raise after a write from a successful prior iteration.
loop_source = SCRATCH / "prior_iteration_write.moss"
loop_source.write_text('''enum Error:
  Bad
fn maybe_fail(flag: Bool):
  if flag:
    raise Error.Bad
domain Sample:
  value = 1
  fn Run():
    var index = 0
    try:
      while index < 2:
        maybe_fail(index == 1)
        value = index
        index = index + 1
    recover:
      pass
fn main():
  sample = Sample()
  message sample.Run()
''')
assert warning(checked(loop_source),
               "RECOVER_EMPTY_AFTER_STATE_WRITE")["line"] == 15

condition_source = SCRATCH / "later_condition_raise.moss"
condition_source.write_text('''enum Error:
  Bad
fn keep_going(index: Int) -> Bool:
  if index == 1:
    raise Error.Bad
  return index < 2
domain Sample:
  value = 1
  fn Run():
    var index = 0
    try:
      while keep_going(index):
        value = index
        index = index + 1
    recover:
      pass
fn main():
  sample = Sample()
  message sample.Run()
''')
assert warning(checked(condition_source),
               "RECOVER_EMPTY_AFTER_STATE_WRITE")["line"] == 15

# A helper's WRITE parameter can be a protected domain leaf at one concrete
# call site while another call passes only an ordinary local.
helper_source = SCRATCH / "helper_state_write.moss"
helper_source.write_text('''enum Error:
  Bad
fn swallow(target: Int):
  try:
    target = 2
    raise Error.Bad
  recover:
    pass
domain Sample:
  value = 1
  fn StateCall():
    swallow(value)
  fn LocalCall():
    var scratch = 1
    swallow(scratch)
fn main():
  sample = Sample()
  message sample.StateCall()
  message sample.LocalCall()
''')
helper_warning = warning(checked(helper_source),
                         "RECOVER_EMPTY_AFTER_STATE_WRITE")
assert helper_warning["line"] == 7, helper_warning
assert "Sample.StateCall' call at line 12 (value)" in (
    helper_warning["message"]), helper_warning
effects = run(COMPILER, "effects", "handler:Sample.StateCall", "--source",
              helper_source, "--json")
assert effects.returncode == 0, effects.stdout
handlers = json.loads(effects.stdout)["result"]["synchronization"][
    "instances"][0]["handlers"]
assert any(item["write_set"] == ["value"] and
           "StateCall" in item["handler_identity"] for item in handlers), handlers
helper_rust = SCRATCH / "helper_state_write.rs"
helper_binary = SCRATCH / "helper_state_write"
assert run(COMPILER, helper_source, "-o", helper_rust).returncode == 0
helper_compiled = run("rustc", "--edition=2021", "-D", "warnings",
                      helper_rust, "-o", helper_binary)
assert helper_compiled.returncode == 0, helper_compiled.stderr
helper_native = run(helper_binary)
assert (helper_native.returncode, helper_native.stdout) == (0, "")
helper_debug = run(COMPILER, "run", "--interp", helper_source)
assert (helper_debug.returncode, helper_debug.stdout) == (0, "")

chain_source = SCRATCH / "helper_chain.moss"
chain_source.write_text('''enum Error:
  Bad
fn swallow(target: Int):
  try:
    target = 2
    raise Error.Bad
  recover:
    pass
fn outer(item: Int) = swallow(item)
domain Sample:
  value = 1
  fn Run():
    outer(value)
fn main():
  sample = Sample()
  message sample.Run()
''')
chain_warning = warning(checked(chain_source),
                        "RECOVER_EMPTY_AFTER_STATE_WRITE")
assert "helper 'swallow'" in chain_warning["message"], chain_warning
assert "(value)" in chain_warning["message"], chain_warning
chain_rust = SCRATCH / "helper_chain.rs"
chain_binary = SCRATCH / "helper_chain"
assert run(COMPILER, chain_source, "-o", chain_rust).returncode == 0
chain_compiled = run("rustc", "--edition=2021", "-D", "warnings",
                     chain_rust, "-o", chain_binary)
assert chain_compiled.returncode == 0, chain_compiled.stderr
chain_native = run(chain_binary)
chain_debug = run(COMPILER, "run", "--interp", chain_source)
assert (chain_native.returncode, chain_native.stdout) == (0, "")
assert (chain_debug.returncode, chain_debug.stdout) == (0, "")

nested_source = SCRATCH / "nested_helper_call.moss"
nested_source.write_text('''enum Error:
  Bad
fn swallow(target: Int) -> Int:
  try:
    target = 2
    raise Error.Bad
  recover:
    pass
  return 7
fn choose(left: Int, right: Int) -> Int:
  return right
domain Sample:
  value = 1
  fn Run():
    result = choose(0, swallow(value))
    echo result
fn main():
  sample = Sample()
  message sample.Run()
''')
nested_warning = warning(checked(nested_source),
                         "RECOVER_EMPTY_AFTER_STATE_WRITE")
assert "Sample.Run' call at line 15 (value)" in (
    nested_warning["message"]), nested_warning
nested_rust = SCRATCH / "nested_helper_call.rs"
nested_binary = SCRATCH / "nested_helper_call"
assert run(COMPILER, nested_source, "-o", nested_rust).returncode == 0
nested_compiled = run("rustc", "--edition=2021", "-D", "warnings",
                      nested_rust, "-o", nested_binary)
assert nested_compiled.returncode == 0, nested_compiled.stderr
nested_native = run(nested_binary)
nested_debug = run(COMPILER, "run", "--interp", nested_source)
assert (nested_native.returncode, nested_native.stdout) == (0, "7\n")
assert (nested_debug.returncode, nested_debug.stdout) == (0, "7\n")

print("Phase 21 EXT-07 warning regressions passed.")
