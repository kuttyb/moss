#!/usr/bin/env python3
"""Compare representative checked Moss semantics with and without NDEBUG."""

import json
import os
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
PROBES = ROOT / "tests/tooling/phase20_review/probes"
CORPUS = {
    "bug_unresolved_callable_release_build.moss": (False, "UNKNOWN_SYMBOL_OR_TYPE"),
    "guard_release_ownership_rejection.moss": (False, "OWNERSHIP_CONFLICTING_ACCESS"),
    "bug_chunk_bound_name_reused_by_parameter.moss": (False, "FILEIO_UNBOUNDED_REQUEST"),
    "bug_range_eq_int.moss": (False, "TYPE_MISMATCH"),
    "phase20_executor_construct_outside_main.moss": (False, "EXECUTOR_CONSTRUCT_OUTSIDE_MAIN"),
    "guard_pure_builtin_constructor_metadata.moss": (True, None),
    "bug_untyped_looping_helper_in_map.moss": (True, None),
}


def check(compiler, source):
    process = subprocess.run(
        [str(compiler), "check", str(source), "--json"], cwd=ROOT,
        capture_output=True, text=True, timeout=120)
    if not process.stdout.strip():
        raise AssertionError(f"{source.name}: compiler produced no JSON: {process.stderr[:200]}")
    document = json.loads(process.stdout)
    diagnostics = list((document.get("result") or {}).get("diagnostics") or [])
    if document.get("error"):
        diagnostics.append(document["error"])
    selected = [
        (item.get("severity"), item.get("code"), item.get("message"),
         item.get("line"), item.get("column"))
        for item in diagnostics
    ]
    return bool(document.get("ok")), selected


def main():
    normal = Path(sys.argv[1] if len(sys.argv) > 1 else "./moss").resolve()
    build = ROOT / "tmp/phase20-review/release"
    build.mkdir(parents=True, exist_ok=True)
    release = build / "moss-ndebug"
    compiler = os.environ.get("CXX", "c++")
    flags = ["-std=c++17", "-O2", "-DNDEBUG", "-Wall", "-Wextra", "-pedantic"]
    sources = [ROOT / "src/moss.cpp", *ROOT.glob("src/*.hpp"),
               *ROOT.glob("src/*.inc")]
    if not release.exists() or any(path.stat().st_mtime > release.stat().st_mtime
                                   for path in sources):
        subprocess.run([compiler, *flags, "src/moss.cpp", "-o", str(release)],
                       cwd=ROOT, check=True, timeout=600)
    # The diagnostic itself must survive preprocessing even when no current
    # source fixture reaches this late fail-safe path.
    preprocessed = subprocess.run(
        [compiler, "-std=c++17", "-DNDEBUG", "-E", "src/moss.cpp"],
        cwd=ROOT, capture_output=True, text=True, check=True, timeout=120)
    assert "FUNCTIONAL_CALLABLE_UNRESOLVED" in preprocessed.stdout, (
        "functional-callable correctness check was compiled out by NDEBUG")
    for name, (expected_ok, expected_code) in CORPUS.items():
        source = (ROOT / "tests/negative" / name) if name.startswith("phase20_executor") else PROBES / name
        debug_result = check(normal, source)
        release_result = check(release, source)
        assert debug_result == release_result, (name, debug_result, release_result)
        ok, diagnostics = debug_result
        assert ok == expected_ok, (name, debug_result)
        if expected_code:
            assert any(item[1] == expected_code for item in diagnostics), (name, diagnostics)
        print(f"PASS  {name}")
    print("PASS  NDEBUG retains FUNCTIONAL_CALLABLE_UNRESOLVED diagnostic")


if __name__ == "__main__":
    main()
