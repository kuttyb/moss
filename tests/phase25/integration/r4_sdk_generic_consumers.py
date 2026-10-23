#!/usr/bin/env python3
"""Three-mode native matrix for the real open generic SDK exports."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile


ROOT = Path(__file__).resolve().parents[3]
MARGO = ROOT / "margo"
TMP = ROOT / "tmp"
COLLECTIONS = ROOT / "sdk/foundations/collections/src/collections.moss"
BYTES = ROOT / "sdk/foundations/bytes/src/bytes.moss"
BYTES_CONSUMER = ROOT / "sdk/foundations/acceptance/repros/source_free_range/src/main.moss"
POISONER_SOURCE = ROOT / "tests/tooling/poison_phase25_generic_callable.cpp"

COLLECTION_CALLS = {
    "consumer_a": ("""module Main
import collections

fn main():
  echo collections.index_of([8, 3, 8], 3)
  echo collections.contains([\"oak\", \"moss\"], \"moss\")
  echo collections.count_equal([1, 3, 1], 1)
""", "1\ntrue\n2\n"),
    "consumer_b": ("""module Main
import collections

fn main():
  echo collections.index_of(Vector[Int](), 5)
  echo collections.contains([\"moss\"], \"cedar\")
  echo collections.count_equal([\"a\", \"b\", \"a\"], \"a\")
""", "-1\nfalse\n2\n"),
}


def run(argv: list[str | Path], cwd: Path, env: dict[str, str], expected: int = 0) -> tuple[str, str]:
    proc = subprocess.run([str(arg) for arg in argv], cwd=cwd, env=env,
                          capture_output=True, text=True, timeout=900)
    combined = proc.stdout + proc.stderr
    assert proc.returncode == expected, (argv, proc.returncode, combined[-8000:])
    return proc.stdout, proc.stderr


def manifest(name: str) -> str:
    return (f'[package]\nname = "{name}"\nversion = "0.1.0"\n\n'
            '[build]\nsource = "src"\n')


def env_for(moss: Path, scratch: Path, mode: str, profile: str, role: str) -> dict[str, str]:
    env = os.environ.copy()
    env.update(MOSS=str(moss), MARGO_HOME=str(scratch / "margo" / mode / profile / role),
               TMPDIR=str(TMP), PYTHONDONTWRITEBYTECODE="1")
    env.pop("MOSS_MODULE_PATH", None)
    env.pop("MOSS_STAGED_ABI", None)
    if mode == "v10":
        env["MOSS_STAGED_ABI"] = "v10"
    return env


def package(root: Path, name: str, source: str, *, module_source: Path | None = None) -> None:
    src = root / "src"
    src.mkdir(parents=True)
    (root / "Moss.toml").write_text(manifest(name), encoding="utf-8")
    (src / "main.moss").write_text(source, encoding="utf-8")
    if module_source:
        shutil.copy2(module_source, src / module_source.name)


def build_args(profile: str) -> list[str]:
    return ["build", "--release"] if profile == "release" else ["build"]


def run_args(profile: str) -> list[str]:
    return ["run", "--release"] if profile == "release" else ["run"]


def build_provider(source: Path, module: str, mode: str, profile: str,
                   scratch: Path, moss: Path, poisoner: Path | None) -> tuple[Path, dict[str, int]]:
    provider = scratch / "provider" / mode / profile / module
    provider.mkdir(parents=True)
    (provider / "src").mkdir()
    shutil.copy2(source, provider / "src" / source.name)
    (provider / "Moss.toml").write_text(manifest(f"r4_{module}_{mode}_{profile}_provider"),
                                            encoding="utf-8")
    env = env_for(moss, scratch, mode, profile, f"provider-{module}")
    run([MARGO, *build_args(profile)], provider, env)
    build_dir = provider / "build" / profile
    interface = build_dir / f"{module}.mossi"
    archive = build_dir / f"lib{module}.rlib"
    assert interface.is_file() and archive.is_file(), (interface, archive)
    header = interface.read_bytes().split(b"binary_contract\n", 1)[0]
    expected_abi = b"native_abi 10\n" if mode == "v10" else b"native_abi 9\n"
    assert expected_abi in header, (module, mode, header[:300])
    artifacts = scratch / "artifacts" / mode / profile / module
    shutil.copytree(build_dir, artifacts)
    poison_counts: dict[str, int] = {}
    if mode == "v10":
        assert poisoner is not None
        names = ("from_range",) if module == "bytes" else ("index_of", "contains", "count_equal")
        for name in names:
            out, err = run([poisoner, artifacts / f"{module}.mossi",
                            artifacts / f"lib{module}.rlib", module, name], ROOT, env)
            found = re.search(rf"removed (\d+) generic_stmt records from {name}", out + err)
            assert found and int(found.group(1)) > 0, (module, name, out, err)
            poison_counts[name] = int(found.group(1))
    shutil.rmtree(provider / "src")
    assert not (provider / "src").exists()
    assert not list(artifacts.rglob("*.moss")), artifacts
    return artifacts, poison_counts


def consumer_source(module: str, source: str) -> Path | None:
    return {"collections": COLLECTIONS, "bytes": BYTES}.get(module) if source == "source" else None


def execute_consumer(module: str, label: str, mode: str, profile: str,
                     scratch: Path, moss: Path, artifacts: Path | None,
                     poison_counts: dict[str, int]) -> dict:
    if module == "collections":
        source, expected = COLLECTION_CALLS[label]
    else:
        source = BYTES_CONSUMER.read_text(encoding="utf-8")
        expected = "329\n"
    app = scratch / "consumer" / mode / profile / module / label
    package(app, f"r4_{module}_{mode}_{profile}_{label}", source,
            module_source=consumer_source(module, mode))
    if module == "bytes":
        (app / "p4.bin").write_bytes(b"ABABC")
    env = env_for(moss, scratch, mode, profile, f"consumer-{module}-{label}")
    if artifacts:
        env["MOSS_MODULE_PATH"] = str(artifacts)
    projected: list[str] = []
    if mode == "v10":
        env["MOSS_PHASE25_CHECKED_BODY_TRACE"] = "1"
        check_out, check_err = run([moss, "check", "src/main.moss", "--json"], app, env)
        assert json.loads(check_out).get("ok") is True, check_out
        projected = re.findall(r"MOSS_CHECKED_BODY_PROJECTED\|([^\n]+)", check_err)
        selected = (("bytes:fn:from_range",) if module == "bytes" else
                    ("collections:fn:index_of", "collections:fn:contains",
                     "collections:fn:count_equal"))
        for callable_name in selected:
            assert any(callable_name in identity and identity.endswith("checked-body-v1")
                       for identity in projected), (callable_name, projected)
    run([MARGO, *build_args(profile)], app, env)
    actual, _ = run([MARGO, *run_args(profile)], app, env)
    assert actual == expected, (module, label, mode, profile, actual, expected)
    return {"module": module, "consumer": label, "mode": mode, "profile": profile,
            "expected": expected, "actual": actual, "provider_source_deleted": mode != "source",
            "checked_projection": mode == "v10", "projected_callables": projected,
            "poisoned_legacy_generic_stmt_counts": poison_counts}


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--moss", type=Path, default=Path(os.environ.get("MOSS", ROOT / "moss")))
    parser.add_argument("--output", type=Path, default=TMP / "r4-sdk-generic-consumers.json")
    args = parser.parse_args()
    moss = args.moss.resolve()
    assert moss.is_file(), moss
    TMP.mkdir(exist_ok=True)
    rows: list[dict] = []
    with tempfile.TemporaryDirectory(prefix="r4-sdk-generics-", dir=TMP) as temp:
        scratch = Path(temp)
        poisoner = scratch / "poison_generic_callable"
        run(["g++", "-std=c++17", "-O0", "-Wall", "-Wextra", "-Werror",
             "-Wno-missing-field-initializers", "-pedantic", "-Isrc",
             POISONER_SOURCE, "-o", poisoner], ROOT, os.environ.copy())
        for mode in ("source", "v9", "v10"):
            for profile in ("debug", "release"):
                for module, source_file in (("collections", COLLECTIONS), ("bytes", BYTES)):
                    artifacts = None
                    poison_counts = {}
                    if mode != "source":
                        artifacts, poison_counts = build_provider(
                            source_file, module, mode, profile, scratch, moss,
                            poisoner if mode == "v10" else None)
                    labels = tuple(COLLECTION_CALLS) if module == "collections" else ("consumer_a",)
                    for label in labels:
                        rows.append(execute_consumer(module, label, mode, profile,
                                                     scratch, moss, artifacts,
                                                     poison_counts))
                    if mode == "v10":
                        assert poison_counts and all(value > 0 for value in poison_counts.values())
        report = {"schema": "phase25-1-r4-real-sdk-generics-v1",
                  "compiler_commit": subprocess.check_output(
                      ["git", "rev-parse", "HEAD"], cwd=ROOT, text=True).strip(),
                  "compiler_executable": str(moss.relative_to(ROOT)) if moss.is_relative_to(ROOT) else str(moss),
                  "compiler_executable_sha256": hashlib.sha256(moss.read_bytes()).hexdigest(),
                  "case_count": len(rows), "cases": rows}
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(json.dumps(report, indent=2, sort_keys=True) + "\n",
                               encoding="utf-8")
        print(f"R4 real SDK generic matrix: {len(rows)} consumer/profile/mode runs PASS")
        print(f"report: {args.output}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
