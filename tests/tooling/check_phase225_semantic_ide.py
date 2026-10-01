#!/usr/bin/env python3
"""Focused Phase 22.5 compiler-owned editor-query contracts."""

from __future__ import annotations

import json
import pathlib
import subprocess
import sys


def fail(message: str) -> None:
    raise SystemExit(f"Phase 22.5 semantic IDE query test failed: {message}")


if len(sys.argv) != 2:
    fail("usage: check_phase225_semantic_ide.py <moss-compiler>")

root = pathlib.Path.cwd().resolve()
compiler = pathlib.Path(sys.argv[1]).resolve()
source = root / "tests" / "phase6_agent_api.moss"
incomplete = root / "tests" / "tooling" / "fixtures" / "phase225_incomplete.moss"
type_incomplete = root / "tests" / "tooling" / "fixtures" / "phase225_type_incomplete.moss"
project = root / "tests" / "tooling" / "fixtures" / "phase225_ide"
project_main = project / "src" / "main.moss"
project_library = project / "src" / "library.moss"
module_project = root / "tests" / "tooling" / "fixtures" / "phase225_modules"
module_main = module_project / "src" / "main.moss"
shadow = root / "tests" / "tooling" / "fixtures" / "phase225_shadow.moss"
semantic_uses = (
    root / "tests" / "tooling" / "fixtures" / "phase225_semantic_uses.moss"
)
pipeline_completion = (
    root / "tests" / "tooling" / "fixtures" /
    "phase225_pipeline_completion.moss"
)
domain_navigation = (
    root / "tests" / "tooling" / "fixtures" /
    "phase225_domain_navigation.moss"
)
overlay_directory = root / "tmp" / "phase225-overlays"
overlay_directory.mkdir(parents=True, exist_ok=True)


def invoke(*args: str, expect: int = 0) -> dict[str, object]:
    process = subprocess.run(
        [str(compiler), *args], cwd=root, stdout=subprocess.PIPE,
        stderr=subprocess.PIPE, check=False,
    )
    if process.returncode != expect:
        fail(
            f"{' '.join(args)} exited {process.returncode}, expected {expect}: "
            + process.stderr.decode("utf-8", "replace")
        )
    try:
        return json.loads(process.stdout)
    except json.JSONDecodeError as error:
        fail(f"{' '.join(args)} did not emit JSON: {error}")


def invoke_overlay(
    name: str, contents: str, *args: str, expect: int = 0
) -> dict[str, object]:
    overlay = overlay_directory / name
    overlay.write_text(contents, encoding="utf-8")
    try:
        return invoke(*args, "--overlay-source", str(overlay), expect=expect)
    finally:
        overlay.unlink(missing_ok=True)


discovery_results = {}
for discovery in ("bootstrap", "capabilities", "schema"):
    discovery_results[discovery] = invoke("agent", discovery, "--json")["result"]
    text = json.dumps(discovery_results[discovery])
    for operation in ("references", "symbols", "complete"):
        if operation not in text:
            fail(f"agent {discovery} omitted {operation}")
for discovery in ("capabilities", "schema"):
    if "--overlay-source" not in json.dumps(discovery_results[discovery]):
        fail(f"agent {discovery} omitted semantic editor overlays")

domain_positions = {
    "Ledger": ((21, 12), "entity-v1:domain:Ledger", 2),
    "Inventory": ((22, 15), "entity-v1:domain:Inventory", 8),
    "App": ((23, 9), "entity-v1:domain:App", 14),
}
handler_positions = {
    "Ledger.Read": ((12, 26), "entity-v1:handler:Ledger.Read", 5),
    "Inventory.Available": (
        (18, 29), "entity-v1:handler:Inventory.Available", 11
    ),
    "App.Run": ((24, 20), "entity-v1:handler:App.Run", 17),
}
for name, (position, expected_id, expected_line) in {
    **domain_positions, **handler_positions,
}.items():
    resolved = invoke(
        "resolve", f"at:{position[0]}:{position[1]}",
        "--source", str(domain_navigation), "--json",
    )["result"]["target"]
    if resolved["id"] != expected_id:
        fail(f"entity-at-point resolved {name} as {resolved['id']}")
    if resolved["source"]["line"] != expected_line:
        fail(f"entity-at-point returned the wrong declaration for {name}")

