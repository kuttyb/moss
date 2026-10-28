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
NATURAL_APP = ROOT / "examples/swarm/modules/lib_and_app/repro/natural_app_attempt"
NATURAL_GEOLIB = ROOT / "examples/swarm/modules/lib_and_app/repro/natural_geolib_attempt"


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

    # The original owner package remains responsible for its concrete method
    # body. It cannot produce a current interface because Triangle.area fails.
    invalid_provider = SCRATCH / "natural_geolib_owner"
    shutil.copytree(NATURAL_GEOLIB, invalid_provider)
    owner_failure = run([MARGO, "build", "--check", "--json"],
                        invalid_provider, env, ok=False)
    owner_output = owner_failure.stdout + owner_failure.stderr
    assert "method:shapes__Triangle.area@46" in owner_output, owner_output
    assert "TYPE_MISMATCH" in owner_output, owner_output

    # Seed a genuinely current interface from a scratch provider with the same
    # exported API and a valid Triangle body. Its source hash matches that
    # scratch package; the consumer therefore exercises the .mossi boundary
    # without reading or rechecking provider bodies.
    provider = SCRATCH / "natural_geolib_attempt"
    app = SCRATCH / "natural_app_attempt"
    shutil.copytree(NATURAL_GEOLIB, provider)
    shutil.copytree(NATURAL_APP, app)
    shapes = provider / "src/shapes.moss"
    original_body = "    return measure.abs_f(twice) / 2.0"
    provider_body = shapes.read_text(encoding="utf-8")
    assert original_body in provider_body, shapes
    shapes.write_text(provider_body.replace(original_body, "    return 1.0", 1),
                      encoding="utf-8")
    seeded = run([MARGO, "build", "--check", "--json"], provider, env,
                 ok=True)
    seeded_payload = json.loads(seeded.stdout)
    interfaces = list(provider.rglob("*.mossi"))
    assert interfaces, (provider, seeded_payload)
    interface_text = "\n".join(path.read_text(errors="replace")
                                for path in interfaces)
    assert "package_source_hash sha256:" in interface_text, interfaces

    # Make the first graph transition after seeding geolib a successful
    # consumer check. This proves the root-produced interface is skipped when
    # that same package first appears as a dependency.
    probe = SCRATCH / "consumer_probe"
    (probe / "src").mkdir(parents=True)
    (probe / "Moss.toml").write_text(
        '[project]\nname = "consumer_probe"\nversion = "0.1.0"\n'
        '[build]\nsource = "src"\n[dependencies]\n'
        'geolib = { path = "../natural_geolib_attempt" }\n',
        encoding="utf-8")
    (probe / "src/main.moss").write_text(
        "module app\nfn main():\n  return\n", encoding="utf-8")
    probe_result = run([MARGO, "build", "--check", "--json"], probe, env,
                       ok=True)
    probe_payload = json.loads(probe_result.stdout)
    assert probe_payload["checked"] == ["consumer_probe"], probe_payload
    assert "geolib" in probe_payload["skipped"], probe_payload

    consumer = run([MARGO, "build", "--check", "--json"], app, env,
                   ok=False)
    consumer_output = consumer.stdout + consumer.stderr
    assert "UNKNOWN_SYMBOL_OR_TYPE" in consumer_output, consumer_output
    assert "natural_geolib_attempt/src/" not in consumer_output, consumer_output
    assert "Triangle.area" not in consumer_output, consumer_output

    print("PASS natural_app has no geolib-body diagnostic; Margo skips unchanged geolib; owner still rejects Triangle.area")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
