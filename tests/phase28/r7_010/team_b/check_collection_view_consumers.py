#!/usr/bin/env python3
"""Check exact Map view authority, borrow lowering, and corruption behavior."""
from __future__ import annotations

import os
from pathlib import Path
import subprocess
import sys


ROOT = Path(__file__).resolve().parents[4]
FIXTURES = (
    (ROOT / "tests/phase28/r7_010/team_b/checked_map_borrowed_views.moss", "2\n3\n"),
    (ROOT / "tests/phase28/take_fixtures/take_map_field.moss", "26\n"),
)
HOOKS = (
    "MOSS_TEST_CONSUMER_DROP_COLLECTION_VIEW_REQUIREMENT",
    "MOSS_TEST_CONSUMER_DUPLICATE_COLLECTION_VIEW_REQUIREMENT",
    "MOSS_TEST_CONSUMER_DROP_COLLECTION_VIEW_FACT",
    "MOSS_TEST_CONSUMER_DUPLICATE_COLLECTION_VIEW_FACT",
    "MOSS_TEST_CONSUMER_PERTURB_COLLECTION_VIEW_FACT",
    "MOSS_TEST_PERTURB_COLLECTION_VIEW_BASE_BINDING",
    "MOSS_TEST_PERTURB_COLLECTION_VIEW_MEMBER_PATH",
    "MOSS_TEST_PERTURB_COLLECTION_VIEW_RECEIVER_TYPE",
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

    for index, (fixture, expected) in enumerate(FIXTURES):
        stem = f"views-{index}"
        fast = run([compiler, "run", "--interp", str(fixture)])
        if fast.stdout != expected:
            raise AssertionError(f"{fixture.name}: Fast Debug expected {expected!r}, got {fast.stdout!r}")
        rust = out / f"{stem}-debug.rs"
        run([compiler, str(fixture), "-o", str(rust)])
        generated = rust.read_text(encoding="utf-8")
        if ".values().cloned()" in generated or ".keys().cloned()" in generated:
            raise AssertionError(f"{fixture.name}: checked nonprimitive Map view still clones elements")
        if ".keys().collect::<std::vec::Vec<_>>()" not in generated or \
                ".values()" in fixture.read_text(encoding="utf-8") and \
                ".values().collect::<std::vec::Vec<_>>()" not in generated:
            raise AssertionError(f"{fixture.name}: checked Map views were not lowered as borrowed references")
        binary = out / f"{stem}-debug"
        run(["rustc", "--edition=2021", str(rust), "-o", str(binary)])
        native = run([str(binary)])
        if native.stdout != expected:
            raise AssertionError(f"{fixture.name}: Native Debug expected {expected!r}, got {native.stdout!r}")

        rust_opt = out / f"{stem}-optimized.rs"
        run([compiler, "-Oshared-memory", str(fixture), "-o", str(rust_opt)])
        binary_opt = out / f"{stem}-optimized"
        run(["rustc", "--edition=2021", str(rust_opt), "-o", str(binary_opt)])
        native_opt = run([str(binary_opt)])
        if native_opt.stdout != expected:
            raise AssertionError(f"{fixture.name}: Native Optimized expected {expected!r}, got {native_opt.stdout!r}")

        for hook in HOOKS:
            env = os.environ.copy()
            env[hook] = "1"
            artifact = out / f"corrupt-{stem}-{hook}.rs"
            artifact.unlink(missing_ok=True)
            native_failure = run([compiler, str(fixture), "-o", str(artifact)],
                                 env=env, expected_ok=False)
            combined_native = (native_failure.stderr + native_failure.stdout).lower()
            if "moss_internal_invariant" not in combined_native or \
                    "collection view" not in combined_native and \
                    "collection-view" not in combined_native:
                raise AssertionError(f"{fixture.name} {hook}: unrelated Native failure:\n{native_failure.stderr}")
            if artifact.exists():
                raise AssertionError(f"{fixture.name} {hook}: failed Native generation left a Rust artifact")
            fast_failure = run([compiler, "run", "--interp", str(fixture)],
                               env=env, expected_ok=False)
            combined_fast = (fast_failure.stderr + fast_failure.stdout).lower()
            if "moss_internal_invariant" not in combined_fast or \
                    "collection view" not in combined_fast and \
                    "collection-view" not in combined_fast:
                raise AssertionError(f"{fixture.name} {hook}: unrelated Fast Debug failure:\n{fast_failure.stderr}")

    print("checked nonprimitive Map.values/keys borrow lowering: Native Debug, Native Optimized, Fast Debug parity PASS")
    print("generated Rust contains reference collection and no `.cloned()` at checked view sites: PASS")
    print("collection-view drop/duplicate/base-binding/member-path/type perturb fail-closed checks: 8/8 hooks across both fixtures and consumers PASS")
    print("failed Native generation leaves no Rust artifact: PASS")


if __name__ == "__main__":
    main()
