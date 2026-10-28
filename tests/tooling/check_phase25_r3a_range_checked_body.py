#!/usr/bin/env python3
"""Rehashed hostile admission checks for the SDK borrowed-Range body."""

import os
from pathlib import Path
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
TMP = ROOT / "tmp"
TMP.mkdir(exist_ok=True)
MOSS = Path(os.environ.get("MOSS", ROOT / "moss")).resolve()
MARGO = ROOT / "margo"
SDK = ROOT / "sdk/foundations/bytes"


def run(argv, *, cwd, env, expected=0):
    result = subprocess.run(argv, cwd=cwd, env=env, capture_output=True,
                            text=True, timeout=300)
    output = result.stdout + result.stderr
    assert result.returncode == expected, (argv, result.returncode, output)
    return output


with tempfile.TemporaryDirectory(prefix="phase25-r3a-range-hostile-",
                                 dir=TMP) as directory:
    scratch = Path(directory)
    provider = scratch / "provider"
    shutil.copytree(SDK, provider,
                    ignore=shutil.ignore_patterns("build", "target", ".moss-cache"))
    base_env = os.environ.copy()
    base_env.update(MOSS_STAGED_ABI="v10", MOSS=str(MOSS), TMPDIR=str(TMP))
    provider_env = base_env | {"MARGO_HOME": str(scratch / "margo-provider")}
    run([str(MARGO), "build"], cwd=provider, env=provider_env)
    run([str(MARGO), "--release", "build"], cwd=provider, env=provider_env)

    artifacts = scratch / "artifacts"
    shutil.copytree(provider / "build/debug", artifacts)
    release_artifacts = scratch / "artifacts-release"
    shutil.copytree(provider / "build/release", release_artifacts)
    interface = artifacts / "bytes.mossi"
    archive = artifacts / "libbytes.rlib"
    assert interface.is_file() and archive.is_file()
    release_interface = release_artifacts / "bytes.mossi"
    release_archive = release_artifacts / "libbytes.rlib"
    assert release_interface.is_file() and release_archive.is_file()

    poisoner = scratch / "poison_generic_callable"
    run(["g++", "-std=c++17", "-O0", "-Wall", "-Wextra", "-Werror",
         "-Wno-missing-field-initializers", "-pedantic", "-Isrc",
         "tests/tooling/poison_phase25_generic_callable.cpp",
         "-o", str(poisoner)], cwd=ROOT, env=base_env)
    mutator = scratch / "mutate_r3a_range_artifact"
    run(["g++", "-std=c++17", "-O0", "-Wall", "-Wextra", "-Werror",
         "-Wno-missing-field-initializers", "-pedantic", "-Isrc",
         "tests/tooling/mutate_phase25_r3a_range_artifact.cpp",
         "-o", str(mutator)], cwd=ROOT, env=base_env)
    run([str(poisoner), str(interface), str(archive), "bytes", "from_range"],
        cwd=ROOT, env=base_env)
    run([str(poisoner), str(release_interface), str(release_archive),
         "bytes", "from_range"], cwd=ROOT, env=base_env)
    shutil.rmtree(provider / "src")
    assert not (provider / "src").exists()

    consumer = scratch / "consumer"
    (consumer / "src").mkdir(parents=True)
    (consumer / "Moss.toml").write_text(
        '[package]\nname = "r3a_range_checked_consumer"\nversion = "0.1.0"\n\n'
        '[build]\nsource = "src"\n')
    source = """module Main
import bytes

domain Reader:
  fn Count(path: String) -> Int:
    file = FileIO.open(path, ro)
    borrowed = file.read(0, 8)
    owned = bytes.from_range(borrowed)
    file.close()
    var weighted = 0
    var weight = 1
    for byte in owned:
      weighted = weighted + byte * weight
      weight = weight + 1
    reply weighted

  on_fail:
    reply -1

fn main():
  reader = Reader()
  echo message reader.Count("input.bin")
"""
    (consumer / "src/main.moss").write_text(source)
    (consumer / "input.bin").write_bytes(bytes([65, 66, 65, 66, 67]))
    for profile, args in (("debug", []), ("release", ["--release"])):
        profile_artifacts = artifacts if profile == "debug" else release_artifacts
        env = base_env | {"MOSS_MODULE_PATH": str(profile_artifacts),
                          "MARGO_HOME": str(scratch / f"margo-consumer-{profile}")}
        run([str(MARGO), *args, "build"], cwd=consumer, env=env)
        output = run([str(MARGO), *args, "run"], cwd=consumer, env=env)
        assert output == "991\n", (profile, output)

    env = base_env | {"MOSS_MODULE_PATH": str(artifacts),
                      "MARGO_HOME": str(scratch / "margo-consumer-hostile")}
    hostile_modes = {
        "iterate-result": ("SOURCE_FREE_", "body disagrees with its callable summary"),
        "assert-effect": ("SOURCE_FREE_", "body disagrees with its callable summary"),
        "missing-call-dependency": ("SOURCE_FREE_", "body disagrees with its callable summary"),
    }
    for mode, expected in hostile_modes.items():
        mutated = scratch / f"artifacts-{mode}"
        shutil.copytree(artifacts, mutated)
        run([str(mutator), str(mutated / "bytes.mossi"),
             str(mutated / "libbytes.rlib"), mode, "bytes"],
            cwd=ROOT, env=base_env)
        bad_env = env | {"MOSS_MODULE_PATH": str(mutated),
                         "MARGO_HOME": str(scratch / f"margo-{mode}"),
                         "MOSS_PHASE25_CHECKED_BODY_TRACE": "1"}
        result = subprocess.run([str(MARGO), "build", "--moss-json"], cwd=consumer,
                                env=bad_env, capture_output=True, text=True,
                                timeout=300)
        diagnostic = result.stdout + result.stderr
        assert result.returncode != 0 and all(
            expected_text in diagnostic for expected_text in expected), (
                mode, diagnostic)
        assert "::bytes:fn:from_range|checked-body-v1" not in diagnostic, (
            mode, "hostile body projected before rejection", diagnostic)
        print(f"PASS rehashed {mode} mutation rejected by checked importer")

print("R3A checked Range positives and rehashed hostile mutations passed")
