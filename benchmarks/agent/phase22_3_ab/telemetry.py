#!/usr/bin/env python3
"""Deterministic event and command telemetry for the Phase 22.3 A/B benchmark."""

from __future__ import annotations

from collections import Counter
import json
from pathlib import Path
import re
import shlex
from typing import Any, Iterable


READERS = {"cat", "sed", "head", "tail", "grep", "rg", "awk", "less", "nl"}
SEMANTIC_QUERIES = {"resolve", "inspect", "type", "ownership", "effects", "calls", "why", "cost"}
VALIDATION_MOSS = {"check", "build", "test", "debug"}
VALIDATION_MARGO = {"build", "test", "run"}


def parse_jsonl(path: Path) -> list[dict[str, Any]]:
    records = []
    if not path.is_file():
        return records
    for line in path.read_text(encoding="utf-8", errors="replace").splitlines():
        try:
            value = json.loads(line)
        except json.JSONDecodeError:
            continue
        if isinstance(value, dict):
            records.append(value)
    return records


def _strings(value: Any, key: str = "") -> Iterable[tuple[str, str]]:
    if isinstance(value, dict):
        for child_key, child in value.items():
            yield from _strings(child, str(child_key))
    elif isinstance(value, list):
        for child in value:
            yield from _strings(child, key)
    elif isinstance(value, str):
        yield key, value


def event_commands(events: list[dict[str, Any]]) -> list[str]:
    commands: list[str] = []
    for event in events:
        if event.get("type") not in {"item.started", "item.completed"}:
            continue
        item = event.get("item")
        if not isinstance(item, dict):
            continue
        if item.get("type") not in {"command_execution", "shell_command", "exec_command"}:
            continue
        candidates = [value for key, value in _strings(item) if key in {"command", "cmd"}]
        if candidates:
            command = candidates[0]
            if not commands or commands[-1] != command or event.get("type") == "item.started":
                if event.get("type") == "item.completed" and commands and commands[-1] == command:
                    continue
                commands.append(command)
    return commands


def _tokens(command: str) -> list[str]:
    try:
        tokens = shlex.split(command)
    except ValueError:
        tokens = command.split()
    if tokens and Path(tokens[0]).name in {"bash", "sh"}:
        for option in ("-lc", "-c"):
            if option in tokens:
                index = tokens.index(option)
                if index + 1 < len(tokens):
                    try:
                        return shlex.split(tokens[index + 1])
                    except ValueError:
                        return tokens[index + 1].split()
    return tokens


def _reader_and_paths(command: str) -> tuple[str | None, list[str]]:
    tokens = _tokens(command)
    for index, token in enumerate(tokens):
        base = Path(token).name
        if base in READERS:
            paths = []
            for candidate in tokens[index + 1:]:
                if candidate.startswith("-") or candidate in {"|", "&&", ";"}:
                    continue
                if re.search(r"(?:\.moss|\.mossi|\.rs|\.md|SKILL\.md|AGENTS\.md)(?::\d+)?$", candidate):
                    paths.append(candidate.split(":", 1)[0])
            return base, paths
    return None, []


def classify_source_reads(commands: list[str], workspace: Path) -> dict[str, Any]:
    counts: Counter[str] = Counter()
    moss_files: set[str] = set()
    source_bytes = 0
    details = []
    for index, command in enumerate(commands, 1):
        reader, paths = _reader_and_paths(command)
        if reader is None:
            continue
        kinds: set[str] = set()
        for raw in paths:
            normalized = raw.lstrip("./")
            if normalized.endswith(".mossi"):
                kinds.add("mossi_inspection_commands")
            elif normalized.endswith(".rs"):
                kinds.add("generated_rust_inspection_commands")
            elif normalized.endswith(".moss"):
                kinds.add("moss_source_inspection_commands")
                moss_files.add(normalized)
                path = workspace / normalized
                if path.is_file():
                    source_bytes += path.stat().st_size
            elif normalized.endswith(".md"):
                kinds.add("documentation_inspection_commands")
        if not paths and re.search(r"(?:docs/|AGENTS\.md|SKILL\.md)", command):
            kinds.add("documentation_inspection_commands")
        for kind in kinds:
            counts[kind] += 1
        if kinds:
            details.append({"event_index": index, "command": command, "classes": sorted(kinds), "paths": paths})
    return {
        "moss_source_inspection_commands": counts["moss_source_inspection_commands"],
        "mossi_inspection_commands": counts["mossi_inspection_commands"],
        "generated_rust_inspection_commands": counts["generated_rust_inspection_commands"],
        "documentation_inspection_commands": counts["documentation_inspection_commands"],
        "unique_moss_source_files_read": len(moss_files),
        "moss_source_files_read": sorted(moss_files),
        "approx_source_bytes_read": source_bytes,
        "source_read_events": details,
    }


def _contains(value: Any, expected: Any) -> bool:
    if value == expected:
        return True
    if isinstance(value, dict):
        return any(_contains(child, expected) for child in value.values())
    if isinstance(value, list):
        return any(_contains(child, expected) for child in value)
    return False


