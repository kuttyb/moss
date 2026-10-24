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


from compiler_test_support import compiler_support_sources

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
    common_lowering = scratch / "check_common_lowering"
    command(["g++", "-std=c++17", "-O0", "-Wall", "-Wextra", "-Werror",
             "-Wno-missing-field-initializers", "-pedantic", "-Isrc",
             "tests/tooling/check_phase25_h3_chain_records.cpp",
             "src/checked_body_ir.cpp", "-o", str(verifier)])
    command(["g++", "-std=c++17", "-O0", "-Wall", "-Wextra", "-Werror",
             "-Wno-missing-field-initializers", "-pedantic", "-Isrc",
             "tests/tooling/check_phase25_h3_poison_legacy.cpp", "-o",
             str(poison)])
    command(["g++", "-std=c++17", "-O0", "-Wall", "-Wextra", "-Werror",
             "-Wno-missing-field-initializers", "-pedantic", "-Isrc",
             "tests/tooling/check_phase25_h3_common_lowering.cpp",
             *compiler_support_sources(), "-o", str(common_lowering)])

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

fn private_identity(x):
  return x

export fn fresh(x):
  result = nested_private_box(x)
  return result

export fn identity_chain(x):
  return private_identity(x)

export fn relay(b: Box):
  extracted = b.value
  result = extracted
  return result
"""
    package(provider, "h3_provider", provider_source)
    command([str(common_lowering), str(provider / "src" / "main.moss")])
    profile_flag = ["--release"] if args.profile == "release" else []
    command([str(MARGO), "build", *profile_flag], cwd=provider, env=env)
    artifacts = scratch / "artifacts"
    shutil.copytree(provider / "build" / args.profile, artifacts)
    interface = artifacts / "Service.mossi"
    archive = artifacts / "libService.rlib"
    assert interface.is_file() and archive.is_file()
    command([str(verifier), str(interface)])
    interface_text = interface.read_text(errors="replace")
    legacy_statements = [line for line in interface_text.splitlines()
                         if "generic_stmt " in line]
    assert len(legacy_statements) == 11, legacy_statements
    command([str(poison), str(interface), str(archive), "Service", "8"])
    poisoned_text = interface.read_text(errors="replace")
    remaining_legacy = [line for line in poisoned_text.splitlines()
                        if "generic_stmt " in line]
    assert len(remaining_legacy) == 3, remaining_legacy
    command([str(verifier), str(interface), "--poisoned"])
    shutil.rmtree(provider)
    assert not list(artifacts.rglob("*.moss"))

    env["MOSS_MODULE_PATH"] = str(artifacts)
    for name, argument, expected in (
            ("int", "99", "99\n99\n"),
            ("string", '"hello"', "hello\nhello\n")):
        consumer = scratch / name
        package(consumer, f"h3_{name}",
                "module Main\nimport Service\n\nfn main():\n"
                f"  item = Service.fresh({argument})\n"
                "  echo Service.relay(item)\n"
                f"  echo Service.identity_chain({argument})\n")
        built = subprocess.run([str(MARGO), "build", *profile_flag],
                               cwd=consumer, env=env,
                               capture_output=True, text=True)
        assert built.returncode == 0, (built.stdout, built.stderr)
        trace_env = env | {"MOSS_PHASE25_CHECKED_BODY_TRACE": "1"}
        traced = subprocess.run([str(MOSS), "check",
                                 str(consumer / "src" / "main.moss"), "--json"],
                                cwd=consumer, env=trace_env,
                                capture_output=True, text=True)
        trace = traced.stdout + traced.stderr
        assert traced.returncode == 0 and (
            "MOSS_CHECKED_BODY_PROJECTED|callable-v1:" in trace), (
                traced.stdout, traced.stderr)
        assert command([str(MARGO), "run", *profile_flag], cwd=consumer, env=env) == expected

    for missing in ("private_box", "private_identity"):
        forged = scratch / f"forged-{missing}"
        shutil.copytree(artifacts, forged)
        command([str(poison), str(forged / "Service.mossi"),
                 str(forged / "libService.rlib"), "Service", "0",
                 f"drop-{missing}-body"])
        bad_env = env.copy()
        bad_env["MOSS_MODULE_PATH"] = str(forged)
        negative = scratch / f"negative-{missing}"
        package(negative, f"h3_missing_{missing}",
                "module Main\nimport Service\n\nfn main():\n"
                f"  item = Service.identity_chain(1)\n"
                "  echo Service.relay(item)\n")
        rejected = subprocess.run([str(MOSS), "build", "--json"], cwd=negative,
                                  env=bad_env, capture_output=True, text=True)
        assert rejected.returncode != 0, (rejected.stdout, rejected.stderr)
        diagnostic = json.loads(rejected.stdout)["error"]
        assert diagnostic["code"] == "SOURCE_FREE_PRIVATE_DEPENDENCY_MISSING", (
            diagnostic, rejected.stderr)

print("H3 common checked-body chain, multi-statement field read, two source-free consumers, poisoned chain fallback, and negative closures: PASS")
