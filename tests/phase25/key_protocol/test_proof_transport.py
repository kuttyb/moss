#!/usr/bin/env python3
"""Staged ABI-v10 nominal key proof transport across source-backed packages."""

import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile


ROOT = Path(__file__).resolve().parents[3]
HERE = Path(__file__).resolve().parent
MOSS = ROOT / "moss"
MARGO = ROOT / "margo"


def run(args, cwd, env):
    result = subprocess.run(
        [str(arg) for arg in args], cwd=cwd, env=env, text=True,
        stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=120,
    )
    if result.returncode:
        raise AssertionError(
            f"{args} failed in {cwd}:\n{result.stdout}\n{result.stderr}"
        )
    return result.stdout


def expected(fixture):
    return (fixture / "EXPECTED").read_text().partition("\n---\n")[2].strip()


def proof_ids(wire_tool, interface, env):
    ids = run([wire_tool, "inspect", interface], ROOT, env).splitlines()
    if len(ids) != 1 or not ids[0].startswith("sha256:"):
        raise AssertionError(f"expected one canonical proof in {interface}: {ids}")
    return ids


def operation_ids(rust, key_type):
    pattern = re.compile(
        r"// Moss key operation specialization: (map|set)\.(\w+)\|" +
        re.escape(key_type) +
        r" proof=(sha256:[0-9a-f]{64}) id=(specialization-v2:sha256:[0-9a-f]{64})"
    )
    return {(family, operation): (proof, identity)
            for family, operation, proof, identity in pattern.findall(rust.read_text())}


def build(package, env):
    result = json.loads(run([MARGO, "build", "--moss-json"], package, env))
    if not result["ok"]:
        raise AssertionError(f"build failed for {package}: {result}")


