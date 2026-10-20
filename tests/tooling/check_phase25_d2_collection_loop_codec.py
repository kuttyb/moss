#!/usr/bin/env python3
"""Compile and run the bounded D2 checked-body collection codec fixtures."""

from pathlib import Path
import subprocess


ROOT = Path(__file__).resolve().parents[2]
TMP = ROOT / "tmp"
TMP.mkdir(exist_ok=True)
SOURCE = ROOT / "tests" / "tooling" / "check_phase25_d2_collection_loop_codec.cpp"
BINARY = TMP / "phase25-d2-collection-loop-codec"
BASELINE = "39e7d8ac810f579243400766f007e9e7550b209b"
BASELINE_TREE = TMP / "d2-preconsumer"
COMPAT_BASE = TMP / "d2-compat.body"
PRECONSUMER_SOURCE = ROOT / "tests" / "tooling" / "check_phase25_d2_preconsumer.cpp"
PRECONSUMER = TMP / "phase25-d2-preconsumer"

subprocess.run([
    "g++", "-std=c++17", "-O0", "-Wall", "-Wextra", "-Werror",
    "-Wno-missing-field-initializers", "-pedantic", str(SOURCE),
    "-o", str(BINARY),
], check=True)
subprocess.run([str(BINARY)], check=True)

subprocess.run([str(BINARY), "--compat-bodies", str(COMPAT_BASE)], check=True)
subprocess.run(["git", "worktree", "add", "--detach", str(BASELINE_TREE),
                BASELINE], cwd=ROOT, check=True)
try:
    subprocess.run([
        "g++", "-std=c++17", "-O0", "-Wall", "-Wextra", "-Werror",
        "-Wno-missing-field-initializers", "-pedantic",
        "-I", str(BASELINE_TREE / "src"), str(PRECONSUMER_SOURCE),
        "-o", str(PRECONSUMER),
    ], check=True)
    for operation, path in (("count", COMPAT_BASE),
                            ("index", Path(str(COMPAT_BASE) + ".index"))):
        subprocess.run([str(PRECONSUMER), str(path), operation], check=True)

    old_compiler = (BASELINE_TREE / "src" / "moss.cpp").read_text()
    projection_start = old_compiler.index(
        "static void project_imported_h3_checked_bodies(Program& program)")
    projection_end = old_compiler.index(
        "static void validate_imported_h3_summary_closure", projection_start)
    projection = old_compiler[projection_start:projection_end]
    assert "catch (const std::invalid_argument& error)" in projection
    assert 'invalid(string("checked ABI-v10 body cannot be decoded: ") + error.what());' in projection
    print("pre-D2 importer maps unsupported-body decode to rejection; no fallback")
finally:
    subprocess.run(["git", "worktree", "remove", "--force",
                    str(BASELINE_TREE)], cwd=ROOT, check=True)
