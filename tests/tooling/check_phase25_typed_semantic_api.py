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
    assert not box["exported"] and box["parameter_leaf_effects"] is None, box
    assert box["key"].startswith("specialization-v2:sha256:")
    assert cell["key"].startswith("specialization-v2:sha256:")
    assert box["key"] != cell["key"]
    assert box["parameter_type_keys"][0] != cell["parameter_type_keys"][0]
    assert box["result_type_key"] == cell["result_type_key"]
    assert not typed["unavailable_specializations"], typed
    assert typed["open_summaries_complete"], typed
    assert not typed["unavailable_open_summaries"], typed
    field_summaries = {entry["callable_id"]: entry for entry in
                       typed["open_summaries"]}
    for name, template in (("unbox", "Box"), ("uncell", "Cell")):
        summary = field_summaries[f"callable-v1:{module}:fn:{name}"]
        assert not summary["exported"], summary
        assert summary["checked_body_ir_available"], summary
        assert summary["body_semantic_hash"].startswith(
            "body-semantic-v1:sha256:"), summary
        assert summary["body_semantic_status"] == "available_checked_body_v1", summary
        assert summary["parameter_leaf_effects"] is None, summary
        assert summary["bound_variable_count"] == 1, summary
        assert summary["result_type"] == {"kind": "bound_var", "ordinal": 0}, summary
        assert summary["parameter_types"] == [{"kind": "nominal",
            "id": f"nominal-template-v1:{module}:type:{template}",
            "arguments": [], "fields": [{"name": "value", "type":
                {"kind": "bound_var", "ordinal": 0}}]}], summary

    operator_source = Path(scratch_path) / "operator.moss"
    operator_source.write_text("""fn add_one(value):
  return value + 1

fn main():
  echo add_one(2)
  echo add_one(2.5)
""")
    operator_checked = subprocess.run([str(MOSS), "inspect", "main",
        "--source", str(operator_source), "--json"], text=True,
        capture_output=True)
    assert operator_checked.returncode == 0, (
        operator_checked.stdout, operator_checked.stderr)
    operator_typed = json.loads(operator_checked.stdout)["result"][
        "typed_semantic_v1"]
    assert operator_typed["open_summaries_complete"], operator_typed
    operator_summary = operator_typed["open_summaries"][0]
    assert operator_summary["callable_id"] == (
        f"callable-v1:{module}:fn:add_one"), operator_summary
    assert operator_summary["parameter_types"] == [
        {"kind": "bound_var", "ordinal": 0}], operator_summary
    assert operator_summary["result_type"] == (
        {"kind": "bound_var", "ordinal": 0}), operator_summary
    assert operator_summary["requirement_count"] == 1, operator_summary
    operator_specs = [entry for entry in operator_typed["specializations"]
                      if entry["callable_id"] == operator_summary["callable_id"]]
    assert len(operator_specs) == 2, operator_specs
    assert len({entry["key"] for entry in operator_specs}) == 2, operator_specs

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
    assert summaries["fresh"]["semantic_dependencies"] == [], summaries
    fresh_id = f"callable-v1:{module}:fn:fresh"
    assert summaries["relay"]["semantic_dependencies"] == [fresh_id], summaries
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

    structural = Path(scratch_path) / "structural.moss"
    structural.write_text("""fn total(values):
  acc = 0
  for item in values:
    acc = acc + item
  return acc

fn main():
  echo total([2, 3])
""")
    structural_checked = subprocess.run([str(MOSS), "inspect", "main",
        "--source", str(structural), "--json"], text=True,
        capture_output=True)
    assert structural_checked.returncode == 0, (
        structural_checked.stdout, structural_checked.stderr)
    structural_typed = json.loads(structural_checked.stdout)["result"][
        "typed_semantic_v1"]
    total_id = f"callable-v1:{module}:fn:total"
    total_summaries = [entry for entry in structural_typed["open_summaries"]
                       if entry["callable_id"] == total_id]
    assert len(total_summaries) == 1, structural_typed
    assert total_summaries[0]["requirement_count"] == 1, total_summaries

    member_source = Path(scratch_path) / "member_access.moss"
    member_source.write_text("""fn append(values, item):
  values.push(item)

fn main():
  values = []
  append(values, 1)
  append(values, 2)
  echo values[0]
""")
    member_checked = subprocess.run([str(MOSS), "inspect", "main", "--source",
        str(member_source), "--json"], text=True, capture_output=True)
    assert member_checked.returncode == 0, (
        member_checked.stdout, member_checked.stderr)
    member_typed = json.loads(member_checked.stdout)["result"]["typed_semantic_v1"]
    append_id = f"callable-v1:{module}:fn:append"
    append_specs = [entry for entry in member_typed["specializations"]
                    if entry["callable_id"] == append_id]
    assert len(append_specs) == 1, append_specs
    assert append_specs[0]["key"].startswith("specialization-v2:sha256:"), (
        append_specs)
    push = append_specs[0]["member_requirements"][0]
    assert push["member_name"] == "push", push
    assert push["receiver"] == {"kind": "builtin", "id": "vector",
        "arguments": [{"kind": "primitive", "id": "int", "arguments": [],
                       "fields": []}], "fields": []}, push
    assert push["argument_types"] == [
        {"kind": "primitive", "id": "int", "arguments": [], "fields": []}], push
    assert push["result"] == {"kind": "unit", "id": "unit",
        "arguments": [], "fields": []}, push
    assert push["receiver_access"] == "WRITE", push
    assert push["argument_access"] == ["CONSUME"], push
    assert push["admitted_template_or_builtin_family"] == (
        "builtin:vector[int]"), push

    index_source = Path(scratch_path) / "generic_index.moss"
    index_source.write_text("""fn first(values):
  return values[0]

fn main():
  echo first([2, 3])
""")
    index_checked = subprocess.run([str(MOSS), "inspect", "main", "--source",
        str(index_source), "--json"], text=True, capture_output=True)
    assert index_checked.returncode == 0, (
        index_checked.stdout, index_checked.stderr)
    index_typed = json.loads(index_checked.stdout)["result"]["typed_semantic_v1"]
    first_summary = [entry for entry in index_typed["open_summaries"]
                     if entry["callable_id"].endswith(":fn:first")]
    assert len(first_summary) == 1, first_summary
    assert first_summary[0]["requirement_count"] == 1, first_summary
    index = first_summary[0]["requirements"][0]
    assert index["kind"] == "index" and index["target_id"] == "builtin:index", index
    assert index["subject"] == {"kind": "bound_var", "ordinal": 0}, index
    assert index["argument_types"] == [{"kind": "primitive", "id": "int",
        "arguments": [], "fields": []}], index
    assert index["result"] == {"kind": "bound_var", "ordinal": 1}, index
    assert index["subject_access"] == "READ" and index["argument_access"] == ["READ"], index
    assert index["origin"]["enclosing_callable"] == first_summary[0]["callable_id"], index
    assert index["origin"]["module_id"].startswith("module-v1:"), index

    two_index_source = Path(scratch_path) / "generic_two_indices.moss"
    two_index_source.write_text("""fn both(values):
  first = values[0]
  return values[1]

fn main():
  echo both([1, 2])
""")
    two_index_checked = subprocess.run([str(MOSS), "inspect", "both", "--source",
        str(two_index_source), "--json"], text=True, capture_output=True)
    assert two_index_checked.returncode == 0, (
        two_index_checked.stdout, two_index_checked.stderr)
    two_index_typed = json.loads(two_index_checked.stdout)["result"][
        "typed_semantic_v1"]
    both_summary = [entry for entry in two_index_typed["open_summaries"]
                    if entry["callable_id"].endswith(":fn:both")]
    assert len(both_summary) == 1, both_summary
    both_requirements = both_summary[0]["requirements"]
    assert len(both_requirements) == 2, both_requirements
    assert all(entry["kind"] == "index" and len(entry["argument_types"]) == 1
               for entry in both_requirements), both_requirements
    assert len({entry["requirement_id"] for entry in both_requirements}) == 2, (
        both_requirements)

    two_index_source.write_text(two_index_source.read_text().replace(
        "values[1]", 'values["bad"]'))
    invalid_index = subprocess.run([str(MOSS), "check", str(two_index_source),
        "--json"], text=True, capture_output=True)
    assert invalid_index.returncode != 0, (
        invalid_index.stdout, invalid_index.stderr)
    invalid_index_error = json.loads(invalid_index.stdout)["error"]
    assert invalid_index_error["code"] == "TYPE_MISMATCH" and (
        "indices must be Int" in invalid_index_error["message"]), (
        invalid_index_error)

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

