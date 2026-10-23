#!/usr/bin/env python3
"""Source-free checked Try/Raise projection and rehashed hostile imports."""

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

enum Failure:
  Bad
  Detailed(code: Int)

export fn nonnegative(value):
  try:
    if value < 0:
      raise Failure.Bad
    return value
  recover Failure.Bad:
    return 0
"""
CONSUMER = """module Main
import Service

fn main():
  echo Service.nonnegative(7)
  echo Service.nonnegative(-2)
"""
EXPECTED = "7\n0\n"


def run(argv, *, cwd, env, expected=0):
    result = subprocess.run(argv, cwd=cwd, env=env, capture_output=True,
                           text=True, timeout=900)
    output = result.stdout + result.stderr
    assert result.returncode == expected, (argv, result.returncode, output)
    return output


with tempfile.TemporaryDirectory(prefix="phase25-r3f-v10-", dir=TMP) as name:
    scratch = Path(name)
    env = os.environ.copy()
    env.update(MOSS_STAGED_ABI="v10", MOSS=str(MOSS), TMPDIR=str(TMP))
    provider = scratch / "provider"
    (provider / "src").mkdir(parents=True)
    (provider / "Moss.toml").write_text(
        '[package]\nname = "r3f_v10_provider"\nversion = "0.1.0"\n\n'
        '[build]\nsource = "src"\n')
    (provider / "src/service.moss").write_text(PROVIDER)
    artifacts_by_profile = {}
    for profile, args in (("debug", []), ("release", ["--release"])):
        profile_env = env | {"MARGO_HOME": str(scratch / f"provider-{profile}")}
        run([str(MARGO), *args, "build"], cwd=provider, env=profile_env)
        artifacts = scratch / f"artifacts-{profile}"
        shutil.copytree(provider / "build" / profile, artifacts)
        artifacts_by_profile[profile] = artifacts

    poisoner = scratch / "poison_legacy"
    mutator = scratch / "mutate_exception"
    for source, output in (
        ("tests/tooling/poison_phase25_generic_callable.cpp", poisoner),
        ("tests/tooling/mutate_phase25_r3f_exception_artifact.cpp", mutator),
    ):
        run(["g++", "-std=c++17", "-O0", "-Wall", "-Wextra", "-Werror",
             "-Wno-missing-field-initializers", "-pedantic", "-Isrc",
             source, "-o", str(output)], cwd=ROOT, env=env)
    for artifacts in artifacts_by_profile.values():
        run([str(poisoner), str(artifacts / "Service.mossi"),
             str(artifacts / "libService.rlib"), "Service", "nonnegative"],
            cwd=ROOT, env=env)
    shutil.rmtree(provider / "src")
    assert not (provider / "src").exists()

    consumer = scratch / "consumer"
    (consumer / "src").mkdir(parents=True)
    (consumer / "Moss.toml").write_text(
        '[package]\nname = "r3f_v10_consumer"\nversion = "0.1.0"\n\n'
        '[build]\nsource = "src"\n')
    main = consumer / "src/main.moss"
    main.write_text(CONSUMER)
    for profile, args in (("debug", []), ("release", ["--release"])):
        artifacts = artifacts_by_profile[profile]
        profile_env = env | {
            "MOSS_MODULE_PATH": str(artifacts),
            "MARGO_HOME": str(scratch / f"consumer-{profile}"),
            "MOSS_PHASE25_CHECKED_BODY_TRACE": "1",
        }
        check = run([str(MOSS), "check", str(main), "--json"],
                    cwd=consumer, env=profile_env)
        assert "Service:fn:nonnegative|checked-body-v1" in check, check
        run([str(MARGO), *args, "build"], cwd=consumer, env=profile_env)
        assert run([str(MARGO), *args, "run"], cwd=consumer,
                   env=profile_env) == EXPECTED

    artifacts = scratch / "hostile-recovery-selection"
    shutil.copytree(artifacts_by_profile["debug"], artifacts)
    run([str(mutator), str(artifacts / "Service.mossi"),
         str(artifacts / "libService.rlib"), "recovery-selection", "Service"],
        cwd=ROOT, env=env)
    hostile_env = env | {
        "MOSS_MODULE_PATH": str(artifacts),
        "MARGO_HOME": str(scratch / "consumer-hostile"),
        "MOSS_PHASE25_CHECKED_BODY_TRACE": "1",
    }
    result = subprocess.run([str(MOSS), "check", str(main), "--json"],
                            cwd=consumer, env=hostile_env,
                            capture_output=True, text=True, timeout=300)
    diagnostic = result.stdout + result.stderr
    assert result.returncode != 0 and "SOURCE_FREE_" in diagnostic, diagnostic
    assert "Service:fn:nonnegative|checked-body-v1" not in diagnostic, diagnostic
    print("PASS source-deleted, poisoned ABI-v10 Try/Raise Debug/Release")
    print("PASS rehashed recovery-selection mutation rejected before projection")
