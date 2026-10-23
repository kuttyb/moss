#!/usr/bin/env python3
"""Exercise direct nominal constructor returns from source-free ABI-v10."""

import json
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


def run(argv, *, cwd=ROOT, env=None, ok=True):
    result = subprocess.run([str(item) for item in argv], cwd=cwd, env=env,
                            capture_output=True, text=True, timeout=300)
    if ok:
        assert result.returncode == 0, (argv, result.stdout, result.stderr)
    return result


def package(path, name, source):
    (path / "src").mkdir(parents=True)
    (path / "Moss.toml").write_text(
        f'[package]\nname = "{name}"\nversion = "0.1.0"\n\n'
        '[build]\nsource = "src"\n', encoding="utf-8")
    (path / "src" / "main.moss").write_text(source, encoding="utf-8")


def env_for(scratch, **extra):
    env = os.environ.copy()
    for key in list(env):
        if key == "MOSS_STAGED_ABI" or key.startswith("MOSS_PHASE25_"):
            env.pop(key, None)
    env.update(TMPDIR=str(TMP), MOSS=str(MOSS), MOSS_STAGED_ABI="v10",
               MARGO_HOME=str(scratch / "margo-home"))
    env.update(extra)
    return env


with tempfile.TemporaryDirectory(prefix="phase25-r4-direct-nominal-",
                                 dir=TMP) as temporary:
    scratch = Path(temporary)
    mutator = scratch / "mutate_nominal"
    poisoner = scratch / "poison_legacy"
    for source, target in (
            ("tests/tooling/mutate_phase25_r2_nominal_artifact.cpp", mutator),
            ("tests/tooling/check_phase25_h3_poison_legacy.cpp", poisoner)):
        run(["g++", "-std=c++17", "-O0", "-Wall", "-Wextra", "-Werror",
             "-Wno-missing-field-initializers", "-pedantic", "-Isrc",
             source, "-o", target])

    provider = scratch / "provider"
    package(provider, "r4_direct_nominal_provider", """module Service
export type Box:
  value

export fn fresh(value):
  return Box(value: value)

export fn unbox(item: Box):
  return item.value
""")
    run([MARGO, "build"], cwd=provider,
        env=env_for(scratch / "provider-home"))
    artifacts = scratch / "provider-artifacts"
    shutil.copytree(provider / "build" / "debug", artifacts)
    interface = artifacts / "Service.mossi"
    archive = artifacts / "libService.rlib"
    run([poisoner, interface, archive, "Service", "2", "nominal"])
    assert b"generic_stmt " not in interface.read_bytes()
    shutil.rmtree(provider)
    assert not list(scratch.rglob("provider/src/*.moss"))

    consumer_source = """module Main
import Service
fn main():
  item = Service.fresh(EXPRESSION)
  echo Service.unbox(item)
"""
    trace_markers = tuple(
        "MOSS_CHECKED_BODY_PROJECTED|"
        "callable-v1:module-v1:r4_direct_nominal_provider@0.1.0::Service:fn:" +
        name + "|checked-body-v1" for name in ("fresh", "unbox"))
    for label, expression, expected in (
            ("int", "99", "99\n"),
            ("string", '"direct"', "direct\n")):
        consumer = scratch / f"consumer-{label}"
        package(consumer, f"r4_direct_nominal_{label}",
                consumer_source.replace("EXPRESSION", expression))
        env = env_for(scratch / f"consumer-home-{label}",
                      MOSS_MODULE_PATH=str(artifacts))
        run([MARGO, "build"], cwd=consumer, env=env)
        traced = run([MOSS, "check", consumer / "src" / "main.moss", "--json"],
                     cwd=consumer,
                     env=env | {"MOSS_PHASE25_CHECKED_BODY_TRACE": "1"})
        assert all(marker in traced.stdout + traced.stderr
                   for marker in trace_markers), (label, traced.stdout,
                                                   traced.stderr)
        executed = run([MARGO, "run"], cwd=consumer, env=env)
        assert executed.stdout == expected, (label, executed.stdout,
                                              executed.stderr)
        print(f"PASS direct-return source-free Box[{label}] checked projection",
              flush=True)

    consumer = scratch / "consumer-hostile"
    package(consumer, "r4_direct_nominal_hostile", consumer_source.replace(
        "EXPRESSION", "99"))
    mutations = ("nominal-identity", "field-ordinal", "field-type",
                 "template-field-type", "visibility", "missing-dependency",
                 "dependency-hash")
    for mutation in mutations:
        hostile = scratch / f"hostile-{mutation}"
        shutil.copytree(artifacts, hostile)
        run([mutator, hostile / "Service.mossi",
             hostile / "libService.rlib", mutation, "Service"])
        env = env_for(scratch / f"hostile-home-{mutation}",
                      MOSS_MODULE_PATH=str(hostile),
                      MOSS_PHASE25_CHECKED_BODY_TRACE="1")
        rejected = run([MOSS, "check", consumer / "src" / "main.moss",
                        "--json"], cwd=consumer, env=env, ok=False)
        error = json.loads(rejected.stdout)["error"]
        assert error["code"].startswith("SOURCE_FREE_"), (mutation, error)
        assert "|fn:fresh|checked-body-v1" not in (
            rejected.stdout + rejected.stderr), (mutation, rejected.stdout,
                                                  rejected.stderr)
        print(f"PASS direct-return hostile {mutation}: {error['code']}",
              flush=True)

print("PASS R4 direct nominal source-free and hostile gate")
