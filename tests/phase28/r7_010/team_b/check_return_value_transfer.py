#!/usr/bin/env python3
"""Check exact ReturnValue transfer lowering and fail-closed corruption."""
from __future__ import annotations

import os
from pathlib import Path
import subprocess
import sys


ROOT = Path(__file__).resolve().parents[4]
FIXTURE = ROOT / "tests/phase28/oracle/fixtures/oracle_13_assignment_consume_return.moss"
SPECIALIZED_FIXTURE = ROOT / "tests/phase28/oracle_r7_005_1/fixtures/oracle_58_specialization_identity_collision.moss"
EXPECTED = "hello\n"
SPECIALIZED_EXPECTED = "1\n2\n"
REPLY_FIXTURE = ROOT / "tests/phase28/r7_010/team_b/owned_reply_authority.moss"
REPLY_EXPECTED = "owned\n"
HOOKS = (
    "MOSS_TEST_CONSUMER_DROP_RETURN_VALUE_REQUIREMENT",
    "MOSS_TEST_CONSUMER_DUPLICATE_RETURN_VALUE_REQUIREMENT",
    "MOSS_TEST_CONSUMER_DROP_RETURN_VALUE_FACT",
    "MOSS_TEST_CONSUMER_DUPLICATE_RETURN_VALUE_FACT",
    "MOSS_TEST_CONSUMER_PERTURB_RETURN_VALUE_FACT",
    "MOSS_TEST_CONSUMER_WRONG_RETURN_VALUE_CONTEXT",
)


