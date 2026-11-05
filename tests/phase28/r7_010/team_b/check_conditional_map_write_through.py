#!/usr/bin/env python3
"""Check conditional Map write-through error lowering and fact corruption."""
from __future__ import annotations

import os
from pathlib import Path
import subprocess
import sys


ROOT = Path(__file__).resolve().parents[4]
ORACLE07 = ROOT / "tests/phase28/oracle/fixtures/oracle_07_catchable_collection_errors.moss"
WRITE_THROUGH = ROOT / "tests/phase28/r7_010/team_b/conditional_map_write_through.moss"
DIRECT_READ = ROOT / "tests/phase28/r7_010/team_b/conditional_map_read.moss"
EXPECTED_ORACLE07 = (
    "invalid_index\n"
    "negative_invalid_index\n"
    "invalid_index\n"
    "negative_invalid_index\n"
    "empty_vector\n"
    "empty_queue\n"
    "missing_key_read\n"
    "missing_key_write_through\n"
    "missing_key_remove\n"
    "missing_key_get\n"
    "11\n"
)
EXPECTED_WRITE = "missing_key_write_through\n"
EXPECTED_READ = "missing_key_read\n"
AUTHORITY_ERROR = "MOSS_INTERNAL_INVARIANT"


def run(args: list[str], *, env: dict[str, str] | None = None,
        expected_ok: bool = True) -> subprocess.CompletedProcess[str]:
    result = subprocess.run(args, cwd=ROOT, env=env, text=True,
                            stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    if (result.returncode == 0) != expected_ok:
        raise AssertionError(
            f"unexpected status {result.returncode}: {' '.join(args)}\n"
            f"stdout:\n{result.stdout}\nstderr:\n{result.stderr}")
    return result


def native_pair(compiler: str, source: Path, expected: str,
                output_dir: Path, label: str, flags: list[str]) -> str:
    rust = output_dir / f"{label}.rs"
    binary = output_dir / label
    run([compiler, *flags, str(source), "-o", str(rust)])
    generated = rust.read_text(encoding="utf-8")
    if source == ORACLE07:
        lines = generated.splitlines()
        read_marker = next((index for index, line in enumerate(lines)
                            if 'Moss line 43: let missing_map_read = m["missing"]' in line), None)
        if read_marker is None or read_marker + 1 >= len(lines):
            raise AssertionError("ORACLE-07 nonprimitive MapIndexRead source marker is missing")
        read_lowering = lines[read_marker + 1]
        if "Some(__moss_entry) => __moss_entry" not in read_lowering or \
                ".clone()" in read_lowering:
            raise AssertionError(
                "ORACLE-07 nonprimitive MapIndexRead lacks checked READ-place lowering")
        marker = next((index for index, line in enumerate(lines)
                       if 'Moss line 48: m["missing"].value = 2' in line), None)
        if marker is None or marker + 1 >= len(lines):
            raise AssertionError("ORACLE-07 write-through source marker is missing")
        lowered = lines[marker + 1]
        if 'get_mut(__moss_checked_key.as_str())' not in lowered or \
                "None => break" not in lowered:
            raise AssertionError("ORACLE-07 write-through does not use checked get_mut lowering")
        if 'Moss missing map key' in lowered:
            raise AssertionError("ORACLE-07 write-through still emits the panic fallback")
    run(["rustc", "--edition=2021", str(rust), "-o", str(binary)])
    result = run([str(binary)])
    if result.stdout != expected:
        raise AssertionError(f"{label}: expected {expected!r}, got {result.stdout!r}")
    return result.stdout


def main() -> None:
    compiler = str(Path(sys.argv[1] if len(sys.argv) > 1 else "./moss").resolve())
    output_dir = Path(sys.argv[2] if len(sys.argv) > 2 else "tmp/team-b-consumer/conditional-write")
    output_dir = (ROOT / output_dir).resolve() if not output_dir.is_absolute() else output_dir
    output_dir.mkdir(parents=True, exist_ok=True)

    for index, (source, expected) in enumerate(
            ((ORACLE07, EXPECTED_ORACLE07), (WRITE_THROUGH, EXPECTED_WRITE),
             (DIRECT_READ, EXPECTED_READ))):
        fast = run([compiler, "run", "--interp", str(source)])
        if fast.stdout != expected:
            raise AssertionError(f"{source.name}: Fast Debug expected {expected!r}, got {fast.stdout!r}")
        native_pair(compiler, source, expected, output_dir, f"{index}-debug", [])
        native_pair(compiler, source, expected, output_dir,
                    f"{index}-optimized", ["-Oshared-memory"])

    corruption_hooks = (
        "MOSS_TEST_CONSUMER_DROP_CONDITIONAL_COLLECTION_ERROR_REQUIREMENT",
        "MOSS_TEST_CONSUMER_DUPLICATE_CONDITIONAL_COLLECTION_ERROR_REQUIREMENT",
        "MOSS_TEST_CONSUMER_DROP_CONDITIONAL_COLLECTION_ERROR_FACT",
        "MOSS_TEST_CONSUMER_DUPLICATE_CONDITIONAL_COLLECTION_ERROR_FACT",
        "MOSS_TEST_CONSUMER_PERTURB_CONDITIONAL_COLLECTION_ERROR_FACT",
    )
    for hook in corruption_hooks:
        env = os.environ.copy()
        env[hook] = "1"
        for label, source in (("write", WRITE_THROUGH), ("read", DIRECT_READ)):
            artifact = output_dir / f"corrupt-{hook}-{label}.rs"
            artifact.unlink(missing_ok=True)
            native = run([compiler, str(source), "-o", str(artifact)],
                         env=env, expected_ok=False)
            if AUTHORITY_ERROR not in native.stderr or "conditional" not in native.stderr.lower():
                raise AssertionError(
                    f"{hook}/{label}: Native failure was not the conditional CollectionError authority guard:\n"
                    f"{native.stderr}")
            if artifact.exists():
                raise AssertionError(f"{hook}/{label}: failed Native generation left a Rust artifact")
            fast = run([compiler, "run", "--interp", str(source)],
                       env=env, expected_ok=False)
            if AUTHORITY_ERROR not in fast.stderr or "conditional" not in fast.stderr.lower():
                raise AssertionError(
                    f"{hook}/{label}: Fast Debug failure was not the conditional CollectionError authority guard:\n"
                    f"{fast.stderr}")

    print("ORACLE-07 plus focused direct Map read/write-through recovery: Native Debug, Native Optimized, Fast Debug parity PASS")
    print("conditional Map error authority drop/perturb/duplicate fail-closed checks: 20/20 hook/source/engine combinations PASS")
    print("authority-specific diagnostics and failed-native artifact absence: PASS")


if __name__ == "__main__":
    main()
