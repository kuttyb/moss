#!/usr/bin/env python3
"""Deterministic Phase 6A Moss agent-protocol regression checks."""

from __future__ import annotations

import json
import pathlib
import subprocess
import sys


def fail(message: str) -> None:
    raise SystemExit(f"Phase 6A agent API test failed: {message}")


if len(sys.argv) != 2:
    fail("usage: check_agent_api.py <moss-compiler>")

root = pathlib.Path.cwd().resolve()
compiler = pathlib.Path(sys.argv[1]).resolve()
source = root / "tests" / "phase6_agent_api.moss"


def source_line(text: str) -> int:
    return next(index for index, line in enumerate(source.read_text().splitlines(), 1)
                if line.strip().startswith(text))


def invoke(*arguments: str, expect: int = 0) -> tuple[dict[str, object], bytes]:
    completed = subprocess.run(
        [str(compiler), *arguments],
        cwd=root,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        check=False,
    )
    if completed.returncode != expect:
        fail(
            f"{' '.join(arguments)} exited {completed.returncode}, expected {expect}: "
            + completed.stderr.decode("utf-8", "replace")
        )
    try:
        document = json.loads(completed.stdout)
    except json.JSONDecodeError as error:
        fail(f"{' '.join(arguments)} did not emit valid JSON: {error}")
    for field in (
        "protocol_version",
        "schema_version",
        "compiler_version",
        "command",
        "ok",
    ):
        if field not in document:
            fail(f"{arguments[0]} omitted envelope field {field}")
    if document["protocol_version"] != 1:
        fail("unexpected protocol version")
    if document["schema_version"] != "moss-agent-1":
        fail("unexpected schema version")
    return document, completed.stdout


bootstrap, bootstrap_bytes = invoke("agent", "bootstrap", "--json")
_, repeated_bootstrap = invoke("agent", "bootstrap", "--json")
if bootstrap_bytes != repeated_bootstrap:
    fail("bootstrap output is not deterministic")
capabilities, _ = invoke("agent", "capabilities", "--json")
schema, _ = invoke("agent", "schema", "--json")
if "semantic_inspection" not in capabilities["result"]["capabilities"]:
    fail("capability discovery omitted semantic inspection")
for capability in ("impact_analysis", "formatter", "semantic_edits",
                    "repair_actions", "static_cost_facts", "package_project_driver",
                    "package_dependencies", "package_lockfile", "tool_invocation", "language_surface",
                    "fast_debug"):
    if capability not in capabilities["result"]["capabilities"]:
        fail(f"capability discovery omitted {capability}")
if not schema["result"]["schema"]["diagnostic_codes_are_stable"]:
    fail("schema discovery did not promise stable diagnostic codes")
teaching_fields = bootstrap["result"].get("diagnostic_contract", {}).get(
    "additive_teaching_fields"
)
if teaching_fields != ["source", "rule", "cause", "related", "guidance"]:
    fail("bootstrap omitted the additive teaching-diagnostic contract")
repair_fields = schema["result"]["schema"].get("diagnostic_repair_fields", [])
for field in ("source", "rule", "cause", "related", "guidance"):
    if field not in repair_fields:
        fail(f"agent schema omitted teaching diagnostic field {field}")
if "impact" not in schema["result"]["schema"]["project_result_kinds"]:
    fail("schema discovery omitted impact result kind")
if "Do not edit generated Rust." not in bootstrap["result"]["safety_rules"]:
    fail("bootstrap omitted the generated-Rust safety rule")
catalog = {item["id"]: item for item in capabilities["result"]["capability_catalog"]}
if catalog["package_project_driver"]["entrypoint"] != "margo build|run|test|bench|clean":
    fail("Margo is not the canonical package/project capability entrypoint")
if catalog["language_surface"]["entrypoint"] != "moss agent bootstrap --json":
    fail("language surface discovery lacks its bootstrap entrypoint")
if catalog["tool_invocation"]["entrypoint"] != "moss agent bootstrap --json":
    fail("tool invocation discovery lacks its bootstrap entrypoint")
surface = bootstrap["result"].get("source_surface", {})
if surface.get("operators", {}).get("boolean_negation") != "not expression":
    fail("bootstrap source surface omitted boolean negation")
if surface.get("operators", {}).get("arithmetic") != ["+", "-", "*", "/", "%"]:
    fail("bootstrap source surface omitted integer remainder")
collections = surface.get("collections", {})
if collections.get("empty_typed_vector") != "Vector[T]()" or collections.get("local_type_annotations") is not False:
    fail("bootstrap source surface omitted typed empty Vector construction")
