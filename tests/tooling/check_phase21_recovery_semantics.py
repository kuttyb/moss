#!/usr/bin/env python3
"""Focused checker coverage for Phase 21 lexical recovery and raise inference."""

from pathlib import Path
import json
import subprocess
import sys


repo = Path(__file__).resolve().parents[2]
compiler = Path(sys.argv[1] if len(sys.argv) > 1 else repo / "moss").resolve()
source = repo / "tests/tooling/fixtures/phase21_recovery_semantics.moss"
invalid_source = repo / "tests/tooling/fixtures/phase21_bare_raise_invalid.moss"


def run(*args):
    return subprocess.run([str(item) for item in args], cwd=repo,
                          text=True, capture_output=True, timeout=60)


checked = run(compiler, "check", source, "--json")
assert checked.returncode == 0, (checked.stdout, checked.stderr)
check_json = json.loads(checked.stdout)
assert check_json["ok"], check_json
warnings = check_json["result"]["diagnostics"]
assert any(item.get("code") == "RECOVER_UNREACHABLE_VARIANT"
           for item in warnings), warnings

effects = run(compiler, "effects", "may_fail", "--source", source, "--json")
assert effects.returncode == 0, (effects.stdout, effects.stderr)
effect_json = json.loads(effects.stdout)
assert effect_json["ok"], effect_json
raises = effect_json["result"]["observable_effects"]["raise_set"]
assert raises == ["enum:WorkError:Failed", "enum:WorkError:Missing"], raises

handled = run(compiler, "effects", "handled", "--source", source, "--json")
assert handled.returncode == 0, (handled.stdout, handled.stderr)
handled_json = json.loads(handled.stdout)
assert handled_json["ok"], handled_json
assert handled_json["result"]["observable_effects"]["raise_set"] == [], handled_json

invalid = run(compiler, "check", invalid_source, "--json")
assert invalid.returncode != 0, invalid.stdout
invalid_json = json.loads(invalid.stdout)
assert not invalid_json["ok"], invalid_json
assert invalid_json["error"]["code"] == "BARE_RAISE_OUTSIDE_RECOVER", invalid_json

invalid_on_fail = {
    "phase21_on_fail_nonexhaustive.moss": "NONEXHAUSTIVE_ON_FAIL",
    "phase21_on_fail_escape.moss": "ON_FAIL_MUST_HANDLE_ERRORS",
    "phase21_on_fail_reply_missing.moss": "ON_FAIL_REPLY_REQUIRED",
    "phase21_on_fail_ambiguous_binding.moss": "AMBIGUOUS_RECOVERY_BINDING",
}
for filename, code in invalid_on_fail.items():
    failure_source = repo / "tests/tooling/fixtures" / filename
    failure = run(compiler, "check", failure_source, "--json")
    assert failure.returncode != 0, (filename, failure.stdout)
    failure_json = json.loads(failure.stdout)
    assert failure_json["error"]["code"] == code, (filename, failure_json)


# Recovery enters from the actual exceptional edge. A move before the raise
# must remain moved, including through a nested try or a fallible consuming
# call. Reinitializing a mutable binding in the recovery arm is legal.
ownership_scratch = repo / "tmp/phase21_recovery_ownership"
ownership_scratch.mkdir(parents=True, exist_ok=True)
ownership_header = '''enum Failure:
  Bad
  Other
enum Data:
  Have(value: String)
'''
ownership_cases = {
    "consumed_before_raise": '''fn main():
  var item = Data.Have(value: "seven")
  try:
    match consume item:
      case Have(value):
        raise Failure.Bad
  recover Failure.Bad:
    match item:
      case Have(value):
        echo value
''',
    "consumed_by_fallible_call": '''fn consume_then_fail(data: Data):
  match consume data:
    case Have(value):
      raise Failure.Bad
fn main():
  var item = Data.Have(value: "seven")
  try:
    consume_then_fail(item)
  recover Failure.Bad:
    match item:
      case Have(value):
        echo value
''',
    "consumed_in_nested_try": '''fn main():
  var item = Data.Have(value: "seven")
  try:
    try:
      match consume item:
        case Have(value):
          raise Failure.Bad
    recover Failure.Other:
      pass
  recover Failure.Bad:
    match item:
      case Have(value):
        echo value
''',
}
for name, body in ownership_cases.items():
    case_source = ownership_scratch / f"{name}.moss"
    case_source.write_text(ownership_header + body)
    failure = run(compiler, "check", case_source, "--json")
    diagnostic = json.loads(failure.stdout)
    assert failure.returncode != 0, (name, diagnostic)
    assert diagnostic["error"]["code"] == "OWNERSHIP_USE_AFTER_CONSUME", (
        name, diagnostic)

