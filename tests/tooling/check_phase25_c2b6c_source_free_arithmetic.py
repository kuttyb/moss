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

    source_backed = scratch / "source-backed"
    package(source_backed, "r3_arithmetic_source", """module Main

fn compute(x):
  return 20 + x * 2

fn nested(x):
  return x * 2 + x * 3

fn main():
  echo compute(5)
  echo compute(2.5)
  echo nested(5)
  echo nested(2.5)
""")
    source_env = os.environ.copy()
    source_env["MOSS"] = str(MOSS)
    source_env["MARGO_HOME"] = str(scratch / "margo-home-source")
    source_debug = run([str(MARGO), "build"], cwd=source_backed,
                       env=source_env)
    assert run([str(MARGO), "run"], cwd=source_backed,
               env=source_env) == "30\n25.0\n25\n12.5\n"
    run([str(MARGO), "build", "--release"], cwd=source_backed,
        env=source_env)
    assert run([str(MARGO), "run", "--release"], cwd=source_backed,
               env=source_env) == "30\n25.0\n25\n12.5\n"
    print("PASS source-backed native Debug/Release numeric parity")

    v9_provider = scratch / "provider-v9"
    package(v9_provider, "r3_arithmetic_v9_provider", """module Service

export fn compute(x):
  return 20 + x * 2

export fn nested(x):
  return x * 2 + x * 3
""")
    v9_env = os.environ.copy()
    v9_env["MOSS"] = str(MOSS)
    v9_env["MARGO_HOME"] = str(scratch / "margo-home-v9")
    run([str(MARGO), "build"], cwd=v9_provider, env=v9_env)
    v9_debug = scratch / "artifacts-v9-debug"
    shutil.copytree(v9_provider / "build" / "debug", v9_debug)
    run([str(MARGO), "build", "--release"], cwd=v9_provider, env=v9_env)
    v9_release = scratch / "artifacts-v9-release"
    shutil.copytree(v9_provider / "build" / "release", v9_release)
    shutil.rmtree(v9_provider)
    assert not list(scratch.rglob("provider-v9/**/*.moss"))

    for flavor, artifacts, release_args in (
            ("debug", v9_debug, []),
            ("release", v9_release, ["--release"])):
        consumer = scratch / f"consumer-v9-{flavor}"
        package(consumer, f"r3_arithmetic_v9_{flavor}", """module Main
import Service

fn main():
  echo Service.compute(5)
  echo Service.compute(2.5)
  echo Service.nested(5)
  echo Service.nested(2.5)
""")
        consumer_env = v9_env.copy()
        consumer_env["MOSS_MODULE_PATH"] = str(artifacts)
        run([str(MARGO), "build", *release_args], cwd=consumer,
            env=consumer_env)
        actual = run([str(MARGO), "run", *release_args], cwd=consumer,
                     env=consumer_env)
        assert actual == "30\n25.0\n25\n12.5\n", (flavor, actual)
    print("PASS source-deleted ABI-v9 native Debug/Release numeric parity")

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
  return 20 + x * 2

export fn nested(x):
  return x * 2 + x * 3
""")
    run([str(MARGO), "build"], cwd=provider, env=staged_env)
    interface = provider / "build" / "debug" / "Service.mossi"
    assert interface.is_file(), interface
    run([str(verifier), str(interface)])

    emitted = scratch / "artifacts-emitted"
    shutil.copytree(provider / "build" / "debug", emitted)
    run([str(MARGO), "build", "--release"], cwd=provider, env=staged_env)
    emitted_release = scratch / "artifacts-emitted-release"
    shutil.copytree(provider / "build" / "release", emitted_release)

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

    unsupported_provider = scratch / "provider-unsupported"
    package(unsupported_provider, "c2b6c_unsupported_provider", """module Legacy

export fn unsupported_operand(x):
  return x * "bad"
