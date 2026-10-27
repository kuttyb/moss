#!/usr/bin/env python3
"""Distinguish a historical ABI-v8 provider from damaged ABI-v9 metadata."""

import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys


ROOT = Path(__file__).resolve().parents[2]
MOSS = Path(sys.argv[1] if len(sys.argv) > 1 else ROOT / "moss").resolve()
MARGO = MOSS.with_name("margo")
PROJECT = ROOT / "tmp/phase21-abi-v9-migration"
shutil.rmtree(PROJECT, ignore_errors=True)
(PROJECT / "src").mkdir(parents=True)
(PROJECT / "moss.toml").write_text(
    '[project]\nname="phase21_abi_v9_migration"\nversion="0.1.0"\n')
provider = PROJECT / "src/provider.moss"
provider.write_text('''module provider
export fn answer() -> Int:
  return 7
''')
main = PROJECT / "src/main.moss"
main.write_text('''module app
import provider
fn main():
  echo provider.answer()
''')


def run(*args, expected=0, cwd=PROJECT, env=None):
    result = subprocess.run([str(arg) for arg in args], cwd=cwd, env=env,
                            capture_output=True, text=True, timeout=180)
    assert result.returncode == expected, (
        args, result.returncode, result.stdout, result.stderr)
    return result


run(MARGO, "build", "--json")
interface = PROJECT / "build/debug/provider.mossi"
original = interface.read_text()
assert "\nnative_abi 9\n" in original
# Package source remains available; binary-only providers are outside the
# Phase 26.1 package contract.
main.write_text(main.read_text() + '  echo "source-free"\n')
native = json.loads(run(MARGO, "build", "--json").stdout)
executable = native["artifacts"]["artifacts"]["executable"]
assert run(executable).stdout == "7\nsource-free\n"
run(MARGO, "build", "--json")
original = interface.read_text()

# Probe provider interface compatibility from a separate package so check
# consumes the external .mossi and validates it against the provider source.
consumer = PROJECT / "consumer"
(consumer / "src").mkdir(parents=True)
(consumer / "moss.toml").write_text(
    '[project]\nname="phase21_abi_v9_consumer"\nversion="0.1.0"\n')
consumer_main = consumer / "src/main.moss"
consumer_main.write_text(main.read_text())
consumer_env = os.environ.copy()
consumer_env["MOSS_MODULE_PATH"] = str(PROJECT / "build/debug")

# An ABI-v8 interface has the v2 container and native_abi 8 but no v9
# provider_contract_digest field. Diagnose the required provider rebuild
# before treating the absent v9-only field as a damaged v9 interface.
legacy = original.replace("\nnative_abi 9\n", "\nnative_abi 8\n", 1)
legacy, count = re.subn(
    r"(?m)^provider_contract_digest sha256:[0-9a-f]{64}\n", "", legacy)
assert count == 1
interface.write_text(legacy)
checked = json.loads(run(MOSS, "check", consumer_main, "--json", expected=1,
                         cwd=consumer, env=consumer_env).stdout)
assert checked["error"]["code"] == "SOURCE_FREE_ABI_REBUILD_REQUIRED", checked
assert "rebuild" in checked["error"]["message"], checked
built = json.loads(run(MARGO, "build", "--json", expected=1,
                       cwd=consumer, env=consumer_env).stdout)
assert "SOURCE_FREE_ABI_REBUILD_REQUIRED" in built["error"], built

# A current-v9 interface with the same missing digest is malformed metadata,
# and must retain the distinct fail-closed diagnostic.
missing_v9, count = re.subn(
    r"(?m)^provider_contract_digest sha256:[0-9a-f]{64}\n", "", original)
assert count == 1
interface.write_text(missing_v9)
checked = json.loads(run(MOSS, "check", consumer_main, "--json", expected=1,
                         cwd=consumer, env=consumer_env).stdout)
assert checked["error"]["code"] == "SOURCE_FREE_CONTRACT_INVALID", checked
built = json.loads(run(MARGO, "build", "--json", expected=1,
                       cwd=consumer, env=consumer_env).stdout)
assert "SOURCE_FREE_CONTRACT_INVALID" in built["error"], built

interface.write_text(original)
assert json.loads(run(MOSS, "check", consumer_main, "--json", cwd=consumer,
                      env=consumer_env).stdout)["ok"]
print("ABI-v9 source-free migration and damaged-v9 diagnostics passed")