reinitialized_source = ownership_scratch / "reinitialized.moss"
reinitialized_source.write_text(ownership_header + '''fn main():
  var item = Data.Have(value: "seven")
  try:
    match consume item:
      case Have(value):
        raise Failure.Bad
  recover Failure.Bad:
    item = Data.Have(value: "new")
    match item:
      case Have(value):
        echo value
''')
reinitialized = run(compiler, "check", reinitialized_source, "--json")
assert reinitialized.returncode == 0, reinitialized.stdout
reinitialized_rust = ownership_scratch / "reinitialized.rs"
reinitialized_native = ownership_scratch / "reinitialized"
assert run(compiler, reinitialized_source, "-o", reinitialized_rust).returncode == 0
compiled = run("rustc", "-D", "warnings", reinitialized_rust, "-o",
               reinitialized_native)
assert compiled.returncode == 0, compiled.stderr
assert run(reinitialized_native).stdout == "new\n"

# D7 is variant-specific: the Other edge retains item even though the Bad
# edge consumed it. Compile and run both paths through the native slot.
variant_source = ownership_scratch / "variant_specific.moss"
variant_source.write_text(ownership_header + '''fn main():
  var item = Data.Have(value: "seven")
  try:
    if true:
      match consume item:
        case Have(value):
          raise Failure.Bad
    else:
      raise Failure.Other
  recover Failure.Bad:
    pass
  recover Failure.Other:
    match item:
      case Have(value):
        echo value
''')
variant_checked = run(compiler, "check", variant_source, "--json")
assert variant_checked.returncode == 0, variant_checked.stdout
for branch, expected in (("true", ""), ("false", "seven\n")):
    source_text = variant_source.read_text().replace("if true:", f"if {branch}:")
    variant_source.write_text(source_text)
    native_rust = ownership_scratch / f"variant_specific_{branch}.rs"
    native_binary = ownership_scratch / f"variant_specific_{branch}"
    assert run(compiler, variant_source, "-o", native_rust).returncode == 0
    compiled = run("rustc", "--edition=2021", "-D", "warnings",
                   native_rust, "-o", native_binary)
    assert compiled.returncode == 0, compiled.stderr
    assert run(native_binary).stdout == expected

# A recovery READ through a helper, reassignment after a consumed branch,
# and post-try use all follow the same checked ownership path.
slot_source = ownership_scratch / "variant_slot_reassign.moss"
slot_source.write_text(ownership_header + '''fn show(data: Data):
  match data:
    case Have(value):
      echo value
fn main():
  var item = Data.Have(value: "seven")
  try:
    if true:
      match consume item:
        case Have(value):
          raise Failure.Bad
    else:
      raise Failure.Other
  recover Failure.Bad:
    item = Data.Have(value: "new")
  recover Failure.Other:
    show(item)
  match item:
    case Have(value):
      echo value
''')
for branch, expected in (("true", "new\n"),
                         ("false", "seven\nseven\n")):
    slot_source.write_text(slot_source.read_text().replace(
        "if true:" if branch == "false" else "if false:", f"if {branch}:"))
    assert run(compiler, "check", slot_source, "--json").returncode == 0
    native_rust = ownership_scratch / f"variant_slot_{branch}.rs"
    native_binary = ownership_scratch / f"variant_slot_{branch}"
    assert run(compiler, slot_source, "-o", native_rust).returncode == 0
    compiled = run("rustc", "--edition=2021", "-D", "warnings",
                   native_rust, "-o", native_binary)
    assert compiled.returncode == 0, compiled.stderr
    assert run(native_binary).stdout == expected

