#!/usr/bin/env python3
"""Exercise an enum exported through a source-free .mossi provider."""

import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile


ROOT = Path(__file__).resolve().parents[2]
FIXTURE = ROOT / "tests" / "tooling" / "fixtures" / "enum_modules"


def main():
    provider = FIXTURE / "build" / "debug"
    scratch = Path(tempfile.mkdtemp(prefix="express005-source-free-", dir=ROOT / "tmp"))
    artifacts = scratch / "artifacts"
    artifacts.mkdir()
    for name in ("tags.mossi", "libtags.rlib"):
        shutil.copy2(provider / name, artifacts / name)
    project = scratch / "consumer"
    (project / "src").mkdir(parents=True)
    (project / "Moss.toml").write_text(
        '[project]\nname = "enum-consumer"\nversion = "0.1.0"\n\n'
        '[build]\nsource = "src"\n',
        encoding="utf-8",
    )
    shutil.copy2(FIXTURE / "src" / "main.moss", project / "src" / "main.moss")
    environment = os.environ.copy()
    environment["MOSS_MODULE_PATH"] = str(artifacts)
    build = subprocess.run(
        [str(ROOT / "moss"), "build", "--json"],
        cwd=project,
        env=environment,
        text=True,
        capture_output=True,
        check=False,
    )
    if build.returncode:
        raise AssertionError(build.stdout + build.stderr)
    result = json.loads(build.stdout)
    executable = result["result"]["artifacts"]["executable"]
    run = subprocess.run(
        [executable],
        cwd=project,
        env=environment,
        text=True,
        capture_output=True,
        check=True,
    )
    if run.stdout != "4\n1\n":
        raise AssertionError(f"source-free enum output: {run.stdout!r}")
    print("source-free enum .mossi provider passed")


if __name__ == "__main__":
    main()
