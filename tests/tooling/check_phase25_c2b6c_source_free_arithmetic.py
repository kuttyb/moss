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
    run(["g++", "-std=c++17", "-O0", "-Wall", "-Wextra", "-Werror",
         "-Wno-missing-field-initializers", "-pedantic", "-Isrc",
         "tests/tooling/mutate_phase25_c2b6c_arithmetic_artifact.cpp",
         "-o", str(mutator)])

    staged_env = os.environ.copy()
    staged_env["MOSS_STAGED_ABI"] = "v10"
    staged_env["MOSS"] = str(MOSS)
    staged_env["MARGO_HOME"] = str(scratch / "margo-home-v10")
    staged_env["TMPDIR"] = str(TMP)

    provider = scratch / "provider-v10"
    package(provider, "c2b6c_provider", """module Service

export fn compute(x):
  return x * 2 + 20
""")
    run([str(MARGO), "build"], cwd=provider, env=staged_env)

    stripped = scratch / "artifacts-stripped"
    shutil.copytree(provider / "build" / "debug", stripped)
    run([str(mutator), str(stripped / "Service.mossi"),
         str(stripped / "libService.rlib"), "Service", "strip-legacy"])

    unsupported = scratch / "artifacts-unsupported"
    shutil.copytree(provider / "build" / "debug", unsupported)
    run([str(mutator), str(unsupported / "Service.mossi"),
         str(unsupported / "libService.rlib"), "Service", "unsupported-body"])
    shutil.rmtree(provider)
    assert not list(scratch.rglob("provider-v10/**/*.moss"))

    env = staged_env.copy()
    env["MOSS_MODULE_PATH"] = str(stripped)
    positive = scratch / "consumer-positive"
    package(positive, "c2b6c_consumer", """module Main
import Service

fn main():
  echo Service.compute(5)
  echo Service.compute(2.5)
""")
    run([str(MARGO), "build"], cwd=positive, env=env)
    actual = run([str(MARGO), "run"], cwd=positive, env=env)
    assert actual == "30\n25.0\n", actual

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

print("PASS source-free checked arithmetic: Int=30, Float=25.0; stripped "
      "generic_stmt body reconstructed from checked IR; ABI-v9 remains 30")
