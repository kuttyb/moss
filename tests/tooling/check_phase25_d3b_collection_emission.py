#!/usr/bin/env python3
"""Provider-only ABI-v10 emission for the checker-retained index_of body."""

import os
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
MOSS = Path(os.environ.get("MOSS", ROOT / "moss")).resolve()
MARGO = ROOT / "margo"
TMP = ROOT / "tmp"
TMP.mkdir(exist_ok=True)


def run(command, cwd=ROOT, env=None):
    result = subprocess.run(command, cwd=cwd, env=env,
                            capture_output=True, text=True, timeout=300)
    assert result.returncode == 0, (
        command, result.returncode, result.stdout + result.stderr)
    return result.stdout + result.stderr


def package(root, name):
    (root / "src").mkdir(parents=True)
    (root / "Moss.toml").write_text(
        f'[package]\nname = "{name}"\nversion = "0.1.0"\n\n'
        '[build]\nsource = "src"\n')
    (root / "src/main.moss").write_text("""module Service

export fn index_of(items, target) -> Int:
  var i = 0
  n = items |> count
  while i < n:
    if items[i] == target:
      return i
    i = i + 1
  return 0 - 1
""")


with tempfile.TemporaryDirectory(prefix="phase25-d3b-collection-", dir=TMP) as root:
    scratch = Path(root)
    provider = scratch / "provider"
    package(provider, "phase25_d3b_provider")
    env = os.environ.copy()
    env.update(MOSS_STAGED_ABI="v10", MOSS=str(MOSS),
               MARGO_HOME=str(scratch / "margo-home"), TMPDIR=str(TMP))
    run([str(MARGO), "build"], cwd=provider, env=env)
    interface = provider / "build/debug/Service.mossi"
    assert interface.is_file(), interface
    data = interface.read_bytes()
    assert b"generic_stmt " in data, "D3b must retain compatibility body records"

    verifier = scratch / "verify_collection_artifact"
    run(["g++", "-std=c++17", "-O0", "-Wall", "-Wextra", "-Werror",
         "-Wno-missing-field-initializers", "-pedantic", "-Isrc",
         "tests/tooling/check_phase25_d3b_collection_artifact.cpp",
         "-o", str(verifier)])
    run([str(verifier), str(interface)])

    provider_v9 = scratch / "provider-v9"
    package(provider_v9, "phase25_d3b_provider_v9")
    v9_env = env.copy()
    v9_env.pop("MOSS_STAGED_ABI", None)
    v9_env["MARGO_HOME"] = str(scratch / "margo-home-v9")
    run([str(MARGO), "build"], cwd=provider_v9, env=v9_env)
    v9_interface = provider_v9 / "build/debug/Service.mossi"
    assert v9_interface.is_file(), v9_interface
    v9_data = v9_interface.read_bytes()
    assert b"generic_stmt " in v9_data
    assert b"checked-body-v1:" not in v9_data, (
        "default ABI-v9 build emitted staged checked-body records")

print("D3b provider-only checked collection emission: PASS")
