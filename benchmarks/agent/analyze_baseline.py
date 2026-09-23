#!/usr/bin/env python3
"""Deterministically aggregate a checked-in Phase 22.2C agent baseline."""

from __future__ import annotations

import argparse
from collections import Counter, defaultdict
import hashlib
import json
from pathlib import Path
import statistics
import sys
from typing import Any


ROOT = Path(__file__).resolve().parents[2]
SUITE = ROOT / "benchmarks" / "agent"
DEFAULT_BASELINE = SUITE / "baselines" / "pre-22.1"
SCHEMA_VERSION = "moss-agent-baseline-aggregate-1"

CORE_TOOLS = (
    "moss agent bootstrap",
    "moss check",
    "moss inspect",
    "moss type",
    "moss effects",
    "moss ownership",
    "moss calls",
    "moss why",
    "moss run --interp",
    "moss debug",
    "trace",
    "margo build",
    "margo test",
    "margo run",
)

# These requirements transcribe tool names or alternatives stated by workflow-task
# prompts. They are corpus annotations for analysis, not pass/fail requirements.
WORKFLOW_TOOL_EXPECTATIONS: dict[str, list[tuple[str, frozenset[str]]]] = {
    "AB015": [
        ("structured check", frozenset({"moss check"})),
        ("effects query", frozenset({"moss effects"})),
        ("program execution", frozenset({"moss run --interp", "moss debug", "trace"})),
    ],
    "AB021": [("structured compiler diagnostic", frozenset({"moss check"}))],
    "AB022": [
        ("effects feedback", frozenset({"moss effects"})),
        ("ownership feedback", frozenset({"moss ownership"})),
    ],
    "AB025": [("Margo run", frozenset({"margo run"}))],
    "AB026": [("Margo run", frozenset({"margo run"}))],
    "AB027": [
        ("debug or interpreter", frozenset({"moss debug", "moss run --interp", "trace"})),
    ],
    "AB028": [("structured trace", frozenset({"trace"}))],
    "AB029": [
        ("bootstrap", frozenset({"moss agent bootstrap"})),
        ("Margo build", frozenset({"margo build"})),
        ("Margo test", frozenset({"margo test"})),
    ],
    "AB030": [("Margo test", frozenset({"margo test"}))],
}


class AnalysisError(Exception):
    pass


def read_json(path: Path) -> dict[str, Any]:
    try:
        value = json.loads(path.read_text(encoding="utf-8"))
    except FileNotFoundError as error:
        raise AnalysisError(f"missing baseline input: {path}") from error
    except json.JSONDecodeError as error:
        raise AnalysisError(f"invalid JSON in {path}: {error}") from error
    if not isinstance(value, dict):
        raise AnalysisError(f"expected JSON object in {path}")
    return value


def read_jsonl(path: Path) -> list[dict[str, Any]]:
    try:
        lines = path.read_text(encoding="utf-8").splitlines()
    except FileNotFoundError as error:
        raise AnalysisError(f"missing baseline input: {path}") from error
    records: list[dict[str, Any]] = []
    for index, line in enumerate(lines, 1):
        try:
            value = json.loads(line)
        except json.JSONDecodeError as error:
            raise AnalysisError(f"invalid JSONL in {path}:{index}: {error}") from error
        if not isinstance(value, dict):
            raise AnalysisError(f"expected JSON object in {path}:{index}")
        records.append(value)
    return sorted(records, key=lambda record: record.get("sequence", 0))


def stable_json(value: Any) -> str:
    return json.dumps(value, indent=2, sort_keys=True) + "\n"


def percentage(count: int, total: int) -> float | None:
    return round(100.0 * count / total, 1) if total else None


def numeric_stats(values: list[int]) -> dict[str, Any]:
    return {
        "count": len(values),
        "mean": round(statistics.mean(values), 3) if values else None,
        "median": statistics.median(values) if values else None,
        "max": max(values) if values else None,
    }


def raw_tree_digest(root: Path) -> str:
    """Digest raw inputs while excluding the two generated analysis products."""
    digest = hashlib.sha256()
    excluded = {"aggregate.json", "REPORT.md"}
    for path in sorted(candidate for candidate in root.rglob("*") if candidate.is_file()):
        if path.parent == root and path.name in excluded:
            continue
        digest.update(path.relative_to(root).as_posix().encode())
        digest.update(b"\0")
        digest.update(path.read_bytes())
        digest.update(b"\0")
    return digest.hexdigest()


def load_tasks(tasks_root: Path) -> dict[str, dict[str, Any]]:
    tasks: dict[str, dict[str, Any]] = {}
    for path in sorted(tasks_root.glob("AB*/task.json")):
        task = read_json(path)
        task_id = task.get("id")
        if not isinstance(task_id, str):
            raise AnalysisError(f"task metadata has no stable ID: {path}")
        if task_id in tasks:
            raise AnalysisError(f"duplicate task metadata ID: {task_id}")
        tasks[task_id] = task
    expected = [f"AB{index:03d}" for index in range(1, 31)]
    if sorted(tasks) != expected:
        missing = sorted(set(expected) - set(tasks))
        extra = sorted(set(tasks) - set(expected))
        raise AnalysisError(f"task metadata IDs do not match AB001-AB030; missing={missing}, extra={extra}")
    return tasks


def tool_key(record: dict[str, Any]) -> str | None:
    if record.get("origin") != "agent":
        return None
    tool = record.get("tool")
    argv = record.get("argv")
    if not isinstance(argv, list) or not argv:
        return None
    operation = argv[0]
    if tool == "margo":
        return f"margo {operation}"
    if tool != "moss":
        return None
    if operation == "agent" and len(argv) > 1 and argv[1] == "bootstrap":
        return "moss agent bootstrap"
    if operation in {"inspect", "type", "effects", "ownership", "calls", "why"}:
        return f"moss {operation}"
    if operation == "run" and "--interp" in argv:
        return "trace" if "--trace" in argv else "moss run --interp"
    if operation == "debug":
        return "trace" if "--trace" in argv else "moss debug"
    if operation in {"check", "--check"}:
        return "moss check"
    if operation == "fmt":
        return "moss fmt"
    if operation in {"build", "test", "run"}:
        return f"moss {operation}"
    return f"moss {operation.lstrip('-')}"


