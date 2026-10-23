#!/usr/bin/env python3
"""Exercise staged-v10 deferred member forwarding and hostile relay records."""

import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile


ROOT = Path(__file__).resolve().parents[2]
MOSS = Path(os.environ.get("MOSS", ROOT / "moss")).resolve()
MARGO = ROOT / "margo"
TMP = ROOT / "tmp"
TMP.mkdir(exist_ok=True)


def run(argv, *, cwd=ROOT, env=None, ok=True):
    result = subprocess.run([str(item) for item in argv], cwd=cwd, env=env,
                            capture_output=True, text=True, timeout=300)
    if ok:
        assert result.returncode == 0, (argv, result.stdout, result.stderr)
    return result


def package(path, name, source):
    (path / "src").mkdir(parents=True, exist_ok=True)
    (path / "Moss.toml").write_text(
        f'[package]\nname = "{name}"\nversion = "0.1.0"\n\n'
        '[build]\nsource = "src"\n', encoding="utf-8")
    (path / "src" / "main.moss").write_text(source, encoding="utf-8")


def env_for(scratch, artifacts=None, **extra):
    env = os.environ.copy()
    for key in list(env):
        if key == "MOSS_STAGED_ABI" or key.startswith("MOSS_PHASE25_"):
            env.pop(key, None)
    env.update(TMPDIR=str(TMP), MOSS=str(MOSS), MOSS_STAGED_ABI="v10",
               MARGO_HOME=str(scratch / "margo-home"))
    if artifacts:
        env["MOSS_MODULE_PATH"] = str(artifacts)
    env.update(extra)
    return env


POSITIVE_SOURCE = """module MethodConsumer
import MethodProvider
type Data:
  value: Int
  fn custom_action():
    assert(value == 40)
    return value + 2
fn main():
  echo MethodProvider.invoke_action(Data(value: 40))
"""

NEGATIVE_SOURCE = """module MethodConsumer
import MethodProvider
type Data:
  id: Int
fn main():
  d = Data(id: 1)
  echo MethodProvider.invoke_action(d)
"""


provider_fixture = ROOT / "tests/phase25/integration/fixtures/abi_v10/" \
    "scenario07_missing_method/provider"
consumer_fixture = ROOT / "tests/phase25/integration/fixtures/abi_v10/" \
    "scenario07_missing_method/consumer"

parity_temp = tempfile.TemporaryDirectory(
    prefix="phase25-r4-member-parity-", dir=TMP)
scratch = Path(parity_temp.name)

for profile in ("debug", "release"):
    profile_args = ["--release"] if profile == "release" else []
    # Source-backed provider and consumer modules share one source package.
    source_project = scratch / f"source-backed-{profile}"
    (source_project / "src").mkdir(parents=True)
    (source_project / "Moss.toml").write_text(
        f'[package]\nname = "relay_source_{profile}"\nversion = "0.1.0"\n\n'
        '[build]\nsource = "src"\n', encoding="utf-8")
    shutil.copy2(provider_fixture / "src/provider.moss",
                 source_project / "src/provider.moss")
    (source_project / "src/main.moss").write_text(
        NEGATIVE_SOURCE, encoding="utf-8")
    source_env = env_for(scratch / f"source-home-{profile}")
    source_env.pop("MOSS_STAGED_ABI", None)
    source_result = run([MARGO, "build", *profile_args, "--json"],
                        cwd=source_project, env=source_env, ok=False)
    source_output = source_result.stdout + source_result.stderr
    assert "METHOD_NOT_FOUND" in source_output and \
        "custom_action" in source_output and "main.moss" in source_output and \
        "provider.moss" in source_output, source_output
    print(f"PASS source-backed {profile} METHOD_NOT_FOUND consumer/provider provenance",
          flush=True)

    # ABI-v9 control uses the real provider interface with its source removed.
    v9_provider = scratch / f"v9-provider-{profile}"
    shutil.copytree(provider_fixture, v9_provider)
    v9_provider_env = env_for(scratch / f"v9-provider-home-{profile}")
    v9_provider_env.pop("MOSS_STAGED_ABI", None)
    run([MARGO, "build", *profile_args], cwd=v9_provider,
        env=v9_provider_env)
    v9_artifacts = scratch / f"v9-artifacts-{profile}"
    shutil.copytree(v9_provider / "build" / profile, v9_artifacts)
    shutil.rmtree(v9_provider)
    v9_consumer = scratch / f"v9-consumer-{profile}"
    shutil.copytree(consumer_fixture, v9_consumer)
    manifest = v9_consumer / "Moss.toml"
    manifest_text = manifest.read_text(encoding="utf-8")
    manifest.write_text(manifest_text.split("[dependencies]")[0].rstrip() + "\n",
                        encoding="utf-8")
    v9_env = env_for(scratch / f"v9-consumer-home-{profile}", v9_artifacts)
    v9_env.pop("MOSS_STAGED_ABI", None)
    checked = run([MARGO, "build", *profile_args, "--json"],
                  cwd=v9_consumer, env=v9_env, ok=False)
    output = checked.stdout + checked.stderr
    assert "METHOD_NOT_FOUND" in output and "custom_action" in output and \
        "main.moss" in output and "provider.moss" in output, output
    print(f"PASS ABI-v9 source-free {profile} METHOD_NOT_FOUND consumer/provider provenance",
          flush=True)
