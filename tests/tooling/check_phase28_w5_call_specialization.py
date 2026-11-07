#!/usr/bin/env python3
"""Exact checker-edge selection for nested Fast Debug call specialization."""

from pathlib import Path
import os
import subprocess
import sys


repo = Path(__file__).resolve().parents[2]
compiler = Path(sys.argv[1] if len(sys.argv) > 1 else repo / "moss").resolve()
source = repo / "tests/tooling/fixtures/phase28_w5_call_specialization.moss"
nested_occurrence_source = repo / "tests/tooling/fixtures/phase28_w5_nested_call_occurrence.moss"
callback_source = repo / "tests/phase26_w2_pipeline_callback.moss"
chained_callback_source = repo / "tests/tooling/fixtures/phase28_w5_chained_pipeline_callbacks.moss"
callback_boundaries_source = repo / "tests/tooling/fixtures/phase28_w5_callback_boundaries.moss"
sort_key_source = repo / "tests/tooling/fixtures/phase28_w5_sort_key_control.moss"


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

# Same-target generic calls nested among same-line siblings retain their
# statement-wide checked source occurrence in Fast Debug and Native.
nested_occurrence = run(compiler, "run", "--interp", nested_occurrence_source)
assert nested_occurrence.returncode == 0 and \
    nested_occurrence.stdout == "0 3\n", (
        nested_occurrence.stdout, nested_occurrence.stderr)
nested_rust = scratch / "nested-occurrence.rs"
nested_native = scratch / "nested-occurrence"
nested_generated = run(compiler, nested_occurrence_source, "-o", nested_rust)
assert nested_generated.returncode == 0, (
    nested_generated.stdout, nested_generated.stderr)
nested_compiled = run("rustc", "-D", "warnings", nested_rust,
                      "-o", nested_native)
assert nested_compiled.returncode == 0, nested_compiled.stderr
nested_native_result = run(nested_native)
assert nested_native_result.returncode == 0 and \
    nested_native_result.stdout == "0 3\n", (
        nested_native_result.stdout, nested_native_result.stderr)

# Named callbacks in a chained pipeline are separate exact EST stage
# occurrences even though the checker publishes their facts incrementally.
chained = run(compiler, "run", "--interp", chained_callback_source)
assert chained.returncode == 0 and chained.stdout.strip() == "[12, 13, 14]", (
    chained.stdout, chained.stderr)
chained_rust = scratch / "chained.rs"
chained_native = scratch / "chained"
chained_generated = run(compiler, chained_callback_source, "-o", chained_rust)
assert chained_generated.returncode == 0, (
    chained_generated.stdout, chained_generated.stderr)
chained_compiled = run("rustc", "-D", "warnings", chained_rust,
                       "-o", chained_native)
assert chained_compiled.returncode == 0, chained_compiled.stderr
chained_native_result = run(chained_native)
assert chained_native_result.returncode == 0 and \
    chained_native_result.stdout.strip() == "[12, 13, 14]", (
        chained_native_result.stdout, chained_native_result.stderr)

# Any/All named callbacks use the exact pipeline stage callback fact producer.
boundaries = run(compiler, "run", "--interp", callback_boundaries_source)
assert boundaries.returncode == 0 and boundaries.stdout.strip() == \
    "[1, 2, 3]\ntrue\ntrue", (boundaries.stdout, boundaries.stderr)
boundaries_rust = scratch / "boundaries.rs"
boundaries_native = scratch / "boundaries"
boundaries_generated = run(compiler, callback_boundaries_source, "-o",
                           boundaries_rust)
assert boundaries_generated.returncode == 0, (
    boundaries_generated.stdout, boundaries_generated.stderr)
boundaries_compiled = run("rustc", "-D", "warnings", boundaries_rust,
                          "-o", boundaries_native)
assert boundaries_compiled.returncode == 0, boundaries_compiled.stderr
boundaries_native_result = run(boundaries_native)
assert boundaries_native_result.returncode == 0 and \
    boundaries_native_result.stdout.strip() == boundaries.stdout.strip(), (
        boundaries_native_result.stdout, boundaries_native_result.stderr)

# Sort keys have a non-PipelineStage exact callback occurrence.
sort_key_result = run(compiler, "run", "--interp", sort_key_source)
assert sort_key_result.returncode == 0 and \
    sort_key_result.stdout.strip() == "[3, 2, 1]", (
        sort_key_result.stdout, sort_key_result.stderr)
sort_key_rust = scratch / "sort-key.rs"
sort_key_native = scratch / "sort-key"
sort_key_generated = run(compiler, sort_key_source, "-o", sort_key_rust)
assert sort_key_generated.returncode == 0, (
    sort_key_generated.stdout, sort_key_generated.stderr)
sort_key_compiled = run("rustc", "-D", "warnings", sort_key_rust,
                        "-o", sort_key_native)
assert sort_key_compiled.returncode == 0, sort_key_compiled.stderr
sort_key_native_result = run(sort_key_native)
assert sort_key_native_result.returncode == 0 and \
    sort_key_native_result.stdout.strip() == "[3, 2, 1]", (
        sort_key_native_result.stdout, sort_key_native_result.stderr)

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
    for call_program in (source, nested_occurrence_source):
        rejected = run(compiler, "run", "--interp", call_program, env=env)
        assert rejected.returncode != 0, (
            mutation, call_program, rejected.stdout, rejected.stderr)
        assert "MOSS_INTERNAL_INVARIANT" in rejected.stderr, (
            mutation, call_program, rejected.stderr)

callback_positive = run(compiler, "run", "--interp", callback_source)
assert callback_positive.returncode == 0 and \
    callback_positive.stdout.strip() == "[2, 3, 4]", (
        callback_positive.stdout, callback_positive.stderr)
callback_mutations = (
    "MOSS_TEST_CONSUMER_DROP_CALLBACK_TARGET",
    "MOSS_TEST_CONSUMER_PERTURB_CALLBACK_TARGET",
)
for mutation in callback_mutations:
    for callback_program in (callback_source, callback_boundaries_source,
                             sort_key_source):
        env = os.environ.copy()
        for name in callback_mutations:
            env.pop(name, None)
        env[mutation] = "1"
        rejected = run(compiler, "run", "--interp", callback_program,
                       env=env)
        assert rejected.returncode != 0, (
            mutation, callback_program, rejected.stdout, rejected.stderr)
        assert "MOSS_INTERNAL_INVARIANT" in rejected.stderr, (
            mutation, callback_program, rejected.stderr)
callback_specialization_mutations = (
    "MOSS_TEST_CONSUMER_DROP_CALLBACK_SPECIALIZATION",
    "MOSS_TEST_CONSUMER_DUPLICATE_CALLBACK_SPECIALIZATION",
    "MOSS_TEST_CONSUMER_PERTURB_CALLBACK_SPECIALIZATION",
)
for mutation in callback_specialization_mutations:
    env = os.environ.copy()
    for name in callback_specialization_mutations:
        env.pop(name, None)
    env[mutation] = "1"
    rejected = run(compiler, "run", "--interp", callback_boundaries_source,
                   env=env)
    assert rejected.returncode != 0, (mutation, rejected.stdout, rejected.stderr)
    assert "MOSS_INTERNAL_INVARIANT" in rejected.stderr, (
        mutation, rejected.stderr)