def is_correctness_attempt(record: dict[str, Any]) -> bool:
    if record.get("origin") != "agent":
        return False
    argv = record.get("argv")
    if not isinstance(argv, list) or not argv:
        return False
    operation = argv[0]
    if record.get("tool") == "margo":
        return operation in {"build", "test", "run"}
    if record.get("tool") != "moss":
        return False
    return (operation in {"check", "build", "test", "debug", "--check"}
            or (operation == "run" and "--interp" in argv))


def command_label(record: dict[str, Any]) -> str:
    tool = record.get("tool", "?")
    argv = record.get("argv", [])
    return " ".join([str(tool), *(str(item) for item in argv)])


def classify_failures(manifest: dict[str, Any], result: dict[str, Any] | None) -> list[str]:
    if manifest.get("state") == "infrastructure_failure":
        return ["infrastructure-failure"]
    if manifest.get("agent_timed_out"):
        return ["did-not-converge"]
    if manifest.get("final_status") != "fail":
        return []
    classes: list[str] = []
    if manifest.get("outside_allowed_paths"):
        classes.append("out-of-scope-edit")
    validations = result.get("result", {}).get("validation", []) if result else []
    failures = [failure for item in validations for failure in item.get("failures", [])]
    if "stdout did not match exactly" in failures:
        classes.append("exact-output-format-mismatch")
    if any(item.get("assertion") is not None and item.get("status") == "fail"
           for item in validations):
        classes.append("requested-structure-missed")
    if any(item.get("command") is not None and item.get("exit_code") not in {0, None}
           for item in validations):
        classes.append("invalid-final-program-or-project")
    return classes or ["behavioral-validator-failure"]


def validation_failure_details(result: dict[str, Any] | None) -> list[str]:
    if not result:
        return []
    details: list[str] = []
    for item in result.get("result", {}).get("validation", []):
        for failure in item.get("failures", []):
            if failure not in details:
                details.append(failure)
    return details


def load_runs(baseline_root: Path, tasks: dict[str, dict[str, Any]]) -> tuple[dict[str, Any], list[dict[str, Any]]]:
    protocol = read_json(baseline_root / "protocol.json")
    runs: dict[str, dict[str, Any]] = {}
    locations: dict[str, Path] = {}
    for directory in sorted(path for path in baseline_root.iterdir() if path.is_dir()):
        manifest_path = directory / "manifest.json"
        if not manifest_path.is_file():
            continue
        manifest = read_json(manifest_path)
        task_id = manifest.get("task_id")
        if not isinstance(task_id, str):
            raise AnalysisError(f"manifest has no task_id: {manifest_path}")
        if task_id in runs:
            raise AnalysisError(
                f"duplicate canonical task ID {task_id}: {locations[task_id]} and {directory}"
            )
        if task_id not in tasks:
            raise AnalysisError(f"unknown canonical task ID {task_id}: {directory}")
        result_path = directory / "result.json"
        result = read_json(result_path) if result_path.is_file() else None
        state = manifest.get("state")
        if state not in {"completed", "agent_timeout", "infrastructure_failure"}:
            raise AnalysisError(f"invalid run state for {task_id}: {state}")
        if manifest.get("trial") != 1:
            raise AnalysisError(f"canonical task does not use trial 1: {task_id}")
        if manifest.get("corpus_commit") != protocol.get("corpus_commit"):
            raise AnalysisError(f"corpus commit mismatch for {task_id}")
        if manifest.get("compiler_commit") != protocol.get("compiler_commit"):
            raise AnalysisError(f"compiler commit mismatch for {task_id}")
        prompt = tasks[task_id].get("prompt")
        if not isinstance(prompt, str):
            raise AnalysisError(f"task metadata has no prompt: {task_id}")
        prompt_sha256 = hashlib.sha256(prompt.encode("utf-8")).hexdigest()
        if manifest.get("task_prompt_sha256") != prompt_sha256:
            raise AnalysisError(f"task prompt hash mismatch for {task_id}")
        if state != "infrastructure_failure" and result is None:
            raise AnalysisError(f"completed task has no authoritative result: {task_id}")
        if state != "infrastructure_failure":
            final_status = manifest.get("final_status")
            result_status = result.get("result", {}).get("status") if result else None
            if final_status not in {"pass", "fail"} or result_status != final_status:
                raise AnalysisError(f"authoritative result mismatch for {task_id}")
        runs[task_id] = {
            "directory": directory,
            "manifest": manifest,
            "result": result,
            "log": read_jsonl(directory / "moss-tool-log.jsonl"),
        }
        locations[task_id] = directory
    missing = sorted(set(tasks) - set(runs))
    if missing:
        raise AnalysisError(f"missing canonical task IDs: {missing}")
    return protocol, [runs[task_id] for task_id in sorted(runs)]


def dimension_summary(records: list[dict[str, Any]], field: str) -> dict[str, Any]:
    groups: dict[str, list[dict[str, Any]]] = defaultdict(list)
    for record in records:
        groups[record[field]].append(record)
    result: dict[str, Any] = {}
    for name in sorted(groups):
        items = groups[name]
        valid = [item for item in items if not item["infrastructure_failure"]]
        attempts = [item["validation_attempts_to_green"] for item in valid
                    if item["validation_attempts_to_green"] is not None]
        diagnostics: Counter[str] = Counter()
        tools: Counter[str] = Counter()
        failures: Counter[str] = Counter()
        for item in items:
            diagnostics.update(item["diagnostic_codes"])
            tools.update(item["tool_usage"])
            failures.update(item["failure_classes"])
        result[name] = {
            "task_ids": [item["task_id"] for item in items],
            "task_count": len(items),
            "valid_task_count": len(valid),
            "pass_count": sum(item["final_status"] == "pass" for item in valid),
            "fail_count": sum(item["final_status"] == "fail" for item in valid),
            "infrastructure_failure_count": len(items) - len(valid),
            "first_validation_success_count": sum(
                item["first_validation_success"] is True for item in valid
            ),
            "eventually_green_count": len(attempts),
            "attempts_to_green": numeric_stats(attempts),
            "diagnostics": dict(sorted(diagnostics.items())),
            "tool_usage": dict(sorted(tools.items())),
            "failure_classes": dict(sorted(failures.items())),
        }
    return result


