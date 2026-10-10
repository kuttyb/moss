#!/usr/bin/env python3
"""Native Root/on_fail ordering and separate failure-plan coverage."""

from pathlib import Path
import json
import shutil
import subprocess
import sys


repo = Path(__file__).resolve().parents[2]
compiler = Path(sys.argv[1] if len(sys.argv) > 1 else repo / "moss").resolve()
source = repo / "tests/tooling/fixtures/phase21_handler_failure_native.moss"
out = repo / "tmp/phase21-handler-failure-native"
out.mkdir(parents=True, exist_ok=True)
rust = out / "handler_failure.rs"
executable = out / "handler_failure"


def run(*args):
    return subprocess.run(
        [str(item) for item in args], cwd=repo, text=True,
        capture_output=True, timeout=120)


lowered = run(compiler, source, "-o", rust)
assert lowered.returncode == 0, (lowered.stdout, lowered.stderr)

rustc = shutil.which("rustc")
assert rustc is not None, "rustc is required for native handler failure coverage"
compiled = run(rustc, "--edition=2021", "-Dwarnings", rust, "-o", executable)
assert compiled.returncode == 0, (compiled.stdout, compiled.stderr)

executed = run(executable)
assert executed.returncode == 0, (executed.stdout, executed.stderr)
assert executed.stdout == "caller\n42\nworker\n-1\n10 0\nasync\n", executed.stdout

effects = run(
    compiler, "effects", "handler:Split.Run", "--source", source, "--json")
assert effects.returncode == 0, (effects.stdout, effects.stderr)
plan = json.loads(effects.stdout)["result"]["synchronization_plan"]
split = next(domain for domain in plan["domains"] if domain["domain"] == "Split")
assert [item["member_leaves"] for item in split["sync_classes"]] == [["x"], ["y"]]
handlers = {item["name"]: item for item in split["handlers"]}
assert len(handlers["Run"]["class_set"]) == 2, handlers["Run"]
assert len(handlers["Run$fail"]["class_set"]) == 1, handlers["Run$fail"]
assert handlers["Run"]["path_placement"]["kind"] == \
    "leading_conditional_continuation_split", handlers["Run"]

print("Phase 21 native nested/Root failure and class-repartition probes passed.")
