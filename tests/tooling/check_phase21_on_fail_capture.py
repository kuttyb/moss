#!/usr/bin/env python3
"""Check D7 owned on_fail capture proofs and native frame handoff."""

import json
import os
from pathlib import Path
import subprocess
import sys


repo = Path(__file__).resolve().parents[2]
compiler = Path(sys.argv[1]).resolve()
fixtures = repo / "tests/tooling/fixtures"
scratch = repo / "tmp/phase21-on-fail-capture"
scratch.mkdir(parents=True, exist_ok=True)


def run(*args, env=None):
    return subprocess.run([str(item) for item in args], cwd=repo,
                          capture_output=True, text=True, timeout=180,
                          env=env)


for name, output in (
        ("owned_string", "hello\n"),
        ("message_result", "hello\n"),
        ("try_local", "9\n"),
        ("owned_payload", "hello\n"),
        ("slot_combined", "seven\n"),
        ("if_join", "yes\nno\n"),
        ("try_join", "no\nyes\n"),
        ("loop_reinit", "after\n"),
        ("nested_reraise", "4\nkept\n"),
        ("method_local", "5\n"),
        ("method_early_raise", "3\n")):
    source = fixtures / f"phase21_capture_{name}.moss"
    checked = run(compiler, "check", source, "--json")
    assert checked.returncode == 0, (name, checked.stdout, checked.stderr)
    generated = scratch / f"{name}.rs"
    executable = scratch / name
    lowered = run(compiler, source, "-o", generated)
    assert lowered.returncode == 0, (name, lowered.stdout, lowered.stderr)
    options = ("0", "3") if name.startswith("method_") else ("0",)
    for opt in options:
        compiled = run("rustc", "--edition=2021", "-D", "warnings",
                       f"-Copt-level={opt}", generated, "-o", executable)
        assert compiled.returncode == 0, (name, opt, compiled.stderr)
        executed = run(executable)
        assert executed.returncode == 0 and executed.stdout == output, (
            name, opt, executed.returncode, executed.stdout, executed.stderr)
    interpreted = run(compiler, "run", "--interp", source)
    assert interpreted.returncode == 0 and interpreted.stdout == output, (
        name, interpreted.returncode, interpreted.stdout, interpreted.stderr)


for name, code in (
        ("preinit", "ON_FAIL_CAPTURE_NOT_INITIALIZED"),
        ("consumed", "ON_FAIL_CAPTURE_CONSUMED"),
        ("branch_mismatch", "ON_FAIL_CAPTURE_BINDING_MISMATCH"),
        ("loop_consumed", "ON_FAIL_CAPTURE_CONSUMED"),
        ("loop_uninit", "ON_FAIL_CAPTURE_NOT_INITIALIZED"),
        ("fileio", "ON_FAIL_CAPTURE_NOT_OWNED"),
        ("borrowed_payload", "ON_FAIL_CAPTURE_NOT_OWNED")):
    source = fixtures / f"phase21_capture_{name}.moss"
    checked = run(compiler, "check", source, "--json")
    assert checked.returncode != 0, (name, checked.stdout, checked.stderr)
    diagnostic = json.loads(checked.stdout)
    assert diagnostic["error"]["code"] == code, (name, diagnostic)


# A protected domain String cannot be moved into a handler-local capture.
# The rejected transfer prevents the local from outliving its state guard.
source = fixtures / "phase21_capture_state_borrow.moss"
checked = run(compiler, "check", source, "--json")
assert checked.returncode != 0, (checked.stdout, checked.stderr)
diagnostic = json.loads(checked.stdout)["error"]
assert diagnostic["code"] == "MOSS_COMPILE_ERROR", diagnostic
assert "domain state 'value' cannot be consumed" in diagnostic["message"], diagnostic


# The same handler dispatches its trailer as a Root, while its nested call
# propagates the typed payload into the caller's lexical recovery arm.
source = fixtures / "phase21_on_fail_root_nested.moss"
assert run(compiler, "check", source, "--json").returncode == 0
generated = scratch / "root_nested.rs"
executable = scratch / "root_nested"
assert run(compiler, source, "-o", generated).returncode == 0
compiled = run("rustc", "--edition=2021", "-D", "warnings",
               generated, "-o", executable)
assert compiled.returncode == 0, compiled.stderr
for result in (run(executable), run(compiler, "run", "--interp", source)):
    assert result.returncode == 0 and result.stdout == "8\n9\n", (
        result.returncode, result.stdout, result.stderr)


# A terminating Try/Recover makes the remaining checked block unreachable.
# Native consumes the checker's exact Try continuation answer and omits that
# syntactic tail; reachable and nested controls preserve their normal result.
for name, output in (
        ("dead_try_tail", "3\n"),
        ("nested_dead_try_tail", "8\n"),
        ("reachable_try_tail", "6\n"),
        ("on_fail_try_tail", "9\n")):
    source = fixtures / f"phase21_{name}.moss"
    checked = run(compiler, "check", source, "--json")
    assert checked.returncode == 0, (name, checked.stdout, checked.stderr)
    generated = scratch / f"{name}.rs"
    lowered = run(compiler, source, "-o", generated)
    assert lowered.returncode == 0, (name, lowered.stdout, lowered.stderr)
    for opt in ("0", "3"):
        executable = scratch / f"{name}-o{opt}"
        compiled = run("rustc", "--edition=2021", "-D", "warnings",
                       f"-Copt-level={opt}", generated, "-o", executable)
        assert compiled.returncode == 0, (name, opt, compiled.stderr)
        native = run(executable)
        assert native.returncode == 0 and native.stdout == output, (
            name, opt, native.returncode, native.stdout, native.stderr)
    interpreted = run(compiler, "run", "--interp", source)
    assert interpreted.returncode == 0 and interpreted.stdout == output, (
        name, interpreted.returncode, interpreted.stdout, interpreted.stderr)


# The on_fail Try answer is exact checker authority. Native rejects dropped,
# duplicated, or contradictory source-free facts instead of lowering a guess.
source = fixtures / "phase21_on_fail_try_tail.moss"
for hook in (
        "MOSS_TEST_CONSUMER_DROP_TRY_CONTINUATION_FACT",
        "MOSS_TEST_CONSUMER_DUPLICATE_TRY_CONTINUATION_FACT",
        "MOSS_TEST_CONSUMER_PERTURB_TRY_CONTINUATION_FACT"):
    env = os.environ.copy()
    env[hook] = "1"
    rejected = run(compiler, source, "-o", scratch / "corrupted-try.rs",
                   env=env)
    combined = rejected.stdout + rejected.stderr
    assert rejected.returncode != 0 and "MOSS_INTERNAL_INVARIANT" in combined, (
        hook, rejected.returncode, combined)


print("Phase 21 D7 owned capture proof and native handoff passed.")
