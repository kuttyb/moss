#!/usr/bin/env python3
"""Source-free escaping Raise effects remain checker-visible and sealed."""

import json
import os
from pathlib import Path
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[2]
TMP = ROOT / "tmp"
TMP.mkdir(exist_ok=True)
MOSS = Path(os.environ.get(
    "MOSS", ROOT / "build/compiler/moss-dev")).resolve()
MARGO = ROOT / "margo"
WORK = TMP / "r3f-escaping-raise-effects"
if WORK.exists():
    shutil.rmtree(WORK)
provider = WORK / "provider"
(provider / "src").mkdir(parents=True)
(provider / "Moss.toml").write_text(
    '[package]\nname = "r3f_escaping_provider"\nversion = "0.1.0"\n\n'
    '[build]\nsource = "src"\n')
(provider / "src/service.moss").write_text("""module Service

enum Failure:
  Detailed(code: Int)
  Alternate(code: Int)

export fn unhandled(code):
  raise Failure.Detailed(code: code)
""")
consumer_text = """module Main
import Service

export fn relay(code: Int):
  Service.unhandled(code)

fn main():
  pass
"""
manifest = ('[package]\nname = "r3f_escaping_consumer"\nversion = "0.1.0"\n\n'
            '[build]\nsource = "src"\n')
env = os.environ.copy()
env.update(MOSS=str(MOSS), MOSS_STAGED_ABI="v10", TMPDIR=str(TMP))
artifacts_by_profile = {}


def run(argv, *, cwd, env, expected=0):
    result = subprocess.run(argv, cwd=cwd, env=env, capture_output=True,
                            text=True, timeout=900)
    output = result.stdout + result.stderr
    assert result.returncode == expected, (argv, result.returncode, output)
    return output


for profile, args in (("debug", []), ("release", ["--release"])):
    profile_env = env | {"MARGO_HOME": str(WORK / f"provider-{profile}")}
    run([str(MARGO), *args, "build"], cwd=provider, env=profile_env)
    artifacts = WORK / f"artifacts-{profile}"
    shutil.copytree(provider / "build" / profile, artifacts)
    artifacts_by_profile[profile] = artifacts

poisoner = WORK / "poison_legacy"
mutator = WORK / "mutate_raise_effect"
for source, output in (
        ("tests/tooling/poison_phase25_generic_callable.cpp", poisoner),
        ("tests/tooling/mutate_phase25_r3f_raise_effect_artifact.cpp", mutator)):
    run(["g++", "-std=c++17", "-O0", "-Wall", "-Wextra", "-Werror",
         "-Wno-missing-field-initializers", "-pedantic", "-Isrc", source,
         "-o", str(output)], cwd=ROOT, env=env)
for artifacts in artifacts_by_profile.values():
    run([str(poisoner), str(artifacts / "Service.mossi"),
         str(artifacts / "libService.rlib"), "Service", "unhandled"],
        cwd=ROOT, env=env)
shutil.rmtree(provider / "src")
assert not (provider / "src").exists()

consumer = WORK / "consumer"
(consumer / "src").mkdir(parents=True)
(consumer / "Moss.toml").write_text(manifest)
main = consumer / "src/main.moss"
main.write_text(consumer_text)
for profile, args in (("debug", []), ("release", ["--release"])):
    artifacts = artifacts_by_profile[profile]
    profile_env = env | {
        "MOSS_MODULE_PATH": str(artifacts),
        "MARGO_HOME": str(WORK / f"consumer-{profile}"),
        "MOSS_PHASE25_CHECKED_BODY_TRACE": "1",
    }
    trace = run([str(MOSS), "check", str(main), "--json"],
                cwd=consumer, env=profile_env)
    assert "Service:fn:unhandled|checked-body-v1" in trace, trace
    run([str(MARGO), *args, "build"], cwd=consumer, env=profile_env)
    effects_env = profile_env.copy()
    effects_env.pop("MOSS_PHASE25_CHECKED_BODY_TRACE", None)
    effects = json.loads(run([
        str(MOSS), "effects", "entity-v1:function:Main__relay",
        "--source", str(main), "--json"], cwd=consumer,
        env=effects_env))
    raised = effects["result"]["observable_effects"]["raise_set"]
    assert raised == ["enum:Service__Failure:Detailed"], raised
    print(f"PASS source-free {profile} projection preserves escaping Raise effect")

invalid = WORK / "invalid-consumer"
(invalid / "src").mkdir(parents=True)
(invalid / "Moss.toml").write_text(
    '[package]\nname = "r3f_escaping_invalid"\nversion = "0.1.0"\n\n'
    '[build]\nsource = "src"\n')
invalid_main = invalid / "src/main.moss"
invalid_main.write_text("""module Main
import Service

fn invalid():
  Service.unhandled("wrong")

fn main():
  pass
""")
invalid_env = env | {
    "MOSS_MODULE_PATH": str(artifacts_by_profile["debug"]),
    "MARGO_HOME": str(WORK / "invalid-consumer-home"),
}
diagnostic = run([str(MOSS), "check", str(invalid_main), "--json"],
                 cwd=invalid, env=invalid_env, expected=1)
assert "ENUM_FIELD_TYPE_MISMATCH" in diagnostic or "TYPE_MISMATCH" in diagnostic, diagnostic
print("PASS source-free String specialization remains rejected")

for mode in ("missing-raise-effect", "wrong-raise-effect",
             "wrong-raise-variant", "wrong-payload-type",
             "wrong-private-dependency-hash"):
    artifacts = WORK / f"hostile-{mode}"
    shutil.copytree(artifacts_by_profile["debug"], artifacts)
    run([str(mutator), str(artifacts / "Service.mossi"),
         str(artifacts / "libService.rlib"), mode, "Service"],
        cwd=ROOT, env=env)
    hostile_env = env | {
        "MOSS_MODULE_PATH": str(artifacts),
        "MARGO_HOME": str(WORK / f"hostile-consumer-{mode}"),
        "MOSS_PHASE25_CHECKED_BODY_TRACE": "1",
    }
    diagnostic = run([str(MOSS), "check", str(main), "--json"],
                     cwd=consumer, env=hostile_env, expected=1)
    assert "SOURCE_FREE_" in diagnostic, (mode, diagnostic)
    assert "Service:fn:unhandled|checked-body-v1" not in diagnostic, (
        mode, "forged raising body projected before rejection", diagnostic)
    print(f"PASS rehashed {mode} mutation rejected before projection")
