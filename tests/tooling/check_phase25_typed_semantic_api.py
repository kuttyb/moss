#!/usr/bin/env python3
"""Exercise the checked Program's typed-semantic-v1 compiler projection."""

import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile


ROOT = Path(__file__).resolve().parents[2]
MOSS = Path(sys.argv[1]).resolve() if len(sys.argv) > 1 else ROOT / "moss"
(ROOT / "tmp").mkdir(exist_ok=True)

with tempfile.TemporaryDirectory(prefix="phase25-typed-api-", dir=ROOT / "tmp") as scratch_path:
    source = Path(scratch_path) / "main.moss"
    source.write_text("""type Box:
  value

type Cell:
  value

type Holder:
  payload: Vector[Int]
  item

fn unbox(item: Box):
  return item.value

fn uncell(item: Cell):
  return item.value

fn main():
  echo unbox(Box(value: 1))
  echo uncell(Cell(value: 1))
""")
    command = [str(MOSS), "inspect", "main", "--source", str(source), "--json"]
    checked = subprocess.run(command, text=True, capture_output=True)
    assert checked.returncode == 0, (checked.stdout, checked.stderr)
    document = json.loads(checked.stdout)
    assert document["ok"], document
    typed = document["result"]["typed_semantic_v1"]
    assert typed["version"] == "typed-semantic-v1", typed
    module = "module-v1:__standalone__@0.0.0::main"
    assert module in typed["modules"], typed
    templates = {entry["id"]: entry for entry in typed["nominal_templates"]}
    for name in ("Box", "Cell"):
        identity = f"nominal-template-v1:{module}:type:{name}"
        assert templates[identity]["module_id"] == module, templates
        assert templates[identity]["fields"] == [{"name": "value",
            "declared_type_key": None, "variable_ordinal": 0}], templates
    holder = templates[f"nominal-template-v1:{module}:type:Holder"]
    assert holder["fields"][0]["name"] == "payload", holder
    assert holder["fields"][0]["declared_type_key"].startswith(
        "type-key-v1:sha256:"), holder
    assert holder["fields"][0]["variable_ordinal"] is None, holder
    assert holder["fields"][1] == {"name": "item", "declared_type_key": None,
                                   "variable_ordinal": 0}, holder
    specializations = {entry["callable_id"]: entry
                       for entry in typed["specializations"]}
    box = specializations[f"callable-v1:{module}:fn:unbox"]
    cell = specializations[f"callable-v1:{module}:fn:uncell"]
    assert box["key"].startswith("specialization-v2:sha256:")
    assert cell["key"].startswith("specialization-v2:sha256:")
    assert box["key"] != cell["key"]
    assert box["parameter_type_keys"][0] != cell["parameter_type_keys"][0]
    assert box["result_type_key"] == cell["result_type_key"]
    assert not typed["unavailable_specializations"], typed
    # Current generic bodies lack a reusable solver-owned open summary. The
    # API reports that omission rather than inventing one from display text.
    assert typed["open_summaries_complete"] is False, typed
    unavailable = {entry["callable_id"] for entry in
                   typed["unavailable_open_summaries"]}
    assert f"callable-v1:{module}:fn:unbox" in unavailable, typed
    assert f"callable-v1:{module}:fn:uncell" in unavailable, typed

    open_source = Path(scratch_path) / "open.moss"
    open_source.write_text("""fn fresh():
  return []

fn relay():
  return fresh()

fn main():
  values = relay()
  words = relay()
  values.push(1)
  words.push("hello")
  echo values[0]
  echo words[0]
""")
    opened = subprocess.run([str(MOSS), "inspect", "main", "--source",
        str(open_source), "--json"], text=True, capture_output=True)
    assert opened.returncode == 0, (opened.stdout, opened.stderr)
    open_typed = json.loads(opened.stdout)["result"]["typed_semantic_v1"]
    assert open_typed["open_summaries_complete"], open_typed
    assert not open_typed["unavailable_open_summaries"], open_typed
    summaries = {entry["callable_id"].split(":fn:")[-1]: entry
                 for entry in open_typed["open_summaries"]}
    assert set(summaries) == {"fresh", "relay"}, summaries
    for summary in summaries.values():
        assert summary["bound_variable_count"] == 1, summary
        assert summary["result_type"] == {"kind": "builtin", "id": "vector",
            "arguments": [{"kind": "bound_var", "ordinal": 0}],
            "fields": []}, summary
        assert summary["requirement_count"] == 0, summary
    relay_id = f"callable-v1:module-v1:__standalone__@0.0.0::main:fn:relay"
    relay_specs = [entry for entry in open_typed["specializations"]
                   if entry["callable_id"] == relay_id]
    assert len(relay_specs) == 2, relay_specs
    assert len({entry["key"] for entry in relay_specs}) == 2, relay_specs
    assert len({entry["open_result_bindings"][0]["type_key"]
                for entry in relay_specs}) == 2, relay_specs
    for entry in relay_specs:
        assert len(entry["open_result_bindings"]) == 1, entry
        assert entry["open_result_bindings"][0]["ordinal"] == 0, entry
        assert entry["receiver_type_key"] is None, entry

    provider = Path(scratch_path) / "provider"
    consumer = Path(scratch_path) / "consumer"
    for package_name, path in (("typed_provider", provider),
                               ("typed_consumer", consumer)):
        (path / "src").mkdir(parents=True)
        (path / "Moss.toml").write_text(
            f'[project]\nname = "{package_name}"\nversion = "1.0.0"\n'
            '\n[build]\nsource = "src"\n')
    (provider / "src" / "provider.moss").write_text("""module Provider
export fn fresh():
  return []

export fn relay():
  return fresh()
""")
    env = os.environ.copy()
    env["MOSS_STAGED_ABI"] = "v10"
    env["TMPDIR"] = str(ROOT / "tmp")
    built = subprocess.run([str(ROOT / "margo"), "build", "--json"],
        cwd=provider, env=env, text=True, capture_output=True)
    assert built.returncode == 0 and json.loads(built.stdout)["ok"], (
        built.stdout, built.stderr)
    (provider / "src" / "provider.moss").unlink()
    (consumer / "src" / "main.moss").write_text("""module Main
import Provider
fn main():
  values = Provider.relay()
  words = Provider.relay()
  values.push(1)
  words.push("hello")
  echo values[0]
  echo words[0]
""")
    env["MOSS_MODULE_PATH"] = str(provider / "build" / "debug")
    imported = subprocess.run([str(MOSS), "inspect", "main", "--source",
        str(consumer / "src" / "main.moss"), "--json"], env=env,
        text=True, capture_output=True)
    assert imported.returncode == 0, (imported.stdout, imported.stderr)
    imported_typed = json.loads(imported.stdout)["result"]["typed_semantic_v1"]
    provider_id = "module-v1:typed_provider@1.0.0::Provider"
    imported_summaries = {entry["callable_id"]: entry
                          for entry in imported_typed["open_summaries"]}
    for name in ("fresh", "relay"):
        identity = f"callable-v1:{provider_id}:fn:{name}"
        assert imported_summaries[identity]["module_id"] == provider_id, (
            identity, imported_typed)
        assert imported_summaries[identity]["bound_variable_count"] == 1
    assert imported_typed["open_summaries_complete"], imported_typed
    assert not imported_typed["unavailable_open_summaries"], imported_typed
    imported_relay = [entry for entry in imported_typed["specializations"]
                      if entry["callable_id"] ==
                      f"callable-v1:{provider_id}:fn:relay"]
    assert len(imported_relay) == 2, imported_relay
    assert len({entry["key"] for entry in imported_relay}) == 2
    assert all(len(entry["open_result_bindings"]) == 1
               for entry in imported_relay), imported_relay

print("Phase 25 typed semantic API compiler projection: PASS")
