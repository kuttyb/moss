#!/usr/bin/env python3
"""Imported generic bodies are checked when a consumer instantiates them."""
from __future__ import annotations

import json
import os
from pathlib import Path
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]
COMPILER = Path(sys.argv[1]).resolve()
MARGO = ROOT / "margo"
FIXTURE = ROOT / "examples/swarm/modules/algo_chain/repro/generic_poisons_import/split"
SCRATCH = ROOT / "tmp/phase26-1-t1-generic-imports"


def run(args: list[object], cwd: Path, env: dict[str, str], *, ok: bool):
    result = subprocess.run([str(arg) for arg in args], cwd=cwd, env=env,
                            text=True, capture_output=True, timeout=240)
    assert (result.returncode == 0) == ok, (
        args, result.returncode, result.stdout, result.stderr)
    return result


def main() -> int:
    shutil.rmtree(SCRATCH, ignore_errors=True)
    SCRATCH.mkdir(parents=True)
    fixture = SCRATCH / "fixture"
    shutil.copytree(FIXTURE, fixture)
    consumer = fixture / "consumer"
    source = consumer / "src/main.moss"
    env = dict(os.environ)
    env["MOSS"] = str(COMPILER)
    env["MARGO_HOME"] = str(SCRATCH / "margo-home")
    for key in ("MOSS_MODULE_PATH", "MOSS_FAST_DEBUG_SOURCE_ROOTS",
                "MOSS_SOURCE_ROOTS"):
        env.pop(key, None)

    # Importing util must not check the unused generic fill body. This command
    # checks provider first, writes its interface, then checks the consumer.
    unused = run([MARGO, "build", "--check", "--json"], consumer, env,
                 ok=True)
    unused_payload = json.loads(unused.stdout)
    assert unused_payload["checked"] == ["gpi_provider", "gpi_consumer"], \
        unused_payload

    # A concrete compatible instantiation checks the generic body and passes.
    source.write_text(
        "module app\nimport util\nfn main():\n  echo util.fill(1)\n",
        encoding="utf-8")
    run([MARGO, "build", "--check", "--json"], consumer, env, ok=True)

    # A concrete incompatible instantiation reaches the same body and fails.
    source.write_text(
        'module app\nimport util\nfn main():\n  echo util.fill("bad")\n',
        encoding="utf-8")
    rejected = run([MARGO, "build", "--check", "--json"], consumer, env,
                   ok=False)
    output = rejected.stdout + rejected.stderr
    assert "no matching method 'box__Box.add'" in output, output
    assert "inside provider function 'util.fill'" in output, output
    print("PASS unused generic import is deferred; concrete instantiations are checked")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