route_ledger = invoke(
    "resolve", "at:9:24", "--source", str(domain_navigation), "--json",
)["result"]["target"]
if route_ledger["id"] != "entity-v1:domain:Ledger":
    fail("route declaration type did not resolve to the Ledger domain")

expected_domain_reference_lines = {
    "Ledger": [2, 9, 15, 21],
    "Inventory": [8, 15, 22],
    "App": [14, 23],
}
for name, (position, expected_id, _) in domain_positions.items():
    references = invoke(
        "references", f"at:{position[0]}:{position[1]}",
        "--source", str(domain_navigation), "--json",
    )["result"]
    if references["target"]["id"] != expected_id:
        fail(f"domain references selected the wrong identity for {name}")
    lines = [item["source"]["line"] for item in references["references"]]
    if lines != expected_domain_reference_lines[name]:
        fail(f"domain references for {name} were {lines}")
    calls = invoke(
        "calls", expected_id, "--source", str(domain_navigation), "--json",
    )["result"]
    if calls["target"]["id"] != expected_id:
        fail(f"calls changed the resolved domain identity for {name}")
    if calls["target"]["source"]["line"] != domain_positions[name][2]:
        fail(f"calls returned the wrong domain declaration for {name}")

expected_handler_callers = {
    "Ledger.Read": "handler:Inventory.Available",
    "Inventory.Available": "handler:App.Run",
    "App.Run": "main",
}
expected_handler_callees = {
    "Ledger.Read": [],
    "Inventory.Available": ["handler:Ledger.Read"],
    "App.Run": ["handler:Inventory.Available"],
}
for name, (position, expected_id, declaration_line) in handler_positions.items():
    references = invoke(
        "references", f"at:{position[0]}:{position[1]}",
        "--source", str(domain_navigation), "--json",
    )["result"]
    if references["target"]["id"] != expected_id:
        fail(f"handler references selected the wrong identity for {name}")
    reference_lines = [
        item["source"]["line"] for item in references["references"]
    ]
    if reference_lines != [declaration_line, position[0]]:
        fail(f"handler references for {name} were {reference_lines}")
    calls = invoke(
        "calls", expected_id, "--source", str(domain_navigation), "--json",
    )["result"]
    callers = [item["source"] for item in calls["callers"]]
    if callers != [expected_handler_callers[name]]:
        fail(f"callers for {name} were {callers}")
    callees = [item["target"] for item in calls["direct_calls"]]
    if callees != expected_handler_callees[name]:
        fail(f"callees for {name} were {callees}")
    for callee in calls["direct_calls"]:
        target_source = callee.get("target_source")
        if not target_source or not target_source.get("file"):
            fail(f"callee declaration provenance is missing for {name}")

references = invoke(
    "references", "fn:inspect", "--source", str(source), "--json"
)["result"]
if references["definition"]["source"]["line"] != 10:
    fail("function definition location is wrong")
reference_kinds = [item["kind"] for item in references["references"]]
if reference_kinds != ["declaration", "call"]:
    fail(f"function references are incomplete or unordered: {reference_kinds}")
if not all(item["entity_id"] == "entity-v1:function:inspect"
           for item in references["references"]):
    fail("references omitted the durable entity identity")

type_references = invoke(
    "references", "type:Counter", "--source", str(project_main), "--json"
)["result"]["references"]
if "type_use" not in [item["kind"] for item in type_references]:
    fail("type-use reference kind was not retained")

method = invoke(
    "references", "method:Sample.read", "--source", str(source), "--json"
)["result"]
if "method_call" not in [item["kind"] for item in method["references"]]:
    fail("method call reference kind was not retained")

