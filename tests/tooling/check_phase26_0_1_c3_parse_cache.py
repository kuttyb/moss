#!/usr/bin/env python3
"""Verify failed call-parse cache replay preserves output mutation semantics."""

from pathlib import Path
import subprocess
import json
import os
import sys

from compiler_test_support import compiler_support_sources

ROOT = Path(__file__).resolve().parents[2]
TMP = ROOT / "tmp"
TMP.mkdir(exist_ok=True)
if len(sys.argv) != 2:
    raise SystemExit("usage: check_phase26_0_1_c3_parse_cache.py COMPILER")
compiler = Path(sys.argv[1]).resolve()

binary = TMP / "phase26-0-1-c3-negative-parse-cache"
subprocess.run([
    "g++", "-std=c++17", "-O0", "-Wall", "-Wextra", "-Werror",
    "-Wno-missing-field-initializers", "-pedantic",
    str(ROOT / "tests/tooling/check_phase26_0_1_c3_parse_cache.cpp"),
    *compiler_support_sources(), "-o", str(binary),
], check=True)
subprocess.run([str(binary)], check=True)

fixtures = {
    "d18": ROOT / "benchmarks/compiler_torture/fixtures/inference/w4_infer_deep_d018.moss",
    "regex": ROOT / "packages/regex/src/main.moss",
    "datastructures": ROOT / "sdk/datastructures/src/datastructures.moss",
}
for name, source in fixtures.items():
    env = dict(os.environ, MOSS_PROFILE_COMPILER="1")
    checked = subprocess.run(
        [str(compiler), "check", str(source), "--json"], cwd=ROOT,
        env=env, capture_output=True, text=True, check=False)
    payload = json.loads(checked.stdout)
    if checked.returncode != 0 or payload.get("ok") is not True:
        raise AssertionError(f"{name} failed check: {checked.stdout!r} {checked.stderr!r}")
    profiles = []
    for line in checked.stderr.splitlines():
        if not line.startswith("MOSS_PROFILE|"):
            continue
        _, key, value = line.split("|", 2)
        if not value.isdigit():
            continue
        if key == "functions_analyzed":
            profiles.append({})
        if profiles:
            profiles[-1][key] = int(value)
    if not profiles:
        raise AssertionError(f"{name} did not emit checker profile counters")
    for index, profile in enumerate(profiles):
        get = lambda key: profile.get(f"parse_cache_{key}", 0)
        misses = get("misses")
        entries = get("entries")
        limit = get("limit")
        insertions = get("insertions_before_cap")
        failed_simple_inserts = get("failed_simple_insertions")
        failed_member_inserts = get("failed_member_insertions")
        after_cap = sum(get(key) for key in (
            "success_after_cap", "failed_simple_after_cap",
            "failed_member_after_cap"))
        if entries != insertions or misses != insertions + after_cap:
            raise AssertionError(
                f"{name} checker {index} parse misses do not partition into "
                f"cached misses and post-cap misses: {profile}"
            )
        if entries >= limit:
            raise AssertionError(
                f"{name} checker {index} unexpectedly filled parse cache: "
                f"{entries}/{limit}"
            )
        if failed_simple_inserts != get("distinct_failed_simple_inputs") or \
                failed_member_inserts != get("distinct_failed_member_inputs"):
            raise AssertionError(
                f"{name} checker {index} failed-call keys were not each cached once"
            )
        if get("failed_simple_cache_hits") + get("failed_member_cache_hits") == 0:
            raise AssertionError(
                f"{name} checker {index} did not exercise negative cache hits"
            )
print("C3 negative call-parse cache replay checks passed")
