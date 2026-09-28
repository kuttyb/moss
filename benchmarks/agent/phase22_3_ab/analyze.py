#!/usr/bin/env python3
"""Deterministic per-profile aggregation for Phase 22.3 A/B runs."""

from __future__ import annotations

import argparse
from collections import Counter, defaultdict
import json
import math
from pathlib import Path
import statistics
import sys
from typing import Any


SUITE = Path(__file__).resolve().parent
DEFAULT_RUNS = SUITE / "runs"


def read_json(path: Path) -> Any:
    return json.loads(path.read_text(encoding="utf-8"))


def write_or_check(path: Path, value: Any, check: bool) -> None:
    content = json.dumps(value, indent=2, sort_keys=True) + "\n"
    if check:
        if not path.is_file() or path.read_text(encoding="utf-8") != content:
            raise RuntimeError(f"stale generated aggregate: {path}")
    else:
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(content, encoding="utf-8")


def numeric(values: list[Any]) -> list[float]:
    return [float(value) for value in values if isinstance(value, (int, float)) and not isinstance(value, bool)]


def stats(values: list[Any]) -> dict[str, Any]:
    data = numeric(values)
    if not data:
        return {"count": 0, "mean": None, "median": None, "standard_deviation": None, "total": 0}
    return {
        "count": len(data), "mean": round(statistics.mean(data), 6),
        "median": round(statistics.median(data), 6),
        "standard_deviation": round(statistics.stdev(data), 6) if len(data) > 1 else 0.0,
        "total": round(sum(data), 6),
    }


def rate(items: list[dict[str, Any]], predicate) -> float | None:
    return None if not items else round(sum(bool(predicate(item)) for item in items) / len(items), 6)


def summarize(items: list[dict[str, Any]], profile: str) -> dict[str, Any]:
    correct = sum(item.get("semantic_facts_correct", 0) for item in items)
    requested = sum(item.get("semantic_facts_requested", 0) for item in items)
    metric_fields = (
        "semantic_detour_cost", "moss_source_inspection_commands", "mossi_inspection_commands",
        "generated_rust_inspection_commands", "documentation_inspection_commands",
        "unique_moss_source_files_read", "approx_source_bytes_read", "semantic_query_invocations",
        "successful_semantic_queries", "failed_semantic_queries", "validation_attempts_to_green",
        "resolved_queries", "ambiguous_queries", "missing_queries",
        "resolve_invocations", "inspect_invocations", "type_invocations",
        "ownership_invocations", "effects_invocations", "calls_invocations",
        "why_invocations", "cost_invocations",
        "validation_attempts_total", "agent_tool_calls", "moss_margo_invocations", "wall_time_ms",
        "input_tokens", "cached_input_tokens", "output_tokens", "reasoning_tokens",
        "diagnostic_occurrences", "repeated_diagnostic_loops", "queries_per_correct_fact",
    )
    query_distribution: Counter[str] = Counter()
    for item in items:
        query_distribution.update(item.get("query_type_distribution", {}))
    semantic = [item for item in items if item.get("semantic_heavy")]
    controls = [item for item in items if not item.get("semantic_heavy")]
    semantic_tasks = {item["task_id"] for item in semantic}
    adopted_tasks = {item["task_id"] for item in semantic if item.get("semantic_query_invocations", 0) > 0}
    resolve_tasks = {item["task_id"] for item in semantic if item.get("resolve_invocations", 0) > 0}
    completed = [item for item in items if item.get("state") not in {"infrastructure_failure"}]
    summary = {
        "trials": len(items), "valid_agent_outcomes": len(completed),
        "infrastructure_failures": sum(item.get("state") == "infrastructure_failure" for item in items),
        "timeouts": sum(item.get("state") == "agent_timeout" for item in items),
        "semantic_facts_correct": correct, "semantic_facts_requested": requested,
        "semantic_fact_accuracy": round(correct / requested, 6) if requested else None,
        "final_task_pass_rate": rate(completed, lambda item: item.get("final_validator_pass")),
        "semantic_heavy_pass_rate": rate(semantic, lambda item: item.get("final_validator_pass")),
        "control_pass_rate": rate(controls, lambda item: item.get("final_validator_pass")),
        "first_validation_success_rate": rate(completed, lambda item: item.get("first_validation_success") is True),
        "semantic_query_adoption": round(len(adopted_tasks) / len(semantic_tasks), 6) if semantic_tasks else None,
        "semantic_query_adoption_trials": rate(semantic, lambda item: item.get("semantic_query_invocations", 0) > 0),
        "query_type_distribution": dict(sorted(query_distribution.items())),
        "metrics": {field: stats([item.get(field) for item in items]) for field in metric_fields},
    }
    resolve_rate = round(len(resolve_tasks) / len(semantic_tasks), 6) if semantic_tasks else None
    resolve_trial_rate = rate(semantic, lambda item: item.get("resolve_invocations", 0) > 0)
    if profile == "legacy":
        summary["legacy_resolve_attempt_rate"] = resolve_rate
        summary["legacy_resolve_attempt_rate_trials"] = resolve_trial_rate
    elif profile == "phase22_3":
        summary["phase22_3_resolve_adoption"] = resolve_rate
        summary["phase22_3_resolve_adoption_trials"] = resolve_trial_rate
    else:
        raise ValueError(f"unknown query profile: {profile}")
    return summary


def build_aggregate(runs_root: Path, profile: str) -> dict[str, Any]:
    protocol = read_json(runs_root / "protocol.json")
    manifests = [read_json(path) for path in sorted((runs_root / profile).glob("P223*/trial-*/manifest.json"))]
    expected = len(protocol["task_ids"]) * protocol["trials_per_task"]
    if len(manifests) != expected:
        raise RuntimeError(f"{profile}: expected {expected} manifests, found {len(manifests)}")
    identities = [(item["task_id"], item["trial_id"]) for item in manifests]
    if len(identities) != len(set(identities)):
        raise RuntimeError(f"{profile}: duplicate task/trial manifests")
    for item in manifests:
        if item["condition"] != profile or item["compiler_commit"] != protocol["compiler_commit"] or item["compiler_binary_sha256"] != protocol["compiler_binary_sha256"]:
            raise RuntimeError(f"{profile}: run protocol mismatch in {item['task_id']} {item['trial_id']}")
    categories = sorted({item["category"] for item in manifests})
    tasks = sorted({item["task_id"] for item in manifests})
    return {
        "schema_version": "moss-phase22.3-ab-aggregate-2", "profile": profile,
        "protocol": protocol, "summary": summarize(manifests, profile),
        "categories": {category: summarize([item for item in manifests if item["category"] == category], profile) for category in categories},
        "tasks": {task_id: summarize([item for item in manifests if item["task_id"] == task_id], profile) for task_id in tasks},
        "trials": manifests,
    }


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--runs-root", type=Path, default=DEFAULT_RUNS)
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args()
    try:
        for profile in ("legacy", "phase22_3"):
            aggregate = build_aggregate(args.runs_root.resolve(), profile)
            write_or_check(args.runs_root.resolve() / profile / "aggregate.json", aggregate, args.check)
        return 0
    except (OSError, ValueError, RuntimeError, KeyError) as error:
        print(f"analysis error: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
