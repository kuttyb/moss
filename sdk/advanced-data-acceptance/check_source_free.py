#!/usr/bin/env python3
"""Build and execute the compiled-only ABI-v9 advanced-data consumer."""

import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile


PROJECT = Path(__file__).resolve().parent
REPO = PROJECT.parents[1]
MOSS = REPO / "moss"
MARGO = REPO / "margo"
SCRATCH = REPO / "tmp"
CONSUMER = PROJECT / "fixtures" / "source_free_consumer.moss"

# Dependency order is explicit. No artifact discovery glob is permitted here.
PROVIDERS = (
    ("core", REPO / "sdk" / "foundations" / "core"),
    ("text", REPO / "sdk" / "text"),
    ("parse", REPO / "sdk" / "parse"),
    ("fmt", REPO / "sdk" / "fmt"),
    ("csv", REPO / "sdk" / "csv"),
    ("bytes", REPO / "sdk" / "foundations" / "bytes"),
    ("encoding", REPO / "sdk" / "encoding"),
    ("math", REPO / "packages" / "math"),
    ("linalg", REPO / "packages" / "linalg"),
    ("ndarray", REPO / "sdk" / "ndarray"),
    ("datastructures", REPO / "sdk" / "datastructures"),
    ("tables", REPO / "sdk" / "tables"),
    ("binary", REPO / "sdk" / "binary"),
    ("cbor", REPO / "sdk" / "cbor"),
)
TEAM_A_MODULES = ("ndarray", "datastructures", "tables", "binary", "cbor")


def run(command: list[str], cwd: Path, env: dict[str, str]) -> subprocess.CompletedProcess:
    return subprocess.run(
        command, cwd=cwd, env=env, check=True, capture_output=True, text=True
    )


def digest(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main() -> None:
    SCRATCH.mkdir(exist_ok=True)
    env = os.environ.copy()
    env.update(
        MOSS=str(MOSS),
        MARGO_HOME=str(SCRATCH / "margo-advanced-data-source-free"),
        TMPDIR=str(SCRATCH),
    )

    # Serial builds avoid package-local/shared Margo artifact races.
    for module, package in PROVIDERS:
        run([str(MARGO), "build", "--json"], package, env)
        artifact_dir = package / "build" / "debug"
        assert (artifact_dir / f"{module}.mossi").is_file(), module
        assert (artifact_dir / f"lib{module}.rlib").is_file(), module

    with tempfile.TemporaryDirectory(
        prefix="advanced-data-source-free-", dir=SCRATCH
    ) as temporary:
        work = Path(temporary)
        provider = work / "provider"
        app = work / "consumer"
        provider.mkdir()
        (app / "src").mkdir(parents=True)

        copied = []
        for module, package in PROVIDERS:
            artifact_dir = package / "build" / "debug"
            for filename in (f"{module}.mossi", f"lib{module}.rlib"):
                source = artifact_dir / filename
                destination = provider / filename
                shutil.copy2(source, destination)
                assert digest(source) == digest(destination), filename
                copied.append(filename)
            interface = (provider / f"{module}.mossi").read_text(encoding="utf-8")
            assert "native_abi 9" in interface, f"{module} is not ABI-v9"

        assert not list(provider.rglob("*.moss"))
        assert len(copied) == len(PROVIDERS) * 2
        (app / "Moss.toml").write_text(
            '[package]\nname = "advanced-data-source-free"\nversion = "0.1.0"\n\n'
            '[build]\nsource = "src"\n',
            encoding="utf-8",
        )
        shutil.copy2(CONSUMER, app / "src" / "main.moss")

        source_free_env = env | {"MOSS_MODULE_PATH": str(provider)}
        checked = run([str(MOSS), "check", "src/main.moss", "--json"], app, source_free_env)
        assert json.loads(checked.stdout)["ok"] is True
        formatted = run(
            [str(MOSS), "fmt", "--check", "--json", "src/main.moss"],
            app,
            source_free_env,
        )
        assert json.loads(formatted.stdout)["result"]["would_change"] is False
        run([str(MOSS), "build", "--json"], app, source_free_env)
        executed = run(
            [str(app / "build" / "debug" / "advanced_data_source_free")],
            app,
            source_free_env,
        )
        assert executed.stdout == "advanced data ABI-v9 source-free ok\n"

    print(
        "Compiled-only ABI-v9 gate passed: "
        f"{len(TEAM_A_MODULES)} Team A modules, {len(PROVIDERS)} exact provider pairs, "
        "no provider Moss source"
    )


if __name__ == "__main__":
    main()
