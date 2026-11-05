#!/usr/bin/env python3
"""Check checked projected Map reads borrow exactly and fail closed on corruption."""
from __future__ import annotations

import os
from pathlib import Path
import subprocess
import sys


ROOT = Path(__file__).resolve().parents[4]
FIXTURE = ROOT / "tests/phase28/oracle/fixtures/oracle_20_take_nested_map_vector.moss"
EXPECTED = "2\nb1\n0\n"
HOOKS = (
    "MOSS_TEST_CONSUMER_DROP_PROJECTED_COLLECTION_READ_REQUIREMENT",
    "MOSS_TEST_CONSUMER_DUPLICATE_PROJECTED_COLLECTION_READ_REQUIREMENT",
    "MOSS_TEST_CONSUMER_DROP_PROJECTED_COLLECTION_READ_FACT",
    "MOSS_TEST_CONSUMER_DUPLICATE_PROJECTED_COLLECTION_READ_FACT",
    "MOSS_TEST_CONSUMER_PERTURB_PROJECTED_COLLECTION_READ_FACT",
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
    out = Path(sys.argv[2] if len(sys.argv) > 2 else "tmp/team-b-consumer/projected-read")
    out = (ROOT / out).resolve() if not out.is_absolute() else out
    out.mkdir(parents=True, exist_ok=True)

    fast = run([compiler, "run", "--interp", str(FIXTURE)])
    if fast.stdout != EXPECTED:
        raise AssertionError(f"Fast Debug expected {EXPECTED!r}, got {fast.stdout!r}")

    for suffix, flags in (("debug", []), ("optimized", ["-Oshared-memory"])):
        rust = out / f"projected-{suffix}.rs"
        run([compiler, *flags, str(FIXTURE), "-o", str(rust)])
        generated = rust.read_text(encoding="utf-8")
        marker = "// Moss line 12: echo (m[\"first\"].items |> count)"
        if marker not in generated:
            raise AssertionError("generated Rust omitted ORACLE-20 projected-read source marker")
        projected = generated.split(marker, 1)[1].split("// Moss tooling end|main", 1)[0]
        if ").items)).clone()" in projected or ".items).clone()" in projected:
            raise AssertionError("ORACLE-20 projected Map read still clones its nonprimitive collection")
        if "__moss_pipeline_source = &(" not in projected or ".items" not in projected or ".iter()" not in projected:
            raise AssertionError("ORACLE-20 projected read was not lowered as a borrowed pipeline source")
        binary = out / f"projected-{suffix}"
        run(["rustc", "--edition=2021", str(rust), "-o", str(binary)])
        native = run([str(binary)])
        if native.stdout != EXPECTED:
            raise AssertionError(f"Native {suffix} expected {EXPECTED!r}, got {native.stdout!r}")

    for hook in HOOKS:
        env = os.environ.copy()
        env[hook] = "1"
        artifact = out / f"corrupt-{hook}.rs"
        artifact.unlink(missing_ok=True)
        failure = run([compiler, str(FIXTURE), "-o", str(artifact)],
                      env=env, expected_ok=False)
        if "MOSS_INTERNAL_INVARIANT" not in failure.stderr or \
                "projected collection-read" not in failure.stderr.lower():
            raise AssertionError(f"{hook}: unrelated Native failure:\n{failure.stderr}")
        if artifact.exists():
            raise AssertionError(f"{hook}: failed Native generation left a Rust artifact")
        failure = run([compiler, "run", "--interp", str(FIXTURE)],
                      env=env, expected_ok=False)
        if "MOSS_INTERNAL_INVARIANT" not in failure.stderr or \
                "projected collection-read" not in failure.stderr.lower():
            raise AssertionError(f"{hook}: unrelated Fast Debug failure:\n{failure.stderr}")

    print("ORACLE-20 exact projected Map read: Native Debug, Native Optimized, Fast Debug parity PASS")
    print("generated Rust borrows projected collection; no nonprimitive collection clone: PASS")
    print("projected collection-read drop/duplicate/perturb checks: 5/5 hooks across Native and Fast Debug PASS")
    print("corrupt Native generation leaves no Rust artifact: PASS")


if __name__ == "__main__":
    main()