def diagnostic_summary(records: list[dict[str, Any]]) -> dict[str, Any]:
    by_code: dict[str, list[dict[str, Any]]] = defaultdict(list)
    for task in records:
        for record in task["raw_log"]:
            for code in record.get("diagnostic_codes", []):
                by_code[code].append({"task": task, "record": record})
    result: dict[str, Any] = {}
    for code in sorted(by_code):
        events = by_code[code]
        task_ids = sorted({event["task"]["task_id"] for event in events})
        recovery_attempts: list[int] = []
        reached_green: list[str] = []
        final_pass: list[str] = []
        per_task: dict[str, Any] = {}
        for task_id in task_ids:
            task = next(event["task"] for event in events if event["task"]["task_id"] == task_id)
            task_events = [event["record"] for event in events if event["task"]["task_id"] == task_id]
            first_sequence = min(event.get("sequence", 0) for event in task_events)
            green_before = any(
                record.get("sequence", 0) < first_sequence
                and is_correctness_attempt(record) and record.get("exit_code") == 0
                for record in task["raw_log"]
            )
            attempts = [record for record in task["raw_log"]
                        if record.get("sequence", 0) > first_sequence and is_correctness_attempt(record)]
            until_green = 0
            green_command: str | None = None
            for attempt in attempts:
                until_green += 1
                if attempt.get("exit_code") == 0:
                    green_command = command_label(attempt)
                    break
            if green_command is not None and not green_before:
                recovery_attempts.append(until_green)
                reached_green.append(task_id)
            if task["final_status"] == "pass":
                final_pass.append(task_id)
            per_task[task_id] = {
                "occurrences": len(task_events),
                "commands": [command_label(event) for event in task_events],
                "green_before_diagnostic": green_before,
                "recovered_to_green_after_diagnostic": green_command is not None and not green_before,
                "additional_correctness_attempts_to_green": (
                    until_green if green_command is not None and not green_before else None
                ),
                "green_command": green_command,
                "final_status": task["final_status"],
            }
        result[code] = {
            "task_ids": task_ids,
            "task_count": len(task_ids),
            "total_occurrences": len(events),
            "diagnostic_before_first_green_task_ids": reached_green,
            "recovered_to_green_count": len(reached_green),
            "final_pass_task_ids": final_pass,
            "final_pass_count": len(final_pass),
            "additional_correctness_attempts_to_green": numeric_stats(recovery_attempts),
            "per_task": per_task,
        }
    return result


def tool_summary(records: list[dict[str, Any]]) -> dict[str, Any]:
    observed = sorted({key for task in records for key in task["tool_usage"]})
    keys = list(CORE_TOOLS) + [key for key in observed if key not in CORE_TOOLS]
    relevant: dict[str, set[str]] = defaultdict(set)
    for task_id, requirements in WORKFLOW_TOOL_EXPECTATIONS.items():
        for _, alternatives in requirements:
            for key in alternatives:
                relevant[key].add(task_id)
    result: dict[str, Any] = {}
    for key in keys:
        used = [task for task in records if task["tool_usage"].get(key, 0)]
        later_green: list[str] = []
        successful_use: list[str] = []
        recovery_association: list[str] = []
        for task in used:
            tool_records = [record for record in task["raw_log"] if tool_key(record) == key]
            sequences = [record.get("sequence", 0) for record in tool_records]
            first_sequence = min(sequences)
            if any(record.get("exit_code") == 0 for record in tool_records):
                successful_use.append(task["task_id"])
            if any(record.get("sequence", 0) > first_sequence
                   and is_correctness_attempt(record) and record.get("exit_code") == 0
                   for record in task["raw_log"]):
                later_green.append(task["task_id"])
            if task["first_validation_success"] is False:
                green = next((record for record in task["raw_log"]
                              if is_correctness_attempt(record) and record.get("exit_code") == 0), None)
                if green is not None and first_sequence < green.get("sequence", 0):
                    recovery_association.append(task["task_id"])
        result[key] = {
            "available_task_count": len(records),
            "explicitly_relevant_task_ids": sorted(relevant.get(key, set())),
            "explicitly_relevant_task_count": len(relevant.get(key, set())),
            "used_task_ids": [task["task_id"] for task in used],
            "used_task_count": len(used),
            "invocation_count": sum(task["tool_usage"].get(key, 0) for task in used),
            "successful_invocation_task_ids": successful_use,
            "successful_invocation_task_count": len(successful_use),
            "tasks_with_later_green_command": later_green,
            "later_green_count": len(later_green),
            "used_before_first_green_in_recovery_task_ids": recovery_association,
            "used_before_first_green_in_recovery_task_count": len(recovery_association),
        }
    return result


def workflow_compliance(records: list[dict[str, Any]]) -> list[dict[str, Any]]:
    by_id = {record["task_id"]: record for record in records}
    result: list[dict[str, Any]] = []
    for task_id in sorted(task_id for task_id, task in by_id.items()
                          if task["task_type"] == "workflow"):
        task = by_id[task_id]
        used = set(task["tool_usage"])
        requirements = WORKFLOW_TOOL_EXPECTATIONS.get(task_id, [])
        checks = [{
            "requirement": label,
            "alternatives": sorted(alternatives),
            "satisfied": bool(used & alternatives),
            "observed": sorted(used & alternatives),
        } for label, alternatives in requirements]
        result.append({
            "task_id": task_id,
            "final_status": task["final_status"],
            "explicit_tool_requirement_count": len(checks),
            "all_explicit_tool_requirements_satisfied": (
                all(check["satisfied"] for check in checks) if checks else None
            ),
            "requirements": checks,
        })
    return result


def repeated_loops(records: list[dict[str, Any]]) -> list[dict[str, Any]]:
    loops: list[dict[str, Any]] = []
    for task in records:
        events: dict[str, list[dict[str, Any]]] = defaultdict(list)
        for record in task["raw_log"]:
            for code in set(record.get("diagnostic_codes", [])):
                events[code].append(record)
        for code, code_events in sorted(events.items()):
            if len(code_events) < 2:
                continue
            last_sequence = max(record.get("sequence", 0) for record in code_events)
            later_green = next((command_label(record) for record in task["raw_log"]
                                if record.get("sequence", 0) > last_sequence
                                and is_correctness_attempt(record)
                                and record.get("exit_code") == 0), None)
            loops.append({
                "task_id": task["task_id"],
                "diagnostic": code,
                "occurrences": len(code_events),
                "commands": [command_label(record) for record in code_events],
                "green_before_loop": any(
                    record.get("sequence", 0) < min(
                        event.get("sequence", 0) for event in code_events
                    ) and is_correctness_attempt(record) and record.get("exit_code") == 0
                    for record in task["raw_log"]
                ),
                "later_green_command": later_green,
                "final_status": task["final_status"],
            })
    return loops


