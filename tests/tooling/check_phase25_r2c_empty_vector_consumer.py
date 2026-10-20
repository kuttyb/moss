#!/usr/bin/env python3
"""Prove source-free ABI-v10 empty-vector import and specialization."""

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


def run(command, *, cwd, env, expected=0):
    result = subprocess.run(command, cwd=cwd, env=env, capture_output=True,
                            text=True, timeout=240)
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


with tempfile.TemporaryDirectory(prefix="phase25-r2c-empty-vector-",
                                 dir=TMP) as temporary:
    scratch = Path(temporary)
    mutator = scratch / "mutate_empty_vector_artifact"
    run(["g++", "-std=c++17", "-O0", "-Wall", "-Wextra", "-Werror",
         "-Wno-missing-field-initializers", "-pedantic", "-Isrc",
         "tests/tooling/mutate_phase25_r2c_empty_vector_artifact.cpp",
         "-o", str(mutator)], cwd=ROOT, env=os.environ.copy())
    provider = scratch / "provider"
    package(provider, "r2c_empty_vector_provider", """module Provider

export fn fresh():
  return []
""")
    staged = os.environ.copy()
    staged.update(MOSS_STAGED_ABI="v10", MOSS_PHASE25_EMPTY_VECTOR_EMIT="1",
                  MOSS=str(MOSS), MARGO_HOME=str(scratch / "margo-provider"),
                  TMPDIR=str(TMP))
    run([str(MARGO), "build"], cwd=provider, env=staged)
    artifacts = scratch / "provider-artifacts"
    shutil.copytree(provider / "build" / "debug", artifacts)
    shutil.rmtree(provider / "src")
    assert not (provider / "src").exists(), "provider source directory remained"

    consumer = scratch / "consumer"
    package(consumer, "r2c_empty_vector_consumer", """module Main
import Provider

fn main():
  numbers = Provider.fresh()
  echo numbers
  numbers.push(7)
  echo numbers
  flags = Provider.fresh()
  echo flags
  flags.push(true)
  echo flags
""")
    consumer_env = staged.copy()
    consumer_env.pop("MOSS_PHASE25_EMPTY_VECTOR_EMIT", None)
    consumer_env.update(MOSS_MODULE_PATH=str(artifacts),
                        MOSS_PHASE25_CHECKED_BODY_TRACE="1",
                        MARGO_HOME=str(scratch / "margo-consumer"))
    run([str(MARGO), "build"], cwd=consumer, env=consumer_env)
    build_output = run([str(MOSS), "check", consumer / "src/main.moss", "--json"],
                       cwd=consumer, env=consumer_env)
    trace = "MOSS_CHECKED_BODY_PROJECTED|callable-v1:"
    assert (trace in build_output and ":fn:fresh|checked-body-v1" in build_output), (
        "consumer did not report checked-body projection", build_output)
    actual = run([str(MARGO), "run"], cwd=consumer, env=consumer_env)
    assert actual == "[]\n[7]\n[]\n[true]\n", actual
    print("PASS source-free checked-body consumer: Vector[i32] and Vector[bool]")
    print("PASS checked-body branch trace present; legacy generic_stmt not selected")
    print("PASS native execution preserves empty vectors and concrete elements")

    # Keep the default ABI-v9 provider and consumer route intact.
    provider_v9 = scratch / "provider-v9"
    package(provider_v9, "r2c_empty_vector_provider_v9", """module Provider

export fn fresh():
  return []
""")
    v9_env = staged.copy()
    v9_env.pop("MOSS_STAGED_ABI", None)
    v9_env["MARGO_HOME"] = str(scratch / "margo-provider-v9")
    run([str(MARGO), "build"], cwd=provider_v9, env=v9_env)
    v9_artifacts = scratch / "provider-v9-artifacts"
    shutil.copytree(provider_v9 / "build" / "debug", v9_artifacts)
    shutil.rmtree(provider_v9 / "src")
    consumer_v9 = scratch / "consumer-v9"
    package(consumer_v9, "r2c_empty_vector_consumer_v9", """module Main
import Provider

fn main():
  numbers = Provider.fresh()
  numbers.push(7)
  echo numbers
  flags = Provider.fresh()
  flags.push(true)
  echo flags
""")
    consumer_v9_env = v9_env.copy()
    consumer_v9_env.pop("MOSS_PHASE25_CHECKED_BODY_TRACE", None)
    consumer_v9_env["MOSS_MODULE_PATH"] = str(v9_artifacts)
    consumer_v9_env["MARGO_HOME"] = str(scratch / "margo-consumer-v9")
    run([str(MARGO), "build"], cwd=consumer_v9, env=consumer_v9_env)
    v9_actual = run([str(MARGO), "run"], cwd=consumer_v9,
                    env=consumer_v9_env)
    assert v9_actual == "[7]\n[true]\n", v9_actual
    print("PASS default ABI-v9 source-free empty-vector behavior")

    failures = {
        "missing-summary": ("SOURCE_FREE_SUMMARY_INVALID",),
        "missing-body": ("SOURCE_FREE_CHECKED_BODY_INVALID",),
        "mismatch-hash": ("SOURCE_FREE_CHECKED_BODY_INVALID",
                          "SOURCE_FREE_SUMMARY_INVALID"),
        "identity": ("SOURCE_FREE_",),
        "invalid-provenance": ("SOURCE_FREE_",),
        "mismatched-callable-provenance": ("SOURCE_FREE_SUMMARY_INVALID",),
        "unbound": ("SOURCE_FREE_",),
        "nonempty": ("SOURCE_FREE_CHECKED_BODY_INVALID",),
        "unsupported-shape": ("SOURCE_FREE_CHECKED_BODY_INVALID",),
    }
    for mode, expected_codes in failures.items():
        mutated = scratch / f"artifacts-{mode}"
        shutil.copytree(artifacts, mutated)
        run([str(mutator), str(mutated / "Provider.mossi"),
             str(mutated / "libProvider.rlib"), mode], cwd=ROOT,
            env=consumer_env)
        bad_consumer = scratch / f"consumer-{mode}"
        package(bad_consumer, f"r2c_empty_vector_{mode.replace('-', '_')}",
                "module Main\nimport Provider\n\nfn main():\n"
                "  echo Provider.fresh()\n")
        bad_env = consumer_env.copy()
        bad_env["MOSS_MODULE_PATH"] = str(mutated)
        rejected = run([str(MARGO), "build"], cwd=bad_consumer, env=bad_env,
                       expected=1)
        assert any(code in rejected for code in expected_codes), (
            mode, expected_codes, rejected)
        print(f"PASS {mode} rejected fail-closed")

    mode_mismatch = consumer_env.copy()
    mode_mismatch.pop("MOSS_STAGED_ABI", None)
    mismatch = run([str(MOSS), "check", consumer / "src/main.moss", "--json"],
                   cwd=consumer, env=mode_mismatch, expected=1)
    assert "staged ABI v10" in mismatch or "ABI mode" in mismatch, mismatch
    print("PASS ABI-mode mismatch rejected")

    provenance_env = os.environ.copy()
    provenance_env.update(MOSS_STAGED_ABI="v10",
                          MOSS_PHASE25_EMPTY_VECTOR_EMIT="1",
                          MOSS_PHASE25_CHECKED_BODY_TRACE="1",
                          MARGO_HOME=str(scratch / "margo-provenance"),
                          TMPDIR=str(TMP))
    provenance = run([sys.executable,
                      "tests/tooling/check_phase25_m2b1_provenance.py",
                      str(MOSS)], cwd=ROOT, env=provenance_env)
    assert "PASS conflicting_fresh_use" in provenance, provenance
    print("PASS M2b.1 source-free consumer provenance acceptance")

print("PASS R2c checked empty-vector consumer")
