#!/usr/bin/env python3
"""Execute a direct checked body after removing legacy generic statements.

The nested private-helper H3 guard remains until its whole chain has records.
"""

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


def run(command, *, cwd=ROOT, env=None):
    result = subprocess.run(command, cwd=cwd, env=env,
                            capture_output=True, text=True)
    assert result.returncode == 0, (command, result.stdout, result.stderr)
    return result.stdout


def package(path, name, source):
    (path / "src").mkdir(parents=True)
    (path / "Moss.toml").write_text(
        f'[package]\nname = "{name}"\nversion = "0.1.0"\n\n'
        '[build]\nsource = "src"\n')
    (path / "src" / "main.moss").write_text(source)


with tempfile.TemporaryDirectory(prefix="phase25-h3-direct-records-", dir=TMP) as td:
    scratch = Path(td)
    verifier = scratch / "verify_records"
    poison = scratch / "poison_legacy"
    run(["g++", "-std=c++17", "-O0", "-Wall", "-Wextra", "-Werror",
         "-pedantic", "-Isrc", "tests/tooling/check_phase25_h3_direct_records.cpp",
         "-o", str(verifier)])
    run(["g++", "-std=c++17", "-O0", "-Wall", "-Wextra", "-Werror",
         "-pedantic", "-Isrc", "tests/tooling/check_phase25_h3_poison_legacy.cpp",
         "-o", str(poison)])

    env = os.environ.copy()
    env["MOSS_STAGED_ABI"] = "v10"
    env["MOSS"] = str(MOSS)
    env["TMPDIR"] = str(TMP)
    provider = scratch / "provider"
    package(provider, "h3_direct", """module Service

export type Box:
  value

export fn fresh(x):
  result = Box(value: x)
  return result
""")
    run([str(MARGO), "build"], cwd=provider, env=env)
    artifacts = scratch / "artifacts"
    shutil.copytree(provider / "build" / "debug", artifacts)
    interface = artifacts / "Service.mossi"
    assert interface.is_file() and (artifacts / "libService.rlib").is_file()
    run([str(verifier), str(interface)])
    run([str(poison), str(interface), str(artifacts / "libService.rlib"),
         "Service"])
    assert b"generic_stmt " not in interface.read_bytes()
    run([str(verifier), str(interface)])
    shutil.rmtree(provider)
    assert not list(artifacts.rglob("*.moss"))

    env["MOSS_MODULE_PATH"] = str(artifacts)
    for name, expression, expected in (
            ("int", "99", "99\n"),
            ("string", '"hello"', "hello\n")):
        consumer = scratch / name
        package(consumer, f"h3_direct_{name}",
                "module Main\nimport Service\n\nfn main():\n"
                f"  item = Service.fresh({expression})\n"
                "  echo item.value\n")
        run([str(MARGO), "build"], cwd=consumer, env=env)
        assert run([str(MARGO), "run"], cwd=consumer, env=env) == expected

print("H3 direct checked import without legacy statements: PASS")
