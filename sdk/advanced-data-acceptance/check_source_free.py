#!/usr/bin/env python3
"""Build and execute the advanced-data consumer through current interfaces."""

import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import threading
import argparse
from contextlib import ExitStack


PROJECT = Path(__file__).resolve().parent
REPO = PROJECT.parents[1]
MOSS = Path(os.environ.get("MOSS", REPO / "moss")).resolve()
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
    result = subprocess.run(
        command, cwd=cwd, env=env, check=False, capture_output=True, text=True
    )
    if result.returncode:
        raise RuntimeError(
            f"{command} failed in {cwd} (exit {result.returncode})\n"
            f"stdout tail:\n{result.stdout[-4000:]}\n"
            f"stderr tail:\n{result.stderr[-4000:]}"
        )
    return result


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--abi", choices=("v9", "v10"), default="v9")
    parser.add_argument("--profile", choices=("debug", "release"), default="debug")
    args = parser.parse_args()
    mode = args.abi
    profile = args.profile
    SCRATCH.mkdir(exist_ok=True)
    env = os.environ.copy()
    env.update(
        MOSS=str(MOSS),
        MARGO_HOME=str(SCRATCH / f"margo-advanced-data-source-free-{mode}-{profile}"),
        TMPDIR=str(SCRATCH),
    )
    env.pop("MOSS_STAGED_ABI", None)
    if mode == "v10":
        env["MOSS_STAGED_ABI"] = "v10"
    release_args = ["--release"] if profile == "release" else []

    # Parallel builds scheduled by Moss.toml dependency DAG
    from concurrent.futures import ThreadPoolExecutor, wait, FIRST_COMPLETED
    import tomllib

    dep_graph: dict[str, set[str]] = {}
    pkg_by_name: dict[str, Path] = {}
    for module, package in PROVIDERS:
        pkg_by_name[module] = package
        manifest = package / "Moss.toml" if (package / "Moss.toml").exists() else package / "moss.toml"
        data = tomllib.loads(manifest.read_text(encoding="utf-8"))
        raw_deps = data.get("dependencies", {})
        dependencies = {dep.removeprefix("moss.") for dep in raw_deps}
        unknown = dependencies - dict(PROVIDERS).keys()
        if unknown:
            raise RuntimeError(f"Provider {module} has untracked dependencies: {sorted(unknown)}")
        dep_graph[module] = dependencies

    # Margo rebuilds every dependency in a package's closure. A topological
    # provider DAG alone does not stop two independent parents from writing the
    # same dependency's build/debug artifacts at once.
    def package_closure(module: str) -> set[str]:
        closure = {module}
        for dependency in dep_graph[module]:
            closure.update(package_closure(dependency))
        return closure

    closures = {module: package_closure(module) for module in dep_graph}
    package_locks = {module: threading.Lock() for module in dep_graph}

    completed: set[str] = set()
    pending = dict(dep_graph)

    def build_provider(mod: str, pkg: Path) -> None:
        provider_env = env.copy()
        provider_env["MARGO_HOME"] = str(SCRATCH / f"margo-advanced-data-source-free-{mode}-{profile}" / mod)
        with ExitStack() as locks:
            for dependency in sorted(closures[mod]):
                locks.enter_context(package_locks[dependency])
            run([str(MARGO), "build", "--json", *release_args], pkg, provider_env)
        artifact_dir = pkg / "build" / profile
        assert (artifact_dir / f"{mod}.mossi").is_file(), mod
        assert (artifact_dir / f"lib{mod}.rlib").is_file(), mod

    max_workers = min(len(PROVIDERS), os.cpu_count() or 8)
    with ThreadPoolExecutor(max_workers=max_workers) as executor:
        running: dict = {}
        while pending or running:
            ready = [mod for mod, deps in pending.items() if deps <= completed]
            for mod in ready:
                del pending[mod]
                pkg = pkg_by_name[mod]
                fut = executor.submit(build_provider, mod, pkg)
                running[fut] = mod

            if not running:
                if pending:
                    raise RuntimeError(f"Cyclic or unresolved provider dependencies: {pending}")
                break

            done, _ = wait(running.keys(), return_when=FIRST_COMPLETED)
            for fut in done:
                mod = running.pop(fut)
                fut.result()
                completed.add(mod)

    with tempfile.TemporaryDirectory(
        prefix="advanced-data-source-free-", dir=SCRATCH
    ) as temporary:
        work = Path(temporary)
        app = work / "consumer"
        (app / "src").mkdir(parents=True)

        provider_roots = []
        for module, package in PROVIDERS:
            artifact_dir = package / "build" / profile
            for filename in (f"{module}.mossi", f"lib{module}.rlib"):
                assert (artifact_dir / filename).is_file(), filename
            interface = (artifact_dir / f"{module}.mossi").read_bytes()
            header = interface.split(b"binary_contract\n", 1)[0]
            expected_abi = f"native_abi {9 if mode == 'v9' else 10}\n".encode()
            assert expected_abi in header, f"{module} is not ABI-{mode[1:]}"
            provider_roots.append(str(artifact_dir))
        (app / "Moss.toml").write_text(
            '[package]\nname = "advanced-data-source-free"\nversion = "0.1.0"\n\n'
            '[build]\nsource = "src"\n',
            encoding="utf-8",
        )
        shutil.copy2(CONSUMER, app / "src" / "main.moss")

        source_free_env = env | {"MOSS_MODULE_PATH": ":".join(provider_roots)}
        checked = run([str(MOSS), "check", "src/main.moss", "--json"], app, source_free_env)
        assert json.loads(checked.stdout)["ok"] is True
        formatted = run(
            [str(MOSS), "fmt", "--check", "--json", "src/main.moss"],
            app,
            source_free_env,
        )
        assert json.loads(formatted.stdout)["result"]["would_change"] is False
        run([str(MOSS), "build", "--json", *release_args], app, source_free_env)
        executable = app / "build" / profile / "advanced_data_source_free"
        executed = run(
            [str(executable)],
            app,
            source_free_env,
        )
        assert executed.stdout == "advanced data source-free consumer ok\n"

    print(
        "Compiled-only advanced-data gate passed: "
        f"ABI-{mode[1:]} {profile}: {len(TEAM_A_MODULES)} Team A modules, "
        f"{len(PROVIDERS)} current source-backed provider interfaces"
    )


if __name__ == "__main__":
    main()
