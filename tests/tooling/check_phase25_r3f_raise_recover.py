#!/usr/bin/env python3
"""ABI-v9 source-free recovery and private typed-error identity regression."""

import hashlib
import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
TMP = ROOT / "tmp"
TMP.mkdir(exist_ok=True)
MOSS = Path(os.environ.get("MOSS", ROOT / "moss")).resolve()
MARGO = ROOT / "margo"
PROVIDER = """module Service

enum Failure:
  Bad
  Detailed(code: Int)

export fn nonnegative(value):
  try:
    if value < 0:
      raise Failure.Bad
    return value
  recover Failure.Bad:
    return 0

export fn payload(code: Int) -> Int:
  try:
    raise Failure.Detailed(code: code)
  recover Failure.Detailed(code):
    return code
"""
CONSUMER = """module Main
import Service

fn main():
  echo Service.nonnegative(7)
  echo Service.nonnegative(-2)
  echo Service.payload(23)
"""
EXPECTED = "7\n0\n23\n"


def run(argv, *, cwd, env, expected=0):
    result = subprocess.run(argv, cwd=cwd, env=env, capture_output=True,
                           text=True, timeout=300)
    output = result.stdout + result.stderr
    assert result.returncode == expected, (argv, result.returncode, output)
    return output


def write_package(path, name, source):
    (path / "src").mkdir(parents=True)
    (path / "Moss.toml").write_text(
        f'[package]\nname = "{name}"\nversion = "0.1.0"\n\n'
        '[build]\nsource = "src"\n')
    (path / "src/service.moss").write_text(source)


with tempfile.TemporaryDirectory(prefix="phase25-r3f-raise-recover-", dir=TMP) as name:
    scratch = Path(name)
    base = os.environ.copy()
    base.update(MOSS=str(MOSS), TMPDIR=str(TMP))
    base.pop("MOSS_STAGED_ABI", None)
    provider = scratch / "provider"
    write_package(provider, "r3f_provider", PROVIDER)
    artifacts_by_profile = {}
    for profile, args in (("debug", []), ("release", ["--release"])):
        env = base | {"MARGO_HOME": str(scratch / f"provider-{profile}")}
        run([str(MARGO), *args, "build"], cwd=provider, env=env)
        artifacts = scratch / f"artifacts-{profile}"
        shutil.copytree(provider / "build" / profile, artifacts)
        interface = artifacts / "Service.mossi"
        text = interface.read_text()
        assert 'generic_error_enum "Failure"' in text, text
        assert 'generic_error_case "Bad" 0' in text, text
        assert 'generic_error_case "Detailed" 1 "code" "int"' in text, text
        assert "export enum Failure" not in text, text
        artifacts_by_profile[profile] = artifacts
    shutil.rmtree(provider / "src")

    consumer = scratch / "consumer"
    write_package(consumer, "r3f_consumer", CONSUMER)
    for profile, args in (("debug", []), ("release", ["--release"])):
        artifacts = artifacts_by_profile[profile]
        env = base | {
            "MOSS_MODULE_PATH": str(artifacts),
            "MARGO_HOME": str(scratch / f"consumer-{profile}"),
        }
        run([str(MARGO), *args, "build"], cwd=consumer, env=env)
        assert run([str(MARGO), *args, "run"], cwd=consumer, env=env) == EXPECTED

    hostile = scratch / "hostile-artifacts"
    shutil.copytree(artifacts_by_profile["debug"], hostile)
    interface = hostile / "Service.mossi"
    original = interface.read_text()
    forged, count = re.subn(
        r'(?m)^(generic_error_enum "Failure" )"[^"]+"$',
        r'\1"forged-private-enum-id"', original)
    assert count == 1 and forged != original
    material = re.sub(
        r"(?m)^provider_contract_digest sha256:[0-9a-f]{64}\n", "", forged)
    material = re.sub(
        r"(?m)^package_source_hash sha256:[0-9a-f]{64}\n", "", material)
    forged, count = re.subn(
        r"(?m)^provider_contract_digest sha256:[0-9a-f]{64}$",
        "provider_contract_digest sha256:" + hashlib.sha256(
            material.encode()).hexdigest(), forged)
    assert count == 1
    interface.write_text(forged)
    env = base | {
        "MOSS_MODULE_PATH": str(hostile),
        "MARGO_HOME": str(scratch / "consumer-hostile"),
        "MOSS_PHASE25_CHECKED_BODY_TRACE": "1",
    }
    result = subprocess.run(
        [str(MOSS), "check", str(consumer / "src/main.moss"), "--json"],
        cwd=consumer, env=env, capture_output=True, text=True, timeout=300)
    diagnostic = result.stdout + result.stderr
    assert result.returncode != 0 and "SOURCE_FREE_ARTIFACT_CONTRACT_MISMATCH" in diagnostic, diagnostic
    assert "Service:fn:nonnegative|checked-body-v1" not in diagnostic, diagnostic

print("R3F ABI-v9 private typed-error Debug/Release and rehashed hostile case passed")
