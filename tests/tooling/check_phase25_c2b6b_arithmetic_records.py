#!/usr/bin/env python3
"""Verify staged ABI-v10 emission of checked generic arithmetic records."""

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


with tempfile.TemporaryDirectory(prefix="phase25-c2b6b-arithmetic-",
                                 dir=TMP) as temp:
    scratch = Path(temp)
    provider = scratch / "provider"
    source_dir = provider / "src"
    source_dir.mkdir(parents=True)
    (provider / "Moss.toml").write_text(
        '[package]\nname = "c2b6b_provider"\nversion = "0.1.0"\n\n'
        '[build]\nsource = "src"\n')
    (source_dir / "service.moss").write_text("""module Service

export fn compute(x):
  return x * 2 + 20

export fn unsupported_sub(x):
  return x * 2 - 20

export fn unsupported_reverse(x):
  return 20 + x * 2
""")

    verifier = scratch / "verify_arithmetic_records"
    run(["g++", "-std=c++17", "-O0", "-Wall", "-Wextra", "-Werror",
         "-Wno-missing-field-initializers", "-pedantic", "-Isrc",
         "tests/tooling/check_phase25_c2b6b_arithmetic_records.cpp",
         "-o", str(verifier)])

    env = os.environ.copy()
    env["MOSS_STAGED_ABI"] = "v10"
    env["MOSS"] = str(MOSS)
    env["MARGO_HOME"] = str(scratch / "margo_home")
    env["TMPDIR"] = str(TMP)
    run([str(MARGO), "build"], cwd=provider, env=env)
    interface = provider / "build" / "debug" / "Service.mossi"
    assert interface.is_file(), interface
    print(run([str(verifier), str(interface)]), end="")

print("PASS staged ABI-v10 checked arithmetic provider records")