def main():
    scratch = ROOT / "tmp"
    scratch.mkdir(exist_ok=True)
    env = dict(os.environ, MOSS_STAGED_ABI="v10", TMPDIR=str(scratch))
    with tempfile.TemporaryDirectory(prefix="key-proof-", dir=scratch) as directory:
        work = Path(directory)
        wire_tool = work / "proof_wire_tool"
        run([
            "g++", "-std=c++17", "-O0", "-I", ROOT / "src",
            HERE / "proof_wire_tool.cpp", "-o", wire_tool,
        ], ROOT, env)

        fixture = HERE / "03-imported-nominal-keys-source-free"
        copied = work / "03"
        shutil.copytree(fixture, copied)
        provider, app = copied / "keyprov", copied / "app"
        build(provider, env)
        provider_artifacts = provider / "build/debug"
        provider_interface = provider_artifacts / "keyprov.mossi"
        canonical = proof_ids(wire_tool, provider_interface, env)
        source_free_env = dict(env, MOSS_MODULE_PATH=str(provider_artifacts))
        checked = json.loads(run(
            [MOSS, "check", "src/main.moss", "--json"], app, source_free_env
        ))
        if not checked["ok"]:
            raise AssertionError(f"source-free check failed: {checked}")
        build(app, source_free_env)
        assert run([app / "build/debug/nominalapp"], app, source_free_env).strip() == expected(fixture)
        assert proof_ids(wire_tool, app / "build/debug/app.mossi", env) == canonical
        operations = operation_ids(app / "build/debug/app.rs", "keyprov__Name")
        assert operations[("map", "insert")][0] == canonical[0]
        assert operations[("map", "get")][0] == canonical[0]

        # Re-encode the proof and reseal the selected rlib marker, so the exact
        # method-hash check must reject this mutation before native linking.
        mutant = provider / "mutant"
        mutant.mkdir()
        mutated_interface = mutant / "keyprov.mossi"
        old_digest, new_digest = run([
            wire_tool, "tamper", provider_interface, mutated_interface,
        ], ROOT, env).splitlines()
        original_rlib = (provider_artifacts / "libkeyprov.rlib").read_bytes()
        old, new = old_digest.encode(), new_digest.encode()
        if original_rlib.count(old) < 1 or len(old) != len(new):
            raise AssertionError("selected rlib has no ABI-v10 digest marker")
        (mutant / "libkeyprov.rlib").write_bytes(original_rlib.replace(old, new))
        result = subprocess.run(
            [str(MOSS), "check", "src/main.moss", "--json"], cwd=app,
            env=dict(env, MOSS_MODULE_PATH=str(mutant)), text=True,
            capture_output=True, timeout=120,
        )
        diagnostic = json.loads(result.stdout)
        if result.returncode == 0 or diagnostic.get("error", {}).get("code") != "KEY_PROOF_INVALID":
            raise AssertionError(f"tampered proof did not fail before link: {result.stdout} {result.stderr}")

        fixture = HERE / "04-two-consumers-shared-map"
        copied = work / "04"
        shutil.copytree(fixture, copied)
        registry = copied / "registry"
        build(registry, env)
        registry_artifacts = registry / "build/debug"
        canonical = proof_ids(wire_tool, registry_artifacts / "registry.mossi", env)
        consumer_artifacts = [registry_artifacts]
        independent_operation_ids = []
        for consumer in ("alpha", "beta"):
            package = copied / consumer
            build(package, dict(env, MOSS_MODULE_PATH=":".join(map(str, consumer_artifacts))))
            artifacts = package / "build/debug"
            assert proof_ids(wire_tool, artifacts / f"{consumer}.mossi", env) == canonical
            operations = operation_ids(artifacts / f"{consumer}.rs", "registry__Name")
            assert operations[("map", "get")][0] == canonical[0]
            independent_operation_ids.append(operations[("map", "get")][1])
            consumer_artifacts.append(artifacts)
        app = copied / "app"
        app_env = dict(env, MOSS_MODULE_PATH=":".join(map(str, consumer_artifacts)))
        build(app, app_env)
        assert run([app / "build/debug/sharedapp"], app, app_env).strip() == expected(fixture)
        assert proof_ids(wire_tool, app / "build/debug/app.mossi", env) == canonical
        assert independent_operation_ids[0] == independent_operation_ids[1]

        mutant_alpha = copied / "alpha" / "mutant-alpha"
        mutant_alpha.mkdir()
        alpha_artifacts = copied / "alpha/build/debug"
        old_digest, new_digest = run([
            wire_tool, "tamper", alpha_artifacts / "alpha.mossi",
            mutant_alpha / "alpha.mossi",
        ], ROOT, env).splitlines()
        alpha_rlib = (alpha_artifacts / "libalpha.rlib").read_bytes()
        old, new = old_digest.encode(), new_digest.encode()
        if alpha_rlib.count(old) < 1:
            raise AssertionError("alpha rlib has no ABI-v10 digest marker")
        (mutant_alpha / "libalpha.rlib").write_bytes(alpha_rlib.replace(old, new))
        conflict_env = dict(env, MOSS_MODULE_PATH=":".join(map(str, [
            registry_artifacts, mutant_alpha, copied / "beta/build/debug"
        ])))
        result = subprocess.run(
            [str(MOSS), "check", "src/main.moss", "--json"], cwd=app,
            env=conflict_env, text=True, capture_output=True, timeout=120,
        )
        diagnostic = json.loads(result.stdout)
        if result.returncode == 0 or diagnostic.get("error", {}).get("code") != "KEY_PROOF_COHERENCE_MISMATCH":
            raise AssertionError(f"independent proof mismatch was accepted: {result.stdout} {result.stderr}")

        both = work / "map_and_set.rs"
        run([MOSS, HERE / "08-map-and-set-same-protocol/case.moss",
             "-o", both], ROOT, env)
        both_operations = operation_ids(both, "Name")
        assert both_operations[("map", "insert")][0] == both_operations[("set", "insert")][0]
        assert both_operations[("map", "insert")][1] != both_operations[("set", "insert")][1]
        float_operations = {}
        for family, fixture_name in (("map", "01-float-keys-map"),
                                     ("set", "02-float-keys-set")):
            rust = work / f"float-{family}.rs"
            run([MOSS, HERE / fixture_name / "case.moss", "-o", rust], ROOT, env)
            float_operations[family] = operation_ids(rust, "float")[(family, "insert")]
        assert float_operations["map"][0] == float_operations["set"][0]
        assert float_operations["map"][1] != float_operations["set"][1]
        float_map_rust = (work / "float-map.rs").read_text()
        assert "moss_runtime::MossMap<f64," in float_map_rust
        assert ".insert_with(moss_float_hash(__moss_key)" in float_map_rust
        assert "|a, b| moss_float_isequal(*a, *b)" in float_map_rust
    print("PASS: one proof ID across source-backed consumers; resealed tamper -> KEY_PROOF_INVALID / KEY_PROOF_COHERENCE_MISMATCH")


if __name__ == "__main__":
    main()
