#!/usr/bin/env python3
"""Prove staged indexed String uses one runtime across source-free Moss crates."""

import json
import os
from pathlib import Path
import shutil
import subprocess


ROOT = Path(__file__).resolve().parents[3]
MOSS = ROOT / "moss"
RUNTIME = ROOT / "runtime" / "moss_runtime"
SCRATCH = ROOT / "tmp" / "phase25-shared-string-linkage"


def project(path: Path, name: str, source: str) -> None:
    (path / "src").mkdir(parents=True)
    (path / "Moss.toml").write_text(
        f'[package]\nname = "{name}"\nversion = "0.1.0"\n'
    )
    (path / "src" / ("provider.moss" if name == "provider_pkg" else "main.moss")).write_text(source)


def build(path: Path, profile: str, provider_artifacts: Path | None = None,
          runtime: Path = RUNTIME) -> subprocess.CompletedProcess[str]:
    env = dict(os.environ)
    env.update(MOSS_STAGED_ABI="v10", MOSS_STAGED_STRING_REPR="indexed-v1",
               MOSS_RUNTIME_SOURCE_DIR=str(runtime))
    if provider_artifacts:
        env["MOSS_MODULE_PATH"] = str(provider_artifacts)
    return subprocess.run(
        [str(MOSS), "build", "--json", *(["--release"] if profile == "release" else [])],
        cwd=path, env=env, text=True, capture_output=True, check=False,
    )


def accepted(result: subprocess.CompletedProcess[str]) -> None:
    assert result.returncode == 0, result.stdout + result.stderr
    assert json.loads(result.stdout)["ok"], result.stdout


def run(path: Path, profile: str, expected: str) -> None:
    executable = path / "build" / profile / path.name
    result = subprocess.run([str(executable)], text=True, capture_output=True, check=False)
    assert result.returncode == 0, result.stderr
    assert result.stdout == expected, result.stdout


shutil.rmtree(SCRATCH, ignore_errors=True)
SCRATCH.mkdir(parents=True)
provider = SCRATCH / "provider_pkg"
project(provider, "provider_pkg",
        'module provider\nexport fn shout(s: String) -> String:\n  return s + "!"\n')
consumers = [("consumer_a", "é"), ("consumer_b", "世界")]
for name, word in consumers:
    project(SCRATCH / name, name,
            f'module app\nimport provider\nfn main():\n  echo provider.shout("{word}")\n')

for profile in ("debug", "release"):
    accepted(build(provider, profile))
    artifacts = provider / "build" / profile
    interface = (artifacts / "provider.mossi").read_text()
    assert "string_representation indexed-v1" in interface
    assert "runtime moss_runtime 0.1.0 sha256:" in interface
    generated = (artifacts / "provider.rs").read_text()
    assert "pub use moss_runtime::string::" in generated
    assert "pub struct MossString" not in generated

# Neither consumer may read or recompile provider Moss source.
(provider / "src" / "provider.moss").unlink()
assert not list((provider / "src").glob("*.moss"))

for profile in ("debug", "release"):
    artifacts = provider / "build" / profile
    for name, word in consumers:
        consumer = SCRATCH / name
        accepted(build(consumer, profile, artifacts))
        run(consumer, profile, word + "!\n")
    print(f"shared indexed String {profile}: two source-free consumers passed")

# A different runtime source must fail at the Moss contract boundary, even
# when the consumer has a warm native artifact from the original runtime.
changed_runtime = SCRATCH / "changed_runtime"
shutil.copytree(RUNTIME, changed_runtime, ignore=shutil.ignore_patterns("build"))
with (changed_runtime / "src" / "string.rs").open("a") as output:
    output.write("\n// Change the runtime source identity for this negative probe.\n")
rejected = build(SCRATCH / "consumer_a", "debug",
                 provider / "build" / "debug", changed_runtime)
assert rejected.returncode != 0, rejected.stdout + rejected.stderr
diagnostic = json.loads(rejected.stdout)["error"]
assert diagnostic["code"] == "SOURCE_FREE_ARTIFACT_CONTRACT_MISMATCH", diagnostic
assert "shared moss_runtime artifact" in diagnostic["message"], diagnostic
print("shared indexed String runtime mismatch rejected before native linking")