def recovery_records(records: list[dict[str, Any]]) -> list[dict[str, Any]]:
    recovered: list[dict[str, Any]] = []
    for task in records:
        if task["first_validation_success"] is not False:
            continue
        green = next((record for record in task["raw_log"]
                      if is_correctness_attempt(record) and record.get("exit_code") == 0), None)
        if green is None:
            continue
        green_sequence = green.get("sequence", 0)
        before_green = [record for record in task["raw_log"]
                        if record.get("sequence", 0) < green_sequence]
        query_tools = sorted({tool_key(record) for record in before_green
                              if tool_key(record) in {
                                  "moss inspect", "moss type", "moss effects",
                                  "moss ownership", "moss calls", "moss why",
                              }})
        trace_tools = sorted({tool_key(record) for record in before_green
                              if tool_key(record) in {"moss run --interp", "moss debug", "trace"}})
        first_diagnostic = next((record for record in before_green
                                 if record.get("diagnostic_codes")), None)
        next_tool = None
        if first_diagnostic is not None:
            next_record = next((record for record in task["raw_log"]
                                if record.get("sequence", 0) > first_diagnostic.get("sequence", 0)
                                and record.get("origin") == "agent"), None)
            next_tool = command_label(next_record) if next_record else None
        recovered.append({
            "task_id": task["task_id"],
            "diagnostic_sequence": [code for record in before_green
                                    for code in record.get("diagnostic_codes", [])],
            "next_tool_after_first_diagnostic": next_tool,
            "first_green_command": command_label(green),
            "correctness_attempts_to_green": task["validation_attempts_to_green"],
            "semantic_queries_before_green": query_tools,
            "debug_tools_before_green": trace_tools,
            "final_status": task["final_status"],
            "source_change_sequence_available": False,
        })
    return recovered


def build_aggregate(baseline_root: Path, tasks_root: Path) -> dict[str, Any]:
    tasks = load_tasks(tasks_root)
    protocol, runs = load_runs(baseline_root, tasks)
    records: list[dict[str, Any]] = []
    for run in runs:
        manifest = run["manifest"]
        task_id = manifest["task_id"]
        task = tasks[task_id]
        log = run["log"]
        tools = Counter(key for record in log if (key := tool_key(record)) is not None)
        diagnostics = Counter(code for record in log for code in record.get("diagnostic_codes", []))
        infrastructure = manifest.get("state") == "infrastructure_failure"
        result = run["result"]
        final_status = None if infrastructure else manifest.get("final_status")
        failure_classes = classify_failures(manifest, result)
        records.append({
            "task_id": task_id,
            "name": task["name"],
            "category": task["category"],
            "task_type": task["task_type"],
            "state": manifest.get("state"),
            "infrastructure_failure": infrastructure,
            "infrastructure_error": manifest.get("infrastructure_error"),
            "final_status": final_status,
            "first_validation_success": manifest.get("first_validation_success"),
            "validation_attempts_to_green": manifest.get("validation_attempts_to_green"),
            "validation_attempts_total": manifest.get("validation_attempts_total"),
            "first_attempt_class": (
                "first-attempt-green" if manifest.get("first_validation_success") is True
                else "eventually-green" if manifest.get("validation_attempts_to_green") is not None
                else "never-green"
            ),
            "diagnostic_codes": dict(sorted(diagnostics.items())),
            "diagnostic_sequence": [code for record in log
                                    for code in record.get("diagnostic_codes", [])],
            "tool_usage": dict(sorted(tools.items())),
            "changed_paths": manifest.get("changed_paths", []),
            "outside_allowed_paths": manifest.get("outside_allowed_paths", []),
            "wall_time_ms": manifest.get("wall_time_ms"),
            "agent_tool_calls": manifest.get("agent_runtime", {}).get("tool_calls"),
            "agent_stopped_but_validator_failed": (
                manifest.get("agent_exit_code") == 0 and final_status == "fail"
            ),
            "failure_classes": failure_classes,
            "validation_failures": validation_failure_details(result),
            "raw_log": log,
        })

    valid = [record for record in records if not record["infrastructure_failure"]]
    attempts = [record["validation_attempts_to_green"] for record in valid
                if record["validation_attempts_to_green"] is not None]
    failures: Counter[str] = Counter()
    for record in records:
        failures.update(record["failure_classes"])
    aggregate = {
        "schema_version": SCHEMA_VERSION,
        "baseline": protocol.get("baseline_id"),
        "protocol_version": protocol.get("protocol_version"),
        "corpus_commit": protocol.get("corpus_commit"),
        "compiler_commit": protocol.get("compiler_commit"),
        "orchestration_commit": protocol.get("orchestration_commit"),
        "prompt_wrapper_sha256": protocol.get("prompt_wrapper_sha256"),
        "agent": protocol.get("agent"),
        "limits": protocol.get("limits"),
        "task_count": len(records),
        "valid_task_count": len(valid),
        "pass_count": sum(record["final_status"] == "pass" for record in valid),
        "fail_count": sum(record["final_status"] == "fail" for record in valid),
        "pass_percentage": percentage(
            sum(record["final_status"] == "pass" for record in valid), len(valid)
        ),
        "infrastructure_failure_count": len(records) - len(valid),
        "infrastructure_failure_task_ids": [record["task_id"] for record in records
                                            if record["infrastructure_failure"]],
        "first_validation_success_count": sum(
            record["first_validation_success"] is True for record in valid
        ),
        "first_validation_success_percentage": percentage(
            sum(record["first_validation_success"] is True for record in valid), len(valid)
        ),
        "eventually_green_count": len(attempts),
        "never_green_count": len(valid) - len(attempts),
        "attempts_to_green": numeric_stats(attempts),
        "out_of_scope_modification_task_count": sum(
            bool(record["outside_allowed_paths"]) for record in valid
        ),
        "agent_stopped_but_validator_failed_count": sum(
            record["agent_stopped_but_validator_failed"] for record in valid
        ),
        "agent_stopped_but_validator_failed_task_ids": [
            record["task_id"] for record in valid
            if record["agent_stopped_but_validator_failed"]
        ],
        "by_category": dimension_summary(records, "category"),
        "by_task_type": dimension_summary(records, "task_type"),
        "diagnostics": diagnostic_summary(records),
        "tool_usage": tool_summary(records),
        "workflow_compliance": workflow_compliance(records),
        "failure_classes": dict(sorted(failures.items())),
        "recovery": recovery_records(records),
        "repeated_diagnostic_loops": repeated_loops(records),
        "task_results": [{key: value for key, value in record.items() if key != "raw_log"}
                         for record in records],
    }
    if aggregate["task_count"] != 30:
        raise AnalysisError(f"expected 30 canonical tasks, found {aggregate['task_count']}")
    if aggregate["pass_count"] + aggregate["fail_count"] != aggregate["valid_task_count"]:
        raise AnalysisError("pass/fail totals do not reconcile with valid task count")
    if sum(item["task_count"] for item in aggregate["by_category"].values()) != 30:
        raise AnalysisError("category totals do not reconcile to 30")
    if sum(item["task_count"] for item in aggregate["by_task_type"].values()) != 30:
        raise AnalysisError("task-type totals do not reconcile to 30")
    return aggregate


