#!/usr/bin/env python3
"""Source-free checked-body projection and Int/String execution for index_of."""

import os
from pathlib import Path
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
MOSS = Path(os.environ.get("MOSS", ROOT / "moss")).resolve()
MARGO = ROOT / "margo"
TMP = ROOT / "tmp"
TMP.mkdir(exist_ok=True)


def run(command, *, cwd=ROOT, env=None, expected=0):
    result = subprocess.run(command, cwd=cwd, env=env,
                            capture_output=True, text=True, timeout=300)
    combined = result.stdout + result.stderr
    assert result.returncode == expected, (
        command, result.returncode, combined)
    return result.stdout, result.stderr


def package(root, name, source):
    (root / "src").mkdir(parents=True)
    (root / "Moss.toml").write_text(
        f'[package]\nname = "{name}"\nversion = "0.1.0"\n\n'
        '[build]\nsource = "src"\n')
    (root / "src/main.moss").write_text(source)


PROVIDER = """module Service

export fn index_of(items, target) -> Int:
  var i = 0
  n = items |> count
  while i < n:
    if items[i] == target:
      return i
    i = i + 1
  return 0 - 1
"""


with tempfile.TemporaryDirectory(prefix="phase25-d3c-collection-", dir=TMP) as root:
    scratch = Path(root)
    provider = scratch / "provider"
    package(provider, "phase25_d3c_provider", PROVIDER)
    staged = os.environ.copy()
    staged.update(MOSS_STAGED_ABI="v10", MOSS=str(MOSS),
                  MARGO_HOME=str(scratch / "margo-provider"), TMPDIR=str(TMP))
    run([str(MARGO), "build"], cwd=provider, env=staged)
    artifacts = scratch / "artifacts"
    shutil.copytree(provider / "build/debug", artifacts)
    shutil.rmtree(provider / "src")
    assert not (provider / "src").exists(), "provider source remained available"

    consumer_source = """module Main
import Service

fn main():
  echo Service.index_of([1, 2], 2)
  echo Service.index_of(["oak", "moss"], "moss")
"""
    consumer = scratch / "consumer"
    package(consumer, "phase25_d3c_consumer", consumer_source)
    env = staged | {"MOSS_MODULE_PATH": str(artifacts),
                    "MARGO_HOME": str(scratch / "margo-consumer")}
    run([str(MARGO), "build"], cwd=consumer, env=env)
    stdout, _ = run([str(MARGO), "run"], cwd=consumer, env=env)
    assert stdout == "1\n1\n", stdout

    trace_env = env | {"MOSS_PHASE25_CHECKED_BODY_TRACE": "1"}
    _, trace = run([str(MOSS), "check", str(consumer / "src/main.moss"),
                    "--json"], env=trace_env)
    trace_line = "MOSS_CHECKED_BODY_PROJECTED|callable-v1:"
    assert trace.count(trace_line) >= 1 and ":fn:index_of|checked-body-v1" in trace, trace

    bad = scratch / "unsupported_equality_consumer"
    package(bad, "phase25_d3c_bad_equality", """module Main
import Service

type Box:
  value: Int

fn main():
  echo Service.index_of([Box(value: 1)], Box(value: 1))
""")
    stdout, stderr = run([str(MOSS), "check", str(bad / "src/main.moss"),
                          "--json"], cwd=bad, env=env, expected=1)
    assert "RECORD_EQUALITY_UNSUPPORTED" in stdout + stderr, stdout + stderr

print("D3c source-free collection projection, Int/String execution, and unsupported equality rejection: PASS")
