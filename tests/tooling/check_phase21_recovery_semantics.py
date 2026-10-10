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
