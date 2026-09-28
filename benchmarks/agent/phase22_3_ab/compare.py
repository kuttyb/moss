#!/usr/bin/env python3
"""Generate the paired machine-readable comparison and human report."""

from __future__ import annotations

import argparse
import json
from pathlib import Path
import random
import statistics
import sys
from typing import Any

from analyze import DEFAULT_RUNS, build_aggregate


SUITE = Path(__file__).resolve().parent
PRIMARY = {
    "semantic_fact_accuracy": lambda item: item.get("semantic_fact_accuracy"),
    "final_task_pass_rate": lambda item: 1.0 if item.get("final_validator_pass") else 0.0,
    "semantic_heavy_pass_rate": lambda item: (1.0 if item.get("final_validator_pass") else 0.0) if item.get("semantic_heavy") else None,
    "control_task_pass_rate": lambda item: (1.0 if item.get("final_validator_pass") else 0.0) if not item.get("semantic_heavy") else None,
    "semantic_detour_cost": lambda item: item.get("semantic_detour_cost"),
    "moss_source_inspection_commands": lambda item: item.get("moss_source_inspection_commands"),
    "generated_rust_inspection_commands": lambda item: item.get("generated_rust_inspection_commands"),
    "semantic_query_invocations": lambda item: item.get("semantic_query_invocations"),
    "queries_per_correct_fact": lambda item: item.get("queries_per_correct_fact"),
    "first_validation_success": lambda item: 1.0 if item.get("first_validation_success") is True else (0.0 if item.get("first_validation_success") is False else None),
    "agent_tool_calls": lambda item: item.get("agent_tool_calls"),
    "moss_margo_invocations": lambda item: item.get("moss_margo_invocations"),
    "validation_attempts_to_green": lambda item: item.get("validation_attempts_to_green"),
    "wall_time_ms": lambda item: item.get("wall_time_ms"),
    "input_tokens": lambda item: item.get("input_tokens"),
}


def write_or_check(path: Path, content: str, check: bool) -> None:
    if check:
        if not path.is_file() or path.read_text(encoding="utf-8") != content:
            raise RuntimeError(f"stale generated comparison: {path}")
    else:
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(content, encoding="utf-8")


def percent(delta: float, baseline: float | None) -> float | None:
    if baseline in {None, 0}:
        return None
    return round(delta / baseline * 100, 6)


def paired_ci(deltas: list[float], seed: int = 223, samples: int = 5000) -> list[float] | None:
    if not deltas:
        return None
    rng = random.Random(seed)
    means = sorted(statistics.mean(rng.choices(deltas, k=len(deltas))) for _ in range(samples))
    return [round(means[int(samples * 0.025)], 6), round(means[min(samples - 1, int(samples * 0.975))], 6)]


def paired_metric(legacy: list[dict[str, Any]], treatment: list[dict[str, Any]], extractor) -> dict[str, Any]:
    left = {(item["task_id"], item["trial_id"]): item for item in legacy}
    right = {(item["task_id"], item["trial_id"]): item for item in treatment}
    if set(left) != set(right):
        raise RuntimeError("profile task/trial identities differ")
    pairs = []
    for identity in sorted(left):
        before, after = extractor(left[identity]), extractor(right[identity])
        if isinstance(before, (int, float)) and isinstance(after, (int, float)):
            pairs.append((identity, float(before), float(after)))
    before_values = [item[1] for item in pairs]
    after_values = [item[2] for item in pairs]
    deltas = [after - before for _, before, after in pairs]
    before_mean = statistics.mean(before_values) if before_values else None
    after_mean = statistics.mean(after_values) if after_values else None
    delta = after_mean - before_mean if before_mean is not None and after_mean is not None else None
    by_task: dict[str, list[float]] = {}
    for (task_id, _), before, after in pairs:
        by_task.setdefault(task_id, []).append(after - before)
    return {
        "paired_observations": len(pairs),
        "legacy_mean": round(before_mean, 6) if before_mean is not None else None,
        "phase22_3_mean": round(after_mean, 6) if after_mean is not None else None,
        "absolute_delta": round(delta, 6) if delta is not None else None,
        "percentage_delta": percent(delta, before_mean) if delta is not None else None,
        "legacy_median": round(statistics.median(before_values), 6) if before_values else None,
        "phase22_3_median": round(statistics.median(after_values), 6) if after_values else None,
        "paired_delta_bootstrap_95ci": paired_ci(deltas),
        "per_task_paired_delta": {task_id: round(statistics.mean(values), 6) for task_id, values in sorted(by_task.items())},
    }


