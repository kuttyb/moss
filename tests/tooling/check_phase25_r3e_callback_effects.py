#!/usr/bin/env python3
"""Check source-free callback effect propagation and pass-only behavior."""

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


def run(argv, *, cwd, env):
    result = subprocess.run(argv, cwd=cwd, env=env, capture_output=True,
                            text=True, timeout=900)
    assert result.returncode == 0, (argv, result.returncode,
                                    result.stdout + result.stderr)
    return result.stdout + result.stderr


work = TMP / "r3e-callback-effects"
if work.exists():
    shutil.rmtree(work)
provider = work / "provider"
consumer = work / "consumer"
(provider / "src").mkdir(parents=True)
(consumer / "src").mkdir(parents=True)
manifest = ('[package]\nname = "r3e_callback_effects"\nversion = "0.1.0"\n'
            '[build]\nsource = "src"\n')
(provider / "Moss.toml").write_text(manifest)
(consumer / "Moss.toml").write_text(manifest)
(provider / "src/service.moss").write_text(case["provider"])
main = consumer / "src/main.moss"
main.write_text(case["consumer"])
artifacts = work / "artifacts"
env = os.environ.copy()
env.update(MOSS_STAGED_ABI="v10", MOSS=str(MOSS),
           MARGO_HOME=str(work / "margo-provider"), TMPDIR=str(TMP))
run([str(MARGO), "build"], cwd=provider, env=env)
shutil.copytree(provider / "build/debug", artifacts)
poisoner = work / "poison_legacy"
run(["g++", "-std=c++17", "-O0", "-Wall", "-Wextra", "-Werror",
     "-Wno-missing-field-initializers", "-pedantic", "-Isrc",
     "tests/tooling/poison_phase25_generic_callable.cpp", "-o",
     str(poisoner)], cwd=ROOT, env=env)
run([str(poisoner), str(artifacts / "Service.mossi"),
     str(artifacts / "libService.rlib"), "Service", "fresh"],
    cwd=ROOT, env=env)
shutil.rmtree(provider / "src")
env.update(MOSS_MODULE_PATH=str(artifacts), MARGO_HOME=str(work / "margo-consumer"))
run([str(MARGO), "build"], cwd=consumer, env=env)


def effects(function):
    output = run([str(MOSS), "effects", f"entity-v1:function:Main__{function}",
                  "--source", str(main), "--json"], cwd=consumer, env=env)
    result = json.loads(output)["result"]
    return result["observable_effects"]


invoked = effects("invoke_checked")
passed = effects("pass_only")
assert invoked["may_panic"] is True, invoked
assert invoked["unresolved"] is False, invoked
assert passed["may_panic"] is False, passed
assert passed["unresolved"] is False, passed
print("PASS source-free invoked callback propagates may_panic")
print("PASS source-free passing-only callback does not propagate effects")