handler = invoke(
    "references", "handler:Worker.Add", "--source", str(source), "--json"
)["result"]
if "handler_message" not in [item["kind"] for item in handler["references"]]:
    fail("handler message reference kind was not retained")

ambiguous = invoke(
    "references", "line:44", "--source", str(source), "--json", expect=1
)["error"]
if ambiguous["details"]["resolution"]["status"] != "ambiguous":
    fail("ambiguous reference target was guessed")

left_binding = invoke(
    "resolve", "line:2", "--kind", "binding", "--source", str(shadow),
    "--json",
)["result"]["target"]["id"]
left_references = invoke(
    "references", left_binding, "--source", str(shadow), "--json"
)["result"]["references"]
if [item["source"]["line"] for item in left_references] != [2, 3, 4]:
    fail("binding references crossed into an unrelated same-name binding")

outer_binding = invoke(
    "resolve", "line:13", "--kind", "binding", "--source",
    str(semantic_uses), "--json",
)["result"]["target"]["id"]
outer_lines = [
    item["source"]["line"] for item in invoke(
        "references", outer_binding, "--source", str(semantic_uses), "--json",
    )["result"]["references"]
]
if outer_lines != [13, 19, 20]:
    fail(f"outer binding references included text or shadowed uses: {outer_lines}")
inner_binding = invoke(
    "resolve", "line:17", "--kind", "binding", "--source",
    str(semantic_uses), "--json",
)["result"]["target"]["id"]
inner_lines = [
    item["source"]["line"] for item in invoke(
        "references", inner_binding, "--source", str(semantic_uses), "--json",
    )["result"]["references"]
]
if inner_lines != [17, 18]:
    fail(f"nested shadow references did not preserve identity: {inner_lines}")

counter_references = invoke(
    "references", "type:Counter", "--source", str(semantic_uses), "--json",
)["result"]["references"]
counter_type_lines = {
    item["source"]["line"] for item in counter_references
    if item["kind"] == "type_use"
}
if counter_type_lines != {5, 6, 7, 9}:
    fail(f"nested/composite type uses were not resolved: {counter_type_lines}")

symbols = invoke("symbols", "", "--source", str(source), "--json")["result"]["symbols"]
projection = [(item["qualified_name"], item["kind"]) for item in symbols]
if projection != sorted(projection):
    fail("workspace symbols are not deterministic")
for required in (("inspect", "function"), ("Sample", "type"),
                 ("Worker", "domain"), ("Worker.Add", "handler")):
    if required not in projection:
        fail(f"workspace symbols omitted {required}")

completion = invoke(
    "complete", "at:7:6", "--source", str(incomplete), "--json"
)["result"]
if not completion["recovery"]["used"]:
    fail("incomplete-source tooling recovery was not reported")
if "total" not in [item["label"] for item in completion["candidates"]]:
    fail("lexical completion omitted the matching function")
if "total_value" not in [item["label"] for item in completion["candidates"]]:
    fail("lexical completion omitted the visible local binding")

type_completion = invoke(
    "complete", "at:4:21", "--source", str(type_incomplete), "--json"
)["result"]["candidates"]
if not any(item["label"] == "Counter" and item["kind"] == "type"
           for item in type_completion):
    fail("type-position completion omitted the semantic type")

member = invoke(
    "complete", "at:8:24", "--source", str(project_main), "--json"
)["result"]
if not any(item["label"] == "Read" and item["kind"] == "method"
           for item in member["candidates"]):
    fail("member completion omitted the resolved method")

message_completion = invoke(
    "complete", "at:11:27", "--source", str(project_main), "--json"
)["result"]["candidates"]
if not any(item["label"] == "Add" and item["kind"] == "handler"
           for item in message_completion):
    fail("domain/message completion omitted the statically legal handler")

