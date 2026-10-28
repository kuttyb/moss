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
(provider_project / "src/bridge.moss").write_text('''module bridge
import provider
export fn answer() -> Int:
  return provider.answer()
''')
(provider_project / "src/main.moss").write_text('''module app
import bridge
fn main():
  echo bridge.answer()
''')
consumer = consumer_project / "src/main.moss"
consumer.write_text('''module app
import bridge
fn main():
  echo bridge.answer()
''')


def run(project, *args, provider_path=None, expected=0):
    env = os.environ.copy()
    env.pop("MOSS_MODULE_PATH", None)
    if provider_path:
        env["MOSS_MODULE_PATH"] = str(provider_path)
    result = subprocess.run([str(arg) for arg in args], cwd=project,
                            env=env, capture_output=True, text=True,
                            timeout=180)
    assert result.returncode == expected, (
        args, result.returncode, result.stdout, result.stderr)
    return result


run(provider_project, MARGO, "build", "--json")
build = provider_project / "build/debug"
interface = build / "provider.mossi"
rlib = build / "libprovider.rlib"
for _ in range(2):
    built = json.loads(run(consumer_project, MARGO, "build", "--json",
                           provider_path=build).stdout)
    executable = built["artifacts"]["artifacts"]["executable"]
    assert run(consumer_project, executable).stdout == "42\n"

# Keep consumer source/cache unchanged, then edit only the transitively
# selected provider metadata and recompute its editable digest. The unchanged
# object binding must reject it before consumer native execution.
original = interface.read_text()
forged = original.replace("return=int", "return=bool", 1)
assert forged != original
material = re.sub(r"(?m)^provider_contract_digest sha256:[0-9a-f]{64}\n",
                  "", forged, count=1)
material = re.sub(r"(?m)^package_source_hash sha256:[0-9a-f]{64}\n",
                  "", material, count=1)
forged = re.sub(r"(?m)^provider_contract_digest sha256:[0-9a-f]{64}$",
                "provider_contract_digest sha256:" +
                hashlib.sha256(material.encode()).hexdigest(), forged)
interface.write_text(forged)
rejected = json.loads(run(consumer_project, MARGO, "build", "--json",
                          provider_path=build, expected=1).stdout)
assert not rejected["ok"], rejected
interface.write_text(original)

# A different, valid provider build with the same module and exported symbol
# names is still stale for this metadata. Reject it even when the consumer's
# own source and cache are unchanged.
alternate = OUT / "alternate-provider"
(alternate / "src").mkdir(parents=True)
(alternate / "moss.toml").write_text(
    '[project]\nname="phase21_abi_v9_external_cache"\nversion="0.1.0"\n')
(alternate / "src/provider.moss").write_text('''module provider
export fn answer() -> Int:
  return 43
''')
(alternate / "src/main.moss").write_text('''module app
import provider
fn main():
  echo provider.answer()
''')
run(alternate, MARGO, "build", "--json")
shutil.copy2(alternate / "build/debug/libprovider.rlib", rlib)
checked = json.loads(run(consumer_project, MOSS, "check", consumer, "--json",
                         provider_path=build).stdout)
assert checked["ok"], checked
rejected = json.loads(run(consumer_project, MARGO, "build", "--json",
                          provider_path=build, expected=1).stdout)
assert not rejected["ok"], rejected

# A selected stale provider artifact must also invalidate the warm consumer.
rlib.write_bytes(b"!<arch>\n")
rejected = json.loads(run(consumer_project, MARGO, "build", "--json",
                          provider_path=build, expected=1).stdout)
assert not rejected["ok"], rejected

