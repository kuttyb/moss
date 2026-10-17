#!/usr/bin/env python3
"""Exercise the checked Program's typed-semantic-v1 compiler projection."""

import json
from pathlib import Path
import subprocess
import sys
import tempfile


ROOT = Path(__file__).resolve().parents[2]
MOSS = Path(sys.argv[1]).resolve() if len(sys.argv) > 1 else ROOT / "moss"
(ROOT / "tmp").mkdir(exist_ok=True)

with tempfile.TemporaryDirectory(prefix="phase25-typed-api-", dir=ROOT / "tmp") as name:
    source = Path(name) / "main.moss"
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

print("Phase 25 typed semantic API compiler projection: PASS")
