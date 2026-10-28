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


def error_code(payload):
    error = payload.get("error")
    if isinstance(error, dict):
        return error.get("code")
    if isinstance(error, str):
        match = re.search(r'"code"\s*:\s*"([A-Z0-9_]+)"', error)
        if match:
            return match.group(1)
    return None


def assert_error_code(payload, expected, label):
    actual = error_code(payload)
    assert actual == expected, f"{label}: expected {expected}, got {actual}: {payload}"
    print(f"  [PASS] {label}: {actual}")


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
assert_error_code(rejected, "SOURCE_FREE_ARTIFACT_CONTRACT_MISMATCH",
                  "forged re-digested .mossi")
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
# Moss check reads the .mossi contract only, so it intentionally does not
# validate whether a native rlib was swapped.
checked = json.loads(run(consumer_project, MOSS, "check", consumer, "--json",
                         provider_path=build).stdout)
assert checked["ok"], checked
rejected = json.loads(run(consumer_project, MARGO, "build", "--json",
                          provider_path=build, expected=1).stdout)
assert_error_code(rejected, "SOURCE_FREE_ARTIFACT_CONTRACT_MISMATCH",
                  "swapped provider rlib")

# A selected stale provider artifact must also invalidate the warm consumer.
rlib.write_bytes(b"!<arch>\n")
rejected = json.loads(run(consumer_project, MARGO, "build", "--json",
                          provider_path=build, expected=1).stdout)
assert_error_code(rejected, "SOURCE_FREE_ARTIFACT_CONTRACT_INVALID",
                  "corrupt provider rlib")

# A genuinely external provider has only its copied interface and rlib. Its
# altered source hash disagrees with the selected rlib contract and must be
# rejected before source freshness is considered, without rewriting either file.
external_source = OUT / "external-provider-source"
(external_source / "src").mkdir(parents=True)
(external_source / "moss.toml").write_text(
    '[project]\nname="external_provider"\nversion="0.1.0"\n')
(external_source / "src/external.moss").write_text('''module external
export fn answer() -> Int:
  return 9
''')
run(external_source, MARGO, "build", "--json")
external_path = OUT / "external-provider-artifacts"
external_path.mkdir()
external_build = external_source / "build/debug"
for artifact in [*external_build.glob("external.*"),
                 external_build / "libexternal.rlib"]:
    assert artifact.is_file(), f"missing external provider artifact: {artifact}"
    shutil.copy2(artifact, external_path / artifact.name)
external_interface = external_path / "external.mossi"
shutil.rmtree(external_source)
external_original = external_interface.read_text()
external_consumer = OUT / "external-consumer"
(external_consumer / "src").mkdir(parents=True)
(external_consumer / "moss.toml").write_text(
    '[project]\nname="external_consumer"\nversion="0.1.0"\n')
(external_consumer / "src/main.moss").write_text('''module app
import external
fn main():
  echo external.answer()
''')
rejected = json.loads(run(external_consumer, MARGO, "build", "--json",
                          provider_path=external_path, expected=1).stdout)
assert_error_code(rejected, "SOURCE_FREE_PROVIDER_SOURCE_MISSING",
                  "valid external provider without source")
assert "provider source is missing" in str(rejected)
assert external_interface.read_text() == external_original, \
    "Margo must not recreate an external provider from an unavailable source"

external_tampered = re.sub(
    r"(?m)^package_source_hash sha256:[0-9a-f]{64}$",
    "package_source_hash sha256:" + "0" * 64, external_original)
assert external_tampered != external_original
external_interface.write_text(external_tampered)
rejected = json.loads(run(external_consumer, MARGO, "build", "--json",
                          provider_path=external_path, expected=1).stdout)
assert_error_code(rejected, "SOURCE_FREE_ARTIFACT_CONTRACT_MISMATCH",
                  "tampered source-free external provider")
assert external_interface.read_text() == external_tampered, \
    "Margo must leave external provider artifacts untouched"

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
assert_error_code(rejected, "MOSS_INTERFACE_STALE", "stale nested source hash")
nested_interface.write_text(nested_original)

# R2 freshness must be rechecked when an ambient provider loses its source and
# again when a different source set is restored. The restored edit remains
# stale until the provider regenerates its interface.
provider_source_bytes = provider_project.joinpath("src/provider.moss").read_text()
provider_project.joinpath("src/provider.moss").unlink()
missing_source = json.loads(run(consumer_project, MARGO, "build", "--json",
                                provider_path=nested_root, expected=1).stdout)
assert_error_code(missing_source, "MOSS_INTERFACE_STALE", "missing provider source")
provider_project.joinpath("src/provider.moss").write_text(
    provider_source_bytes.replace("return 42", "return 44"))
changed_source = json.loads(run(consumer_project, MARGO, "build", "--json",
                                provider_path=nested_root, expected=1).stdout)
assert_error_code(changed_source, "MOSS_INTERFACE_STALE", "changed provider source")
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
assert_error_code(deps_stale, "MOSS_INTERFACE_STALE", "stale package-local source hash")

print("ABI-v9 transitive external-provider warm-cache revalidation passed")
