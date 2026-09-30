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


for discovery in ("bootstrap", "capabilities", "schema"):
    text = json.dumps(invoke("agent", discovery, "--json")["result"])
    for operation in ("references", "symbols", "complete"):
        if operation not in text:
            fail(f"agent {discovery} omitted {operation}")

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

print("Phase 22.5 semantic IDE query checks passed")