def paired_fact_accuracy(legacy: list[dict[str, Any]], treatment: list[dict[str, Any]]) -> dict[str, Any]:
    """Pair trials, but compute the specified pooled correct/requested ratio."""
    left = {(item["task_id"], item["trial_id"]): item for item in legacy}
    right = {(item["task_id"], item["trial_id"]): item for item in treatment}
    if set(left) != set(right):
        raise RuntimeError("profile task/trial identities differ")
    pairs = []
    for identity in sorted(left):
        before, after = left[identity], right[identity]
        before_requested = before.get("semantic_facts_requested")
        after_requested = after.get("semantic_facts_requested")
        if not isinstance(before_requested, int) or before_requested < 1 or before_requested != after_requested:
            continue
        pairs.append((
            identity, before.get("semantic_facts_correct", 0),
            after.get("semantic_facts_correct", 0), before_requested,
        ))
    before_requested = sum(item[3] for item in pairs)
    before_ratio = sum(item[1] for item in pairs) / before_requested if before_requested else None
    after_ratio = sum(item[2] for item in pairs) / before_requested if before_requested else None
    delta = after_ratio - before_ratio if before_ratio is not None and after_ratio is not None else None
    trial_before = [item[1] / item[3] for item in pairs]
    trial_after = [item[2] / item[3] for item in pairs]
    by_task: dict[str, list[tuple[int, int, int]]] = {}
    for (task_id, _), before, after, requested in pairs:
        by_task.setdefault(task_id, []).append((before, after, requested))
    per_task = {}
    for task_id, values in sorted(by_task.items()):
        requested = sum(item[2] for item in values)
        per_task[task_id] = round(
            sum(item[1] for item in values) / requested - sum(item[0] for item in values) / requested,
            6,
        )
    interval = None
    if pairs:
        rng = random.Random(223)
        bootstrap = []
        for _ in range(5000):
            sample = rng.choices(pairs, k=len(pairs))
            requested = sum(item[3] for item in sample)
            bootstrap.append(
                sum(item[2] for item in sample) / requested
                - sum(item[1] for item in sample) / requested
            )
        bootstrap.sort()
        interval = [round(bootstrap[125], 6), round(bootstrap[4875], 6)]
    return {
        "paired_observations": len(pairs),
        "legacy_mean": round(before_ratio, 6) if before_ratio is not None else None,
        "phase22_3_mean": round(after_ratio, 6) if after_ratio is not None else None,
        "absolute_delta": round(delta, 6) if delta is not None else None,
        "percentage_delta": percent(delta, before_ratio) if delta is not None else None,
        "legacy_median": round(statistics.median(trial_before), 6) if trial_before else None,
        "phase22_3_median": round(statistics.median(trial_after), 6) if trial_after else None,
        "paired_delta_bootstrap_95ci": interval,
        "per_task_paired_delta": per_task,
    }


def verify_integrity(legacy: dict[str, Any], treatment: dict[str, Any]) -> None:
    lp, tp = legacy["protocol"], treatment["protocol"]
    for key in ("compiler_commit", "benchmark_commit", "compiler_binary_sha256", "agent", "limits", "task_ids", "trials_per_task"):
        if lp[key] != tp[key]:
            raise RuntimeError(f"profile protocol mismatch: {key}")
    left = {(item["task_id"], item["trial_id"]): item for item in legacy["trials"]}
    right = {(item["task_id"], item["trial_id"]): item for item in treatment["trials"]}
    if set(left) != set(right):
        raise RuntimeError("profile trial sets differ")
    for identity in left:
        for key in ("compiler_commit", "compiler_binary_sha256", "prompt_sha256", "prompt_wrapper_sha256", "starter_tree_sha256", "task_metadata_sha256", "validator_sha256"):
            if left[identity][key] != right[identity][key]:
                raise RuntimeError(f"trial integrity mismatch {identity}: {key}")
        if left[identity]["agent"] != right[identity]["agent"]:
            raise RuntimeError(f"trial agent mismatch {identity}")