# The compiler recursively searches MOSS_MODULE_PATH. A nested interface must
# therefore participate in Margo's fingerprint even though it is not at the
# root of the inherited path.
run(provider_project, MARGO, "build", "--json")
nested_root = provider_project / "build/ambient-root"
nested_artifacts = nested_root / "published"
nested_artifacts.mkdir(parents=True, exist_ok=True)
for provider_interface in build.glob("*.mossi"):
    shutil.copy2(provider_interface, nested_artifacts / provider_interface.name)
    provider_rlib = build / ("lib" + provider_interface.stem + ".rlib")
    if provider_rlib.is_file():
        shutil.copy2(provider_rlib, nested_artifacts / provider_rlib.name)
nested_interface = nested_artifacts / interface.name
nested_build = json.loads(run(consumer_project, MARGO, "build", "--json",
                              provider_path=nested_root).stdout)
assert nested_build["ok"], nested_build
nested_original = nested_interface.read_text()
nested_stale = re.sub(
    r"(?m)^package_source_hash sha256:[0-9a-f]{64}$",
    "package_source_hash sha256:" + "0" * 64, nested_original)
assert nested_stale != nested_original
nested_interface.write_text(nested_stale)
rejected = json.loads(run(consumer_project, MARGO, "build", "--json",
                          provider_path=nested_root, expected=1).stdout)
assert not rejected["ok"] and "stale" in rejected["error"], rejected
nested_interface.write_text(nested_original)

# R2 freshness must be rechecked when an ambient provider loses its source and
# again when a different source set is restored. The restored edit remains
# stale until the provider regenerates its interface.
provider_source_bytes = provider_project.joinpath("src/provider.moss").read_text()
provider_project.joinpath("src/provider.moss").unlink()
missing_source = json.loads(run(consumer_project, MARGO, "build", "--json",
                                provider_path=nested_root, expected=1).stdout)
assert not missing_source["ok"] and "stale" in missing_source["error"], missing_source
provider_project.joinpath("src/provider.moss").write_text(
    provider_source_bytes.replace("return 42", "return 44"))
changed_source = json.loads(run(consumer_project, MARGO, "build", "--json",
                                provider_path=nested_root, expected=1).stdout)
assert not changed_source["ok"] and "stale" in changed_source["error"], changed_source
provider_project.joinpath("src/provider.moss").write_text(provider_source_bytes)
restored = json.loads(run(consumer_project, MARGO, "build", "--json",
                          provider_path=nested_root).stdout)
assert restored["ok"], restored

# Package-local deps/ is another recursive source-free provider search root.
# Its nested interface hash must prevent a warm package skip after staleness.
deps_consumer = OUT / "deps-consumer"
(deps_consumer / "src").mkdir(parents=True)
(deps_consumer / "moss.toml").write_text(
    '[project]\nname="phase21_abi_v9_deps_consumer"\nversion="0.1.0"\n')
(deps_consumer / "src/main.moss").write_text(consumer.read_text())
embedded_provider = deps_consumer / "deps/provider"
embedded_provider.mkdir(parents=True)
shutil.copy2(provider_project / "moss.toml", embedded_provider / "moss.toml")
shutil.copytree(provider_project / "src", embedded_provider / "src")
embedded_build = embedded_provider / "build/debug"
embedded_build.mkdir(parents=True)
for artifact in build.glob("*.mossi"):
    shutil.copy2(artifact, embedded_build / artifact.name)
for artifact in build.glob("*.rlib"):
    shutil.copy2(artifact, embedded_build / artifact.name)
deps_build = json.loads(run(deps_consumer, MARGO, "build", "--json").stdout)
assert deps_build["ok"], deps_build
embedded_interface = embedded_build / "provider.mossi"
embedded_text = embedded_interface.read_text()
embedded_interface.write_text(re.sub(
    r"(?m)^package_source_hash sha256:[0-9a-f]{64}$",
    "package_source_hash sha256:" + "0" * 64, embedded_text))
deps_stale = json.loads(run(deps_consumer, MARGO, "build", "--json",
                            expected=1).stdout)
assert not deps_stale["ok"] and "stale" in deps_stale["error"], deps_stale

print("ABI-v9 transitive external-provider warm-cache revalidation passed")