export fn size(value):
  return value.length()
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
  echo Provider.size("abc")
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
        assert imported_summaries[identity]["exported"], imported_summaries
        assert not imported_summaries[identity]["checked_body_ir_available"], (
            imported_summaries)
    assert imported_summaries[f"callable-v1:{provider_id}:fn:relay"]["semantic_dependencies"] == [
        f"callable-v1:{provider_id}:fn:fresh"], imported_summaries
    assert not imported_typed["open_summaries_complete"], imported_typed
    assert imported_typed["unavailable_open_summaries"] == [{
        "callable_id": f"callable-v1:{provider_id}:fn:size",
        "gap": "unresolved_effects",
        "reason": "checker result or effect facts remain unresolved",
    }], imported_typed
    imported_relay = [entry for entry in imported_typed["specializations"]
                      if entry["callable_id"] ==
                      f"callable-v1:{provider_id}:fn:relay"]
    assert len(imported_relay) == 2, imported_relay
    assert len({entry["key"] for entry in imported_relay}) == 2
    assert all(len(entry["open_result_bindings"]) == 1
               for entry in imported_relay), imported_relay

    # A concrete imported member requirement keeps a caller edge back to the
    # provider's source-free open summary. Neither identity is reconstructed
    # from diagnostic prose or generated Rust names.
    provider_member = f"callable-v1:{provider_id}:fn:size"
    source_requirements = [entry for entry in
        imported_typed["source_member_requirements"]
        if entry["origin"]["enclosing_callable"] == provider_member]
    assert len(source_requirements) == 1, imported_typed
    source_requirement = source_requirements[0]
    assert source_requirement["origin"]["module_id"] == provider_id
    assert source_requirement["origin"]["source_file"].endswith(
        "Provider.mossi"), source_requirement
    assert source_requirement["parent_requirement_id"] is None
    assert source_requirement["receiver"]["kind"] == "bound_var", (
        source_requirement)
    assert source_requirement["receiver_access"] == "READ", source_requirement
    assert source_requirement["argument_types"] == [], source_requirement
    assert source_requirement["argument_access"] == [], source_requirement
    assert source_requirement["selected_callable_id"] is None, source_requirement
    size_specs = [entry for entry in imported_typed["specializations"]
                  if entry["callable_id"] == provider_member]
    assert len(size_specs) == 1, size_specs
    closed_requirement = size_specs[0]["member_requirements"][0]
    assert closed_requirement["parent_requirement_id"] == (
        source_requirement["requirement_id"]), closed_requirement
    assert closed_requirement["requirement_id"] != (
        source_requirement["requirement_id"]), closed_requirement
    assert closed_requirement["selected_callable_id"] == (
        "builtin:string.length"), closed_requirement
    assert closed_requirement["receiver"]["kind"] == "primitive", (
        closed_requirement)
    assert closed_requirement["receiver_access"] == "READ", closed_requirement
    assert closed_requirement["argument_types"] == [], closed_requirement
    assert closed_requirement["argument_access"] == [], closed_requirement
    assert closed_requirement["admitted_template_or_builtin_family"], (
        closed_requirement)
    consumer_id = "module-v1:typed_consumer@1.0.0::Main"
    assert closed_requirement["origin"]["module_id"] == consumer_id, (
        closed_requirement, imported_typed)
    assert closed_requirement["origin"]["source_file"].endswith(
        "main.moss"), closed_requirement
    assert closed_requirement["origin"]["line"] == 10, closed_requirement
    assert closed_requirement["origin"]["call_expression_id"], (
        closed_requirement, imported_typed)

print("Phase 25 typed semantic API compiler projection: PASS")
