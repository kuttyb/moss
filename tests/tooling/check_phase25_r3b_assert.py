#!/usr/bin/env python3
"""Source-free generic assert projection and hostile ABI-v10 imports."""

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

export fn positive(value):
  assert(value > 0)
  return value
"""
CONSUMER = """module Main
import Service

fn main():
  echo Service.positive(7)
"""
EXPECTED = "7\n"


def run(argv, *, cwd, env, expected=0):
    result = subprocess.run(argv, cwd=cwd, env=env, capture_output=True,
                           text=True, timeout=300)
    output = result.stdout + result.stderr
    assert result.returncode == expected, (argv, result.returncode, output)
    return output


with tempfile.TemporaryDirectory(prefix="phase25-r3b-assert-", dir=TMP) as name:
    scratch = Path(name)
    base_env = os.environ.copy()
    base_env.update(MOSS_STAGED_ABI="v10", MOSS=str(MOSS), TMPDIR=str(TMP))
    provider = scratch / "provider"
    (provider / "src").mkdir(parents=True)
    (provider / "Moss.toml").write_text(
        '[package]\nname = "r3b_assert_provider"\nversion = "0.1.0"\n\n'
        '[build]\nsource = "src"\n')
    (provider / "src/service.moss").write_text(PROVIDER)
    provider_artifacts = {}
    for profile, args in (("debug", []), ("release", ["--release"])):
        provider_env = base_env | {
            "MARGO_HOME": str(scratch / f"margo-provider-{profile}")}
        run([str(MARGO), *args, "build"], cwd=provider, env=provider_env)
        artifacts = scratch / f"artifacts-{profile}"
        shutil.copytree(provider / "build" / profile, artifacts)
        provider_artifacts[profile] = artifacts

    poisoner = scratch / "poison_generic_callable"
    mutator = scratch / "mutate_r3b_assert"
    for source, output in (
        ("tests/tooling/poison_phase25_generic_callable.cpp", poisoner),
        ("tests/tooling/mutate_phase25_r3b_numeric_artifact.cpp", mutator),
    ):
        run(["g++", "-std=c++17", "-O0", "-Wall", "-Wextra", "-Werror",
             "-Wno-missing-field-initializers", "-pedantic", "-Isrc",
             source, "-o", str(output)], cwd=ROOT, env=base_env)
    for artifacts in provider_artifacts.values():
        run([str(poisoner), str(artifacts / "Service.mossi"),
             str(artifacts / "libService.rlib"), "Service", "positive"],
            cwd=ROOT, env=base_env)
    shutil.rmtree(provider / "src")
    assert not (provider / "src").exists()

    consumer = scratch / "consumer"
    (consumer / "src").mkdir(parents=True)
    (consumer / "Moss.toml").write_text(
        '[package]\nname = "r3b_assert_consumer"\nversion = "0.1.0"\n\n'
        '[build]\nsource = "src"\n')
    source_path = consumer / "src/main.moss"
    source_path.write_text(CONSUMER)
    for profile, args in (("debug", []), ("release", ["--release"])):
        artifacts = provider_artifacts[profile]
        env = base_env | {
            "MOSS_MODULE_PATH": str(artifacts),
            "MARGO_HOME": str(scratch / f"margo-consumer-{profile}"),
        }
        run([str(MARGO), *args, "build"], cwd=consumer, env=env)
        trace_env = env | {"MOSS_PHASE25_CHECKED_BODY_TRACE": "1"}
        check = run([str(MOSS), "check", str(source_path), "--json"],
                    cwd=consumer, env=trace_env)
        assert "MOSS_CHECKED_BODY_PROJECTED|" in check and \
            "Service:fn:positive|checked-body-v1" in check, check
        output = run([str(MARGO), *args, "run"], cwd=consumer, env=env)
        assert output == EXPECTED, (profile, output)

    for mode in ("assert-predicate", "assert-return-identity",
                 "assert-requirement-result", "assert-effect"):
        mutated = scratch / f"artifacts-{mode}"
        shutil.copytree(provider_artifacts["debug"], mutated)
        run([str(mutator), str(mutated / "Service.mossi"),
             str(mutated / "libService.rlib"), mode, "Service"],
            cwd=ROOT, env=base_env)
        bad_env = base_env | {
            "MOSS_MODULE_PATH": str(mutated),
            "MARGO_HOME": str(scratch / f"margo-{mode}"),
            "MOSS_PHASE25_CHECKED_BODY_TRACE": "1",
        }
        result = subprocess.run([str(MOSS), "check", str(source_path), "--json"],
                                cwd=consumer, env=bad_env, capture_output=True,
                                text=True, timeout=300)
        diagnostic = result.stdout + result.stderr
        assert result.returncode != 0 and "SOURCE_FREE_" in diagnostic, (
            mode, diagnostic)
        assert "Service:fn:positive|checked-body-v1" not in diagnostic, (
            mode, "forged assertion body projected before rejection", diagnostic)
        print(f"PASS rehashed {mode} mutation rejected before projection")

print("R3B generic assert source-free Debug/Release and hostile cases passed")