""")
    run([str(MARGO), "build"], cwd=unsupported_provider, env=staged_env)
    unsupported_legacy = scratch / "artifacts-unsupported-legacy"
    shutil.copytree(unsupported_provider / "build" / "debug", unsupported_legacy)

    mismatched_hash = scratch / "artifacts-mismatched-body-hash"
    shutil.copytree(provider / "build" / "debug", mismatched_hash)
    run([str(mutator), str(mismatched_hash / "Service.mossi"),
         str(mismatched_hash / "libService.rlib"), "Service",
         "mismatch-body-hash"])
    shutil.rmtree(provider)
    assert not list(scratch.rglob("provider-v10/**/*.moss"))

    env = staged_env.copy()
    env["MOSS_MODULE_PATH"] = str(emitted)
    env["MOSS_PHASE25_CHECKED_BODY_TRACE"] = "1"
    positive = scratch / "consumer-positive"
    package(positive, "c2b6c_consumer", """module Main
import Service

fn main():
  echo Service.compute(5)
  echo Service.compute(2.5)
  echo Service.nested(5)
  echo Service.nested(2.5)
""")
    run([str(MARGO), "build"], cwd=positive, env=env)
    projection_trace = run(
        [str(MOSS), "check", str(positive / "src" / "main.moss"), "--json"],
        cwd=positive, env=env)
    assert "MOSS_CHECKED_BODY_PROJECTED|" in projection_trace and \
        "Service:fn:compute|checked-body-v1" in projection_trace and \
        "Service:fn:nested|checked-body-v1" in projection_trace, projection_trace
    actual = run([str(MARGO), "run"], cwd=positive, env=env)
    assert actual == "30\n25.0\n25\n12.5\n", actual
    release_env = staged_env.copy()
    release_env["MOSS_MODULE_PATH"] = str(emitted_release)
    run([str(MARGO), "build", "--release"], cwd=positive, env=release_env)
    actual_release = run([str(MARGO), "run", "--release"], cwd=positive,
                         env=release_env)
    assert actual_release == "30\n25.0\n25\n12.5\n", actual_release

    for mode in ("wrong-operation", "wrong-operands", "wrong-result",
                 "wrong-parent", "duplicate-fact", "missing-fact"):
        artifacts = scratch / f"artifacts-{mode}"
        shutil.copytree(emitted, artifacts)
        run([str(mutator), str(artifacts / "Service.mossi"),
             str(artifacts / "libService.rlib"), "Service", mode])
        hostile_env = staged_env.copy()
        hostile_env["MOSS_MODULE_PATH"] = str(artifacts)
        hostile = scratch / f"consumer-{mode}"
        package(hostile, f"c2b6c_consumer_{mode.replace('-', '_')}", """module Main
import Service

fn main():
  echo Service.compute(5)
""")
        result = subprocess.run([str(MARGO), "build"], cwd=hostile,
                                env=hostile_env, capture_output=True, text=True)
        output = result.stdout + result.stderr
        assert result.returncode != 0, f"{mode} unexpectedly accepted"
        assert "SOURCE_FREE_" in output, (mode, output)
        print(f"PASS rehashed hostile arithmetic mutation rejected: {mode}")

    for case, argument in (("string", '"bad"'), ("bool", "true")):
        negative = scratch / f"consumer-{case}"
        package(negative, f"c2b6c_consumer_{case}",
                "module Main\nimport Service\n\nfn main():\n"
                f"  echo Service.compute({argument})\n")
        negative_env = env.copy()
        negative_env.pop("MOSS_PHASE25_CHECKED_BODY_TRACE", None)
        rejected = run([str(MARGO), "build"], cwd=negative,
                       env=negative_env, expected=1)
        assert any(marker in rejected for marker in (
            "TYPE_MISMATCH", "NO_VALID_SPECIALIZATION",
            "SOURCE_FREE_")), rejected
        print(f"PASS {case} rejected during checking")

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
    assert "SOURCE_FREE_" in rejected, rejected
    print("PASS unsupported sealed arithmetic body rejected fail-closed")

    legacy_env = staged_env.copy()
    legacy_env["MOSS_MODULE_PATH"] = str(unsupported_legacy)
    unsupported_consumer = scratch / "consumer-unsupported-legacy"
    package(unsupported_consumer, "c2b6c_consumer_unsupported_legacy", """module Main
import Legacy

fn main():
  echo Legacy.unsupported_operand(5)
""")
    rejected = run([str(MARGO), "build"], cwd=unsupported_consumer,
                   env=legacy_env, expected=1)
    assert "SOURCE_FREE_CHECKED_BODY_UNAVAILABLE" in rejected, rejected
    print("PASS unsupported generic legacy-only callable rejected in staged ABI-v10")

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