parity_temp.cleanup()


with tempfile.TemporaryDirectory(prefix="phase25-r4-member-relay-",
                                 dir=TMP) as temporary:
    scratch = Path(temporary)
    mutator = scratch / "mutate_member_relay"
    poisoner = scratch / "poison_legacy"
    for source, target in (
            ("tests/tooling/mutate_phase25_r4_member_relay.cpp", mutator),
            ("tests/tooling/check_phase25_h3_poison_legacy.cpp", poisoner)):
        run(["g++", "-std=c++17", "-O0", "-Wall", "-Wextra", "-Werror",
             "-Wno-missing-field-initializers", "-pedantic", "-Isrc",
             source, "-o", target])

    provider_fixture = ROOT / "tests/phase25/integration/fixtures/abi_v10/" \
        "scenario07_missing_method/provider"
    provider = scratch / "provider"
    shutil.copytree(provider_fixture, provider)
    provider_env = env_for(scratch / "provider-home")
    run([MARGO, "build"], cwd=provider, env=provider_env)
    artifacts = scratch / "provider-artifacts"
    shutil.copytree(provider / "build/debug", artifacts)
    run([poisoner, artifacts / "MethodProvider.mossi",
         artifacts / "libMethodProvider.rlib", "MethodProvider", "2",
         "member-relay"])
    assert b"generic_stmt " not in (artifacts / "MethodProvider.mossi").read_bytes()
    shutil.rmtree(provider)
    assert not list(scratch.rglob("provider/src/*.moss"))

    positive = scratch / "positive-consumer"
    package(positive, "relay_positive_consumer", POSITIVE_SOURCE)
    positive_env = env_for(scratch / "positive-home", artifacts)
    run([MARGO, "build"], cwd=positive, env=positive_env)
    executed = run([MARGO, "run"], cwd=positive, env=positive_env)
    assert "42\n" in executed.stdout, executed.stdout
    print("PASS source-free ABI-v10 concrete custom_action result/effect control",
          flush=True)

    hostile_consumer = scratch / "hostile-consumer"
    package(hostile_consumer, "relay_hostile_consumer", POSITIVE_SOURCE)
    for mode in ("member-identity", "result-type", "effect",
                 "private-dependency", "provenance"):
        altered = scratch / f"hostile-{mode}"
        shutil.copytree(artifacts, altered)
        run([mutator, altered / "MethodProvider.mossi",
             altered / "libMethodProvider.rlib", mode, "MethodProvider"])
        env = env_for(scratch / f"home-{mode}", altered,
                      MOSS_PHASE25_CHECKED_BODY_TRACE="1")
        rejected = run([MOSS, "check", "src/main.moss", "--json"],
                       cwd=hostile_consumer, env=env, ok=False)
        error = json.loads(rejected.stdout)["error"]
        assert error["code"].startswith("SOURCE_FREE_"), (mode, error)
        print(f"PASS member relay hostile {mode}: {error['code']}", flush=True)

print("PASS R4 deferred-member relay and hostile gate")
