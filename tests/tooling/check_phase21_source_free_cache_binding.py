#!/usr/bin/env python3
"""A warm consumer build must revalidate external ABI-v9 provider artifacts."""

import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys


repo = Path(__file__).resolve().parents[2]
compiler = Path(sys.argv[1]).resolve()
scratch = repo / "tmp/phase21-source-free-cache-binding"
if scratch.exists():
    shutil.rmtree(scratch)
scratch.mkdir(parents=True)
provider = scratch / "provider"
consumer = scratch / "consumer"
(provider / "src").mkdir(parents=True)
(consumer / "src").mkdir(parents=True)
(provider / "moss.toml").write_text(
    '[project]\nname="p21_cache_provider"\nversion="0.1.0"\n')
provider_source = provider / "src/service.moss"
provider_source.write_text('''module service
export domain Worker:
  fn Run() -> Int:
    reply 7
''')
(consumer / "moss.toml").write_text(
    '[project]\nname="p21_cache_consumer"\nversion="0.1.0"\n')
consumer_source = consumer / "src/main.moss"
# Root module equals the project artifact stem. The generated Rust path then
# exists when compile_native_artifact tests its early-reuse condition.
consumer_source.write_text('''module p21_cache_consumer
import service
fn main():
  worker = service.Worker()
  echo message worker.Run()
''')


def run(*args, cwd, provider_path=None):
    env = os.environ.copy()
    if provider_path is not None:
        env["MOSS_MODULE_PATH"] = str(provider_path)
    return subprocess.run([str(arg) for arg in args], cwd=cwd, env=env,
                          capture_output=True, text=True, timeout=180)


cxx = os.environ.get("CXX", "c++")
identity_test = scratch / "native-cache-identity-test"
identity_build = run(cxx, "-std=c++17", "-Wall", "-Wextra", "-Werror",
                     "-pedantic", "-Isrc",
                     "tests/tooling/native_cache_identity_test.cpp", "-o",
                     identity_test, cwd=repo)
assert identity_build.returncode == 0, (identity_build.stdout,
                                       identity_build.stderr)
identity_run = run(identity_test, cwd=repo)
assert identity_run.returncode == 0, (identity_run.stdout,
                                      identity_run.stderr)


def build(cwd, provider_path=None):
    result = run(compiler, "build", "--json", cwd=cwd,
                 provider_path=provider_path)
    return result, json.loads(result.stdout)


first, result = build(provider)
assert first.returncode == 0 and result["ok"], (first.stdout, first.stderr)
provider_path = provider / "build/debug"
interface = provider_path / "service.mossi"
rlib = provider_path / "libservice.rlib"
original = interface.read_text()
provider_source.rename(provider_source.with_suffix(".removed"))

warm = None
for _ in range(4):
    built, result = build(consumer, provider_path)
    assert built.returncode == 0 and result["ok"], (built.stdout, built.stderr)
    warm = result["result"]
    if warm["reused"]:
        break
assert warm and warm["reused"], warm
assert Path(warm["artifacts"]["generated_rust"]).name == \
    "p21_cache_consumer.rs", warm
executed = run(warm["artifacts"]["executable"], cwd=consumer)
assert executed.returncode == 0 and executed.stdout == "7\n", executed

# The metadata's own digest remains valid, but the selected provider rlib
# still contains its original provider-owned marker.
forged, count = re.subn(r"(?m)^compiler [^\n]+$", "compiler forged", original)
assert count == 1
material, count = re.subn(
    r"(?m)^provider_contract_digest sha256:[0-9a-f]{64}\n", "", forged)
assert count == 1
digest = hashlib.sha256(material.encode()).hexdigest()
forged, count = re.subn(
    r"(?m)^provider_contract_digest sha256:[0-9a-f]{64}$",
    "provider_contract_digest sha256:" + digest, forged)
assert count == 1
interface.write_text(forged)
checked = run(compiler, "check", consumer_source, "--json", cwd=consumer,
              provider_path=provider_path)
assert checked.returncode != 0, (checked.stdout, checked.stderr)
assert json.loads(checked.stdout)["error"]["code"] == \
    "SOURCE_FREE_ARTIFACT_CONTRACT_MISMATCH", checked.stdout
built, result = build(consumer, provider_path)
assert built.returncode != 0 and not result["ok"], (built.stdout, built.stderr)
assert result["error"]["code"] == \
    "SOURCE_FREE_ARTIFACT_CONTRACT_MISMATCH", result

# Losing the selected native artifact must also invalidate a warm build.
interface.write_text(original)
rlib.unlink()
checked = run(compiler, "check", consumer_source, "--json", cwd=consumer,
              provider_path=provider_path)
assert checked.returncode != 0, (checked.stdout, checked.stderr)
assert json.loads(checked.stdout)["error"]["code"] == \
    "SOURCE_FREE_PROVIDER_ARTIFACT_MISSING", checked.stdout
built, result = build(consumer, provider_path)
assert built.returncode != 0 and not result["ok"], (built.stdout, built.stderr)
assert result["error"]["code"] == \
    "SOURCE_FREE_PROVIDER_ARTIFACT_MISSING", result

print("Phase 21 source-free external provider warm-cache binding passed.")
