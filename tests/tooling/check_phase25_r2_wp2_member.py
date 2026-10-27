#!/usr/bin/env python3
"""Source-free checked member requirement proof for ABI-v10."""

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
    env.update(TMPDIR=str(TMP), MOSS=str(MOSS),
               MARGO_HOME=str(base / "margo-home"))
    env.update(extra)
    return env


with tempfile.TemporaryDirectory(prefix="phase25-r2-wp2-", dir=TMP) as td:
    scratch = Path(td)
    mutator = scratch / "mutate_member"
    poisoner = scratch / "poison_legacy"
    for source, target in (
            ("tests/tooling/mutate_phase25_member_artifact.cpp", mutator),
            ("tests/tooling/check_phase25_h3_poison_legacy.cpp", poisoner)):
        run(["g++", "-std=c++17", "-O0", "-Wall", "-Wextra", "-Werror",
             "-Wno-missing-field-initializers", "-pedantic", "-Isrc",
             source, "src/checked_body_ir.cpp", "-o", target])

    provider = scratch / "provider"
    package(provider, "wp2_member_provider", """module Service
export fn size(value):
  return value.length()
""")

    artifacts_by_profile = {}
    for profile in ("debug", "release"):
        env = fresh_env(scratch / f"provider-{profile}",
                        MOSS_STAGED_ABI="v10")
        flags = ["--release"] if profile == "release" else []
        run([MARGO, "build", *flags], cwd=provider, env=env)
        artifacts = scratch / f"artifacts-{profile}"
        shutil.copytree(provider, artifacts)
        artifact_dir = artifacts / "build" / profile
        run([poisoner, artifact_dir / "Service.mossi",
             artifact_dir / "libService.rlib", "Service", "1", "member"])
        interface = (artifact_dir / "Service.mossi").read_text(errors="replace")
        assert "generic_stmt" not in interface, interface
        assert 'generic_constraint 1 "value" "forged_member"' in interface
        artifacts_by_profile[profile] = artifacts

    consumer_source = """module Main
import Service
fn main():
  echo Service.size("abc")
"""
    for profile, artifacts in artifacts_by_profile.items():
        consumer = scratch / f"consumer-{profile}"
        package(consumer, f"wp2_member_consumer_{profile}", consumer_source)
        flags = ["--release"] if profile == "release" else []
        env = fresh_env(scratch / f"consumer-home-{profile}",
                        MOSS_MODULE_PATH=str(artifacts / "build" / profile),
                        MOSS_STAGED_ABI="v10")
        run([MARGO, "build", *flags], cwd=consumer, env=env)
        traced_env = env | {"MOSS_PHASE25_CHECKED_BODY_TRACE": "1"}
        traced = run([MOSS, "check", consumer / "src" / "main.moss", "--json"],
                     cwd=consumer, env=traced_env)
        marker = ("MOSS_CHECKED_BODY_PROJECTED|"
                  "callable-v1:module-v1:wp2_member_provider@0.1.0::Service:fn:size"
                  "|checked-body-v1")
        assert marker in traced.stdout + traced.stderr, (
            marker, traced.stdout, traced.stderr)
        executed = run([MARGO, "run", *flags], cwd=consumer, env=env)
        assert executed.stdout == "3\n", (profile, executed.stdout,
                                           executed.stderr)
        print(f"PASS imported member interface specialization {profile}", flush=True)

    mutations = (
        "member-identity", "summary-member-identity", "receiver-type",
        "result-type", "access", "effect", "duplicate-requirement",
        "ordering",
    )
    base = artifacts_by_profile["debug"]
    for mutation in mutations:
        artifacts = scratch / f"hostile-{mutation}"
        shutil.copytree(base, artifacts)
        artifact_dir = artifacts / "build/debug"
        run([mutator, artifact_dir / "Service.mossi",
             artifact_dir / "libService.rlib", mutation, "Service"])
        consumer = scratch / f"hostile-consumer-{mutation}"
        package(consumer, f"wp2_member_bad_{mutation}", consumer_source)
        env = fresh_env(scratch / f"hostile-home-{mutation}",
                        MOSS_MODULE_PATH=str(artifact_dir),
                        MOSS_STAGED_ABI="v10",
                        MOSS_PHASE25_CHECKED_BODY_TRACE="1")
        rejected = run([MOSS, "check", consumer / "src" / "main.moss", "--json"],
                       cwd=consumer, env=env, ok=False)
        error = json.loads(rejected.stdout)["error"]
        assert rejected.returncode != 0 and error["code"].startswith("SOURCE_FREE_"), (
            mutation, error, rejected.stderr)
        assert "MOSS_CHECKED_BODY_PROJECTED|" not in (
            rejected.stdout + rejected.stderr), (mutation, rejected.stdout,
                                                  rejected.stderr)
        print(f"PASS hostile member {mutation}: {error['code']}", flush=True)

print("PASS R2 WP2 checked member requirements", flush=True)
