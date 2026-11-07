#!/usr/bin/env python3
"""Exact checker-edge selection for nested Fast Debug call specialization."""

from pathlib import Path
import os
import subprocess
import sys


repo = Path(__file__).resolve().parents[2]
compiler = Path(sys.argv[1] if len(sys.argv) > 1 else repo / "moss").resolve()
source = repo / "tests/tooling/fixtures/phase28_w5_call_specialization.moss"
callback_source = repo / "tests/phase26_w2_pipeline_callback.moss"


def run(*args, env=None):
    return subprocess.run([str(arg) for arg in args], cwd=repo, env=env,
                          text=True, capture_output=True, timeout=90)


interpreted = run(compiler, "run", "--interp", source)
assert interpreted.returncode == 0, (interpreted.stdout, interpreted.stderr)
assert interpreted.stdout == "15\n", interpreted.stdout

# Native execution is a control for the same source and output.
scratch = repo / "tmp/phase28-w5-call-specialization"
scratch.mkdir(parents=True, exist_ok=True)
rust = scratch / "positive.rs"
native = scratch / "positive"
generated = run(compiler, source, "-o", rust)
assert generated.returncode == 0, (generated.stdout, generated.stderr)
compiled = run("rustc", "-D", "warnings", rust, "-o", native)
assert compiled.returncode == 0, compiled.stderr
native_result = run(native)
assert native_result.returncode == 0 and native_result.stdout == "15\n", (
    native_result.stdout, native_result.stderr)
optimized_native = scratch / "positive-optimized"
optimized_compiled = run("rustc", "-D", "warnings", "-C", "opt-level=3",
                         rust, "-o", optimized_native)
assert optimized_compiled.returncode == 0, optimized_compiled.stderr
optimized_result = run(optimized_native)
assert optimized_result.returncode == 0 and optimized_result.stdout == "15\n", (
    optimized_result.stdout, optimized_result.stderr)

# Each mutation goes through the Fast Debug production edge resolver. A
# missing, duplicated, perturbed specialization, or wrong target identity
# must not fall back to runtime argument types or the generic edge.
mutations = (
    "MOSS_TEST_CONSUMER_DROP_SEMANTIC_CALL_EDGE",
    "MOSS_TEST_CONSUMER_DUPLICATE_SEMANTIC_CALL_EDGE",
    "MOSS_TEST_CONSUMER_PERTURB_SEMANTIC_CALL_EDGE",
    "MOSS_TEST_CONSUMER_PERTURB_SEMANTIC_CALL_EDGE_TARGET",
)
for mutation in mutations:
    env = os.environ.copy()
    for name in mutations:
        env.pop(name, None)
    env[mutation] = "1"
    rejected = run(compiler, "run", "--interp", source, env=env)
    assert rejected.returncode != 0, (mutation, rejected.stdout, rejected.stderr)
    assert "MOSS_INTERNAL_INVARIANT" in rejected.stderr, (
        mutation, rejected.stderr)

callback_positive = run(compiler, "run", "--interp", callback_source)
assert callback_positive.returncode == 0 and \
    callback_positive.stdout.strip() == "[2, 3, 4]", (
        callback_positive.stdout, callback_positive.stderr)
callback_mutations = (
    "MOSS_TEST_CONSUMER_DROP_CALLBACK_SPECIALIZATION",
    "MOSS_TEST_CONSUMER_DUPLICATE_CALLBACK_SPECIALIZATION",
    "MOSS_TEST_CONSUMER_PERTURB_CALLBACK_SPECIALIZATION",
    "MOSS_TEST_CONSUMER_PERTURB_CALLBACK_TARGET",
)
for mutation in callback_mutations:
    env = os.environ.copy()
    for name in callback_mutations:
        env.pop(name, None)
    env[mutation] = "1"
    rejected = run(compiler, "run", "--interp", callback_source, env=env)
    assert rejected.returncode != 0, (mutation, rejected.stdout, rejected.stderr)
    assert "MOSS_INTERNAL_INVARIANT" in rejected.stderr, (
        mutation, rejected.stderr)