builtin_overlays = {
    "vector": ("fn main():\n  values = [1, 2]\n  echo values.\n", 3,
               {"push", "pop"}, {"get"}),
    "map": ("fn main():\n  values = Map()\n  values[\"one\"] = 1\n"
            "  echo values.\n", 4,
            {"get", "keys", "values", "delete"}, {"push"}),
    "queue": ("fn main():\n  values = Queue()\n  values.push(1)\n"
              "  echo values.\n", 4, {"push", "pop"}, {"keys"}),
    "string": ("fn main():\n  value = \"moss\"\n  echo value.\n", 3,
               {"length", "char_at", "chars", "split", "join"}, {"push"}),
}
for family, (contents, line, required, forbidden) in builtin_overlays.items():
    result = invoke_overlay(
        f"completion-{family}.moss", contents,
        "complete", f"at:{line}:999", "--source", str(shadow), "--json",
    )["result"]
    labels = {item["label"] for item in result["candidates"]}
    if not required <= labels:
        fail(f"{family} completion omitted compiler builtins: {required - labels}")
    if labels & forbidden:
        fail(f"{family} completion offered invalid receiver operations: {labels & forbidden}")

pipeline_source = "fn main():\n  values = [1, 2]\n  echo values |> \n"
pipeline = invoke_overlay(
    "completion-pipeline.moss", pipeline_source,
    "complete", "at:3:999", "--source", str(shadow), "--json",
)["result"]["candidates"]
pipeline_labels = {item["label"] for item in pipeline}
functional_operations = {"map", "filter", "reduce", "sum", "count", "any", "all"}
if not functional_operations <= pipeline_labels:
    fail("functional pipeline completion omitted compiler-owned operations: "
         f"{functional_operations - pipeline_labels}")


def pipeline_completion_labels(expression: str, binding: str = "values") -> set[str]:
    contents = pipeline_completion.read_text(encoding="utf-8").replace(
        f"  echo {binding}\n", f"  echo {expression} |> \n", 1
    )
    line = 16 if binding == "values" else 17
    candidates = invoke_overlay(
        "pipeline-legality.moss", contents,
        "complete", f"at:{line}:999", "--source", str(pipeline_completion),
        "--json",
    )["result"]["candidates"]
    return {item["label"] for item in candidates}


pipeline_start = pipeline_completion_labels("values")
if pipeline_start != functional_operations:
    fail(f"pipeline-start legality was not compiler-filtered: {pipeline_start}")

pipeline_after_map = pipeline_completion_labels("values |> map(to_string)")
expected_after_map = {"map", "reduce", "count", "any", "all"}
if pipeline_after_map != expected_after_map:
    fail("type-changing map did not update the legal next-stage set: "
         f"{pipeline_after_map}")

pipeline_after_filter = pipeline_completion_labels(
    "values |> filter(keep_int)"
)
if pipeline_after_filter != functional_operations:
    fail(f"filter did not preserve collection pipeline state: {pipeline_after_filter}")

terminal_expressions = {
    "sum": "values |> sum",
    "count": "values |> count",
    "any": "values |> any(keep_int)",
    "all": "values |> all(keep_int)",
    "reduce": "values |> reduce(0, add_int)",
}
for terminal, expression in terminal_expressions.items():
    labels = pipeline_completion_labels(expression)
    if labels:
        fail(f"terminal {terminal} offered a following pipeline stage: {labels}")

item_labels = pipeline_completion_labels("items", "items")
if "sum" in item_labels:
    fail("non-numeric user values offered numeric-only sum completion")
if "count" not in item_labels:
    fail("non-numeric collection lost the legal count terminal")

cross_file = invoke(
    "references", "fn:leaf", "--source", str(project_main), "--json"
)["result"]
if pathlib.Path(cross_file["definition"]["source"]["file"]) != project_library:
    fail("cross-file definition did not retain its physical source")
if len([item for item in cross_file["references"] if item["kind"] == "call"]) != 3:
    fail("cross-file references did not retain all three call sites")

