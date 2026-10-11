#!/usr/bin/env python3
"""Recover joins ownership at each expression's actual raising subsite."""

import json
from pathlib import Path
import subprocess
import sys


repo = Path(__file__).resolve().parents[2]
compiler = Path(sys.argv[1]).resolve()
fixtures = repo / "tests/tooling/fixtures"
scratch = repo / "tmp/phase21-expression-ownership-edges"
scratch.mkdir(parents=True, exist_ok=True)


def run(*args):
    return subprocess.run([str(arg) for arg in args], cwd=repo,
                          capture_output=True, text=True, timeout=180)


positive = fixtures / "phase21_expression_raise_before_consume.moss"
checked = run(compiler, "check", positive, "--json")
assert checked.returncode == 0, (checked.stdout, checked.stderr)
effects = run(compiler, "effects", "function:fail", "--source", positive,
              "--json")
assert effects.returncode == 0, (effects.stdout, effects.stderr)
assert json.loads(effects.stdout)["result"]["observable_effects"]["raise_set"] == [
    "enum:Failure:Bad"], effects.stdout
generated = scratch / "positive.rs"
lowered = run(compiler, positive, "-o", generated)
assert lowered.returncode == 0, (lowered.stdout, lowered.stderr)
for opt in ("0", "3"):
    binary = scratch / f"positive-o{opt}"
    compiled = run("rustc", "--edition=2021", "-D", "warnings",
                   f"-Copt-level={opt}", generated, "-o", binary)
    assert compiled.returncode == 0, (opt, compiled.stdout, compiled.stderr)
    native = run(binary)
    assert native.returncode == 0 and native.stdout == "3\n", (
        opt, native.returncode, native.stdout, native.stderr)
debug = run(compiler, "run", "--interp", positive)
assert debug.returncode == 0 and debug.stdout == "3\n", (
    debug.returncode, debug.stdout, debug.stderr)

negative = fixtures / "phase21_expression_consume_before_raise.moss"
rejected = run(compiler, "check", negative, "--json")
assert rejected.returncode != 0, (rejected.stdout, rejected.stderr)
diagnostic = json.loads(rejected.stdout)
assert diagnostic["error"]["code"] == "OWNERSHIP_USE_AFTER_CONSUME", diagnostic

# The argument and callee may raise the same variant. Both ownership states
# reach the same arm; preserving only the early argument edge would be unsound.
shared = fixtures / "phase21_expression_shared_variant_edges.moss"
rejected = run(compiler, "check", shared, "--json")
assert rejected.returncode != 0, (rejected.stdout, rejected.stderr)
diagnostic = json.loads(rejected.stdout)
assert diagnostic["error"]["code"] == "OWNERSHIP_USE_AFTER_CONSUME", diagnostic

# A pipeline evaluates its source before transferring the reduce initializer.
# The source raise keeps data available; a callback raise after the transfer
# reaches the same arm with data consumed and must keep the rejection.
pipeline_positive = fixtures / "phase21_pipeline_source_before_consume.moss"
checked = run(compiler, "check", pipeline_positive, "--json")
assert checked.returncode == 0, (checked.stdout, checked.stderr)
generated = scratch / "pipeline-positive.rs"
lowered = run(compiler, pipeline_positive, "-o", generated)
assert lowered.returncode == 0, (lowered.stdout, lowered.stderr)
for opt in ("0", "3"):
    binary = scratch / f"pipeline-positive-o{opt}"
    compiled = run("rustc", "--edition=2021", "-D", "warnings",
                   f"-Copt-level={opt}", generated, "-o", binary)
    assert compiled.returncode == 0, (opt, compiled.stdout, compiled.stderr)
    native = run(binary)
    assert native.returncode == 0 and native.stdout == "3\n", (
        opt, native.returncode, native.stdout, native.stderr)
debug = run(compiler, "run", "--interp", pipeline_positive)
assert debug.returncode == 0 and debug.stdout == "3\n", (
    debug.returncode, debug.stdout, debug.stderr)

pipeline_shared = fixtures / "phase21_pipeline_shared_variant_edges.moss"
rejected = run(compiler, "check", pipeline_shared, "--json")
assert rejected.returncode != 0, (rejected.stdout, rejected.stderr)
diagnostic = json.loads(rejected.stdout)
assert diagnostic["error"]["code"] == "OWNERSHIP_USE_AFTER_CONSUME", diagnostic

print("Phase 21 expression-originated exceptional ownership edges passed.")
