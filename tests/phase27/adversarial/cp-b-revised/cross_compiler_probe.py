#!/usr/bin/env python3
"""Recompute Phase 27 .mossi identity-derived fields under baseline identity.

Usage: python3 tests/tooling/check_phase27_mossi_baseline.py CANDIDATE BASELINE
The same absolute compiler path and project path are used for both builds.
"""
import hashlib
import importlib.util
from importlib.machinery import SourceFileLoader
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import time

ROOT = Path(__file__).resolve().parents[4]
CANDIDATE = Path(sys.argv[1]).resolve()
BASELINE = Path(sys.argv[2]).resolve()
WORK = ROOT / "tmp" / "phase27-mossi-baseline" / str(time.time_ns())
LIB = WORK / "library"
APP = WORK / "app"
ACTIVE = WORK / "active-moss"
MARGO_PATH = ROOT / "margo"
RUNTIME = ROOT / "runtime" / "moss_runtime"


def load_margo():
    spec = importlib.util.spec_from_loader(
        "phase27_margo", SourceFileLoader("phase27_margo", str(MARGO_PATH)))
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def run_build(env):
    result = subprocess.run([str(MARGO_PATH), "build", "--json"], cwd=APP,
                            env=env, text=True, capture_output=True)
    if result.returncode:
        raise SystemExit(result.stdout + result.stderr)
    if not json.loads(result.stdout).get("ok"):
        raise SystemExit("Margo reported an unsuccessful build")


def rewrite_derived(data, fingerprint):
    marker = b"execution_body_payload\n"
    payload_start = data.find(marker)
    if payload_start < 0:
        raise AssertionError("missing execution-body payload")
    payload_start += len(marker)
    end_marker = b"execution_body_end\n"
    payload_end = data.find(end_marker, payload_start)
    if payload_end < 0:
        raise AssertionError("missing execution-body end marker")
    payload = data[payload_start:payload_end]
    payload, count = re.subn(
        rb"(?m)^package_fingerprint [0-9a-f]{64}$",
        f"package_fingerprint {fingerprint}".encode(), payload, count=1)
    if count != 1:
        raise AssertionError("expected exactly one package fingerprint")
    body_hash = hashlib.sha256(payload).hexdigest()
    rewritten = data[:payload_start] + payload + data[payload_end:]
    rewritten, count = re.subn(
        rb"(?m)^execution_body_hash sha256:[0-9a-f]{64}$",
        f"execution_body_hash sha256:{body_hash}".encode(), rewritten, count=1)
    if count != 1:
        raise AssertionError("expected exactly one execution body hash")
    return rewritten, body_hash


