#!/usr/bin/env python3
"""A warm consumer must revalidate a provider outside its build directory."""

import hashlib
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
OUT = ROOT / "tmp/phase21-abi-v9-external-cache"
shutil.rmtree(OUT, ignore_errors=True)
provider_project = OUT / "provider-project"
consumer_project = OUT / "consumer-project"
for project in (provider_project, consumer_project):
    (project / "src").mkdir(parents=True)
    (project / "moss.toml").write_text(
        '[project]\nname="phase21_abi_v9_external_cache"\nversion="0.1.0"\n')
(provider_project / "src/provider.moss").write_text('''module provider
export fn answer() -> Int:
  return 42
''')
(provider_project / "src/main.moss").write_text('''module app
import provider
fn main():
  echo provider.answer()
''')
consumer = consumer_project / "src/main.moss"
consumer.write_text('''module app
import provider
fn main():
  echo provider.answer()
''')


def run(project, *args, provider_path=None, expected=0):
    env = os.environ.copy()
    if provider_path:
        env["MOSS_MODULE_PATH"] = str(provider_path)
    result = subprocess.run([str(arg) for arg in args], cwd=project,
                            env=env, capture_output=True, text=True,
                            timeout=180)
    assert result.returncode == expected, (
        args, result.returncode, result.stdout, result.stderr)
    return result


run(provider_project, MARGO, "build", "--json")
external = OUT / "external-provider"
external.mkdir()
build = provider_project / "build/debug"
interface = external / "provider.mossi"
rlib = external / "libprovider.rlib"
shutil.copy2(build / "provider.mossi", interface)
shutil.copy2(build / "libprovider.rlib", rlib)
provider_source = provider_project / "src/provider.moss"
provider_source.rename(provider_source.with_suffix(".removed"))

for _ in range(2):
    built = json.loads(run(consumer_project, MARGO, "build", "--json",
                           provider_path=external).stdout)
    executable = built["artifacts"]["artifacts"]["executable"]
    assert run(consumer_project, executable).stdout == "42\n"

# Keep consumer source/cache unchanged, then edit only external metadata and
# recompute its editable digest. The unchanged object binding must reject it.
original = interface.read_text()
forged = original.replace("return=int", "return=bool", 1)
assert forged != original
material = re.sub(r"(?m)^provider_contract_digest sha256:[0-9a-f]{64}\n",
                  "", forged, count=1)
forged = re.sub(r"(?m)^provider_contract_digest sha256:[0-9a-f]{64}$",
                "provider_contract_digest sha256:" +
                hashlib.sha256(material.encode()).hexdigest(), forged)
interface.write_text(forged)
rejected = json.loads(run(consumer_project, MARGO, "build", "--json",
                          provider_path=external, expected=1).stdout)
assert not rejected["ok"], rejected
interface.write_text(original)

# A selected stale provider artifact must also invalidate the warm consumer.
rlib.write_bytes(b"!<arch>\n")
rejected = json.loads(run(consumer_project, MARGO, "build", "--json",
                          provider_path=external, expected=1).stdout)
assert not rejected["ok"], rejected

print("ABI-v9 external-provider warm-cache revalidation passed")
