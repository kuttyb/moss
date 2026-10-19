#!/usr/bin/env python3
"""Build every primary SDK package under the staged ABI in both native profiles."""

from __future__ import annotations

import hashlib
import json
import os
from pathlib import Path
import subprocess


ROOT = Path(__file__).resolve().parents[3]
MARGO = ROOT / "margo"
MOSS = ROOT / "moss"

# Acceptance projects, examples, negative repros and host-I/O feasibility
# projects are separate test fixtures. Keep this inventory explicit so a new
# primary SDK package cannot silently be omitted from the gate.
PRIMARY_PACKAGES = (
    "sdk/binary",
    "sdk/cbor",
    "sdk/csv",
    "sdk/datastructures",
    "sdk/encoding",
    "sdk/fmt",
    "sdk/foundations/algorithms",
    "sdk/foundations/bytes",
    "sdk/foundations/collections",
    "sdk/foundations/core",
    "sdk/fs",
    "sdk/json",
    "sdk/ndarray",
    "sdk/parse",
    "sdk/path",
    "sdk/tables",
    "sdk/text",
)
EXCLUDED_PARTS = {"acceptance", "examples", "feasibility", "repros", "tests"}


def discovered_primary_packages() -> set[str]:
    packages = set()
    for manifest in (ROOT / "sdk").rglob("Moss.toml"):
        package = manifest.parent.relative_to(ROOT)
        if any(part in EXCLUDED_PARTS or part.endswith("acceptance")
               for part in package.parts):
            continue
        packages.add(package.as_posix())
    return packages


def run_build(package: str, profile: str) -> str:
    environment = dict(os.environ)
    environment.update(MOSS=str(MOSS), MOSS_STAGED_ABI="v10",
                       MARGO_HOME=str(ROOT / "tmp/phase25-sdk-matrix-home"),
                       TMPDIR=str(ROOT / "tmp"), PYTHONDONTWRITEBYTECODE="1")
    command = [str(MARGO), "build", "--json"]
    if profile == "release":
        command.append("--release")
    result = subprocess.run(command, cwd=ROOT / package, env=environment,
                            text=True, capture_output=True, check=False)
    assert result.returncode == 0, (package, profile, result.stdout, result.stderr)
    report = json.loads(result.stdout)
    assert report.get("ok") is True, (package, profile, report)
    artifacts = report["artifacts"]["artifacts"]
    interfaces = artifacts["module_interfaces"]
    assert interfaces, (package, profile, "no .mossi artifacts")
    digests = []
    for name in interfaces:
        interface = Path(name)
        assert interface.is_file() and interface.is_relative_to(ROOT / package), name
        content = interface.read_bytes()
        assert b"schema_version 10\nnative_abi 10\n" in content, (package, profile, name)
        digests.append(hashlib.sha256(content).hexdigest())
    for name in artifacts["module_rlibs"]:
        rlib = Path(name)
        assert rlib.is_file() and rlib.is_relative_to(ROOT / package), name
    return hashlib.sha256("\n".join(sorted(digests)).encode()).hexdigest()


def main() -> None:
    assert MOSS.is_file() and MARGO.is_file(), "build repository-local moss and margo first"
    assert discovered_primary_packages() == set(PRIMARY_PACKAGES), (
        "SDK inventory changed", sorted(discovered_primary_packages() ^ set(PRIMARY_PACKAGES)))
    (ROOT / "tmp").mkdir(exist_ok=True)
    passed = 0
    for profile in ("debug", "release"):
        for package in PRIMARY_PACKAGES:
            digest = run_build(package, profile)
            print(f"G5 SDK {profile} {package} PASS sha256:{digest}", flush=True)
            passed += 1
    print(f"G5 SDK ABI-v10 matrix: {passed}/{2 * len(PRIMARY_PACKAGES)} builds, "
          f"{len(PRIMARY_PACKAGES)} packages, 2 profiles")


if __name__ == "__main__":
    main()
