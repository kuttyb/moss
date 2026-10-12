#!/usr/bin/env python3
"""Run Team T benchmarks and prove every path provider uses release artifacts."""

import json
from pathlib import Path
import subprocess
import tomllib


PROJECT = Path(__file__).resolve().parent
REPO = PROJECT.parents[1]
MARGO = REPO / "margo"


def path_dependencies(package: Path) -> list[Path]:
    manifest = tomllib.loads((package / "Moss.toml").read_text(encoding="utf-8"))
    result = []
    for dependency in manifest.get("dependencies", {}).values():
        if isinstance(dependency, dict) and "path" in dependency:
            result.append((package / dependency["path"]).resolve())
    return result


def dependency_closure() -> list[Path]:
    pending = path_dependencies(PROJECT)
    seen = set()
    ordered = []
    while pending:
        package = pending.pop()
        if package in seen:
            continue
        seen.add(package)
        ordered.append(package)
        pending.extend(path_dependencies(package))
    return sorted(ordered)


def assert_release_metadata(package: Path) -> dict[str, object]:
    metadata_files = sorted((package / "build" / "release").glob("*.mossbuild"))
    if not metadata_files:
        raise AssertionError(f"missing release .mossbuild metadata for {package}")
    verified = []
    for metadata_file in metadata_files:
        lines = metadata_file.read_text(encoding="utf-8").splitlines()
        profile_index = lines.index("profile")
        flags_index = lines.index("compile-flags")
        profile = lines[profile_index + 1]
        flags = lines[flags_index + 1 :]
        assert profile == "release", (metadata_file, profile)
        assert "-O" in flags, (metadata_file, flags)
        verified.append(str(metadata_file.relative_to(REPO)))
    return {"package": str(package.relative_to(REPO)), "metadata": verified}


completed = subprocess.run(
    [str(MARGO), "bench", "--release", "--json"],
    cwd=PROJECT,
    text=True,
    capture_output=True,
    check=True,
)
payload = json.loads(completed.stdout)
assert payload["ok"] is True, payload
result = payload["result"]
assert result["profile"] == "release", result
assert "-O" in result["backend_toolchain"]["compile_flags"], result
providers = [assert_release_metadata(package) for package in dependency_closure()]

print(json.dumps({
    "benchmark_profile": result["profile"],
    "benchmarks": result["benchmarks"],
    "provider_count": len(providers),
    "release_providers": providers,
}, sort_keys=True))
