#!/usr/bin/env python3
"""Run two typed consumers from one checked private-chain ABI-v10 provider."""

import argparse
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile


ROOT = Path(__file__).resolve().parents[2]
parser = argparse.ArgumentParser()
parser.add_argument("moss", nargs="?", default=str(ROOT / "moss"))
parser.add_argument("--profile", choices=("debug", "release"), default="debug")
args = parser.parse_args()
MOSS = Path(args.moss).resolve()
MARGO = ROOT / "margo"
TMP = ROOT / "tmp"
TMP.mkdir(exist_ok=True)


def command(argv, *, cwd=ROOT, env=None):
    result = subprocess.run(argv, cwd=cwd, env=env,
                            capture_output=True, text=True)
    assert result.returncode == 0, (argv, result.stdout, result.stderr)
    return result.stdout


def package(path, name, source):
    (path / "src").mkdir(parents=True)
    (path / "Moss.toml").write_text(
        f'[package]\nname = "{name}"\nversion = "0.1.0"\n\n'
        '[build]\nsource = "src"\n')
    (path / "src" / "main.moss").write_text(source)


with tempfile.TemporaryDirectory(prefix="phase25-h3-chain-", dir=TMP) as td:
    scratch = Path(td)
    verifier = scratch / "verify_chain"
    poison = scratch / "poison_legacy"
    for source, output in (
            ("tests/tooling/check_phase25_h3_chain_records.cpp", verifier),
            ("tests/tooling/check_phase25_h3_poison_legacy.cpp", poison)):
        command(["g++", "-std=c++17", "-O0", "-Wall", "-Wextra", "-Werror",
                 "-Wno-missing-field-initializers",
                 "-pedantic", "-Isrc", source, "-o", str(output)])

    env = os.environ.copy()
    env.update(MOSS_STAGED_ABI="v10", MOSS=str(MOSS), TMPDIR=str(TMP))
    provider = scratch / "provider"
    provider_source = """module Service

export type Box:
  value

fn private_box(x):
  result = Box(value: x)
  return result

fn nested_private_box(x):
  result = private_box(x)
  return result

export fn fresh(x):
  result = nested_private_box(x)
  return result

export fn relay(b: Box):
  return b.value
"""
    package(provider, "h3_provider", provider_source)
    profile_flag = ["--release"] if args.profile == "release" else []
    command([str(MARGO), "build", *profile_flag], cwd=provider, env=env)
    artifacts = scratch / "artifacts"
    shutil.copytree(provider / "build" / args.profile, artifacts)
    interface = artifacts / "Service.mossi"
    archive = artifacts / "libService.rlib"
    assert interface.is_file() and archive.is_file()
    command([str(verifier), str(interface)])
    command([str(poison), str(interface), str(archive), "Service", "6"])
    assert interface.read_bytes().count(b"generic_stmt ") == 1
    command([str(verifier), str(interface), "--poisoned"])
    shutil.rmtree(provider)
    assert not list(artifacts.rglob("*.moss"))

    env["MOSS_MODULE_PATH"] = str(artifacts)
    for name, argument, expected in (
            ("int", "99", "99\n"),
            ("string", '"hello"', "hello\n")):
        consumer = scratch / name
        package(consumer, f"h3_{name}",
                "module Main\nimport Service\n\nfn main():\n"
                f"  item = Service.fresh({argument})\n"
                "  echo Service.relay(item)\n")
        command([str(MARGO), "build", *profile_flag], cwd=consumer, env=env)
        assert command([str(MARGO), "run", *profile_flag], cwd=consumer, env=env) == expected

    # A checked private helper reached through an exported body outside the
    # supported two-statement producer must not ship a generic_stmt fallback.
    unrecorded = scratch / "unrecorded"
    package(unrecorded, "h3_unrecorded", provider_source.replace(
        "  result = nested_private_box(x)\n  return result",
        "  return nested_private_box(x)"))
    rejected_unrecorded = subprocess.run(
        [str(MOSS), "build", "--json"], cwd=unrecorded, env=env,
        capture_output=True, text=True)
    assert rejected_unrecorded.returncode != 0, (
        rejected_unrecorded.stdout, rejected_unrecorded.stderr)
    unrecorded_error = json.loads(rejected_unrecorded.stdout)["error"]
    assert unrecorded_error["code"] == "SOURCE_FREE_CHECKED_BODY_UNAVAILABLE", (
        unrecorded_error, rejected_unrecorded.stderr)

    forged = scratch / "forged-artifacts"
    shutil.copytree(artifacts, forged)
    command([str(poison), str(forged / "Service.mossi"),
             str(forged / "libService.rlib"), "Service", "0",
             "drop-private-box-body"])
    bad_env = env.copy()
    bad_env["MOSS_MODULE_PATH"] = str(forged)
    negative = scratch / "negative"
    package(negative, "h3_missing_private",
            "module Main\nimport Service\n\nfn main():\n"
            "  item = Service.fresh(1)\n  echo Service.relay(item)\n")
    rejected = subprocess.run([str(MOSS), "build", "--json"], cwd=negative,
                              env=bad_env, capture_output=True, text=True)
    assert rejected.returncode != 0, (rejected.stdout, rejected.stderr)
    diagnostic = json.loads(rejected.stdout)["error"]
    assert diagnostic["code"] == "SOURCE_FREE_PRIVATE_DEPENDENCY_MISSING", (
        diagnostic, rejected.stderr)

print("H3 checked private chain, two source-free consumers, unrecorded-chain and missing-body rejection: PASS")