branch_assign_source = ownership_scratch / "variant_branch_reassign.moss"
branch_assign_source.write_text(ownership_header + '''fn main():
  var item = Data.Have(value: "seven")
  try:
    if true:
      match consume item:
        case Have(value):
          raise Failure.Bad
    else:
      raise Failure.Other
  recover Failure.Bad:
    pass
  recover Failure.Other:
    match item:
      case Have(value):
        echo value
  if true:
    item = Data.Have(value: "newer")
  else:
    item = Data.Have(value: "other")
  match item:
    case Have(value):
      echo value
''')
assert run(compiler, "check", branch_assign_source, "--json").returncode == 0
native_rust = ownership_scratch / "branch_assign.rs"
native_binary = ownership_scratch / "branch_assign"
assert run(compiler, branch_assign_source, "-o", native_rust).returncode == 0
compiled = run("rustc", "--edition=2021", "-D", "warnings",
               native_rust, "-o", native_binary)
assert compiled.returncode == 0, compiled.stderr
assert run(native_binary).stdout == "newer\n"

post_assign_source = ownership_scratch / "variant_post_try_reassign.moss"
for branch, expected in (("true", "new\n"),
                         ("false", "seven\nnew\n")):
    post_assign_source.write_text(ownership_header + f'''fn main():
  var item = Data.Have(value: "seven")
  try:
    if {branch}:
      match consume item:
        case Have(value):
          raise Failure.Bad
    else:
      raise Failure.Other
  recover Failure.Bad:
    pass
  recover Failure.Other:
    match item:
      case Have(value):
        echo value
  item = Data.Have(value: "new")
  match item:
    case Have(value):
      echo value
''')
    assert run(compiler, "check", post_assign_source, "--json").returncode == 0
    native_rust = ownership_scratch / f"post_assign_{branch}.rs"
    native_binary = ownership_scratch / f"post_assign_{branch}"
    assert run(compiler, post_assign_source, "-o", native_rust).returncode == 0
    compiled = run("rustc", "--edition=2021", "-D", "warnings",
                   native_rust, "-o", native_binary)
    assert compiled.returncode == 0, compiled.stderr
    assert run(native_binary).stdout == expected

for fixture_name, recovered_output in (
        ("phase21_helper_consume_variant", "seven\n"),
        ("phase21_string_method_variant", "5\n")):
    fixture = repo / "tests/tooling/fixtures" / f"{fixture_name}.moss"
    for branch, expected in (("true", ""), ("false", recovered_output)):
        case_source = ownership_scratch / f"{fixture_name}_{branch}.moss"
        case_source.write_text(fixture.read_text().replace(
            "if true:", f"if {branch}:"))
        assert run(compiler, "check", case_source, "--json").returncode == 0
        native_rust = case_source.with_suffix(".rs")
        native_binary = case_source.with_suffix("")
        assert run(compiler, case_source, "-o", native_rust).returncode == 0
        compiled = run("rustc", "--edition=2021", "-D", "warnings",
                       native_rust, "-o", native_binary)
        assert compiled.returncode == 0, compiled.stderr
        assert run(native_binary).stdout == expected

for name in ("range_fileio", "range_direct", "fileio_direct"):
    fileio_source = repo / "tests/tooling/fixtures/generic_specialization" / f"{name}.moss"
    fileio_check = run(compiler, "check", fileio_source, "--json")
    assert fileio_check.returncode == 0, (name, fileio_check.stdout,
                                          fileio_check.stderr)

unclosed_source = repo / "tmp/phase21-unclosed-try-file.moss"
unclosed_source.parent.mkdir(parents=True, exist_ok=True)
unclosed_source.write_text('''fn main():
  try:
    f = FileIO.open("tmp/phase21-unclosed-try-file.txt", create)
    pass
  recover:
    pass
''')
unclosed = run(compiler, "check", unclosed_source, "--json")
assert unclosed.returncode != 0, unclosed.stdout
unclosed_json = json.loads(unclosed.stdout)
assert unclosed_json["error"]["code"] == "FILEIO_MUST_CLOSE", unclosed_json

interpreted = run(compiler, "run", "--interp", source)
assert interpreted.returncode == 0, (interpreted.stdout, interpreted.stderr)
assert interpreted.stdout == "missing\nfailed\nWorkError.Missing\n", interpreted.stdout

traced = run(compiler, "run", "--interp", "--trace", source)
assert traced.returncode == 0, (traced.stdout, traced.stderr)
events = [json.loads(line) for line in traced.stderr.splitlines() if line.strip()]
assert sum(item.get("event") == "raise" for item in events) == 4, events
assert sum(item.get("event") == "recover" for item in events) == 4, events

print("Phase 21 recovery checker, effects, and Fast Debug probes passed.")
