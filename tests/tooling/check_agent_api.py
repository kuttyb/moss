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
                    "package_dependencies", "package_lockfile", "language_surface",
                    "fast_debug"):
    if capability not in capabilities["result"]["capabilities"]:
        fail(f"capability discovery omitted {capability}")
if not schema["result"]["schema"]["diagnostic_codes_are_stable"]:
    fail("schema discovery did not promise stable diagnostic codes")
if "impact" not in schema["result"]["schema"]["project_result_kinds"]:
    fail("schema discovery omitted impact result kind")
if "Do not edit generated Rust." not in bootstrap["result"]["safety_rules"]:
    fail("bootstrap omitted the generated-Rust safety rule")
catalog = {item["id"]: item for item in capabilities["result"]["capability_catalog"]}
if catalog["package_project_driver"]["entrypoint"] != "margo build|run|test|bench|clean":
    fail("Margo is not the canonical package/project capability entrypoint")
if catalog["language_surface"]["entrypoint"] != "moss agent bootstrap --json":
    fail("language surface discovery lacks its bootstrap entrypoint")
surface = bootstrap["result"].get("source_surface", {})
if surface.get("operators", {}).get("boolean_negation") != "not expression":
    fail("bootstrap source surface omitted boolean negation")
if surface.get("operators", {}).get("arithmetic") != ["+", "-", "*", "/", "%"]:
    fail("bootstrap source surface omitted integer remainder")
collections = surface.get("collections", {})
if collections.get("empty_typed_vector") != "Vector[T]()" or collections.get("local_type_annotations") is not False:
    fail("bootstrap source surface omitted typed empty Vector construction")
if surface.get("locals", {}).get("immutable") != "let x = expression":
    fail("bootstrap source surface omitted immutable locals")
if surface.get("domains", {}).get("fn_inside_domain") != "handler":
    fail("bootstrap source surface omitted domain handler distinction")
debug_features = {item["name"]: item for item in bootstrap["result"]["debugging_features"]}
if "for traversal is not currently supported" not in debug_features["fast_debug"].get("limitations", []):
    fail("Fast Debug discovery omitted its verified for traversal limitation")
if "container methods reached through object fields are not currently supported" not in debug_features["fast_debug"].get("limitations", []):
    fail("Fast Debug discovery omitted its verified container-field limitation")
actions = {item["name"]: item for item in bootstrap["result"]["actions"]}
for action in ("package_build", "package_run", "package_test", "package_bench", "package_clean"):
    if action not in actions or not actions[action]["command"].startswith("margo "):
        fail(f"bootstrap omitted canonical Margo action {action}")
for capability in ("package_project_driver", "package_dependencies", "package_lockfile"):
    if not bootstrap["result"]["capability_flags"].get(capability):
        fail(f"bootstrap did not flag {capability} as available")
schema_names = {item["name"] for item in schema["result"]["schema"]["command_schemas"]}
if "package_project_driver" not in schema_names:
    fail("agent schema omitted the Margo package/project contract")

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
