#!/usr/bin/env python3
"""Source-free acceptance for the actual SDK collections.index_of."""

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


with tempfile.TemporaryDirectory(prefix="phase25-d3d-sdk-collections-", dir=TMP) as root:
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
    poison_stdout, poison_stderr = run(
        [str(poisoner), str(interface), str(archive), "collections", "index_of"])
    report = poison_stdout + poison_stderr
    match = re.search(r"removed (\d+) generic_stmt records from index_of", report)
    assert match and int(match.group(1)) > 0, report
    assert b"generic_stmt " in interface.read_bytes(), (
        "other SDK collection callables should retain their compatibility bodies")

    # Remove every provider lookup source before the consumer is built.
    shutil.rmtree(provider / "src")
    assert not (provider / "src").exists()

    consumer = scratch / "consumer"
    (consumer / "src").mkdir(parents=True)
    (consumer / "Moss.toml").write_text(
        '[package]\nname = "phase25_d3d_collections_consumer"\n'
        'version = "0.1.0"\n\n[build]\nsource = "src"\n')
    source = """module Main
import collections

fn main():
  echo collections.index_of([8, 3, 8], 3)
  echo collections.index_of([8, 3, 8], 9)
  echo collections.index_of([8, 3, 8], 8)
  echo collections.index_of([3, 8, 3], 3)
  echo collections.index_of(["oak", "moss"], "moss")
  echo collections.index_of(["oak", "moss"], "cedar")
  echo collections.index_of(Vector[Int](), 5)
"""
    (consumer / "src/main.moss").write_text(source)
    env = staged | {"MOSS_MODULE_PATH": str(artifacts),
                    "MARGO_HOME": str(scratch / "margo-consumer")}
    run([str(MARGO), "build"], cwd=consumer, env=env)
    output, _ = run([str(MARGO), "run"], cwd=consumer, env=env)
    assert output == "1\n-1\n0\n0\n1\n-1\n-1\n", output

    trace_env = env | {"MOSS_PHASE25_CHECKED_BODY_TRACE": "1"}
    _, trace = run([str(MOSS), "check", str(consumer / "src/main.moss"),
                    "--json"], env=trace_env)
    assert "MOSS_CHECKED_BODY_PROJECTED|" in trace and \
        "::collections:fn:index_of|checked-body-v1" in trace, trace

print("D3d actual SDK index_of: source-free Int/String native execution and legacy-body poison passed")
