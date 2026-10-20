#!/usr/bin/env python3
"""Verify staged ABI-v10 admission is scoped to the selected callable."""

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


def run(command, *, cwd=ROOT, env=None, expected=0):
    result = subprocess.run(command, cwd=cwd, env=env, capture_output=True,
                            text=True, timeout=300)
    combined = result.stdout + result.stderr
    assert result.returncode == expected, (
        command, result.returncode, combined)
    return result.stdout, result.stderr


def package(path, name, source):
    (path / "src").mkdir(parents=True)
    (path / "Moss.toml").write_text(
        f'[package]\nname = "{name}"\nversion = "0.1.0"\n\n'
        '[build]\nsource = "src"\n')
    (path / "src/main.moss").write_text(source)


def consumer(path, name, body):
    package(path, name, "module Main\nimport Service\n\nfn main():\n" + body)


provider_source = """module Service

export fn supported(value):
  return value

export fn unsupported(value):
  var doubled = value + value
  return doubled
"""

with tempfile.TemporaryDirectory(prefix="phase25-mixed-provider-",
                                 dir=TMP) as temporary:
    scratch = Path(temporary)
    mutator = scratch / "mutate_mixed_admission"
    run(["g++", "-std=c++17", "-O0", "-Wall", "-Wextra", "-Werror",
         "-Wno-missing-field-initializers", "-pedantic", "-Isrc",
         "tests/tooling/mutate_phase25_mixed_admission_artifact.cpp",
         "-o", str(mutator)])

    staged = os.environ.copy()
    staged.update(MOSS=str(MOSS), MOSS_STAGED_ABI="v10",
                  MARGO_HOME=str(scratch / "margo-v10-provider"),
                  TMPDIR=str(TMP))
    provider = scratch / "provider-v10"
    package(provider, "mixed_provider", provider_source)
    run([str(MARGO), "build"], cwd=provider, env=staged)
    artifacts = scratch / "artifacts-v10"
    shutil.copytree(provider / "build/debug", artifacts)
    interface = artifacts / "Service.mossi"
    archive = artifacts / "libService.rlib"
    encoded = interface.read_text()
    assert "export fn supported " in encoded and "export fn unsupported " in encoded
    assert encoded.count("generic_stmt") >= 3, "legacy compatibility rows were not retained"
    shutil.rmtree(provider / "src")
    assert not (provider / "src").exists()

    supported_consumer = scratch / "consumer-supported"
    consumer(supported_consumer, "mixed_supported_consumer",
             "  echo Service.supported(17)\n")
    supported_env = staged | {
        "MOSS_MODULE_PATH": str(artifacts),
        "MARGO_HOME": str(scratch / "margo-v10-supported")}
    run([str(MARGO), "build"], cwd=supported_consumer, env=supported_env)
    output, _ = run([str(MARGO), "run"], cwd=supported_consumer,
                    env=supported_env)
    assert output == "17\n", output
    trace_env = supported_env | {"MOSS_PHASE25_CHECKED_BODY_TRACE": "1"}
    _, trace = run([str(MOSS), "check", "src/main.moss", "--json"],
                   cwd=supported_consumer, env=trace_env)
    assert "MOSS_CHECKED_BODY_PROJECTED|" in trace and \
        "::Service:fn:supported|checked-body-v1" in trace, trace
    print("PASS mixed provider imports and executes supported(17) by checked-body projection")

    unsupported_consumer = scratch / "consumer-unsupported"
    consumer(unsupported_consumer, "mixed_unsupported_consumer",
             "  echo Service.unsupported(17)\n")
    unsupported_env = staged | {
        "MOSS_MODULE_PATH": str(artifacts),
        "MARGO_HOME": str(scratch / "margo-v10-unsupported")}
    rejected = subprocess.run([str(MARGO), "build"], cwd=unsupported_consumer,
                              env=unsupported_env, capture_output=True,
                              text=True, timeout=300)
    diagnostic = rejected.stdout + rejected.stderr
    assert rejected.returncode != 0 and \
        "SOURCE_FREE_CHECKED_BODY_UNAVAILABLE" in diagnostic, diagnostic
    assert "unsupported" in diagnostic and "main.moss" in diagnostic, diagnostic
    print("PASS unsupported(17) rejects with unavailable checked body and provider origin")

    missing_body = scratch / "artifacts-missing-supported-body"
    shutil.copytree(artifacts, missing_body)
    run([str(mutator), str(missing_body / "Service.mossi"),
         str(missing_body / "libService.rlib"), "missing-supported-body"])
    missing_env = supported_env | {
        "MOSS_MODULE_PATH": str(missing_body),
        "MARGO_HOME": str(scratch / "margo-v10-missing-body")}
    rejected = subprocess.run([str(MARGO), "build"], cwd=supported_consumer,
                              env=missing_env, capture_output=True,
                              text=True, timeout=300)
    diagnostic = rejected.stdout + rejected.stderr
    assert rejected.returncode != 0 and \
        "SOURCE_FREE_CHECKED_BODY_INVALID" in diagnostic, diagnostic
    assert "generic_stmt" in missing_body.joinpath("Service.mossi").read_text()
    print("PASS missing supported body rejects although its legacy generic_stmt remains")

    forged = scratch / "artifacts-forged-unsupported-provenance"
    shutil.copytree(artifacts, forged)
    run([str(mutator), str(forged / "Service.mossi"),
         str(forged / "libService.rlib"), "forged-unsupported-provenance"])
    forged_supported_env = supported_env | {
        "MOSS_MODULE_PATH": str(forged),
        "MARGO_HOME": str(scratch / "margo-v10-forged-supported")}
    run([str(MARGO), "build"], cwd=supported_consumer,
        env=forged_supported_env)
    output, _ = run([str(MARGO), "run"], cwd=supported_consumer,
                    env=forged_supported_env)
    assert output == "17\n", output
    print("PASS forged provenance on unsupported export does not block supported(17)")
    forged_env = unsupported_env | {
        "MOSS_MODULE_PATH": str(forged),
        "MARGO_HOME": str(scratch / "margo-v10-forged-provenance")}
    rejected = subprocess.run([str(MARGO), "build"], cwd=unsupported_consumer,
                              env=forged_env, capture_output=True,
                              text=True, timeout=300)
    diagnostic = rejected.stdout + rejected.stderr
    assert rejected.returncode != 0 and "SOURCE_FREE_SUMMARY_INVALID" in diagnostic, diagnostic
    print("PASS forged unsupported callable provenance rejects on use")

    v9 = os.environ.copy()
    v9.pop("MOSS_STAGED_ABI", None)
    v9.pop("MOSS_PHASE25_EMPTY_VECTOR_EMIT", None)
    v9.update(MOSS=str(MOSS), MARGO_HOME=str(scratch / "margo-v9-provider"),
              TMPDIR=str(TMP))
    provider_v9 = scratch / "provider-v9"
    package(provider_v9, "mixed_provider_v9", provider_source)
    run([str(MARGO), "build"], cwd=provider_v9, env=v9)
    artifacts_v9 = scratch / "artifacts-v9"
    shutil.copytree(provider_v9 / "build/debug", artifacts_v9)
    shutil.rmtree(provider_v9 / "src")
    v9_consumer = scratch / "consumer-v9"
    consumer(v9_consumer, "mixed_v9_consumer",
             "  echo Service.supported(17)\n"
             "  echo Service.unsupported(17)\n")
    v9_env = v9 | {"MOSS_MODULE_PATH": str(artifacts_v9),
                   "MARGO_HOME": str(scratch / "margo-v9-consumer")}
    run([str(MARGO), "build"], cwd=v9_consumer, env=v9_env)
    output, _ = run([str(MARGO), "run"], cwd=v9_consumer, env=v9_env)
    assert output == "17\n34\n", output
    print("PASS ABI-v9 source-free mixed provider retains both generic bodies")

print("Mixed-provider per-callable ABI-v10 admission: PASS")