def outcomes(legacy: dict[str, Any], treatment: dict[str, Any]) -> dict[str, list[str]]:
    result = {"legacy_wins": [], "treatment_wins": [], "both_fail": [], "both_pass_treatment_more_efficient": [], "treatment_regressions": []}
    for task_id in sorted(legacy["tasks"]):
        left = legacy["tasks"][task_id]["summary"] if "summary" in legacy["tasks"][task_id] else legacy["tasks"][task_id]
        right = treatment["tasks"][task_id]["summary"] if "summary" in treatment["tasks"][task_id] else treatment["tasks"][task_id]
        lp, rp = left["final_task_pass_rate"], right["final_task_pass_rate"]
        la, ra = left["semantic_fact_accuracy"], right["semantic_fact_accuracy"]
        if rp > lp or (rp == lp and ra > la): result["treatment_wins"].append(task_id)
        if lp > rp or (lp == rp and la > ra): result["legacy_wins"].append(task_id)
        if lp == 0 and rp == 0: result["both_fail"].append(task_id)
        left_sdc = left["metrics"]["semantic_detour_cost"]["mean"]
        right_sdc = right["metrics"]["semantic_detour_cost"]["mean"]
        if lp == 1 and rp == 1 and left_sdc is not None and right_sdc is not None and right_sdc < left_sdc:
            result["both_pass_treatment_more_efficient"].append(task_id)
        if rp < lp or ra < la: result["treatment_regressions"].append(task_id)
    return result


def build_comparison(runs_root: Path) -> dict[str, Any]:
    legacy = build_aggregate(runs_root, "legacy")
    treatment = build_aggregate(runs_root, "phase22_3")
    verify_integrity(legacy, treatment)
    metrics = {name: paired_metric(legacy["trials"], treatment["trials"], extractor) for name, extractor in PRIMARY.items()}
    metrics["semantic_fact_accuracy"] = paired_fact_accuracy(legacy["trials"], treatment["trials"])
    categories = {}
    for category in legacy["categories"]:
        left = [item for item in legacy["trials"] if item["category"] == category]
        right = [item for item in treatment["trials"] if item["category"] == category]
        categories[category] = {
            "semantic_fact_accuracy": paired_fact_accuracy(left, right),
            "final_task_pass_rate": paired_metric(left, right, PRIMARY["final_task_pass_rate"]),
            "semantic_detour_cost": paired_metric(left, right, PRIMARY["semantic_detour_cost"]),
        }
    return {
        "schema_version": "moss-phase22.3-ab-comparison-1",
        "protocol": legacy["protocol"], "metrics": metrics, "categories": categories,
        "adoption": {
            "legacy": {key: legacy["summary"][key] for key in ("semantic_query_adoption", "resolve_adoption", "query_type_distribution")},
            "phase22_3": {key: treatment["summary"][key] for key in ("semantic_query_adoption", "resolve_adoption", "query_type_distribution")},
        },
        "failure_analysis": outcomes(legacy, treatment),
        "profile_summaries": {"legacy": legacy["summary"], "phase22_3": treatment["summary"]},
    }


def fmt(value: Any, percent_value: bool = False) -> str:
    if value is None: return "n/a"
    if percent_value: return f"{value * 100:.1f}%"
    if isinstance(value, float): return f"{value:.3f}"
    return str(value)


