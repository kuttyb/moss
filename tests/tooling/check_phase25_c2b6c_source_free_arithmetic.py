#!/usr/bin/env python3
"""Exercise checked arithmetic import from an actual source-free provider."""

import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile


ROOT = Path(__file__).resolve().parents[2]
MOSS = Path(sys.argv[1]).resolve() if len(sys.argv) > 1 else ROOT / "moss"
MARGO = ROOT / "margo"
TMP = ROOT / "tmp"
TMP.mkdir(exist_ok=True)


def run(command, *, cwd=ROOT, env=None, expected=0):
    result = subprocess.run(command, cwd=cwd, env=env,
                            capture_output=True, text=True)
    output = result.stdout + result.stderr
    assert result.returncode == expected, (
        command, result.returncode, output)
    return output


def package(path, name, source):
    (path / "src").mkdir(parents=True)
    (path / "Moss.toml").write_text(
        f'[package]\nname = "{name}"\nversion = "0.1.0"\n\n'
        '[build]\nsource = "src"\n')
    (path / "src" / "main.moss").write_text(source)


with tempfile.TemporaryDirectory(prefix="phase25-c2b6c-arithmetic-",
                                 dir=TMP) as temp:
    scratch = Path(temp)
    mutator = scratch / "mutate_arithmetic_artifact"
    verifier = scratch / "verify_emitted_arithmetic"
    run(["g++", "-std=c++17", "-O0", "-Wall", "-Wextra", "-Werror",
         "-Wno-missing-field-initializers", "-pedantic", "-Isrc",
         "tests/tooling/mutate_phase25_c2b6c_arithmetic_artifact.cpp",
         "-o", str(mutator)])
    run(["g++", "-std=c++17", "-O0", "-Wall", "-Wextra", "-Werror",
         "-Wno-missing-field-initializers", "-pedantic", "-Isrc",
         "tests/tooling/check_phase25_c2c_emitted_arithmetic.cpp",
         "-o", str(verifier)])

    staged_env = os.environ.copy()
    staged_env["MOSS_STAGED_ABI"] = "v10"
    staged_env["MOSS"] = str(MOSS)
    staged_env["MARGO_HOME"] = str(scratch / "margo-home-v10")
    staged_env["TMPDIR"] = str(TMP)

    provider = scratch / "provider-v10"
    package(provider, "c2b6c_provider", """module Service

export fn compute(x):
  return x * 2 + 20

export fn unsupported_sub(x):
  return x * 2 - 20

export fn unsupported_reverse(x):
  return 20 + x * 2
""")
    run([str(MARGO), "build"], cwd=provider, env=staged_env)
    interface = provider / "build" / "debug" / "Service.mossi"
    assert interface.is_file(), interface
    run([str(verifier), str(interface)])

    emitted = scratch / "artifacts-emitted"
    shutil.copytree(provider / "build" / "debug", emitted)

    unsupported = scratch / "artifacts-unsupported"
    shutil.copytree(provider / "build" / "debug", unsupported)
    run([str(mutator), str(unsupported / "Service.mossi"),
         str(unsupported / "libService.rlib"), "Service", "unsupported-body"])

    missing_body = scratch / "artifacts-missing-body"
    shutil.copytree(provider / "build" / "debug", missing_body)
    run([str(mutator), str(missing_body / "Service.mossi"),
         str(missing_body / "libService.rlib"), "Service",
         "drop-body-alternative-legacy"])
    missing_body_no_legacy = scratch / "artifacts-missing-body-no-legacy"
    shutil.copytree(provider / "build" / "debug", missing_body_no_legacy)
    run([str(mutator), str(missing_body_no_legacy / "Service.mossi"),
         str(missing_body_no_legacy / "libService.rlib"), "Service",
         "drop-body-no-legacy"])

    mismatched_hash = scratch / "artifacts-mismatched-body-hash"
    shutil.copytree(provider / "build" / "debug", mismatched_hash)
    run([str(mutator), str(mismatched_hash / "Service.mossi"),
         str(mismatched_hash / "libService.rlib"), "Service",
         "mismatch-body-hash"])
    shutil.rmtree(provider)
    assert not list(scratch.rglob("provider-v10/**/*.moss"))

    env = staged_env.copy()
    env["MOSS_MODULE_PATH"] = str(emitted)
    positive = scratch / "consumer-positive"
    package(positive, "c2b6c_consumer", """module Main
import Service

fn main():
  echo Service.compute(5)
  echo Service.compute(2.5)
  echo Service.unsupported_sub(5)
  echo Service.unsupported_reverse(5)
""")
    run([str(MARGO), "build"], cwd=positive, env=env)
    actual = run([str(MARGO), "run"], cwd=positive, env=env)
    assert actual == "30\n25.0\n-10\n30\n", actual

    for case, argument in (("string", '"bad"'), ("bool", "true")):
        negative = scratch / f"consumer-{case}"
        package(negative, f"c2b6c_consumer_{case}",
                "module Main\nimport Service\n\nfn main():\n"
                f"  echo Service.compute({argument})\n")
        rejected = run([str(MARGO), "build"], cwd=negative, env=env,
                       expected=1)
        assert "TYPE_MISMATCH" in rejected, rejected
        print(f"PASS {case} rejected during checking with TYPE_MISMATCH")

    bad_env = staged_env.copy()
    bad_env["MOSS_MODULE_PATH"] = str(unsupported)
    invalid = scratch / "consumer-unsupported"
    package(invalid, "c2b6c_consumer_unsupported", """module Main
import Service

fn main():
  echo Service.compute(5)
""")
    rejected = run([str(MARGO), "build"], cwd=invalid, env=bad_env,
                   expected=1)
    assert "SOURCE_FREE_CHECKED_BODY_INVALID" in rejected, rejected
    print("PASS unsupported sealed arithmetic body rejected fail-closed")

    for case, artifacts in (
            ("missing-body-alternate-legacy", missing_body),
            ("missing-body-no-legacy", missing_body_no_legacy)):
        missing_env = staged_env.copy()
        missing_env["MOSS_MODULE_PATH"] = str(artifacts)
        missing = scratch / f"consumer-{case}"
        package(missing, f"c2b6c_consumer_{case.replace('-', '_')}", """module Main
import Service

fn main():
  echo Service.compute(5)
""")
        missing_build = subprocess.run([str(MARGO), "build"], cwd=missing,
                                       env=missing_env, capture_output=True,
                                       text=True)
        rejected = missing_build.stdout + missing_build.stderr
        assert missing_build.returncode != 0, (
            f"{case}: consumer compilation succeeded unexpectedly")
        assert "SOURCE_FREE_CHECKED_BODY_INVALID" in rejected, rejected
        print(f"PASS {case} failed closed with "
              "SOURCE_FREE_CHECKED_BODY_INVALID")

    mismatch_env = staged_env.copy()
    mismatch_env["MOSS_MODULE_PATH"] = str(mismatched_hash)
    mismatch = scratch / "consumer-mismatched-body-hash"
    package(mismatch, "c2b6c_consumer_mismatched_body_hash", """module Main
import Service

fn main():
  echo Service.compute(5)
""")
    mismatch_build = subprocess.run([str(MARGO), "build"], cwd=mismatch,
                                    env=mismatch_env, capture_output=True,
                                    text=True)
    mismatch_diagnostic = mismatch_build.stdout + mismatch_build.stderr
    assert mismatch_build.returncode != 0, (
        "consumer compilation accepted mismatched summary/body hashes")
    mismatch_code = next((code for code in (
        "SOURCE_FREE_CHECKED_BODY_INVALID", "SOURCE_FREE_SUMMARY_INVALID")
        if code in mismatch_diagnostic), None)
    assert mismatch_code, mismatch_diagnostic
    print(f"PASS mismatched summary/body hash rejected with {mismatch_code}")

    v9_env = os.environ.copy()
    v9_env.pop("MOSS_STAGED_ABI", None)
    v9_env["MOSS"] = str(MOSS)
    v9_env["MARGO_HOME"] = str(scratch / "margo-home-v9")
    v9_env["TMPDIR"] = str(TMP)
    provider_v9 = scratch / "provider-v9"
    package(provider_v9, "c2b6c_provider_v9", """module Service

export fn compute(x):
  return x * 2 + 20
""")
    run([str(MARGO), "build"], cwd=provider_v9, env=v9_env)
    artifacts_v9 = scratch / "artifacts-v9"
    shutil.copytree(provider_v9 / "build" / "debug", artifacts_v9)
    v9_interface = artifacts_v9 / "Service.mossi"
    assert b"generic_stmt " in v9_interface.read_bytes()
    shutil.rmtree(provider_v9)
    v9_env["MOSS_MODULE_PATH"] = str(artifacts_v9)
    consumer_v9 = scratch / "consumer-v9"
    package(consumer_v9, "c2b6c_consumer_v9", """module Main
import Service

fn main():
  echo Service.compute(5)
""")
    run([str(MARGO), "build"], cwd=consumer_v9, env=v9_env)
    assert run([str(MARGO), "run"], cwd=consumer_v9,
               env=v9_env) == "30\n"

print("PASS source-free checked arithmetic: Int=30, Float=25.0; normal staged "
      "artifact omits compute generic_stmt; unsupported legacy forms and "
      "default ABI-v9 remain executable")