def task_ids(records: list[dict[str, Any]], classification: str) -> str:
    values = [record["task_id"] for record in records if record["first_attempt_class"] == classification]
    return ", ".join(values) if values else "none"


def format_tools(values: dict[str, int], limit: int = 4) -> str:
    ordered = sorted(values.items(), key=lambda item: (-item[1], item[0]))[:limit]
    return ", ".join(f"{name} ({count})" for name, count in ordered) or "none"


def format_diagnostics(values: dict[str, int], limit: int = 3) -> str:
    ordered = sorted(values.items(), key=lambda item: (-item[1], item[0]))[:limit]
    return ", ".join(f"{name} ({count})" for name, count in ordered) or "none"


def human_number(value: int | float | None) -> str:
    if value is None:
        return "n/a"
    if isinstance(value, float) and value.is_integer():
        return str(int(value))
    if isinstance(value, float):
        return f"{value:.2f}".rstrip("0").rstrip(".")
    return str(value)


def build_backlog(aggregate: dict[str, Any]) -> list[dict[str, str]]:
    diagnostics = aggregate["diagnostics"]
    candidates: list[dict[str, str]] = []
    definitions = [
        (
            "Give domain/message restrictions stable, specific diagnostic IDs",
            "MOSS_COMPILE_ERROR", {"AB008", "AB009", "AB021", "AB023"},
            "Self-send, same-domain handler chaining, and naked cross-domain calls all collapsed into the generic MOSS_COMPILE_ERROR ID.",
            "Emit rule-specific IDs and name the ordinary-helper or message rewrite appropriate to the rejected call.",
            "Compare diagnosis/tool-call counts on the affected domain and synchronization repairs; all currently require a second correctness attempt.",
        ),
        (
            "Make overlapping-access diagnostics identify the conflicting argument roles",
            "OWNERSHIP_CONFLICTING_ACCESS", None,
            "AB022 received the same conflict from check, effects, and ownership before recovery; AB012 also encountered the code.",
            "Name the overlapping expressions and inferred WRITE/READ roles, then state that distinct owned values are required.",
            "Reduce the three-diagnostic AB022 query loop and preserve one-step recovery in AB012.",
        ),
        (
            "Specialize functional-pipeline diagnostics by rejected callable rule",
            "FUNCTIONAL_SEMANTIC_ERROR", None,
            "Placeholder, named-callable, and captured-effect repairs share one diagnostic ID across three tasks.",
            "Identify the unsupported callable form or captured effect and show the accepted placeholder/named-callable shape.",
            "Reduce diagnosis effort and tool calls in AB018–AB020; each already recovers by the next correctness attempt.",
        ),
        (
            "Teach canonical semantic-query target qualification",
            "QUERY_TARGET_NOT_FOUND", None,
            "AB015 tried bare Read and Accept targets before qualified Reader.Accept and Store.Read succeeded.",
            "Return candidate fully qualified targets and the canonical handler:Domain.Name spelling in the diagnostic payload.",
            "Remove the two failed query calls in AB015 without changing task behavior.",
        ),
        (
            "Add concrete inference context to TYPE_INFERENCE_FAILED",
            "TYPE_INFERENCE_FAILED", None,
            "AB010 and AB017 encountered the same broad inference code for different source concepts.",
            "Name the unresolved expression, expected type source, and callable/collection context in structured fields.",
            "Measure tool-call and edit-count reduction in AB010 and AB017; final AB017 failure is unrelated output formatting.",
        ),
    ]
    for title, code, restricted, observed, suggestion, hypothesis in definitions:
        if code not in diagnostics:
            continue
        evidence = diagnostics[code]
        ids = evidence["task_ids"]
        if restricted is not None:
            ids = [task_id for task_id in ids if task_id in restricted]
        if not ids:
            continue
        occurrences = sum(evidence["per_task"][task_id]["occurrences"] for task_id in ids)
        candidates.append({
            "candidate": title,
            "affected_tasks": ", ".join(ids),
            "leverage": f"{len(ids)}/{aggregate['task_count']} tasks, {occurrences} recorded diagnostic occurrence(s)",
            "current_diagnostics": code,
            "observed_problem": observed,
            "suggested_improvement": suggestion,
            "benchmark_hypothesis": hypothesis,
        })
    return candidates


