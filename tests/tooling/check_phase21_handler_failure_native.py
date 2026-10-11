#!/usr/bin/env python3
"""Native Root/on_fail ordering and separate failure-plan coverage."""

from pathlib import Path
import json
import os
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
assert executed.stdout == \
    "caller\n7\n42\nworker\n7\n-1\n10 0\nasync\n5\n", executed.stdout

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

module_project = repo / "tests/tooling/fixtures/phase21_failure_modules"
module_env = os.environ.copy()
module_env["MOSS"] = str(compiler)
module_clean = subprocess.run(
    [str(repo / "margo"), "clean"], cwd=module_project, text=True,
    capture_output=True, timeout=120, env=module_env)
assert module_clean.returncode == 0, (module_clean.stdout, module_clean.stderr)
module_run = subprocess.run(
    [str(repo / "margo"), "run"], cwd=module_project, text=True,
    capture_output=True, timeout=120, env=module_env)
assert module_run.returncode == 0, (module_run.stdout, module_run.stderr)
assert module_run.stdout == "4\n5\n", module_run.stdout

print("Phase 21 native nested/Root failure and class-repartition probes passed.")
