#!/usr/bin/env python3
"""Run READ-only field aliases and explicit field-consume controls."""
from __future__ import annotations

import os
from pathlib import Path
import shutil
import subprocess
import sys


ROOT = Path(__file__).resolve().parents[2]
FIXTURES = ROOT / "tests/tooling/fixtures/phase28_l8"
READ_CASES = {
    "o15b": "3",
    "field_read": "3",
    "field_nested_index": "3",
    "field_after_vector_literal": "1\n3",
    "field_parameter": "3",
    "field_parameter_caller_reuse": "3\n3",
    "field_rebind_after_vector_literal": "3",
}


def invoke(args: list[str]) -> subprocess.CompletedProcess[str]:
    return subprocess.run(args, cwd=ROOT, capture_output=True, text=True,
                          timeout=120)


def run(args: list[str]) -> subprocess.CompletedProcess[str]:
    result = invoke(args)
    if result.returncode:
        raise RuntimeError(
            f"command failed ({result.returncode}): {args}\n"
            f"{result.stdout}{result.stderr}")
    return result


def native_output(compiler: str, source: Path, scratch: Path, tag: str) -> str:
    rust = scratch / f"{tag}.rs"
    binary = scratch / tag
    run([compiler, str(source), "-o", str(rust)])
    run(["rustc", "--edition=2021", "-D", "warnings", str(rust), "-o",
         str(binary)])
    return run([str(binary)]).stdout.strip()


def expect_step2_map_view(compiler: str, source: Path, scratch: Path,
                         tag: str, engine: str) -> None:
    if engine == "Fast Debug":
        result = invoke([compiler, "run", "--interp", str(source)])
        expected = "MOSS_INTERNAL_INVARIANT nonprimitive Map view lacks checked nonescaping CollectionView authority"
    else:
        result = invoke([compiler, str(source), "-o", str(scratch / f"{tag}.rs")])
        expected = "MOSS_INTERNAL_INVARIANT nonprimitive Map.keys requires checked nonescaping CollectionView authority"
    combined = result.stdout + result.stderr
    if result.returncode == 0 or expected not in combined:
        raise RuntimeError(
            f"{engine} did not reproduce the recorded Step 2 Map.keys blocker; "
            f"exit={result.returncode}\n{combined}")
    print(f"EXPECTED Step 2 CollectionView failure: {tag} {engine}: {expected}")


def main() -> int:
    if len(sys.argv) != 2:
        print("usage: check_phase28_l8_field_controls.py COMPILER", file=sys.stderr)
        return 2
    compiler = str(Path(sys.argv[1]).resolve())
    scratch = ROOT / "tmp" / f"phase28-l8-field-controls-{os.getpid()}"
    scratch.mkdir(parents=True, exist_ok=True)
    failures: list[str] = []
    try:
        for name, expected in READ_CASES.items():
            source = FIXTURES / f"{name}.moss"
            try:
                fast = run([compiler, "run", "--interp", str(source)]).stdout.strip()
                native = native_output(compiler, source, scratch, name)
                if fast != expected or native != expected or fast != native:
                    raise RuntimeError(
                        f"expected {expected!r}; Fast Debug={fast!r}; "
                        f"Native={native!r}")
                print(f"PASS {name}: Fast Debug and Native output {fast!r}")
            except (OSError, RuntimeError, subprocess.TimeoutExpired) as error:
                failures.append(f"{name}: {error}")
                print(f"FAIL {name}: {error}", file=sys.stderr)

        consume = FIXTURES / "field_consume.moss"
        diagnostic = (
            "moss:9: error: [OWNERSHIP_ILLEGAL_CONSUME] "
            "plain field access cannot consume or transfer a non-Copy field; use take or copy")
        for engine, args in (
            ("Fast Debug", [compiler, "run", "--interp", str(consume)]),
            ("Native", [compiler, str(consume), "-o",
                        str(scratch / "field_consume.rs")]),
        ):
            result = invoke(args)
            if result.returncode == 0 or diagnostic not in result.stdout + result.stderr:
                failures.append(
                    f"field_consume {engine}: expected unchanged rejection at line 9, "
                    f"got exit={result.returncode}: {result.stdout}{result.stderr}")
            else:
                print(f"PASS field_consume {engine}: unchanged reject at line 9")

        for tag, expected in (
            ("swarm_027_indexed_collection_method", True),
            ("swarm_028_self_method_argument_borrow", False),
            ("swarm_030_string_comparison_borrowed", True),
        ):
            source = ROOT / f"tests/{tag}.moss"
            if expected:
                for engine in ("Fast Debug", "Native"):
                    try:
                        expect_step2_map_view(compiler, source, scratch, tag, engine)
                    except (OSError, RuntimeError, subprocess.TimeoutExpired) as error:
                        failures.append(f"{tag} expected Step 2 classification: {error}")
                        print(f"FAIL {tag}: {error}", file=sys.stderr)
                continue
            fast = native = None
            for engine in ("Fast Debug", "Native"):
                try:
                    if engine == "Fast Debug":
                        fast = run([compiler, "run", "--interp", str(source)]).stdout.strip()
                    else:
                        native = native_output(compiler, source, scratch, tag)
                except (OSError, RuntimeError, subprocess.TimeoutExpired) as error:
                    combined = str(error)
                    if "CollectionView" in combined or "Map.keys" in combined:
                        failure = (
                            f"{tag} {engine}: separate CollectionView failure, "
                            f"unexpected in this gate: {error}")
                        failures.append(failure)
                        print(f"UNEXPECTED CollectionView failure {failure}", file=sys.stderr)
                    else:
                        failure = f"{tag} {engine}: {error}"
                        failures.append(failure)
                        print(f"FAIL {failure}", file=sys.stderr)
            if fast is not None and native is not None:
                if fast == native:
                    print(f"PASS {tag}: Fast Debug and Native output {fast!r}")
                else:
                    failures.append(
                        f"{tag}: Fast Debug output {fast!r} differs from Native {native!r}")
                    print(f"FAIL {tag}: Fast Debug={fast!r}; Native={native!r}",
                          file=sys.stderr)
    finally:
        shutil.rmtree(scratch, ignore_errors=True)
    if failures:
        print("\nFAILURES:", file=sys.stderr)
        for failure in failures:
            print(f"- {failure}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, RuntimeError, subprocess.TimeoutExpired) as error:
        print(f"FAIL check_phase28_l8_field_controls: {error}", file=sys.stderr)
        raise SystemExit(1)
