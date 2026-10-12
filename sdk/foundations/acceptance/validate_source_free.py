#!/usr/bin/env python3
"""Exercise imported generic collections with the provider's Moss source removed."""

import argparse
import json
import os
import shutil
import subprocess
from pathlib import Path


def invoke(command, cwd, env):
    result = subprocess.run(command, cwd=cwd, env=env, text=True,
                            stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    if result.returncode:
        raise RuntimeError(f"{command}: exit {result.returncode}\n{result.stdout}\n{result.stderr}")
    return result.stdout


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--foundations-root", type=Path, required=True)
    parser.add_argument("--workdir", type=Path, required=True)
    parser.add_argument("--moss", type=Path, required=True)
    args = parser.parse_args()
    root = args.foundations_root.resolve()
    workdir = args.workdir.resolve()
    repository_tmp = Path(__file__).resolve().parents[3] / "tmp"
    if not workdir.is_relative_to(repository_tmp) or workdir == repository_tmp:
        parser.error("workdir must be a child of this repository's tmp directory")
    moss = str(args.moss.resolve())
    if workdir.exists():
        shutil.rmtree(workdir)
    provider = workdir / "provider"
    consumer = workdir / "consumer"
    (provider / "src").mkdir(parents=True)
    (consumer / "src").mkdir(parents=True)
    shutil.copy2(root / "collections/src/collections.moss", provider / "src/collections.moss")
    source = Path(__file__).parent / "repros/source_free_generics/src/main.moss"
    shutil.copy2(source, consumer / "src/main.moss")
    (provider / "Moss.toml").write_text('[project]\nname="foundation_provider"\nversion="0.1.0"\n')
    (consumer / "Moss.toml").write_text('[project]\nname="foundation_consumer"\nversion="0.1.0"\n')
    environment = os.environ.copy()
    environment["TMPDIR"] = str(workdir)
    provider_build = json.loads(invoke([moss, "build", "--json"], provider, environment))
    assert provider_build["ok"]
    provider_interface = provider / "build/debug/collections.mossi"
    provider_rlib = provider / "build/debug/libcollections.rlib"
    assert provider_interface.exists() and provider_rlib.exists()
    (provider / "src/collections.moss").rename(provider / "src/collections.removed")
    assert not (provider / "src/collections.moss").exists()
    environment["MOSS_MODULE_PATH"] = str(provider / "build/debug")
    consumer_build = json.loads(invoke([moss, "build", "--json"], consumer, environment))
    assert consumer_build["ok"]
    executable = consumer_build["result"]["artifacts"]["executable"]
    output = invoke([executable], consumer, environment)
    assert output == "source-free generic ok\n", output

    range_provider = workdir / "range_provider"
    range_consumer = workdir / "range_consumer"
    (range_provider / "src").mkdir(parents=True)
    (range_consumer / "src").mkdir(parents=True)
    shutil.copy2(root / "bytes/src/bytes.moss", range_provider / "src/bytes.moss")
    range_source = Path(__file__).parent / "repros/source_free_range/src/main.moss"
    shutil.copy2(range_source, range_consumer / "src/main.moss")
    (range_provider / "Moss.toml").write_text('[project]\nname="byte_provider"\nversion="0.1.0"\n')
    (range_consumer / "Moss.toml").write_text('[project]\nname="byte_consumer"\nversion="0.1.0"\n')
    (range_consumer / "p4.bin").write_bytes(b"ABABC")
    environment.pop("MOSS_MODULE_PATH")
    built = json.loads(invoke([moss, "build", "--json"], range_provider, environment))
    assert built["ok"]
    byte_interface = range_provider / "build/debug/bytes.mossi"
    byte_rlib = range_provider / "build/debug/libbytes.rlib"
    assert byte_interface.exists() and byte_rlib.exists()
    (range_provider / "src/bytes.moss").rename(range_provider / "src/bytes.removed")
    assert not (range_provider / "src/bytes.moss").exists()
    environment["MOSS_MODULE_PATH"] = str(range_provider / "build/debug")
    rebuilt = json.loads(invoke([moss, "build", "--json"], range_consumer, environment))
    assert rebuilt["ok"]
    range_executable = rebuilt["result"]["artifacts"]["executable"]
    range_output = invoke([range_executable], range_consumer, environment)
    assert range_output == "329\n", range_output
    print(json.dumps({"source_removed": True, "provider_abi": "v9",
                      "generic_output": output.strip(), "range_output": range_output.strip(),
                      "native": "pass"}, sort_keys=True))


if __name__ == "__main__":
    main()
