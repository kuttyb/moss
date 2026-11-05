#!/usr/bin/env python3
"""Check exact Map view authority, borrow lowering, and corruption behavior."""
from __future__ import annotations

import os
from pathlib import Path
import subprocess
import sys


ROOT = Path(__file__).resolve().parents[4]
FIXTURE = ROOT / "tests/phase28/r7_010/team_b/checked_map_borrowed_views.moss"
EXPECTED = "2\n3\n"
HOOKS = (
    "MOSS_TEST_CONSUMER_DROP_COLLECTION_VIEW_REQUIREMENT",
    "MOSS_TEST_CONSUMER_DUPLICATE_COLLECTION_VIEW_REQUIREMENT",
    "MOSS_TEST_CONSUMER_DROP_COLLECTION_VIEW_FACT",
    "MOSS_TEST_CONSUMER_DUPLICATE_COLLECTION_VIEW_FACT",
    "MOSS_TEST_CONSUMER_PERTURB_COLLECTION_VIEW_FACT",
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
    out = Path(sys.argv[2] if len(sys.argv) > 2 else "tmp/team-b-consumer/views")
    out = (ROOT / out).resolve() if not out.is_absolute() else out
    out.mkdir(parents=True, exist_ok=True)

    fast = run([compiler, "run", "--interp", str(FIXTURE)])
    if fast.stdout != EXPECTED:
        raise AssertionError(f"Fast Debug expected {EXPECTED!r}, got {fast.stdout!r}")
    rust = out / "views-debug.rs"
    run([compiler, str(FIXTURE), "-o", str(rust)])
    generated = rust.read_text(encoding="utf-8")
    if ".values().cloned()" in generated or ".keys().cloned()" in generated:
        raise AssertionError("checked nonprimitive Map view still clones elements")
    if ".values().collect::<std::vec::Vec<_>>()" not in generated or \
            ".keys().collect::<std::vec::Vec<_>>()" not in generated:
        raise AssertionError("checked Map views were not lowered as borrowed references")
    binary = out / "views-debug"
    run(["rustc", "--edition=2021", str(rust), "-o", str(binary)])
    native = run([str(binary)])
    if native.stdout != EXPECTED:
        raise AssertionError(f"Native Debug expected {EXPECTED!r}, got {native.stdout!r}")

    rust_opt = out / "views-optimized.rs"
    run([compiler, "-Oshared-memory", str(FIXTURE), "-o", str(rust_opt)])
    binary_opt = out / "views-optimized"
    run(["rustc", "--edition=2021", str(rust_opt), "-o", str(binary_opt)])
    native_opt = run([str(binary_opt)])
    if native_opt.stdout != EXPECTED:
        raise AssertionError(f"Native Optimized expected {EXPECTED!r}, got {native_opt.stdout!r}")

    for hook in HOOKS:
        env = os.environ.copy()
        env[hook] = "1"
        artifact = out / f"corrupt-{hook}.rs"
        artifact.unlink(missing_ok=True)
        native_failure = run([compiler, str(FIXTURE), "-o", str(artifact)],
                             env=env, expected_ok=False)
        if "MOSS_INTERNAL_INVARIANT" not in native_failure.stderr or \
                "collection view" not in native_failure.stderr.lower() and \
                "collection-view" not in native_failure.stderr.lower():
            raise AssertionError(f"{hook}: unrelated Native failure:\n{native_failure.stderr}")
        if artifact.exists():
            raise AssertionError(f"{hook}: failed Native generation left a Rust artifact")
        fast_failure = run([compiler, "run", "--interp", str(FIXTURE)],
                           env=env, expected_ok=False)
        if "MOSS_INTERNAL_INVARIANT" not in fast_failure.stderr or \
                "collection view" not in fast_failure.stderr.lower() and \
                "collection-view" not in fast_failure.stderr.lower():
            raise AssertionError(f"{hook}: unrelated Fast Debug failure:\n{fast_failure.stderr}")

    print("checked nonprimitive Map.values/keys borrow lowering: Native Debug, Native Optimized, Fast Debug parity PASS")
    print("generated Rust contains reference collection and no `.cloned()` at checked view sites: PASS")
    print("collection-view drop/duplicate/perturb fail-closed checks: 5/5 hooks across Native and Fast Debug PASS")
    print("failed Native generation leaves no Rust artifact: PASS")


if __name__ == "__main__":
    main()
