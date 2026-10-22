#!/usr/bin/env python3
"""Source-free typed enum Match projection and internally rehashed attacks."""

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
PROVIDER = """module Service

export enum Outcome:
  Ready(value: Int)
  Empty

export fn unwrap(adjustment, outcome: Outcome) -> Int:
  match outcome:
    case Ready(value):
      return value + adjustment
    case Empty:
      return adjustment
"""
CONSUMER = """module Main
import Service

fn main():
  echo Service.unwrap(5, Service.Outcome.Ready(value: 7))
  echo Service.unwrap(5, Service.Outcome.Empty)
"""
EXPECTED = "12\n5\n"


def run(argv, *, cwd, env, expected=0):
    result = subprocess.run(argv, cwd=cwd, env=env, capture_output=True,
                           text=True, timeout=300)
    output = result.stdout + result.stderr
    assert result.returncode == expected, (argv, result.returncode, output)
    return output


with tempfile.TemporaryDirectory(prefix="phase25-r3c-match-", dir=TMP) as name:
    scratch = Path(name)
    base_env = os.environ.copy()
    base_env.update(MOSS_STAGED_ABI="v10", MOSS=str(MOSS), TMPDIR=str(TMP))
    mutator = scratch / "mutate_r3c_match"
    poisoner = scratch / "poison_generic_callable"
    for source, output in (
        ("tests/tooling/mutate_phase25_r3c_match_artifact.cpp", mutator),
        ("tests/tooling/poison_phase25_generic_callable.cpp", poisoner),
    ):
        run(["g++", "-std=c++17", "-O0", "-Wall", "-Wextra", "-Werror",
             "-Wno-missing-field-initializers", "-pedantic", "-Isrc",
             source, "-o", str(output)], cwd=ROOT, env=base_env)

    provider = scratch / "provider"
    (provider / "src").mkdir(parents=True)
    (provider / "Moss.toml").write_text(
        '[package]\nname = "r3c_match_provider"\nversion = "0.1.0"\n\n'
        '[build]\nsource = "src"\n')
    (provider / "src/service.moss").write_text(PROVIDER)
    artifacts_by_profile = {}
    for profile, args in (("debug", []), ("release", ["--release"])):
        env = base_env | {"MARGO_HOME": str(scratch / f"provider-{profile}")}
        run([str(MARGO), *args, "build"], cwd=provider, env=env)
        artifacts = scratch / f"artifacts-{profile}"
        shutil.copytree(provider / "build" / profile, artifacts)
        artifacts_by_profile[profile] = artifacts
        run([str(poisoner), str(artifacts / "Service.mossi"),
             str(artifacts / "libService.rlib"), "Service", "unwrap"],
            cwd=ROOT, env=base_env)
    shutil.rmtree(provider / "src")

    consumer = scratch / "consumer"
    (consumer / "src").mkdir(parents=True)
    (consumer / "Moss.toml").write_text(
        '[package]\nname = "r3c_match_consumer"\nversion = "0.1.0"\n\n'
        '[build]\nsource = "src"\n')
    (consumer / "src/main.moss").write_text(CONSUMER)
    for profile, args in (("debug", []), ("release", ["--release"])):
        artifacts = artifacts_by_profile[profile]
        env = base_env | {
            "MOSS_MODULE_PATH": str(artifacts),
            "MARGO_HOME": str(scratch / f"consumer-{profile}"),
        }
        trace_env = env | {"MOSS_PHASE25_CHECKED_BODY_TRACE": "1"}
        run([str(MARGO), *args, "build"], cwd=consumer, env=trace_env)
        check = run([str(MOSS), "check", str(consumer / "src/main.moss"),
                     "--json"], cwd=consumer, env=trace_env)
        assert "Service:fn:unwrap|checked-body-v1" in check, check
        assert run([str(MARGO), *args, "run"], cwd=consumer, env=env) == EXPECTED

    good_artifacts = artifacts_by_profile["debug"]
    mutations = (
        "wrong-variant", "payload-ordinal", "payload-type", "branch-identity",
        "return-lineage", "return-result", "provider-identity",
        "numeric-semantics",
    )
    for mode in mutations:
        mutated = scratch / f"artifacts-{mode}"
        shutil.copytree(good_artifacts, mutated)
        run([str(mutator), str(mutated / "Service.mossi"),
             str(mutated / "libService.rlib"), mode, "Service"],
            cwd=ROOT, env=base_env)
        env = base_env | {
            "MOSS_MODULE_PATH": str(mutated),
            "MARGO_HOME": str(scratch / f"consumer-{mode}"),
            "MOSS_PHASE25_CHECKED_BODY_TRACE": "1",
        }
        result = subprocess.run(
            [str(MOSS), "check", str(consumer / "src/main.moss"), "--json"],
            cwd=consumer, env=env, capture_output=True, text=True, timeout=300)
        diagnostic = result.stdout + result.stderr
        assert result.returncode != 0 and "SOURCE_FREE_" in diagnostic, (
            mode, diagnostic)
        assert "Service:fn:unwrap|checked-body-v1" not in diagnostic, (
            mode, "forged Match projected before rejection", diagnostic)
        print(f"PASS rehashed {mode} mutation rejected before projection")

print("R3C source-free Match Debug/Release and hostile cases passed")
