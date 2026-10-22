#!/usr/bin/env python3
"""Source-free Map mutation acceptance and rehashed hostile imports."""

import os
from pathlib import Path
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
TMP = ROOT / "tmp"
TMP.mkdir(exist_ok=True)
MOSS = Path(os.environ.get("MOSS", ROOT / "moss")).resolve()
MARGO = ROOT / "margo"
PROVIDER_SOURCE = """module Service

export fn insert_and_read(values, key, value) -> Int:
  values[key] = value
  return values[key]
"""
CONSUMER_SOURCE = """module Main
import Service

fn main():
  values = Map()
  values["seed"] = 0
  echo Service.insert_and_read(values, "moss", 7)
  echo Service.insert_and_read(values, "codex", 9)
  echo values["moss"]
"""


def run(argv, *, cwd, env, expected=0):
    result = subprocess.run(argv, cwd=cwd, env=env, capture_output=True,
                           text=True, timeout=300)
    output = result.stdout + result.stderr
    assert result.returncode == expected, (argv, result.returncode, output)
    return output


with tempfile.TemporaryDirectory(prefix="phase25-r3b-map-", dir=TMP) as name:
    scratch = Path(name)
    provider = scratch / "provider"
    (provider / "src").mkdir(parents=True)
    (provider / "Moss.toml").write_text(
        '[package]\nname = "r3b_map_provider"\nversion = "0.1.0"\n\n'
        '[build]\nsource = "src"\n')
    (provider / "src/service.moss").write_text(PROVIDER_SOURCE)
    base_env = os.environ.copy()
    base_env.update(MOSS_STAGED_ABI="v10", MOSS=str(MOSS),
                    TMPDIR=str(TMP),
                    MARGO_HOME=str(scratch / "margo-provider"))
    run([str(MARGO), "build"], cwd=provider, env=base_env)

    artifacts = scratch / "artifacts"
    shutil.copytree(provider / "build/debug", artifacts)
    interface = artifacts / "Service.mossi"
    archive = artifacts / "libService.rlib"
    assert interface.is_file() and archive.is_file()

    def compile_tool(source, output):
        run(["g++", "-std=c++17", "-O0", "-Wall", "-Wextra", "-Werror",
             "-Wno-missing-field-initializers", "-pedantic", "-Isrc",
             source, "-o", str(output)], cwd=ROOT, env=base_env)

    poisoner = scratch / "poison_generic_callable"
    mutator = scratch / "mutate_r3b_map"
    compile_tool("tests/tooling/poison_phase25_generic_callable.cpp", poisoner)
    compile_tool("tests/tooling/mutate_phase25_r3b_map_artifact.cpp", mutator)
    run([str(poisoner), str(interface), str(archive), "Service",
         "insert_and_read"], cwd=ROOT, env=base_env)
    shutil.rmtree(provider / "src")
    assert not (provider / "src").exists()

    consumer = scratch / "consumer"
    (consumer / "src").mkdir(parents=True)
    (consumer / "Moss.toml").write_text(
        '[package]\nname = "r3b_map_consumer"\nversion = "0.1.0"\n\n'
        '[build]\nsource = "src"\n')
    (consumer / "src/main.moss").write_text(CONSUMER_SOURCE)
    consumer_env = base_env | {
        "MOSS_MODULE_PATH": str(artifacts),
        "MARGO_HOME": str(scratch / "margo-consumer"),
    }
    run([str(MARGO), "build"], cwd=consumer, env=consumer_env)
    trace_env = consumer_env | {"MOSS_PHASE25_CHECKED_BODY_TRACE": "1"}
    check = run([str(MOSS), "check", str(consumer / "src/main.moss"),
                 "--json"], cwd=consumer, env=trace_env)
    assert "MOSS_CHECKED_BODY_PROJECTED|" in check and \
        "Service:fn:insert_and_read|checked-body-v1" in check, check
    output = run([str(MARGO), "run"], cwd=consumer, env=consumer_env)
    assert output == "7\n9\n7\n", output

    expected_modes = {
        "index-access": "SOURCE_FREE_",
        "value-relation": "SOURCE_FREE_",
        "missing-may-panic": "SOURCE_FREE_",
        "missing-body": "SOURCE_FREE_",
    }
    for mode, marker in expected_modes.items():
        mutated = scratch / f"artifacts-{mode}"
        shutil.copytree(artifacts, mutated)
        run([str(mutator), str(mutated / "Service.mossi"),
             str(mutated / "libService.rlib"), mode, "Service"],
            cwd=ROOT, env=base_env)
        bad_env = consumer_env | {
            "MOSS_MODULE_PATH": str(mutated),
            "MARGO_HOME": str(scratch / f"margo-{mode}"),
            "MOSS_PHASE25_CHECKED_BODY_TRACE": "1",
        }
        result = subprocess.run(
            [str(MOSS), "check", str(consumer / "src/main.moss"), "--json"],
            cwd=consumer, env=bad_env, capture_output=True, text=True,
            timeout=300)
        diagnostic = result.stdout + result.stderr
        assert result.returncode != 0 and marker in diagnostic, (mode, diagnostic)
        assert "Service:fn:insert_and_read|checked-body-v1" not in diagnostic, (
            mode, "forged Map body projected before rejection", diagnostic)
        print(f"PASS rehashed Map {mode} mutation rejected before projection")

print("R3B checked Map source-free positive and hostile cases passed")
