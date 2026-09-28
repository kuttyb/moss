#!/usr/bin/env python3
"""Benchmark-only Moss/Margo proxy for query-profile gating and telemetry."""

from __future__ import annotations

import fcntl
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
import time
from typing import Any


QUERY_COMMANDS = {"resolve", "inspect", "type", "effects", "ownership", "calls", "why", "cost"}
OLD_TARGET_FIELDS = {
    "semantic_identity", "durable_identity", "identity_version", "construct_kind",
    "source_identity", "specialization_identity", "name", "module_identity",
    "export_visibility", "export_kind", "source", "type", "specialization_fields",
    "provenance", "implementation_hash", "semantic_interface_hash",
}
OLD_RESULT_FIELDS = {
    "inspect": {"target", "resolved_type", "ownership", "observable_effects", "enclosing_callable_effects", "direct_calls", "callers", "explanations", "cost_facts", "synchronization_plan", "synchronization_dump", "concrete_domain_graph"},
    "type": {"target", "resolved_type"},
    "effects": {"target", "ownership", "observable_effects", "enclosing_callable_effects", "synchronization_plan", "synchronization_dump"},
    "ownership": {"target", "parameters_and_values", "reason"},
    "calls": {"target", "direct_calls", "callers"},
    "why": {"target", "explanations", "synchronization_plan", "synchronization_dump"},
    "cost": {"target", "cost_facts"},
}


def next_sequence(path: Path) -> int:
    path.parent.mkdir(parents=True, exist_ok=True)
    counter = path.with_suffix(path.suffix + ".sequence")
    lock_path = path.with_suffix(path.suffix + ".lock")
    with lock_path.open("a", encoding="utf-8") as lock:
        fcntl.flock(lock.fileno(), fcntl.LOCK_EX)
        try:
            value = int(counter.read_text(encoding="utf-8")) + 1
        except (OSError, ValueError):
            value = 1
        counter.write_text(str(value), encoding="utf-8")
        return value


def append_record(path: Path, value: dict[str, Any]) -> None:
    lock_path = path.with_suffix(path.suffix + ".lock")
    with lock_path.open("a", encoding="utf-8") as lock:
        fcntl.flock(lock.fileno(), fcntl.LOCK_EX)
        with path.open("a", encoding="utf-8") as output:
            output.write(json.dumps(value, sort_keys=True, separators=(",", ":")) + "\n")


def parse_document(stdout: str) -> dict[str, Any] | None:
    try:
        value = json.loads(stdout)
    except json.JSONDecodeError:
        return None
    return value if isinstance(value, dict) else None


def diagnostic_codes(document: dict[str, Any] | None) -> list[str]:
    if not document:
        return []
    codes: list[str] = []
    error = document.get("error")
    if isinstance(error, dict) and isinstance(error.get("code"), str):
        codes.append(error["code"])
    result = document.get("result")
    diagnostics = result.get("diagnostics") if isinstance(result, dict) else None
    if isinstance(diagnostics, list):
        codes.extend(item["code"] for item in diagnostics if isinstance(item, dict) and isinstance(item.get("code"), str))
    return list(dict.fromkeys(codes))


def query_status(document: dict[str, Any] | None) -> str | None:
    if not document:
        return None
    result = document.get("result")
    if isinstance(result, dict) and result.get("status") == "resolved":
        return "resolved"
    error = document.get("error")
    details = error.get("details") if isinstance(error, dict) else None
    resolution = details.get("resolution") if isinstance(details, dict) else None
    status = resolution.get("status") if isinstance(resolution, dict) else None
    return status if status in {"ambiguous", "missing"} else None


def legacy_unavailable(command: str, feature: str = "resolve") -> tuple[int, str, str]:
    document = {
        "protocol_version": 1,
        "schema_version": "moss-agent-1",
        "compiler_version": "0.1.0",
        "command": command,
        "ok": False,
        "error": {
            "code": "QUERY_PROFILE_UNAVAILABLE",
            "message": f"{feature} is intentionally unavailable in the legacy benchmark query profile",
        },
    }
    return 2, json.dumps(document, indent=2) + "\n", ""


def remove_resolve_strings(values: Any) -> Any:
    if isinstance(values, list):
        return [remove_resolve_strings(item) for item in values if not (isinstance(item, str) and ("moss resolve" in item or "Use resolve" in item))]
    if isinstance(values, dict):
        return {key: remove_resolve_strings(value) for key, value in values.items() if key not in {"query_resolution", "semantic_entity_fields"}}
    return values


