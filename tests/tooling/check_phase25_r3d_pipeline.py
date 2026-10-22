#!/usr/bin/env python3
"""Source-free checked Map projection and rehashed hostile imports."""

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

export fn double(values):
  return values |> map(_ * 2)
"""
CONSUMER = """module Main
import Service

fn main():
  echo Service.double([3, 4])[0]
  echo Service.double([3, 4])[1]
  echo Service.double([1.5, 2.5])[0]
  echo Service.double([1.5, 2.5])[1]
"""
EXPECTED = "6\n8\n3.0\n5.0\n"


def run(argv, *, cwd, env, expected=0):
    result = subprocess.run(argv, cwd=cwd, env=env, capture_output=True,
                            text=True, timeout=900)
    output = result.stdout + result.stderr
    assert result.returncode == expected, (argv, result.returncode, output)
    return output


with tempfile.TemporaryDirectory(prefix="phase25-r3d-map-", dir=TMP) as name:
    scratch = Path(name)
    base_env = os.environ.copy()
    base_env.update(MOSS_STAGED_ABI="v10", MOSS=str(MOSS), TMPDIR=str(TMP))
    provider = scratch / "provider"
    (provider / "src").mkdir(parents=True)
    (provider / "Moss.toml").write_text(
        '[package]\nname = "r3d_map_provider"\nversion = "0.1.0"\n\n'
        '[build]\nsource = "src"\n')
    (provider / "src/service.moss").write_text(PROVIDER)
    artifacts_by_profile = {}
    for profile, args in (("debug", []), ("release", ["--release"])):
        env = base_env | {"MARGO_HOME": str(scratch / f"provider-{profile}")}
        run([str(MARGO), *args, "build"], cwd=provider, env=env)
        artifacts = scratch / f"artifacts-{profile}"
        shutil.copytree(provider / "build" / profile, artifacts)
        artifacts_by_profile[profile] = artifacts

    poisoner = scratch / "poison_generic_callable"
    mutator = scratch / "mutate_r3d_pipeline"
    for source, output in (
        ("tests/tooling/poison_phase25_generic_callable.cpp", poisoner),
        ("tests/tooling/mutate_phase25_r3d_pipeline_artifact.cpp", mutator),
    ):
        run(["g++", "-std=c++17", "-O0", "-Wall", "-Wextra", "-Werror",
             "-Wno-missing-field-initializers", "-pedantic", "-Isrc",
             source, "-o", str(output)], cwd=ROOT, env=base_env)
    for artifacts in artifacts_by_profile.values():
        run([str(poisoner), str(artifacts / "Service.mossi"),
             str(artifacts / "libService.rlib"), "Service", "double"],
            cwd=ROOT, env=base_env)
    shutil.rmtree(provider / "src")
    assert not (provider / "src").exists()

    consumer = scratch / "consumer"
    (consumer / "src").mkdir(parents=True)
    (consumer / "Moss.toml").write_text(
        '[package]\nname = "r3d_map_consumer"\nversion = "0.1.0"\n\n'
        '[build]\nsource = "src"\n')
    main = consumer / "src/main.moss"
    main.write_text(CONSUMER)
    for profile, args in (("debug", []), ("release", ["--release"])):
        artifacts = artifacts_by_profile[profile]
        env = base_env | {
            "MOSS_MODULE_PATH": str(artifacts),
            "MARGO_HOME": str(scratch / f"consumer-{profile}"),
        }
        trace_env = env | {"MOSS_PHASE25_CHECKED_BODY_TRACE": "1"}
        run([str(MARGO), *args, "build"], cwd=consumer, env=trace_env)
        check = run([str(MOSS), "check", str(main), "--json"],
                    cwd=consumer, env=trace_env)
        assert "MOSS_CHECKED_BODY_PROJECTED|" in check and \
            "Service:fn:double|checked-body-v1" in check, check
        output = run([str(MARGO), *args, "run"], cwd=consumer, env=env)
        assert output == EXPECTED, (profile, output)

    original = main.read_text()
    main.write_text("""module Main
import Service

fn main():
  echo Service.double([\"not numeric\"])[0]
""")
    bad_env = base_env | {
        "MOSS_MODULE_PATH": str(artifacts_by_profile["debug"]),
        "MARGO_HOME": str(scratch / "unsupported-numeric-source"),
        "MOSS_PHASE25_CHECKED_BODY_TRACE": "1",
    }
    result = subprocess.run([str(MOSS), "check", str(main), "--json"],
                            cwd=consumer, env=bad_env, capture_output=True,
                            text=True, timeout=300)
    diagnostic = result.stdout + result.stderr
    assert result.returncode != 0 and "TYPE_MISMATCH" in diagnostic, diagnostic
    main.write_text(original)

    for mode in ("operation", "placeholder", "result-relation", "effects"):
        mutated = scratch / f"artifacts-{mode}"
        shutil.copytree(artifacts_by_profile["debug"], mutated)
        run([str(mutator), str(mutated / "Service.mossi"),
             str(mutated / "libService.rlib"), mode, "Service"],
            cwd=ROOT, env=base_env)
        hostile_env = base_env | {
            "MOSS_MODULE_PATH": str(mutated),
            "MARGO_HOME": str(scratch / f"consumer-{mode}"),
            "MOSS_PHASE25_CHECKED_BODY_TRACE": "1",
        }
        result = subprocess.run([str(MOSS), "check", str(main), "--json"],
                                cwd=consumer, env=hostile_env,
                                capture_output=True, text=True, timeout=300)
        diagnostic = result.stdout + result.stderr
        assert result.returncode != 0 and "SOURCE_FREE_" in diagnostic, (
            mode, diagnostic)
        assert "Service:fn:double|checked-body-v1" not in diagnostic, (
            mode, "forged Map body projected before rejection", diagnostic)
        print(f"PASS rehashed {mode} mutation rejected before projection")

print("R3D source-free Map Debug/Release and hostile cases passed")