project_symbols = invoke(
    "symbols", "", "--source", str(project_main), "--json"
)["result"]["symbols"]
project_projection = {(item["qualified_name"], item["kind"])
                      for item in project_symbols}
for required in (("Counter.Read", "method"), ("Choice", "enum"),
                 ("Worker.Add", "handler")):
    if required not in project_projection:
        fail(f"multi-file semantic symbols omitted {required}")

calls = invoke(
    "calls", "fn:top", "--source", str(project_main), "--json"
)["result"]
if not calls["direct_calls"] or not calls["transitive_calls"]:
    fail("nested direct/transitive call hierarchy is incomplete")
main_calls = invoke(
    "calls", "main", "--source", str(project_main), "--json"
)["result"]["direct_calls"]
if not any(item["target"].endswith("Worker.Add") and
           item["boundary"] == "synchronous_message_by_value"
           for item in main_calls):
    fail("domain message call-hierarchy edge is missing")

unsaved_shadow = "\n" + shadow.read_text(encoding="utf-8")
unsaved_references = invoke_overlay(
    "unsaved-shadow.moss", unsaved_shadow,
    "references", "at:3:4", "--source", str(shadow), "--json",
)["result"]
if unsaved_references["definition"]["source"]["line"] != 3:
    fail("references used the stale saved declaration location")
if [item["source"]["line"] for item in unsaved_references["references"]] != [3, 4, 5]:
    fail("references used stale saved use locations")

project_overlay = project_main.read_text(encoding="utf-8").replace(
    "middle(value)", "leaf(value)", 1
)
unsaved_calls = invoke_overlay(
    "unsaved-project-main.moss", project_overlay,
    "calls", "fn:top", "--source", str(project_main), "--json",
)["result"]["direct_calls"]
call_targets = [item["target"] for item in unsaved_calls]
if any(target.endswith("middle") for target in call_targets):
    fail("calls used the stale saved call target")
if len([target for target in call_targets if target.endswith("leaf")]) != 2:
    fail("calls did not use the valid unsaved overlay")

invalid_overlay = invoke_overlay(
    "invalid-unsaved.moss", "fn broken(:\n",
    "references", "fn:left", "--source", str(shadow), "--json", expect=1,
)
if invalid_overlay.get("ok") is not False:
    fail("invalid unsaved source did not produce an explicit semantic error")

module_symbols = invoke(
    "symbols", "math.", "--source", str(module_main), "--json"
)["result"]["symbols"]
if not any(item["qualified_name"] == "math.leaf" for item in module_symbols):
    fail("module-qualified exported symbol is missing")
all_module_symbols = invoke(
    "symbols", "math", "--source", str(module_main), "--json"
)["result"]["symbols"]
if not any(item["qualified_name"] == "math" and item["kind"] == "module"
           for item in all_module_symbols):
    fail("module semantic entity is missing")

module_completion = invoke(
    "complete", "at:5:13", "--source", str(module_main), "--json"
)["result"]["candidates"]
if not any(item["label"] == "leaf" for item in module_completion):
    fail("module-qualified completion omitted exported member")

for selector in ("at:2:8", "at:5:8"):
    module_target = invoke(
        "resolve", selector, "--source", str(module_main), "--json",
    )["result"]["target"]
    if module_target["id"] != "entity-v1:module:math":
        fail(f"module token {selector} did not resolve semantically")
    if pathlib.Path(module_target["source"]["file"]) != (
        module_project / "src" / "math.moss"
    ):
        fail(f"module token {selector} resolved to the wrong physical source")
    if module_target["source"]["line"] != 1:
        fail(f"module token {selector} did not resolve to the module declaration")

module_references = invoke(
    "references", "at:2:8", "--source", str(module_main), "--json",
)["result"]["references"]
if [(item["kind"], item["source"]["line"]) for item in module_references] != [
    ("module_import", 2),
    ("module_qualifier", 5),
    ("declaration", 1),
]:
    fail("module references omitted the import or checked qualified use")

print("Phase 22.5 semantic IDE query checks passed")