def run(args: list[str], env: dict[str, str] | None = None,
        expected_ok: bool = True) -> subprocess.CompletedProcess[str]:
    result = subprocess.run(args, cwd=ROOT, env=env, text=True,
                            stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    if (result.returncode == 0) != expected_ok:
        raise AssertionError(
            f"unexpected status {result.returncode}: {' '.join(args)}\n"
            f"stdout:\n{result.stdout}\nstderr:\n{result.stderr}")
    return result


def main() -> None:
    compiler = str(Path(sys.argv[1] if len(sys.argv) > 1 else "./moss").resolve())
    out = Path(sys.argv[2] if len(sys.argv) > 2 else "tmp/team-b-consumer/returns")
    out = (ROOT / out).resolve() if not out.is_absolute() else out
    out.mkdir(parents=True, exist_ok=True)

    fast = run([compiler, "run", "--interp", str(FIXTURE)])
    if fast.stdout != EXPECTED:
        raise AssertionError(f"Fast Debug expected {EXPECTED!r}, got {fast.stdout!r}")
    for suffix, flags in (("debug", []), ("optimized", ["-Oshared-memory"])):
        rust = out / f"return-{suffix}.rs"
        run([compiler, *flags, str(FIXTURE), "-o", str(rust)])
        generated = rust.read_text(encoding="utf-8")
        if "return b;" not in generated or "return (b).clone()" in generated:
            raise AssertionError("ORACLE-13 return is not a direct checked transfer")
        binary = out / f"return-{suffix}"
        run(["rustc", "--edition=2021", str(rust), "-o", str(binary)])
        native = run([str(binary)])
        if native.stdout != EXPECTED:
            raise AssertionError(f"Native {suffix} expected {EXPECTED!r}, got {native.stdout!r}")

    for hook in HOOKS:
        env = os.environ.copy()
        env[hook] = "1"
        artifact = out / f"corrupt-{hook}.rs"
        artifact.unlink(missing_ok=True)
        native_failure = run([compiler, str(FIXTURE), "-o", str(artifact)],
                             env=env, expected_ok=False)
        if "MOSS_INTERNAL_INVARIANT" not in native_failure.stderr or \
                "return-value" not in native_failure.stderr.lower():
            raise AssertionError(f"{hook}: unrelated Native failure:\n{native_failure.stderr}")
        if artifact.exists():
            raise AssertionError(f"{hook}: failed Native generation left a Rust artifact")
        fast_failure = run([compiler, "run", "--interp", str(FIXTURE)],
                           env=env, expected_ok=False)
        if "MOSS_INTERNAL_INVARIANT" not in fast_failure.stderr or \
                "return-value" not in fast_failure.stderr.lower():
            raise AssertionError(f"{hook}: unrelated Fast Debug failure:\n{fast_failure.stderr}")

    # A handler Reply carries the same checker-owned ReturnValue authority.
    reply_fast = run([compiler, "run", "--interp", str(REPLY_FIXTURE)])
    if reply_fast.stdout != REPLY_EXPECTED:
        raise AssertionError(
            f"Reply Fast Debug expected {REPLY_EXPECTED!r}, got {reply_fast.stdout!r}")
    for suffix, flags in (("debug", []), ("optimized", ["-Oshared-memory"])):
        rust = out / f"owned-reply-{suffix}.rs"
        run([compiler, *flags, str(REPLY_FIXTURE), "-o", str(rust)])
        generated = rust.read_text(encoding="utf-8")
        handler_start = generated.find("fn __moss_body_ReplyWorker_Text")
        handler_end = generated.find("// Moss tooling end|handler:ReplyWorker.Text", handler_start)
        if handler_start < 0 or handler_end < 0:
            raise AssertionError("owned handler Reply function missing from generated Rust")
        handler = generated[handler_start:handler_end]
        if "__moss_reply = Some(value);" not in handler:
            raise AssertionError("owned handler Reply did not transfer its checked value")
        if ".clone()" in handler or ".to_owned()" in handler:
            raise AssertionError("owned handler Reply emitted an implicit clone")
        binary = out / f"owned-reply-{suffix}"
        run(["rustc", "--edition=2021", str(rust), "-o", str(binary)])
        native = run([str(binary)])
        if native.stdout != REPLY_EXPECTED:
            raise AssertionError(
                f"Owned Reply Native {suffix} expected {REPLY_EXPECTED!r}, got {native.stdout!r}")
    for hook in HOOKS:
        env = os.environ.copy()
        env[hook] = "1"
        artifact = out / f"owned-reply-corrupt-{hook}.rs"
        artifact.unlink(missing_ok=True)
        failure = run([compiler, str(REPLY_FIXTURE), "-o", str(artifact)],
                      env=env, expected_ok=False)
        if "MOSS_INTERNAL_INVARIANT" not in failure.stderr or \
                "return-value" not in failure.stderr.lower():
            raise AssertionError(f"Owned Reply {hook}: unrelated failure:\n{failure.stderr}")
        if artifact.exists():
            raise AssertionError(f"Owned Reply {hook}: failed generation left an artifact")
        fast_failure = run([compiler, "run", "--interp", str(REPLY_FIXTURE)],
                           env=env, expected_ok=False)
        if "MOSS_INTERNAL_INVARIANT" not in fast_failure.stderr or \
                "return-value" not in fast_failure.stderr.lower():
            raise AssertionError(f"Owned Reply Fast Debug {hook}: unrelated failure:\n{fast_failure.stderr}")

    specialized_fast = run([compiler, "run", "--interp", str(SPECIALIZED_FIXTURE)])
    if specialized_fast.stdout != SPECIALIZED_EXPECTED:
        raise AssertionError(
            f"ORACLE-58 Fast Debug expected {SPECIALIZED_EXPECTED!r}, "
            f"got {specialized_fast.stdout!r}")
    for suffix, flags in (("debug", []), ("optimized", ["-Oshared-memory"])):
        rust = out / f"specialized-return-{suffix}.rs"
        run([compiler, *flags, str(SPECIALIZED_FIXTURE), "-o", str(rust)])
        binary = out / f"specialized-return-{suffix}"
        run(["rustc", "--edition=2021", str(rust), "-o", str(binary)])
        native = run([str(binary)])
        if native.stdout != SPECIALIZED_EXPECTED:
            raise AssertionError(
                f"ORACLE-58 Native {suffix} expected {SPECIALIZED_EXPECTED!r}, "
                f"got {native.stdout!r}")

    specialized_hooks = (*HOOKS,
        "MOSS_TEST_CONSUMER_WRONG_RETURN_VALUE_SPECIALIZATION")
    for hook in specialized_hooks:
        env = os.environ.copy()
        env[hook] = "1"
        artifact = out / f"specialized-corrupt-{hook}.rs"
        artifact.unlink(missing_ok=True)
        native_failure = run(
            [compiler, str(SPECIALIZED_FIXTURE), "-o", str(artifact)],
            env=env, expected_ok=False)
        if "MOSS_INTERNAL_INVARIANT" not in native_failure.stderr or \
                "return-value" not in native_failure.stderr.lower():
            raise AssertionError(
                f"ORACLE-58 {hook}: unrelated Native failure:\n{native_failure.stderr}")
        if artifact.exists():
            raise AssertionError(f"ORACLE-58 {hook}: failure left a Rust artifact")
        fast_failure = run(
            [compiler, "run", "--interp", str(SPECIALIZED_FIXTURE)],
            env=env, expected_ok=False)
        if "MOSS_INTERNAL_INVARIANT" not in fast_failure.stderr or \
                "return-value" not in fast_failure.stderr.lower():
            raise AssertionError(
                f"ORACLE-58 {hook}: unrelated Fast Debug failure:\n{fast_failure.stderr}")

    print("ORACLE-13 ReturnValue exact owned transfer: Native Debug, Native Optimized, Fast Debug parity PASS")
    print("generated Rust return has no implicit nonprimitive clone: PASS")
    print("ReturnValue requirement/fact/context drop/duplicate/perturb checks: 6/6 hooks across Native and Fast Debug PASS")
    print("failed Native generation leaves no Rust artifact: PASS")
    print("ORACLE-58 specialized ReturnValue Native Debug/Optimized/Fast Debug parity PASS")
    print("ORACLE-58 ReturnValue authority corruption checks: 7/7 hooks across Native and Fast Debug PASS")
    print("Owned handler Reply: Native Debug/Optimized/Fast Debug parity and no implicit clone PASS")
    print("Owned handler Reply authority corruption checks: 6/6 hooks across Native and Fast Debug PASS")


if __name__ == "__main__":
    main()
