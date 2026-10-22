#!/usr/bin/env python3
"""Prove provider-owned generic nominal identity across independent consumers."""

import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile


ROOT = Path(__file__).resolve().parents[2]
MOSS = Path(sys.argv[1] if len(sys.argv) > 1 else
            os.environ.get("MOSS", ROOT / "moss")).resolve()
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


def fresh_env(base, **extra):
    env = os.environ.copy()
    for key in list(env):
        if key == "MOSS_STAGED_ABI" or key.startswith("MOSS_PHASE25_"):
            env.pop(key, None)
    env.update(TMPDIR=str(TMP), MOSS=str(MOSS),
               MARGO_HOME=str(base / "margo-home"))
    env.update(extra)
    return env


with tempfile.TemporaryDirectory(prefix="phase25-r2-wp3-", dir=TMP) as td:
    scratch = Path(td)
    mutator = scratch / "mutate_nominal"
    poisoner = scratch / "poison_legacy"
    for source, target in (
            ("tests/tooling/mutate_phase25_r2_nominal_artifact.cpp", mutator),
            ("tests/tooling/check_phase25_h3_poison_legacy.cpp", poisoner)):
        run(["g++", "-std=c++17", "-O0", "-Wall", "-Wextra", "-Werror",
             "-Wno-missing-field-initializers", "-pedantic", "-Isrc",
             source, "-o", target])

    provider = scratch / "provider"
    package(provider, "wp3_nominal_provider", """module Service
export type Box:
  value

export fn fresh(value):
  result = Box(value: value)
  return result

export fn unbox(item: Box):
  return item.value
""")
    provider_env = fresh_env(scratch / "provider-home", MOSS_STAGED_ABI="v10")
    run([MARGO, "build"], cwd=provider, env=provider_env)
    artifacts = scratch / "provider-artifacts"
    shutil.copytree(provider / "build" / "debug", artifacts)
    interface = artifacts / "Service.mossi"
    archive = artifacts / "libService.rlib"
    run([poisoner, interface, archive, "Service", "3", "nominal"])
    assert b"generic_stmt " not in interface.read_bytes()
    shutil.rmtree(provider)
    assert not list(scratch.rglob("provider/src/*.moss"))

    consumers = (
        ("int", "99", "99\n"),
        ("string", '"hello"', "hello\n"),
    )
    consumer_source = """module Main
import Service
fn main():
  item = Service.fresh(EXPRESSION)
  echo Service.unbox(item)
"""
    trace_markers = tuple(
        "MOSS_CHECKED_BODY_PROJECTED|"
        "callable-v1:module-v1:wp3_nominal_provider@0.1.0::Service:fn:" + name +
        "|checked-body-v1" for name in ("fresh", "unbox"))
    instance_keys = set()
    expected_nominal_id = (
        "nominal-template-v1:module-v1:wp3_nominal_provider@0.1.0"
        "::Service:type:Box")
    for profile, expression, expected in consumers:
        consumer = scratch / f"consumer-{profile}"
        package(consumer, f"wp3_nominal_{profile}",
                consumer_source.replace("EXPRESSION", expression))
        env = fresh_env(scratch / f"consumer-home-{profile}",
                        MOSS_MODULE_PATH=str(artifacts),
                        MOSS_STAGED_ABI="v10")
        run([MARGO, "build"], cwd=consumer, env=env)
        traced = run([MOSS, "check", consumer / "src" / "main.moss", "--json"],
                     cwd=consumer,
                     env=env | {"MOSS_PHASE25_CHECKED_BODY_TRACE": "1"})
        assert all(marker in traced.stdout + traced.stderr
                   for marker in trace_markers), (
            profile, traced.stdout, traced.stderr)
        typed = run([MOSS, "type", "item", "--source",
                     consumer / "src" / "main.moss", "--json", "--kind",
                     "binding", "--enclosing", "main"], cwd=consumer, env=env)
        facts = json.loads(typed.stdout)["result"]["type_facts"]["nominal"]
        assert facts["declaration_id"] == expected_nominal_id, facts
        assert facts["module_identity"] == (
            "module-v1:wp3_nominal_provider@0.1.0::Service"), facts
        instance_keys.add(facts["instance_key"])
        executed = run([MARGO, "run"], cwd=consumer, env=env)
        assert executed.stdout == expected, (profile, executed.stdout,
                                              executed.stderr)
        print(f"PASS independent source-free Box[{profile}] consumer",
              flush=True)
    assert len(instance_keys) == 2, instance_keys

    consumer = scratch / "hostile-consumer"
    package(consumer, "wp3_nominal_hostile", consumer_source.replace(
        "EXPRESSION", "99"))
    mutations = ("nominal-identity", "field-ordinal", "field-type",
                 "template-field-type", "visibility", "missing-dependency",
                 "dependency-hash")
    for mutation in mutations:
        hostile = scratch / f"hostile-{mutation}"
        shutil.copytree(artifacts, hostile)
        run([mutator, hostile / "Service.mossi",
             hostile / "libService.rlib", mutation, "Service"])
        env = fresh_env(scratch / f"hostile-home-{mutation}",
                        MOSS_MODULE_PATH=str(hostile),
                        MOSS_STAGED_ABI="v10",
                        MOSS_PHASE25_CHECKED_BODY_TRACE="1")
        rejected = run([MOSS, "check", consumer / "src" / "main.moss", "--json"],
                       cwd=consumer, env=env, ok=False)
        error = json.loads(rejected.stdout)["error"]
        assert rejected.returncode != 0 and error["code"].startswith(
            "SOURCE_FREE_"), (mutation, error, rejected.stderr)
        target = "unbox" if mutation.startswith("field-") else "fresh"
        target_marker = "|fn:" + target + "|checked-body-v1"
        assert target_marker not in (rejected.stdout + rejected.stderr), (
            mutation, target_marker, rejected.stdout, rejected.stderr)
        print(f"PASS hostile nominal {mutation}: {error['code']}", flush=True)

print("PASS R2 WP3 independent nominal consumers", flush=True)
