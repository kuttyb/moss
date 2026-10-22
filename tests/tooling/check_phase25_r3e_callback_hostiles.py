#!/usr/bin/env python3
"""Rehashed ABI-v10 callback authority mutations reject before projection."""

import json
import os
from pathlib import Path
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[2]
TMP = ROOT / "tmp"
TMP.mkdir(exist_ok=True)
MOSS = Path(os.environ.get("MOSS", ROOT / "moss")).resolve()
MARGO = ROOT / "margo"
case = next(row for row in json.loads(
    (ROOT / "tests/phase25/source_free_corpus/corpus.json").read_text())["cases"]
    if row["id"] == "returned-callback")
work = TMP / "r3e-callback-hostiles"
if work.exists():
    shutil.rmtree(work)
provider = work / "provider"
consumer = work / "consumer"
(provider / "src").mkdir(parents=True)
(consumer / "src").mkdir(parents=True)
manifest = ('[package]\nname = "r3e_callback_hostiles"\nversion = "0.1.0"\n'
            '[build]\nsource = "src"\n')
(provider / "Moss.toml").write_text(manifest)
(consumer / "Moss.toml").write_text(manifest)
(provider / "src/service.moss").write_text(case["provider"])
main = consumer / "src/main.moss"
main.write_text(case["consumer"])
base_env = os.environ.copy()
base_env.update(MOSS_STAGED_ABI="v10", MOSS=str(MOSS), TMPDIR=str(TMP))


def run(argv, *, cwd, env, expected=0):
    result = subprocess.run(argv, cwd=cwd, env=env, capture_output=True,
                            text=True, timeout=900)
    output = result.stdout + result.stderr
    assert result.returncode == expected, (argv, result.returncode, output)
    return output


run([str(MARGO), "build"], cwd=provider,
    env=base_env | {"MARGO_HOME": str(work / "provider-home")})
artifact_base = work / "artifacts-base"
shutil.copytree(provider / "build/debug", artifact_base)
poisoner = work / "poison_legacy"
mutator = work / "mutate_callback"
for source, output in (
        ("tests/tooling/poison_phase25_generic_callable.cpp", poisoner),
        ("tests/tooling/mutate_phase25_r3e_callback_artifact.cpp", mutator)):
    run(["g++", "-std=c++17", "-O0", "-Wall", "-Wextra", "-Werror",
         "-Wno-missing-field-initializers", "-pedantic", "-Isrc", source,
         "-o", str(output)], cwd=ROOT, env=base_env)
run([str(poisoner), str(artifact_base / "Service.mossi"),
     str(artifact_base / "libService.rlib"), "Service", "fresh"],
    cwd=ROOT, env=base_env)
shutil.rmtree(provider / "src")
assert not (provider / "src").exists()

for mode in ("missing-effect-dependency", "wrong-callback-identity",
             "callback-argument", "callable-argument-type",
             "callable-result-type", "return-result", "effect-weakening"):
    artifacts = work / f"artifacts-{mode}"
    shutil.copytree(artifact_base, artifacts)
    run([str(mutator), str(artifacts / "Service.mossi"),
         str(artifacts / "libService.rlib"), mode, "Service"],
        cwd=ROOT, env=base_env)
    env = base_env | {
        "MOSS_MODULE_PATH": str(artifacts),
        "MARGO_HOME": str(work / f"consumer-{mode}"),
        "MOSS_PHASE25_CHECKED_BODY_TRACE": "1",
    }
    result = subprocess.run([str(MOSS), "check", str(main), "--json"],
                            cwd=consumer, env=env, capture_output=True,
                            text=True, timeout=300)
    diagnostic = result.stdout + result.stderr
    assert result.returncode != 0 and "SOURCE_FREE_" in diagnostic, (
        mode, diagnostic)
    assert "Service:fn:fresh|checked-body-v1" not in diagnostic, (
        mode, "forged callback body projected before rejection", diagnostic)
    print(f"PASS rehashed {mode} mutation rejected before projection")