vector = collections.get("Vector", {})
if (vector.get("construction") != {"literal": "[a, b, c]", "empty_typed": "Vector[T]()"}
        or vector.get("methods") != ["push(item)", "pop()"]
        or vector.get("indexing") != {"read": "vec[i]", "write": "vec[i] = item"}
        or vector.get("cardinality") != "vec |> count"):
    fail("bootstrap collection surface omitted Vector operations")
map_surface = collections.get("Map", {})
if (map_surface.get("construction") != {"inferred": "Map()"}
        or map_surface.get("indexing") != {"read": "map[key]", "write": "map[key] = value"}
        or map_surface.get("methods") != ["get(key, default)", "keys()", "values()"]
        or map_surface.get("iteration_note") != "keys() and values() return eager owned Vector snapshots"
        or map_surface.get("deletion_supported") is not False):
    fail("bootstrap collection surface omitted Map operations")
queue = collections.get("Queue", {})
if queue.get("construction") != {"inferred": "Queue()"} or queue.get("methods") != ["push(item)", "pop()"]:
    fail("bootstrap collection surface omitted Queue operations")
if surface.get("locals", {}).get("immutable") != "let x = expression":
    fail("bootstrap source surface omitted immutable locals")
if surface.get("domains", {}).get("fn_inside_domain") != "handler":
    fail("bootstrap source surface omitted domain handler distinction")
tests_surface = surface.get("tests", {})
if (tests_surface.get("syntax") != 'test "name":'
        or tests_surface.get("assertions") != ["assert(condition)", "assertEqual(actual, expected)"]
        or tests_surface.get("domain_topology") != {
            "test_blocks_are_composition_roots": False,
            "composition_root": "main initial composition prefix",
        }):
    fail("bootstrap source surface omitted test/domain topology")
composition = surface.get("domains", {}).get("composition", {})
if composition.get("domain_instances") != "constructed statically in main's initial composition prefix":
    fail("bootstrap source surface omitted domain composition location")
if "side-effect-free" not in composition.get("initializer_rule", ""):
    fail("bootstrap source surface omitted composition initializer restriction")
canonical_docs = bootstrap["result"].get("canonical_docs", {})
if canonical_docs != {
    "practical_language_guide": "docs/GENTLE_INTRODUCTION_TO_MOSS.md",
    "formal_language_design": "docs/MOSS_V0_1_LANGUAGE_DESIGN.md",
    "project_workflow": "docs/PROJECT_WORKFLOW.md",
    "testing": "docs/TESTING.md",
}:
    fail("bootstrap canonical documentation routing drifted")
project_surface = bootstrap["result"].get("project_surface", {})
if project_surface != {
    "manifest": "Moss.toml",
    "minimal_manifest": '[project]\nname = "app"\nversion = "0.1.0"\n\n[build]\nsource = "src"\n',
    "source_directory": "src",
    "project_driver": "./margo",
}:
    fail("bootstrap project surface drifted")
debug_features = {item["name"]: item for item in bootstrap["result"]["debugging_features"]}
if "for traversal is not currently supported" not in debug_features["fast_debug"].get("limitations", []):
    fail("Fast Debug discovery omitted its verified for traversal limitation")
if "container methods reached through object fields are not currently supported" in debug_features["fast_debug"].get("limitations", []):
    fail("Fast Debug discovery retained a repaired container-field limitation")
actions = {item["name"]: item for item in bootstrap["result"]["actions"]}
for action in ("package_build", "package_run", "package_test", "package_bench", "package_clean"):
    if action not in actions or not actions[action]["command"].startswith("margo "):
        fail(f"bootstrap omitted canonical Margo action {action}")
for capability in ("package_project_driver", "package_dependencies", "package_lockfile"):
    if not bootstrap["result"]["capability_flags"].get(capability):
        fail(f"bootstrap did not flag {capability} as available")
if not bootstrap["result"]["capability_flags"].get("tool_invocation"):
    fail("bootstrap did not flag tool_invocation as available")
if bootstrap["result"].get("tool_invocation") != {
    "compiler": {"repo_local": "./moss", "path_name": "moss", "build_if_missing": "make"},
    "project_driver": {"repo_local": "./margo", "path_name": "margo"},
    "preferred_in_checkout": "repo_local",
    "path_names_allowed": True,
    "path_fallback": "PATH",
    "bootstrap_command": "./moss agent bootstrap --json",
}:
    fail("bootstrap tool_invocation metadata drifted")
discovery = bootstrap["result"].get("discovery", {})
if discovery.get("capabilities_command") != "./moss agent capabilities --json":
    fail("bootstrap discovery capabilities command is not repo-local")
if discovery.get("schema_command") != "./moss agent schema --json":
    fail("bootstrap discovery schema command is not repo-local")
schema_names = {item["name"] for item in schema["result"]["schema"]["command_schemas"]}
if "package_project_driver" not in schema_names:
    fail("agent schema omitted the Margo package/project contract")
