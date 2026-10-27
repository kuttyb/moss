#!/usr/bin/env python3
"""Source-free ABI-v10 proof for a three-hop generic call-result chain."""

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
                            capture_output=True, text=True, timeout=240)
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
    env.update(TMPDIR=str(TMP), MOSS=str(MOSS), MARGO_HOME=str(base / "margo-home"))
    env.update(extra)
    return env


def trace_all(callables, text):
    for name in callables:
        marker = ("MOSS_CHECKED_BODY_PROJECTED|"
                  f"callable-v1:module-v1:r2_chain_provider@0.1.0::Service:fn:{name}"
                  "|checked-body-v1")
        assert marker in text, (marker, text)


with tempfile.TemporaryDirectory(prefix="phase25-r2-wp1-", dir=TMP) as td:
    scratch = Path(td)
    mutator = scratch / "mutate_call_chain"
    poisoner = scratch / "poison_legacy"
    run(["g++", "-std=c++17", "-O0", "-Wall", "-Wextra", "-Werror",
         "-Wno-missing-field-initializers", "-pedantic", "-Isrc",
         "tests/tooling/mutate_phase25_call_chain_artifact.cpp",
         "src/checked_body_ir.cpp", "-o", mutator])
    run(["g++", "-std=c++17", "-O0", "-Wall", "-Wextra", "-Werror",
         "-Wno-missing-field-initializers", "-pedantic", "-Isrc",
         "tests/tooling/check_phase25_h3_poison_legacy.cpp",
         "src/checked_body_ir.cpp", "-o", poisoner])

    provider = scratch / "provider"
    package(provider, "r2_chain_provider", """module Service
fn fresh():
  return []
fn middle():
  return fresh()
export fn relay():
  return middle()
""")

    artifacts_by_profile = {}
    for profile in ("debug", "release"):
        env = fresh_env(scratch / f"provider-{profile}", MOSS_STAGED_ABI="v10")
        flags = ["--release"] if profile == "release" else []
        run([MARGO, "build", *flags], cwd=provider, env=env)
        artifacts = scratch / f"artifacts-v10-{profile}"
        shutil.copytree(provider, artifacts)
        artifact_dir = artifacts / "build" / profile
        interface = artifact_dir / "Service.mossi"
        archive = artifact_dir / "libService.rlib"
        before = interface.read_text(errors="replace")
        assert "generic_stmt" in before, "provider has no legacy generic body to poison"
        run([poisoner, interface, archive, "Service", "3", "call-chain"])
        after = interface.read_text(errors="replace")
        assert "generic_stmt" not in after, after
        artifacts_by_profile[profile] = artifacts

    consumer_source = """module Main
import Service
fn main():
  numbers = Service.relay()
  numbers.push(17)
  echo numbers
  flags = Service.relay()
  flags.push(true)
  echo flags
"""
    expected = "[17]\n[true]\n"
    callables = ("fresh", "middle", "relay")
    for profile, artifacts in artifacts_by_profile.items():
        flags = ["--release"] if profile == "release" else []
        env = fresh_env(scratch / f"consumer-v10-{profile}",
                        MOSS_MODULE_PATH=str(artifacts / "build" / profile),
                        MOSS_STAGED_ABI="v10")
        consumer = scratch / f"consumer-v10-{profile}"
        package(consumer, f"r2_chain_consumer_{profile}", consumer_source)
        run([MARGO, "build", *flags], cwd=consumer, env=env)
        traced_env = env | {"MOSS_PHASE25_CHECKED_BODY_TRACE": "1"}
        traced = run([MOSS, "check", consumer / "src" / "main.moss", "--json"],
                     cwd=consumer, env=traced_env)
        trace_all(callables, traced.stdout + traced.stderr)
        executed = run([MARGO, "run", *flags], cwd=consumer, env=env)
        assert executed.stdout == expected, (profile, executed.stdout, executed.stderr)

    # ABI-v9 source-backed compatibility control, in both native profiles.
    for profile in ("debug", "release"):
        flags = ["--release"] if profile == "release" else []
        v9_provider = scratch / f"v9-provider-{profile}"
        package(v9_provider, f"r2_chain_v9_provider_{profile}", """module Service
fn fresh():
  return []
fn middle():
  return fresh()
export fn relay():
  return middle()
""")
        env = fresh_env(scratch / f"v9-provider-home-{profile}")
        run([MARGO, "build", *flags], cwd=v9_provider, env=env)
        artifacts = scratch / f"artifacts-v9-{profile}"
        shutil.copytree(v9_provider, artifacts)
        consumer = scratch / f"v9-consumer-{profile}"
        package(consumer, f"r2_chain_v9_consumer_{profile}", consumer_source)
        consumer_env = fresh_env(scratch / f"v9-consumer-home-{profile}",
                                 MOSS_MODULE_PATH=str(artifacts / "build" / profile))
        run([MARGO, "build", *flags], cwd=consumer, env=consumer_env)
        executed = run([MARGO, "run", *flags], cwd=consumer, env=consumer_env)
        assert executed.stdout == expected, ("v9", profile, executed.stdout,
                                               executed.stderr)

    mutations = (
        "forged-intermediate-identity", "wrong-result-type",
        "missing-intermediate-summary", "missing-deep-summary",
        "missing-intermediate-body", "missing-deep-body",
        "missing-dependency", "dependency-hash", "duplicate-summary",
        "duplicate-body", "duplicate-dependency", "missing-dependency-hash",
    )
    base = artifacts_by_profile["debug"]
    for mutation in mutations:
        artifacts = scratch / f"hostile-{mutation}"
        shutil.copytree(base, artifacts)
        artifact_dir = artifacts / "build/debug"
        outcome = run([mutator, artifact_dir / "Service.mossi",
                       artifact_dir / "libService.rlib", mutation, "Service"])
        if mutation in ("duplicate-body", "duplicate-dependency",
                        "missing-dependency-hash"):
            assert f"rejected {mutation}" in outcome.stdout, outcome.stdout
            print(f"PASS canonical ABI writer rejected {mutation}", flush=True)
            continue
        assert f"mutated {mutation}" in outcome.stdout, outcome.stdout
        consumer = scratch / f"consumer-{mutation}"
        package(consumer, f"r2_chain_bad_{mutation}", consumer_source)
        env = fresh_env(scratch / f"home-{mutation}",
                        MOSS_MODULE_PATH=str(artifact_dir),
                        MOSS_STAGED_ABI="v10",
                        MOSS_PHASE25_CHECKED_BODY_TRACE="1")
        rejected = run([MOSS, "build", "--json"], cwd=consumer, env=env, ok=False)
        diagnostic = json.loads(rejected.stdout)["error"]
        assert rejected.returncode != 0 and diagnostic["code"].startswith("SOURCE_FREE_"), (
            mutation, diagnostic, rejected.stderr)
        rejected_trace = rejected.stdout + rejected.stderr
        target = "fresh" if "deep" in mutation else "middle"
        target_marker = f"::Service:fn:{target}|checked-body-v1"
        assert target_marker not in rejected_trace, (
            mutation, target, rejected.stdout, rejected.stderr)
        print(f"PASS hostile call-chain {mutation}: {diagnostic['code']}", flush=True)

print("R2 WP1 three-hop generic result chain, source-backed Debug/Release interfaces, ABI-v9 controls, poisoned legacy bodies, and hostile closure: PASS")
