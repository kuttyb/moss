#!/usr/bin/env python3
"""Check opt-in ABI-v10 emission of checked empty-vector records."""

import os
from pathlib import Path
import subprocess
import sys
import tempfile


from compiler_test_support import compiler_support_sources

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
    support_sources = compiler_support_sources()
    run(["g++", "-std=c++17", "-O0", "-w", "-Isrc",
         "tests/tooling/check_phase25_empty_vector_records.cpp",
         *support_sources, "-o", str(verifier)])

    experimental = scratch / "experimental"
    make_provider(experimental, "empty_vector_provider")
    enabled_env = os.environ.copy()
    enabled_env.update(
        MOSS_STAGED_ABI="v10",
        MOSS=str(MOSS),
        MARGO_HOME=str(scratch / "margo-v10"),
        TMPDIR=str(TMP),
    )
    run([str(MARGO), "build"], cwd=experimental, env=enabled_env)
    experimental_interface = experimental / "build/debug/Provider.mossi"
    assert experimental_interface.is_file(), experimental_interface
    print(run([str(verifier), str(experimental_interface)]), end="")

print("PASS staged ABI-v10 opt-in empty-vector provider emission")