if "tool_invocation" not in schema_names:
    fail("agent schema omitted repository-local tool invocation")

checked, checked_bytes = invoke("check", str(source), "--json")
_, repeated_check = invoke("check", str(source), "--json")
if checked_bytes != repeated_check:
    fail("structured check output is not deterministic")
if not checked["ok"] or checked["result"]["diagnostics"]:
    fail("valid source did not produce an empty structured diagnostic list")

warning, _ = invoke(
    "check", str(root / "tests" / "large_payload_warning.moss"), "--json"
)
warning_codes = [item["code"] for item in warning["result"]["diagnostics"]]
if "MESSAGE_PAYLOAD_COPY_LARGE" not in warning_codes:
    fail("large message warning omitted its stable diagnostic code")

ownership_error, _ = invoke(
    "check",
    str(root / "examples" / "use_after_transfer.moss"),
    "--json",
    expect=1,
)
error = ownership_error["error"]
if error["code"] != "OWNERSHIP_USE_AFTER_CONSUME":
    fail("use-after-consume diagnostic code is not stable")
if error["details"]["ownership_actual"] != "CONSUMED":
    fail("ownership diagnostic omitted structured state")
if not error["semantic_identity"]:
    fail("structured diagnostic omitted available semantic identity")


def query(command: str, target: str, *, optimized: bool = False) -> dict[str, object]:
    arguments = [command, target, "--source", str(source), "--json"]
    if optimized:
        arguments.append("-O")
    document, _ = invoke(*arguments)
    return document["result"]


inspection = query("inspect", "fn:inspect")
if inspection["target"]["construct_kind"] != "function":
    fail("inspect did not resolve a function semantic target")
if inspection["direct_calls"][0]["target"] != "method:Sample.read":
    fail("inspect omitted the statically resolved method call")

type_result = query("type", "Sample.value")
if type_result["resolved_type"] != "int":
    fail("type query did not return the resolved field type")

effects = query("effects", "fn:noisy")
if not effects["observable_effects"]["external_io"]:
    fail("effects query omitted existing I/O effect information")
if effects["ownership"][0]["effect"] != "READ":
    fail("effects query conflated ownership and observable effects")

statement_effects = query("effects", f"line:{source_line('local = value + 1')}")
if statement_effects["target"]["construct_kind"] != "binding":
    fail("statement-effect regression did not resolve the binding target")
if statement_effects["observable_effects"] is not None:
    fail("a binding inherited effects from elsewhere in its callable")
enclosing = statement_effects["enclosing_callable_effects"]
if not enclosing or not enclosing["external_io"]:
    fail("statement query omitted separately labelled enclosing-callable effects")

contextual_callable = query("effects", "fn:locally_pure_statement")
if not contextual_callable["observable_effects"]["external_io"]:
    fail("callable effect summaries changed while fixing statement precision")
if contextual_callable["enclosing_callable_effects"] is not None:
    fail("callable target incorrectly reports itself as an enclosing callable")

pipeline_target = f"main@{source_line('observed_count =')}:expression:0"
pipeline_effects = query("effects", pipeline_target, optimized=True)
if not pipeline_effects["observable_effects"]["external_io"]:
    fail("precise pipeline effect summaries changed")
if pipeline_effects["enclosing_callable_effects"] is not None:
    fail("pipeline target incorrectly inherited a callable effect summary")

ownership = query("ownership", "fn:transfer")
if ownership["parameters_and_values"][0]["effect"] != "CONSUME":
    fail("ownership query omitted inferred consume behavior")

calls = query("calls", "fn:inspect")
if calls["direct_calls"][0]["line"] != 11:
    fail("call edge omitted Moss source provenance")

topology = query("inspect", "handler:Router.Route")["concrete_domain_graph"]
if not topology["closed"] or not topology["edges"]:
    fail("synchronous domain topology is missing")

why = query("why", pipeline_target, optimized=True)
explanations = "\n".join(why["explanations"])
if "fusion stopped" not in explanations or "observable callback effect" not in explanations:
    fail("why query did not reuse functional optimization explanations")

location = query("type", f"line:{source_line('worker = Worker()')}")
if location["target"]["construct_kind"] != "binding":
    fail("source-location query did not resolve an exact semantic target")

missing, _ = invoke(
    "inspect",
    "fn:not_present",
    "--source",
    str(source),
    "--json",
    expect=1,
)
if missing["error"]["code"] != "QUERY_TARGET_NOT_FOUND":
    fail("malformed/missing query target lacked a structured error")

missing_source, _ = invoke("inspect", "fn:inspect", "--json", expect=2)
if missing_source["error"]["code"] != "QUERY_SOURCE_REQUIRED":
    fail("missing query source lacked a structured error")

print("Phase 6A agent API checks passed")