def report(document: dict[str, Any]) -> str:
    protocol = document["protocol"]
    metrics = document["metrics"]
    rows = [
        ("Semantic fact accuracy", "semantic_fact_accuracy", True),
        ("Final task pass rate", "final_task_pass_rate", True),
        ("Semantic-heavy pass rate", "semantic_heavy_pass_rate", True),
        ("Control-task pass rate", "control_task_pass_rate", True),
        ("Median semantic detour cost", "semantic_detour_cost", False),
        ("Source inspection commands", "moss_source_inspection_commands", False),
        ("Generated Rust inspections", "generated_rust_inspection_commands", False),
        ("Semantic queries/task", "semantic_query_invocations", False),
        ("Queries/correct fact", "queries_per_correct_fact", False),
        ("First-validation success", "first_validation_success", True),
        ("Agent tool calls/task", "agent_tool_calls", False),
        ("Moss/Margo calls/task", "moss_margo_invocations", False),
        ("Attempts-to-green", "validation_attempts_to_green", False),
        ("Wall time/task (ms)", "wall_time_ms", False),
        ("Input tokens/task", "input_tokens", False),
    ]
    lines = [
        "# Phase 22.3 semantic-query A/B benchmark", "", "## Protocol", "",
        f"- Compiler and benchmark SHA: `{protocol['compiler_commit']}`",
        f"- Compiler binary SHA-256: `{protocol['compiler_binary_sha256']}`",
        f"- Model: `{protocol['agent']['model']}`; reasoning: `{protocol['agent']['reasoning_effort']}`; Codex: `{protocol['agent']['runtime_version']}`",
        f"- Rust: `{protocol['environment']['rustc_version'].splitlines()[0]}`",
        f"- Tasks: {protocol['task_count']}; trials/task/profile: {protocol['trials_per_task']}; timeout: {protocol['limits']['wall_time_seconds']} seconds",
        f"- Profiles: legacy post-22.1 projection from `{protocol['profile_definitions']['legacy']['docs_commit']}`; full current Phase 22.3 treatment",
        f"- Legacy docs/skills hashes: `{json.dumps(protocol['profile_docs_sha256']['legacy'], sort_keys=True)}`",
        f"- Phase 22.3 docs/skills hashes: `{json.dumps(protocol['profile_docs_sha256']['phase22_3'], sort_keys=True)}`",
        f"- Restrictions: {protocol['environment']['network_policy']}; {protocol['environment']['sandbox']}",
        "", "## Headline result", "", "| Metric | Legacy | Phase 22.3 | Delta |", "|---|---:|---:|---:|",
    ]
    for label, key, as_percent in rows:
        item = metrics[key]
        left = item["legacy_mean"]
        right = item["phase22_3_mean"]
        delta = item["absolute_delta"]
        if key == "semantic_detour_cost":
            left, right = item["legacy_median"], item["phase22_3_median"]
            delta = right - left if left is not None and right is not None else None
        lines.append(f"| {label} | {fmt(left, as_percent)} | {fmt(right, as_percent)} | {fmt(delta, as_percent)} |")
    lines.extend(["", "Bootstrap intervals are paired 95% intervals over task/trial observations and are descriptive, not a claim of statistical significance.", "", "## Category breakdown", "", "| Category | Accuracy delta | Pass-rate delta | SDC delta |", "|---|---:|---:|---:|"])
    for category, values in document["categories"].items():
        lines.append(f"| {category} | {fmt(values['semantic_fact_accuracy']['absolute_delta'], True)} | {fmt(values['final_task_pass_rate']['absolute_delta'], True)} | {fmt(values['semantic_detour_cost']['absolute_delta'])} |")
    adoption = document["adoption"]
    lines.extend([
        "", "## Adoption", "",
        f"Legacy semantic-query adoption: {fmt(adoption['legacy']['semantic_query_adoption'], True)}. Phase 22.3 adoption: {fmt(adoption['phase22_3']['semantic_query_adoption'], True)}.",
        f"Treatment `resolve` adoption: {fmt(adoption['phase22_3']['resolve_adoption'], True)}.",
        f"Legacy query distribution: `{json.dumps(adoption['legacy']['query_type_distribution'], sort_keys=True)}`.",
        f"Treatment query distribution: `{json.dumps(adoption['phase22_3']['query_type_distribution'], sort_keys=True)}`.",
        "", "## Failure analysis", "",
    ])
    for key, values in document["failure_analysis"].items():
        lines.append(f"- {key.replace('_', ' ').title()}: {', '.join(values) if values else 'none'}")
    accuracy = metrics["semantic_fact_accuracy"]["absolute_delta"]
    detour = metrics["semantic_detour_cost"]["absolute_delta"]
    completion = metrics["final_task_pass_rate"]["absolute_delta"]
    lines.extend(["", "## Interpretation", ""])
    lines.append(f"Under the controlled query-surface intervention, Phase 22.3 changed pooled semantic fact accuracy by {fmt(accuracy, True)}, mean semantic detour cost by {fmt(detour)}, and final completion by {fmt(completion, True)}. These deltas concern agent semantic observability and workflow efficiency; they do not imply any change to Moss language semantics.")
    if adoption["phase22_3"]["semantic_query_adoption"] is not None and adoption["phase22_3"]["semantic_query_adoption"] < 1:
        lines.append("\nPhase 22.3 improves semantic retrieval when agents use it, while discovery/adoption remains a separate agent-workflow problem.")
    failures = sum(
        summary.get("infrastructure_failures", 0)
        for summary in document["profile_summaries"].values()
    )
    remaining = (
        f"{failures} infrastructure-failed trials remain a measurement gap."
        if failures else "None. All planned trials completed with the required raw evidence."
    )
    lines.extend(["", "## Remaining work", "", remaining, ""])
    return "\n".join(lines)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--runs-root", type=Path, default=DEFAULT_RUNS)
    parser.add_argument("--comparison", type=Path)
    parser.add_argument("--report", type=Path)
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args()
    try:
        runs = args.runs_root.resolve()
        document = build_comparison(runs)
        comparison_path = (args.comparison or (SUITE / "comparison.json" if runs == DEFAULT_RUNS.resolve() else runs / "comparison.json")).resolve()
        report_path = (args.report or (SUITE / "REPORT.md" if runs == DEFAULT_RUNS.resolve() else runs / "REPORT.md")).resolve()
        write_or_check(comparison_path, json.dumps(document, indent=2, sort_keys=True) + "\n", args.check)
        write_or_check(report_path, report(document), args.check)
        return 0
    except (OSError, ValueError, RuntimeError, KeyError) as error:
        print(f"comparison error: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