def query_contains_ground_truth(record: dict[str, Any], facts: dict[str, Any]) -> bool:
    if record.get("exit_code") != 0 or not facts:
        return False
    document = record.get("query_document")
    if not isinstance(document, dict) or document.get("ok") is not True:
        return False
    return all(_contains(document.get("result"), expected) for expected in facts.values())


def validation_kind(record: dict[str, Any]) -> str | None:
    if record.get("origin") != "agent":
        return None
    argv = record.get("argv")
    if not isinstance(argv, list) or not argv:
        return None
    if record.get("tool") == "margo" and argv[0] in VALIDATION_MARGO:
        return f"margo-{argv[0]}"
    if record.get("tool") == "moss":
        if argv[0] in VALIDATION_MOSS:
            return f"moss-{argv[0]}"
        if argv[0] == "run" and "--interp" in argv:
            return "moss-interpreter"
        if argv[0] == "--check":
            return "moss-check"
    return None


def query_operation(record: dict[str, Any]) -> str | None:
    argv = record.get("argv")
    if record.get("tool") == "moss" and record.get("origin") == "agent" and isinstance(argv, list) and argv and argv[0] in SEMANTIC_QUERIES:
        return argv[0]
    return None


def semantic_and_efficiency_metrics(tool_records: list[dict[str, Any]], facts: dict[str, Any]) -> dict[str, Any]:
    direct = sorted((record for record in tool_records if record.get("origin") == "agent"), key=lambda item: item.get("sequence", 0))
    validations = [record for record in direct if validation_kind(record)]
    attempts_to_green = next((index for index, record in enumerate(validations, 1) if record.get("exit_code") == 0), None)
    query_records = [record for record in direct if query_operation(record)]
    successful = [record for record in query_records if record.get("exit_code") == 0 and isinstance(record.get("query_document"), dict) and record["query_document"].get("ok") is True]
    correct_query = next((record for record in query_records if query_contains_ground_truth(record, facts)), None)
    correct_query_ordinal = next((index for index, record in enumerate(query_records, 1) if record is correct_query), None)
    boundary = correct_query.get("sequence") if correct_query else float("inf")
    detours = 0
    for record in direct:
        if record.get("sequence", 0) >= boundary:
            break
        operation = query_operation(record)
        if operation and (record.get("exit_code") != 0 or record.get("semantic_query_status") in {"ambiguous", "missing"}):
            detours += 1
        elif validation_kind(record):
            detours += 1
    statuses = Counter(record.get("semantic_query_status") for record in query_records if record.get("semantic_query_status"))
    operations = Counter(query_operation(record) for record in query_records)
    diagnostics = Counter(code for record in direct for code in record.get("diagnostic_codes", []))
    repeated = sum(count - 1 for count in diagnostics.values() if count > 1)
    return {
        "first_validation_success": None if not validations else validations[0].get("exit_code") == 0,
        "validation_attempts_to_green": attempts_to_green,
        "validation_attempts_total": len(validations),
        "moss_margo_invocations": len(direct),
        "semantic_query_invocations": len(query_records),
        "successful_semantic_queries": len(successful),
        "failed_semantic_queries": len(query_records) - len(successful),
        "resolved_queries": statuses["resolved"], "ambiguous_queries": statuses["ambiguous"], "missing_queries": statuses["missing"],
        "query_type_distribution": dict(sorted((key, value) for key, value in operations.items() if key)),
        **{f"{name}_invocations": operations[name] for name in sorted(SEMANTIC_QUERIES)},
        "semantic_fact_obtained_by_query": correct_query is not None,
        "semantic_fact_query_sequence": correct_query.get("sequence") if correct_query else None,
        "semantic_fact_query_ordinal": correct_query_ordinal,
        "semantic_detour_cost_tool_actions": detours,
        "diagnostic_occurrences": sum(diagnostics.values()),
        "diagnostic_codes": dict(sorted(diagnostics.items())),
        "repeated_diagnostic_loops": repeated,
    }


def commands_before_correct_query(commands: list[str], query_ordinal: int | None) -> list[str]:
    """Bound event commands at the exec item containing the fact-yielding query."""
    if query_ordinal is None:
        return commands
    seen = 0
    for index, command in enumerate(commands):
        tokens = _tokens(command)
        for token_index, token in enumerate(tokens[:-1]):
            if Path(token).name == "moss" and tokens[token_index + 1] in SEMANTIC_QUERIES:
                seen += 1
                if seen == query_ordinal:
                    return commands[:index]
    return commands


def parse_agent_runtime(events: list[dict[str, Any]]) -> dict[str, Any]:
    thread_id = None
    usage = None
    tool_calls = 0
    item_types: Counter[str] = Counter()
    for event in events:
        if event.get("type") == "thread.started" and isinstance(event.get("thread_id"), str):
            thread_id = event["thread_id"]
        if event.get("type") == "item.completed" and isinstance(event.get("item"), dict):
            kind = event["item"].get("type")
            if isinstance(kind, str):
                item_types[kind] += 1
                if kind not in {"agent_message", "reasoning"}:
                    tool_calls += 1
        if event.get("type") == "turn.completed" and isinstance(event.get("usage"), dict):
            usage = event["usage"]
    return {"thread_id": thread_id, "tool_calls": tool_calls if events else None, "completed_item_types": dict(sorted(item_types.items())), "token_usage": usage}
