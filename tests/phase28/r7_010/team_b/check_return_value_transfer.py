#!/usr/bin/env python3
"""Check exact ReturnValue transfer lowering and fail-closed corruption."""
from __future__ import annotations

import os
from pathlib import Path
import subprocess
import sys


ROOT = Path(__file__).resolve().parents[4]
FIXTURE = ROOT / "tests/phase28/oracle/fixtures/oracle_13_assignment_consume_return.moss"
EXPECTED = "hello\n"
HOOKS = (
    "MOSS_TEST_CONSUMER_DROP_RETURN_VALUE_REQUIREMENT",
    "MOSS_TEST_CONSUMER_DUPLICATE_RETURN_VALUE_REQUIREMENT",
    "MOSS_TEST_CONSUMER_DROP_RETURN_VALUE_FACT",
    "MOSS_TEST_CONSUMER_DUPLICATE_RETURN_VALUE_FACT",
    "MOSS_TEST_CONSUMER_PERTURB_RETURN_VALUE_FACT",
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

    print("ORACLE-13 ReturnValue exact owned transfer: Native Debug, Native Optimized, Fast Debug parity PASS")
    print("generated Rust return has no implicit nonprimitive clone: PASS")
    print("ReturnValue requirement/fact drop/duplicate/perturb checks: 5/5 hooks across Native and Fast Debug PASS")
    print("failed Native generation leaves no Rust artifact: PASS")


if __name__ == "__main__":
    main()
