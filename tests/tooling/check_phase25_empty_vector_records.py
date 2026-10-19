#!/usr/bin/env python3
"""Check opt-in ABI-v10 emission of checked empty-vector records."""

import os
from pathlib import Path
import subprocess
import sys
import tempfile


ROOT = Path(__file__).resolve().parents[2]
MOSS = Path(sys.argv[1]).resolve() if len(sys.argv) > 1 else ROOT / "moss"
MARGO = ROOT / "margo"
TMP = ROOT / "tmp"
TMP.mkdir(exist_ok=True)


def run(command, *, cwd=ROOT, env=None):
    result = subprocess.run(command, cwd=cwd, env=env,
                            capture_output=True, text=True)
    assert result.returncode == 0, (command, result.stdout, result.stderr)
    return result.stdout


def make_provider(root: Path, package: str):
    source_dir = root / "src"
    source_dir.mkdir(parents=True)
    (root / "Moss.toml").write_text(
        f'[package]\nname = "{package}"\nversion = "0.1.0"\n\n'
        '[build]\nsource = "src"\n')
    (source_dir / "provider.moss").write_text("""module Provider

export fn fresh():
  return []

export fn nonempty():
  return [1]

export fn dependent(values):
  return values

export fn scalar():
  return 1

export fn multiple():
  values = []
  return values
""")


with tempfile.TemporaryDirectory(prefix="phase25-empty-vector-",
                                 dir=TMP) as temporary:
    scratch = Path(temporary)
    verifier = scratch / "verify_empty_vector_records"
    run(["g++", "-std=c++17", "-O0", "-w", "-Isrc",
         "tests/tooling/check_phase25_empty_vector_records.cpp",
         "-o", str(verifier)])

    experimental = scratch / "experimental"
    make_provider(experimental, "empty_vector_provider")
    enabled_env = os.environ.copy()
    enabled_env.update(
        MOSS_STAGED_ABI="v10",
        MOSS_PHASE25_EMPTY_VECTOR_EMIT="1",
        MOSS=str(MOSS),
        MARGO_HOME=str(scratch / "margo-experimental"),
        TMPDIR=str(TMP),
    )
    run([str(MARGO), "build"], cwd=experimental, env=enabled_env)
    experimental_interface = experimental / "build/debug/Provider.mossi"
    assert experimental_interface.is_file(), experimental_interface
    print(run([str(verifier), str(experimental_interface), "experimental"]),
          end="")

    disabled = scratch / "disabled"
    make_provider(disabled, "empty_vector_provider_disabled")
    disabled_env = enabled_env.copy()
    disabled_env.pop("MOSS_PHASE25_EMPTY_VECTOR_EMIT")
    disabled_env["MARGO_HOME"] = str(scratch / "margo-disabled")
    run([str(MARGO), "build"], cwd=disabled, env=disabled_env)
    disabled_interface = disabled / "build/debug/Provider.mossi"
    assert disabled_interface.is_file(), disabled_interface
    print(run([str(verifier), str(disabled_interface), "disabled"]), end="")

print("PASS staged ABI-v10 opt-in empty-vector provider emission")