def render_report(aggregate: dict[str, Any]) -> str:
    tasks = aggregate["task_results"]
    failed_task_ids = [task["task_id"] for task in tasks if task["final_status"] == "fail"]
    perfect_categories = [name for name, item in aggregate["by_category"].items()
                          if item["fail_count"] == 0 and item["infrastructure_failure_count"] == 0]
    failed_categories = [name for name, item in aggregate["by_category"].items()
                         if item["fail_count"]]
    repair = aggregate["by_task_type"]["repair"]
    workflow = aggregate["by_task_type"]["workflow"]
    write = aggregate["by_task_type"]["write"]
    named_workflows = [item for item in aggregate["workflow_compliance"]
                       if item["all_explicit_tool_requirements_satisfied"] is not None]
    compliant_workflows = [item for item in named_workflows
                           if item["all_explicit_tool_requirements_satisfied"]]
    tools = aggregate["tool_usage"]
    execution_task_ids = (set(tools["moss run --interp"]["used_task_ids"])
                          | set(tools["trace"]["used_task_ids"])
                          | set(tools["moss debug"]["used_task_ids"]))
    lines = [
        "# Moss Pre-22.1 Fresh-Agent Baseline",
        "",
        "## Protocol",
        "",
        (f"This report aggregates the frozen `{aggregate['baseline']}` baseline using schema "
         f"`{aggregate['schema_version']}`. The corpus commit is `{aggregate['corpus_commit']}`, "
         f"the compiler commit is `{aggregate['compiler_commit']}`, and the orchestration commit is "
         f"`{aggregate['orchestration_commit']}`. The recorded agent is "
         f"{aggregate['agent']['implementation']} {aggregate['agent']['runtime_version']} with "
         f"`{aggregate['agent']['model']}` at `{aggregate['agent']['reasoning_effort']}` reasoning. "
         f"Each task had one fresh session and a {aggregate['limits']['wall_time_seconds']}-second wall limit."),
        "",
        ("Reference solutions, usable Git history, external web access, and shell network access were "
         "unavailable to agents. Final pass/fail came from the authoritative validator after each agent stopped."),
        "",
        "## Executive Summary",
        "",
        (f"The baseline passed **{aggregate['pass_count']}/{aggregate['valid_task_count']} tasks "
         f"({aggregate['pass_percentage']}%)**. There were {aggregate['fail_count']} agent/task failures and "
         f"{aggregate['infrastructure_failure_count']} infrastructure failures. The first meaningful "
         f"Moss/Margo correctness command succeeded in **{aggregate['first_validation_success_count']}/"
         f"{aggregate['valid_task_count']} tasks ({aggregate['first_validation_success_percentage']}%)**. "
         f"All {aggregate['eventually_green_count']} valid sessions reached a green correctness command."),
        "",
        (f"The {aggregate['fail_count']} final failures were exact-output formatting mismatches after successful compilation: "
         f"{', '.join(failed_task_ids)} printed the correct values on separate lines instead of one space-separated line. "
         f"No diagnostic-bearing task failed to reach green, and all {repair['task_count']} repair tasks ultimately passed. "
         "This baseline therefore supports diagnostic improvements aimed at reducing diagnosis effort and "
         "tool calls more strongly than it supports a claim that current diagnostics prevent convergence."),
        "",
        "## Overall Results",
        "",
        "| Metric | Result |",
        "|---|---:|",
        f"| Valid tasks | {aggregate['valid_task_count']} |",
        f"| Final pass | {aggregate['pass_count']} ({aggregate['pass_percentage']}%) |",
        f"| Final fail | {aggregate['fail_count']} |",
        f"| Infrastructure failures | {aggregate['infrastructure_failure_count']} |",
        f"| First validation green | {aggregate['first_validation_success_count']} ({aggregate['first_validation_success_percentage']}%) |",
        f"| Eventually green | {aggregate['eventually_green_count']} |",
        f"| Never green | {aggregate['never_green_count']} |",
        f"| Attempts to green, median | {human_number(aggregate['attempts_to_green']['median'])} |",
        f"| Attempts to green, mean | {human_number(aggregate['attempts_to_green']['mean'])} |",
        f"| Attempts to green, max | {human_number(aggregate['attempts_to_green']['max'])} |",
        f"| Out-of-scope modification tasks | {aggregate['out_of_scope_modification_task_count']} |",
        f"| Agent stopped but validator failed | {aggregate['agent_stopped_but_validator_failed_count']} |",
        "",
        "Wall-clock values are retained per task but are not treated as a score because runtime load is noisy.",
        "",
        "## Results by Category",
        "",
        "| Category | Tasks | Pass | Fail | First green | Attempts median / mean / max | Diagnostics | Major tool use | Failure pattern |",
        "|---|---:|---:|---:|---:|---|---|---|---|",
    ]
    for category, item in aggregate["by_category"].items():
        stats = item["attempts_to_green"]
        lines.append(
            f"| {category} | {item['task_count']} | {item['pass_count']} | {item['fail_count']} | "
            f"{item['first_validation_success_count']} | {human_number(stats['median'])} / {human_number(stats['mean'])} / {human_number(stats['max'])} | "
            f"{format_diagnostics(item['diagnostics'])} | {format_tools(item['tool_usage'])} | "
            f"{format_diagnostics(item['failure_classes'])} |"
        )
    lines += [
        "",
        (f"The categories with 100% final pass rates were {', '.join(perfect_categories)}. "
         f"Final failures occurred only in {', '.join(failed_categories)}. Synchronization had "
         f"{aggregate['by_category']['synchronization']['first_validation_success_count']}/"
         f"{aggregate['by_category']['synchronization']['task_count']} first-validation successes and functional/dataflow had "
         f"{aggregate['by_category']['functional-dataflow']['first_validation_success_count']}/"
         f"{aggregate['by_category']['functional-dataflow']['task_count']}, but those groups contain intentionally broken repair/workflow starters; every task reached "
         "green in at most two correctness attempts. These first-attempt rates measure agent workflow as well "
         "as source fluency and should not be read as category failure rates."),
        "",
        "## Results by Task Type",
        "",
        "| Task type | Tasks | Pass | Fail | First green | Attempts median / mean / max |",
        "|---|---:|---:|---:|---:|---|",
    ]
    for task_type, item in aggregate["by_task_type"].items():
        stats = item["attempts_to_green"]
        lines.append(
            f"| {task_type} | {item['task_count']} | {item['pass_count']} | {item['fail_count']} | "
            f"{item['first_validation_success_count']} | {human_number(stats['median'])} / {human_number(stats['mean'])} / {human_number(stats['max'])} |"
        )
    lines += [
        "",
        (f"All {repair['task_count']} repair and {workflow['task_count']} workflow/debug tasks passed. Write tasks passed "
         f"{write['pass_count']}/{write['task_count']}; the write-task failures compiled and ran but missed exact output shape. "
         f"Repair tasks were first-green only {repair['first_validation_success_count']}/{repair['task_count']} because most agents compiled "
         "the supplied broken program before editing it, then recovered on the next correctness command."),
        "",
        "## First-Attempt Performance",
        "",
        f"- First attempt green ({aggregate['first_validation_success_count']}): {task_ids(tasks, 'first-attempt-green')}.",
        f"- Eventually green after an initial failure ({len(aggregate['recovery'])}): {task_ids(tasks, 'eventually-green')}.",
        f"- Never green ({aggregate['never_green_count']}): {task_ids(tasks, 'never-green')}.",
        "",
        ("AB014 was compiler-valid on its first correctness command but failed final output validation. AB017 "
         "recovered from TYPE_INFERENCE_FAILED and reached green, then failed the same output-shape requirement. "
         "Those cases separate Moss/compiler fluency from task-comprehension and validator compliance."),
        "",
        "## Diagnostics and Recovery",
        "",
        "| Diagnostic ID | Tasks | Occurrences | Before first green and recovered | Final pass | Additional attempts to green, median | Task IDs |",
        "|---|---:|---:|---:|---:|---:|---|",
    ]
    for code, item in sorted(aggregate["diagnostics"].items(),
                             key=lambda pair: (-pair[1]["total_occurrences"], pair[0])):
        lines.append(
            f"| `{code}` | {item['task_count']} | {item['total_occurrences']} | "
            f"{item['recovered_to_green_count']} | {item['final_pass_count']} | "
            f"{human_number(item['additional_correctness_attempts_to_green']['median'])} | "
            f"{', '.join(item['task_ids'])} |"
        )
    lines += [
        "",
        (f"Observed recovery was shallow: all {len(aggregate['recovery'])} initially failing sessions reached green by correctness attempt "
         f"{aggregate['attempts_to_green']['max']}, and no task had a higher attempts-to-green value. The raw telemetry does not contain source-edit "
         f"snapshots between commands, so it cannot prove which edit or diagnostic text caused recovery. "
         f"{sum(item['final_status'] == 'pass' for item in aggregate['recovery'])} of these {len(aggregate['recovery'])} sessions finally passed; "
         "AB017's later output mismatch was unrelated to its initial type error."),
        "",
        ("Only AB022 used semantic queries before reaching green: `moss effects` and `moss ownership`. Other "
         "initially failing sessions recorded no semantic query before their first green command. That is an "
         "association, not evidence that compiler feedback alone caused the edits."),
        "",
        "### Recorded recovery paths",
        "",
        "| Task | Diagnostic sequence | Next Moss/Margo tool | First green command | Semantic queries before green | Final |",
        "|---|---|---|---|---|---|",
    ]
    for item in aggregate["recovery"]:
        diagnostic_sequence = " → ".join(f"`{code}`" for code in item["diagnostic_sequence"]) or "none recorded"
        semantic_queries = ", ".join(f"`{tool}`" for tool in item["semantic_queries_before_green"]) or "none"
        lines.append(
            f"| {item['task_id']} | {diagnostic_sequence} | "
            f"`{item['next_tool_after_first_diagnostic']}` | `{item['first_green_command']}` | "
            f"{semantic_queries} | {item['final_status']} |"
        )
    lines += [
        "",
        ("The telemetry can order commands and diagnostic IDs but does not preserve source snapshots for each edit. "
         "The table therefore reports association and sequence, not that a particular diagnostic or tool caused recovery."),
        "",
        "## Tool Discovery and Usage",
        "",
        (f"`Available` is {aggregate['task_count']} for each normal agent-facing command. `Explicitly relevant` is deliberately narrow: "
         "it counts only tools named by a workflow prompt, including stated alternatives."),
        "",
        "| Tool | Available tasks | Explicitly relevant | Used tasks | Invocations | Successful-use tasks | Used before green in recovery |",
        "|---|---:|---:|---:|---:|---:|---:|",
    ]
    for tool, item in aggregate["tool_usage"].items():
        lines.append(
            f"| `{tool}` | {item['available_task_count']} | {item['explicitly_relevant_task_count']} | "
            f"{item['used_task_count']} | {item['invocation_count']} | "
            f"{item['successful_invocation_task_count']} | "
            f"{item['used_before_first_green_in_recovery_task_count']} |"
        )
    lines += [
        "",
        f"Bootstrap was used in all {aggregate['task_count']} tasks. `moss check` appeared in "
        f"{tools['moss check']['used_task_count']} tasks; interpreter, debug, or trace execution appeared in "
        f"{len(execution_task_ids)}. Effects was used in {tools['moss effects']['used_task_count']} tasks, ownership in "
        f"{tools['moss ownership']['used_task_count']}, inspect in {tools['moss inspect']['used_task_count']}, and calls in "
        f"{tools['moss calls']['used_task_count']}. ",
        "No agent invoked `moss type` or `moss why`; no prompt explicitly required either, so this is evidence of ",
        "non-discovery but not proof that either tool would have changed an outcome. Direct `moss debug` was not ",
        "used without trace, while both Fast Debug tasks used trace successfully.",
        "",
        "### Workflow-task compliance",
        "",
        "| Task | Final validator | Explicit tool requirements | Agent used intended tool(s) |",
        "|---|---|---:|---|",
    ]
    for item in aggregate["workflow_compliance"]:
        compliance = ("yes" if item["all_explicit_tool_requirements_satisfied"] is True
                      else "no" if item["all_explicit_tool_requirements_satisfied"] is False
                      else "no named tool requirement")
        lines.append(
            f"| {item['task_id']} | {item['final_status']} | "
            f"{item['explicit_tool_requirement_count']} | {compliance} |"
        )
    lines += [
        "",
        f"All {len(compliant_workflows)} workflow tasks with an explicit tool requirement used a permitted intended command and passed. ",
        "AB023 also passed but its prompt prescribed a source repair rather than a particular diagnostic command. ",
        "This separates final behavior from tool compliance without adding tool use to benchmark pass/fail.",
        "",
        "## Failure Modes",
        "",
        ("Failure classes are assigned deterministically from run state, path violations, and authoritative validator "
         "failure records. They do not rely on an inferred reading of the agent's prose."),
        "",
    ]
    for failure_class, count in aggregate["failure_classes"].items():
        affected = [task["task_id"] for task in tasks if failure_class in task["failure_classes"]]
        lines.append(f"- `{failure_class}`: {count} task(s): {', '.join(affected)}.")
    lines += [
        "",
        f"The only canonical final-failure class across {aggregate['fail_count']} tasks was exact output formatting. There were no invalid final projects, ",
        "structural-shortcut failures, out-of-scope edits, timeouts, or infrastructure failures. Both agents exited ",
        "normally and reported completion before the authoritative validator rejected their output.",
        "",
        "## Repeated Failure Loops",
        "",
    ]
    if aggregate["repeated_diagnostic_loops"]:
        for loop in aggregate["repeated_diagnostic_loops"]:
            commands = " → ".join(f"`{command}`" for command in loop["commands"])
            timing = ("The program was already green before this loop."
                      if loop["green_before_loop"] else "The loop occurred before first green.")
            lines.append(
                f"- **{loop['task_id']} — `{loop['diagnostic']}` ×{loop['occurrences']}**: {commands}. "
                f"{timing} Later green command: `{loop['later_green_command']}`; "
                f"final status: {loop['final_status']}."
            )
    else:
        lines.append("No diagnostic ID repeated within a canonical task session.")
    lines += [
        "",
        "AB015 shows query-target discovery friction: bare `Read` and `Accept` targets failed before qualified ",
        "`Reader.Accept` and `Store.Read` succeeded. AB022 shows the ownership conflict propagating unchanged ",
        "through check, effects, and ownership queries before the source repair. No task cycled through the same ",
        "failed correctness command more than once, and no A→B→A diagnostic loop was recorded.",
        "",
        "## Phase 22.1 Opportunities",
        "",
        "The candidates below are ordered by affected tasks and repeated recorded friction. They are hypotheses ",
        "for the later post-22.1 comparison, not changes made by this phase.",
        "",
        "| Candidate | Evidence/leverage | Affected tasks | Current diagnostic | Suggested improvement | Expected measurable effect |",
        "|---|---|---|---|---|---|",
    ]
    for candidate in build_backlog(aggregate):
        lines.append(
            f"| {candidate['candidate']} | {candidate['leverage']} | {candidate['affected_tasks']} | "
            f"`{candidate['current_diagnostics']}` | {candidate['suggested_improvement']} | "
            f"{candidate['benchmark_hypothesis']} |"
        )
    lines += [
        "",
        "The strongest limitation on headline pass rate was not a compiler diagnostic: the final failures already had ",
        "green compilation and execution. Phase 22.1 should therefore use diagnostic-specific measures such as ",
        "failed query count, tool calls, and recovery shape alongside final pass rate. The baseline provides no ",
        "evidence for changing Moss semantics or accepting additional programs.",
        "",
        "## Methodological Limitations",
        "",
        "- There is one canonical agent trial per task; model behavior is stochastic.",
        "- Thirty tasks are a small corpus, and category groups contain only two to five tasks.",
        "- Wall-clock time depends on runtime load and is retained as context rather than a primary score.",
        "- Some tasks admit multiple correct implementations. Structural assertions test requested concepts but ",
        "  are not formal equivalence proofs.",
        "- The baseline measures the recorded Codex/model/reasoning/runtime configuration. A future model or agent ",
        "  upgrade is a confounder and must not be presented as a Moss-only effect.",
        "- Moss/Margo telemetry records commands and diagnostic IDs, not full agent reasoning or source snapshots ",
        "  after every edit. Recovery causality and exact edit sequences are therefore unavailable.",
        "- First-validation success includes agents that intentionally compiled an invalid repair starter before ",
        "  editing, so it measures workflow behavior as well as source-generation quality.",
        "",
        "## Comparison Contract",
        "",
        "A post-22.1 comparison must retain AB001–AB030 at corpus revision ",
        f"`{aggregate['corpus_commit']}`, the task prompts, generic wrapper hash ",
        f"`{aggregate['prompt_wrapper_sha256']}`, one fresh trial per task, the same agent/model/reasoning ",
        "configuration where possible, the same isolation and network restrictions, the same per-task budget, ",
        "and the same definitions of first validation, attempts to green, final pass, diagnostics, tools, and path ",
        "violations. Any unavoidable difference must be identified as a comparison confounder.",
        "",
        "The machine-readable source for every table is `aggregate.json`; per-task evidence remains in the adjacent ",
        "Phase 22.2C manifests, tool logs, validator results, and final workspaces.",
    ]
    return "\n".join(line.rstrip() for line in lines) + "\n"


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(prog="analyze-agent-baseline")
    parser.add_argument("--baseline-root", type=Path, default=DEFAULT_BASELINE)
    parser.add_argument("--tasks-root", type=Path, default=SUITE / "tasks")
    parser.add_argument("--aggregate", type=Path)
    parser.add_argument("--report", type=Path)
    parser.add_argument("--check", action="store_true")
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    baseline_root = args.baseline_root.resolve()
    aggregate_path = (args.aggregate or baseline_root / "aggregate.json").resolve()
    report_path = (args.report or baseline_root / "REPORT.md").resolve()
    before = raw_tree_digest(baseline_root)
    aggregate = build_aggregate(baseline_root, args.tasks_root.resolve())
    aggregate_text = stable_json(aggregate)
    report_text = render_report(aggregate)
    if args.check:
        mismatches = []
        if not aggregate_path.is_file() or aggregate_path.read_text(encoding="utf-8") != aggregate_text:
            mismatches.append(str(aggregate_path))
        if not report_path.is_file() or report_path.read_text(encoding="utf-8") != report_text:
            mismatches.append(str(report_path))
        if mismatches:
            raise AnalysisError(f"generated analysis is stale: {', '.join(mismatches)}")
    else:
        aggregate_path.write_text(aggregate_text, encoding="utf-8")
        report_path.write_text(report_text, encoding="utf-8")
    after = raw_tree_digest(baseline_root)
    if before != after:
        raise AnalysisError("analysis modified raw baseline inputs")
    print(json.dumps({
        "schema_version": SCHEMA_VERSION,
        "task_count": aggregate["task_count"],
        "pass_count": aggregate["pass_count"],
        "fail_count": aggregate["fail_count"],
        "infrastructure_failure_count": aggregate["infrastructure_failure_count"],
        "aggregate": str(aggregate_path),
        "report": str(report_path),
        "mode": "check" if args.check else "write",
    }, sort_keys=True))
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except AnalysisError as error:
        print(f"agent-baseline-analysis: {error}", file=sys.stderr)
        raise SystemExit(2)