def project_legacy_discovery(document: dict[str, Any]) -> dict[str, Any]:
    result = document.get("result")
    if not isinstance(result, dict):
        return document
    capabilities = result.get("capabilities")
    if isinstance(capabilities, list):
        result["capabilities"] = [item for item in capabilities if item not in {"robust_query_targeting", "semantic_query_expansion"}]
    flags = result.get("capability_flags")
    if isinstance(flags, dict):
        flags.pop("robust_query_targeting", None)
        flags.pop("semantic_query_expansion", None)
    queries = result.get("semantic_queries")
    if isinstance(queries, list):
        result["semantic_queries"] = [item for item in queries if not (isinstance(item, dict) and item.get("name") == "resolve")]
    commands = result.get("commands")
    if isinstance(commands, list):
        result["commands"] = [item for item in commands if not (isinstance(item, str) and item.startswith("moss resolve "))]
    hints = result.get("workflow_hints")
    if isinstance(hints, list):
        result["workflow_hints"] = [item for item in hints if not (isinstance(item, dict) and item.get("capability") == "resolve")]
    schema = result.get("schema")
    if isinstance(schema, dict):
        schema.pop("query_resolution", None)
        schema.pop("semantic_entity_fields", None)
        schema["target_selectors"] = [
            "durable entity-v1 identity", "debug/source provenance identity",
            "construct name", "line:<number>",
        ]
        command_schemas = schema.get("command_schemas")
        if isinstance(command_schemas, list):
            for item in command_schemas:
                if isinstance(item, dict) and item.get("name") == "semantic_query":
                    item["required"] = ["operation", "target", "--source", "--json"]
                    item["purpose"] = "Inspect existing checked semantic facts."
                    if isinstance(item.get("output"), str):
                        item["output"] = "moss-agent-1 envelope with the exact legacy target and operation-specific facts"
    result["recommended_workflow"] = [
        item for item in result.get("recommended_workflow", [])
        if not (isinstance(item, str) and "Use resolve" in item)
    ]
    return remove_resolve_strings(document)


def project_legacy_query(document: dict[str, Any], command: str) -> dict[str, Any]:
    if document.get("ok") is not True:
        error = document.get("error")
        if isinstance(error, dict):
            details = error.get("details")
            if isinstance(details, dict):
                details.pop("resolution", None)
        return document
    result = document.get("result")
    if not isinstance(result, dict):
        return document
    allowed = OLD_RESULT_FIELDS.get(command, set())
    projected = {key: value for key, value in result.items() if key in allowed}
    target = projected.get("target")
    if isinstance(target, dict):
        projected["target"] = {key: value for key, value in target.items() if key in OLD_TARGET_FIELDS}
    document["result"] = projected
    return document


def transform(profile: str, tool: str, argv: list[str], exit_code: int, stdout: str, stderr: str) -> tuple[int, str, str]:
    if profile != "legacy" or tool != "moss" or not argv:
        return exit_code, stdout, stderr
    if argv[0] == "resolve":
        return legacy_unavailable("resolve")
    document = parse_document(stdout)
    if document is None:
        return exit_code, stdout, stderr
    if argv[0] == "agent" and len(argv) > 1 and argv[1] in {"bootstrap", "capabilities", "schema"}:
        document = project_legacy_discovery(document)
    elif argv[0] in OLD_RESULT_FIELDS:
        if "--kind" in argv or "--enclosing" in argv or (len(argv) > 1 and argv[1].startswith("at:")):
            return legacy_unavailable(argv[0], "Phase 22.3 target narrowing")
        document = project_legacy_query(document, argv[0])
    return exit_code, json.dumps(document, indent=2) + "\n", stderr


def main() -> int:
    tool = Path(sys.argv[0]).name
    if tool not in {"moss", "margo"}:
        print(f"phase22.3 A/B proxy: unsupported tool {tool}", file=sys.stderr)
        return 127
    environment = dict(os.environ)
    real = environment.get("MOSS_AB_REAL_" + tool.upper())
    log_path = environment.get("MOSS_AB_TOOL_LOG")
    profile = environment.get("MOSS_AGENT_QUERY_PROFILE")
    if not real or not log_path or profile not in {"legacy", "phase22_3"}:
        print("phase22.3 A/B proxy: required environment is missing or invalid", file=sys.stderr)
        return 127
    sequence = next_sequence(Path(log_path))
    origin = environment.get("MOSS_AB_TOOL_ORIGIN", "agent")
    if tool == "margo":
        environment["MOSS_AB_TOOL_ORIGIN"] = "margo-internal"
    started_unix_ns = time.time_ns()
    started = time.monotonic()
    try:
        process = subprocess.run([real, *sys.argv[1:]], text=True, capture_output=True, check=False, env=environment)
        exit_code, stdout, stderr = process.returncode, process.stdout, process.stderr
    except OSError as error:
        exit_code, stdout, stderr = 127, "", str(error) + "\n"
    exit_code, stdout, stderr = transform(profile, tool, sys.argv[1:], exit_code, stdout, stderr)
    duration_ms = round((time.monotonic() - started) * 1000)
    document = parse_document(stdout)
    cwd = Path.cwd().resolve()
    workspace = Path(environment.get("MOSS_AB_WORKSPACE", "/")).resolve()
    try:
        relative_cwd = cwd.relative_to(workspace).as_posix() or "."
    except ValueError:
        relative_cwd = str(cwd)
    operation = sys.argv[1] if len(sys.argv) > 1 else ""
    record = {
        "sequence": sequence, "started_unix_ns": started_unix_ns,
        "tool": tool, "origin": origin, "argv": sys.argv[1:], "cwd": relative_cwd,
        "profile": profile, "exit_code": exit_code, "duration_ms": duration_ms,
        "stdout_bytes": len(stdout.encode()), "stdout_sha256": hashlib.sha256(stdout.encode()).hexdigest(),
        "diagnostic_codes": diagnostic_codes(document),
        "semantic_query_status": query_status(document) if tool == "moss" and operation in QUERY_COMMANDS else None,
        "query_document": document if tool == "moss" and operation in QUERY_COMMANDS else None,
    }
    append_record(Path(log_path), record)
    sys.stdout.write(stdout)
    sys.stderr.write(stderr)
    return exit_code


if __name__ == "__main__":
    raise SystemExit(main())