def main():
    for path in (LIB / "src", APP / "src"):
        path.mkdir(parents=True, exist_ok=True)
    (LIB / "Moss.toml").write_text(
        '[package]\nname="fp_library"\nversion="0.1.0"\n'
        '\n[build]\nsource="src"\n', encoding="utf-8")
    (LIB / "src" / "fp_library.moss").write_text(
        "module fp_library\n"
        "export fn generic(value):\n  return value + 1\n"
        "export fn concrete(value: Int) -> Int:\n  return value * 2 + 1\n",
        encoding="utf-8")
    (APP / "Moss.toml").write_text(
        '[package]\nname="fp_app"\nversion="0.1.0"\n'
        '\n[build]\nsource="src"\n'
        '\n[dependencies]\nfp_library = { path = "../library" }\n',
        encoding="utf-8")
    (APP / "src" / "main.moss").write_text(
        "module fp_app\nimport fp_library\nfn main():\n"
        "  echo fp_library.generic(3)\n"
        "  echo fp_library.concrete(4)\n", encoding="utf-8")

    env = os.environ.copy()
    env.update({"MOSS": str(ACTIVE), "MOSS_EST": "off",
                "MOSS_RUNTIME_SOURCE_DIR": str(RUNTIME)})
    artifact_paths = {
        "library": LIB / "build" / "debug" / "fp_library.mossi",
        "app": APP / "build" / "debug" / "fp_app.mossi",
    }
    outputs, identities = {}, {}
    for name, compiler in (("baseline", BASELINE), ("candidate", CANDIDATE)):
        shutil.copy2(compiler, ACTIVE)
        for package_root in (LIB, APP):
            shutil.rmtree(package_root / "build", ignore_errors=True)
        env["MOSS"] = str(ACTIVE)
        os.environ.update({"MOSS": str(ACTIVE),
                           "MOSS_RUNTIME_SOURCE_DIR": str(RUNTIME)})
        margo = load_margo()
        identities[name] = margo.package_toolchain_identity(False)
        run_build(env)
        outputs[name] = {key: path.read_bytes()
                         for key, path in artifact_paths.items()}

    baseline_identity = identities["baseline"]
    if any(identities["baseline"][key] != identities["candidate"][key]
           for key in identities["baseline"] if key != "moss_sha256"):
        raise AssertionError("toolchain identities differ beyond Moss binary hash")
    if identities["baseline"]["moss_sha256"] == identities["candidate"]["moss_sha256"]:
        raise AssertionError("expected distinct compiler binary identities")

    os.environ.update({"MOSS": str(ACTIVE), "MOSS_EST": "off",
                       "MOSS_RUNTIME_SOURCE_DIR": str(RUNTIME)})
    margo = load_margo()
    root, ordered = margo.resolve_graph(APP)
    fingerprints = {}
    candidate_fingerprints = {}
    for package in ordered:
        fingerprints[package.data["name"]] = margo.package_fingerprint(
            package, False, False, baseline_identity, ordered, False)
        candidate_fingerprints[package.data["name"]] = margo.package_fingerprint(
            package, False, False, identities["candidate"], ordered, False)

    result = {}
    for package_name, filename in (("fp_library", "library"), ("fp_app", "app")):
        expected = fingerprints[package_name]
        baseline_data = outputs["baseline"][filename]
        candidate_data = outputs["candidate"][filename]
        recorded_baseline = next(
            line.split(b" ", 1)[1].decode()
            for line in baseline_data.splitlines()
            if line.startswith(b"package_fingerprint "))
        if recorded_baseline != expected:
            raise AssertionError(
                f"baseline toolchain identity recomputed {expected}, "
                f"but baseline output records {recorded_baseline}")
        recorded_candidate = next(
            line.split(b" ", 1)[1].decode()
            for line in candidate_data.splitlines()
            if line.startswith(b"package_fingerprint "))
        expected_candidate = candidate_fingerprints[package_name]
        if recorded_candidate != expected_candidate:
            raise AssertionError(
                f"candidate package_toolchain_identity fingerprint mismatch for "
                f"{package_name}: expected {expected_candidate}, "
                f"recorded {recorded_candidate}")
        if recorded_candidate == recorded_baseline:
            raise AssertionError(
                f"compiler-bound package fingerprints unexpectedly match for {package_name}")
        normalized_baseline, baseline_body_hash = rewrite_derived(
            baseline_data, expected)
        normalized_candidate, candidate_body_hash = rewrite_derived(
            candidate_data, expected)
        if normalized_baseline != normalized_candidate:
            raise AssertionError(f"non-derived .mossi bytes differ for {filename}")
        result[filename] = {
            "baseline_raw_sha256": hashlib.sha256(baseline_data).hexdigest(),
            "candidate_raw_sha256": hashlib.sha256(candidate_data).hexdigest(),
            "recomputed_baseline_package_fingerprint": expected,
            "recomputed_execution_body_hash": candidate_body_hash,
            "candidate_recorded_package_fingerprint": recorded_candidate,
            "candidate_recomputed_package_fingerprint": expected_candidate,
            "baseline_execution_body_hash": baseline_body_hash,
            "normalized_bytes_equal": True,
        }

    report = {"baseline_toolchain_identity": baseline_identity,
              "candidate_toolchain_identity": identities["candidate"],
              "results": result}
    output = WORK / "report.json"
    output.write_text(json.dumps(report, indent=2, sort_keys=True) + "\n")
    print(json.dumps(report, indent=2, sort_keys=True))
    print(f"report: {output.relative_to(ROOT)}")


if __name__ == "__main__":
    main()
