#!/usr/bin/env python3
"""Keep unsupported staged source-free private generic chains fail closed."""

import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile


ROOT = Path(__file__).resolve().parents[2]
MOSS = Path(sys.argv[1]).resolve() if len(sys.argv) > 1 else ROOT / "moss"
MARGO = ROOT / "margo"
(ROOT / "tmp").mkdir(exist_ok=True)

with tempfile.TemporaryDirectory(prefix="phase25-h3-guard-", dir=ROOT / "tmp") as scratch:
    source = Path(scratch) / "service.moss"
    source.write_text("""module Service

export type Box:
  value

fn private_box(x):
  result = Box(value: x)
  return result

fn nested_private_box(x):
  result = private_box(x)
  return result

export fn fresh(x):
  result = nested_private_box(x)
  return result

export fn relay(b: Box):
  return b.value
""")
    command = [str(MOSS), "check", str(source), "--json"]
    staged = os.environ.copy()
    staged["MOSS_STAGED_ABI"] = "v10"
    rejected = subprocess.run(command, env=staged, capture_output=True, text=True)
    assert rejected.returncode != 0, (rejected.stdout, rejected.stderr)
    error = json.loads(rejected.stdout)["error"]
    assert error["code"] == "SOURCE_FREE_CHECKED_BODY_UNAVAILABLE", error
    assert error["source"]["line"] == 14 and "fresh" in error["message"], error

    default = os.environ.copy()
    default.pop("MOSS_STAGED_ABI", None)
    accepted = subprocess.run(command, env=default, capture_output=True, text=True)
    assert accepted.returncode == 0 and json.loads(accepted.stdout)["ok"], (
        accepted.stdout, accepted.stderr)

    probe = Path(scratch) / "probe.moss"
    probe.write_text(source.read_text() + '\nfn main():\n  item = fresh(99)\n'
                     '  echo relay(item)\n')
    inspected = subprocess.run([str(MOSS), "inspect", "main", "--source",
                                str(probe), "--json"], env=default,
                               capture_output=True, text=True)
    assert inspected.returncode == 0, (inspected.stdout, inspected.stderr)
    typed = json.loads(inspected.stdout)["result"]["typed_semantic_v1"]
    summaries = {entry["callable_id"].split(":fn:")[-1]: entry
                 for entry in typed["open_summaries"]}
    assert {"fresh", "nested_private_box", "private_box"} <= summaries.keys(), typed
    expected_field = {"name": "value", "type": {"kind": "bound_var", "ordinal": 0}}
    for name in ("fresh", "nested_private_box", "private_box"):
        result = summaries[name]["result_type"]
        assert result["kind"] == "nominal" and result["fields"] == [
            expected_field], (name, result)
        assert not summaries[name]["checked_body_ir_available"], summaries[name]
    assert summaries["nested_private_box"]["semantic_dependencies"] == [
        summaries["private_box"]["callable_id"]], summaries
    assert summaries["fresh"]["semantic_dependencies"] == [
        summaries["nested_private_box"]["callable_id"]], summaries

    # The pre-cutover ABI-v9 path still accepts the same provider and keeps
    # two independently typed source-free consumers working from one artifact.
    provider = Path(scratch) / "provider"
    (provider / "src").mkdir(parents=True)
    (provider / "src" / "service.moss").write_text(source.read_text())
    (provider / "Moss.toml").write_text(
        '[package]\nname = "h3_provider"\nversion = "0.1.0"\n\n'
        '[build]\nsource = "src"\n')
    default["MOSS"] = str(MOSS)
    default["TMPDIR"] = str(ROOT / "tmp")

    def margo(package: Path, action: str, env: dict) -> subprocess.CompletedProcess:
        result = subprocess.run([str(MARGO), action], cwd=package, env=env,
                                capture_output=True, text=True)
        assert result.returncode == 0, (action, result.stdout, result.stderr)
        return result

    margo(provider, "build", default)
    artifacts = Path(scratch) / "provider-artifacts"
    shutil.copytree(provider / "build" / "debug", artifacts)
    assert list(artifacts.glob("*.mossi")) and list(artifacts.glob("*.rlib"))
    shutil.rmtree(provider)
    assert not provider.exists() and not list(artifacts.rglob("*.moss"))

    consumer_env = default.copy()
    consumer_env["MOSS_MODULE_PATH"] = str(artifacts)
    for name, argument, output in (("int", "99", "99\n"),
                                   ("string", '"hello"', "hello\n")):
        consumer = Path(scratch) / f"consumer-{name}"
        (consumer / "src").mkdir(parents=True)
        (consumer / "Moss.toml").write_text(
            f'[package]\nname = "h3_{name}"\nversion = "0.1.0"\n\n'
            '[build]\nsource = "src"\n')
        (consumer / "src" / "main.moss").write_text(
            'module Main\nimport Service\n\nfn main():\n'
            f'  item = Service.fresh({argument})\n'
            '  echo Service.relay(item)\n')
        margo(consumer, "build", consumer_env)
        assert margo(consumer, "run", consumer_env).stdout == output

print("Phase 25 H3 staged guard and default-v9 two-consumer baseline: PASS")
