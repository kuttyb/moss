#!/usr/bin/env python3
"""Source-free staged-v10 execution for the bounded collection loops."""

import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
MOSS = Path(os.environ.get("MOSS", ROOT / "moss")).resolve()
MARGO = ROOT / "margo"
TMP = ROOT / "tmp"
TMP.mkdir(exist_ok=True)
SDK = ROOT / "sdk/foundations/collections"


def run(command, *, cwd=ROOT, env=None, expected=0):
    result = subprocess.run(command, cwd=cwd, env=env,
                            capture_output=True, text=True, timeout=300)
    combined = result.stdout + result.stderr
    assert result.returncode == expected, (
        command, result.returncode, combined)
    return result.stdout, result.stderr


with tempfile.TemporaryDirectory(prefix="phase25-d4-sdk-collections-", dir=TMP) as root:
    scratch = Path(root)
    provider = scratch / "provider"
    shutil.copytree(SDK, provider,
                    ignore=shutil.ignore_patterns("build", "target", ".moss-cache"))
    staged = os.environ.copy()
    staged.update(MOSS_STAGED_ABI="v10", MOSS=str(MOSS),
                  MARGO_HOME=str(scratch / "margo-provider"), TMPDIR=str(TMP))
    run([str(MARGO), "build"], cwd=provider, env=staged)
    artifacts = scratch / "artifacts"
    shutil.copytree(provider / "build/debug", artifacts)
    interface = artifacts / "collections.mossi"
    archive = artifacts / "libcollections.rlib"
    assert interface.is_file() and archive.is_file()

    poisoner = scratch / "poison_generic_callable"
    run(["g++", "-std=c++17", "-O0", "-Wall", "-Wextra", "-Werror",
         "-Wno-missing-field-initializers", "-pedantic", "-Isrc",
         "tests/tooling/poison_phase25_generic_callable.cpp",
         "-o", str(poisoner)])
    mutator = scratch / "mutate_d4_collection_artifact"
    run(["g++", "-std=c++17", "-O0", "-Wall", "-Wextra", "-Werror",
         "-Wno-missing-field-initializers", "-pedantic", "-Isrc",
         "tests/tooling/mutate_phase25_d4_collection_artifact.cpp",
         "-o", str(mutator)])
    checked_body_mutator = scratch / "mutate_checked_collection_artifact"
    run(["g++", "-std=c++17", "-O0", "-Wall", "-Wextra", "-Werror",
         "-Wno-missing-field-initializers", "-pedantic", "-Isrc",
         "tests/tooling/mutate_phase25_parameter_return_artifact.cpp",
         "-o", str(checked_body_mutator)])
    poison_counts = {}
    for callable_name in ("index_of", "contains", "count_equal"):
        out, err = run([str(poisoner), str(interface), str(archive),
                        "collections", callable_name])
        report = out + err
        match = re.search(
            rf"removed (\d+) generic_stmt records from {callable_name}", report)
        assert match and int(match.group(1)) > 0, report
        poison_counts[callable_name] = int(match.group(1))
    shutil.rmtree(provider / "src")
    assert not (provider / "src").exists()
    consumer = scratch / "consumer"
    (consumer / "src").mkdir(parents=True)
    (consumer / "Moss.toml").write_text(
        '[package]\nname = "phase25_d4_collections_consumer"\n'
        'version = "0.1.0"\n\n[build]\nsource = "src"\n')
    source = """module Main
import collections

fn main():
  echo collections.index_of([8, 3, 8], 3)
  echo collections.index_of([8, 3, 8], 9)
  echo collections.index_of(["oak", "moss"], "moss")
  echo collections.contains([8, 3, 8], 8)
  echo collections.contains([8, 3, 8], 9)
  echo collections.contains(["oak", "moss"], "moss")
  echo collections.contains(["oak", "moss"], "cedar")
  echo collections.count_equal([8, 3, 8, 8], 8)
  echo collections.count_equal([8, 3, 8], 9)
  echo collections.count_equal(["oak", "moss", "oak"], "oak")
  echo collections.count_equal(["oak", "moss"], "cedar")
  echo collections.index_of(Vector[Int](), 5)
  echo collections.contains(Vector[String](), "moss")
  echo collections.count_equal(Vector[Int](), 5)
"""
    (consumer / "src/main.moss").write_text(source)
    env = staged | {"MOSS_MODULE_PATH": str(artifacts),
                    "MARGO_HOME": str(scratch / "margo-consumer")}
    run([str(MARGO), "build"], cwd=consumer, env=env)
    output, _ = run([str(MARGO), "run"], cwd=consumer, env=env)
    expected = "1\n-1\n1\ntrue\nfalse\ntrue\nfalse\n3\n0\n2\n0\n-1\nfalse\n0\n"
    assert output == expected, output

    trace_env = env | {"MOSS_PHASE25_CHECKED_BODY_TRACE": "1"}
    _, trace = run([str(MOSS), "check", str(consumer / "src/main.moss"),
                    "--json"], env=trace_env)
    for callable_name in ("index_of", "contains", "count_equal"):
        assert ("MOSS_CHECKED_BODY_PROJECTED|" in trace and
                f"::collections:fn:{callable_name}|checked-body-v1" in trace), trace

    for mode in ("bad-requirement", "bad-effect"):
        mutated = scratch / f"artifacts-{mode}"
        shutil.copytree(artifacts, mutated)
        run([str(mutator), str(mutated / "collections.mossi"),
             str(mutated / "libcollections.rlib"), mode])
        bad_consumer = scratch / f"consumer-{mode}"
        (bad_consumer / "src").mkdir(parents=True)
        (bad_consumer / "Moss.toml").write_text(
            '[package]\nname = "phase25_d4_bad_collection_' +
            mode.replace("-", "_") +
            '"\nversion = "0.1.0"\n\n[build]\nsource = "src"\n')
        (bad_consumer / "src/main.moss").write_text(source)
        bad_env = env | {"MOSS_MODULE_PATH": str(mutated),
                         "MARGO_HOME": str(scratch / f"margo-{mode}")}
        rejected = subprocess.run([str(MARGO), "build"], cwd=bad_consumer,
                                  env=bad_env, capture_output=True, text=True,
                                  timeout=300)
        diagnostic = rejected.stdout + rejected.stderr
        assert rejected.returncode != 0 and "SOURCE_FREE_" in diagnostic, (
            mode, rejected.returncode, diagnostic)
        print(f"PASS {mode} rejected through the checked ABI-v10 path")

    mutated = scratch / "artifacts-count-counter-start"
    shutil.copytree(artifacts, mutated)
    run([str(checked_body_mutator), str(mutated / "collections.mossi"),
         str(mutated / "libcollections.rlib"), "collection-count-start",
         "count_equal", "collections"])
    bad_consumer = scratch / "consumer-count-counter-start"
    (bad_consumer / "src").mkdir(parents=True)
    (bad_consumer / "Moss.toml").write_text(
        '[package]\nname = "phase25_d4_bad_count_counter_start"\n'
        'version = "0.1.0"\n\n[build]\nsource = "src"\n')
    (bad_consumer / "src/main.moss").write_text(source)
    bad_env = env | {"MOSS_MODULE_PATH": str(mutated),
                     "MARGO_HOME": str(scratch / "margo-count-counter-start")}
    rejected = subprocess.run([str(MARGO), "build"], cwd=bad_consumer,
                              env=bad_env, capture_output=True, text=True,
                              timeout=300)
    assert rejected.returncode != 0, ("collection-count-start", rejected.returncode,
                                      rejected.stdout + rejected.stderr)
    checked = subprocess.run(
        [str(MOSS), "check", str(bad_consumer / "src/main.moss"), "--json"],
        cwd=bad_consumer,
        env=bad_env | {"MOSS_PHASE25_CHECKED_BODY_TRACE": "1"},
        capture_output=True, text=True, timeout=300)
    diagnostic = checked.stdout + checked.stderr
    assert checked.returncode != 0 and "SOURCE_FREE_CHECKED_BODY_INVALID" in diagnostic, (
        "collection-count-start", checked.returncode, diagnostic)
    assert "checked ABI-v10 collection loop counters must start at zero" in diagnostic, diagnostic
    assert "MOSS_CHECKED_BODY_PROJECTED|callable-v1:collections:fn:count_equal|" not in diagnostic, diagnostic
    print("PASS collection-count-start rejected through the checked ABI-v10 path")

print("D4 actual SDK source-free outputs passed; legacy records removed: " +
      ", ".join(f"{name}={count}" for name, count in poison_counts.items()))
