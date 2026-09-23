#!/usr/bin/env python3
"""Build the deterministic Phase 22.1 pre/post agent-benchmark comparison."""

from __future__ import annotations

import argparse
import json
from pathlib import Path
import sys
from typing import Any

import analyze_baseline as analysis


ROOT = Path(__file__).resolve().parents[2]
SUITE = ROOT / "benchmarks" / "agent"
DEFAULT_PRE = SUITE / "baselines" / "pre-22.1"
DEFAULT_POST = SUITE / "baselines" / "post-22.1"
SCHEMA_VERSION = "moss-agent-baseline-comparison-1"
QUERY_OPERATIONS = {"inspect", "type", "effects", "ownership", "calls", "why"}
TARGET_TASKS = (
    "AB008", "AB009", "AB010", "AB012", "AB015", "AB017",
    "AB018", "AB019", "AB020", "AB021", "AB022", "AB023",
)


def delta(before: int | float, after: int | float) -> int | float:
    return round(after - before, 3)


def tool_invocations(aggregate: dict[str, Any]) -> int:
    return sum(item["invocation_count"] for item in aggregate["tool_usage"].values())


def agent_tool_calls(aggregate: dict[str, Any]) -> int:
    return sum(item["agent_tool_calls"] for item in aggregate["task_results"])


def diagnostic_occurrences(aggregate: dict[str, Any]) -> int:
    return sum(item["total_occurrences"] for item in aggregate["diagnostics"].values())


def failed_correctness_attempts(aggregate: dict[str, Any]) -> int:
    return sum(item["validation_attempts_to_green"] - 1
               for item in aggregate["task_results"]
               if item["validation_attempts_to_green"] is not None)


def query_failures(root: Path, task_id: str | None = None) -> dict[str, int]:
    task_ids = [task_id] if task_id else [f"AB{index:03d}" for index in range(1, 31)]
    failed = 0
    target_not_found = 0
    for current in task_ids:
        for record in analysis.read_jsonl(root / current / "moss-tool-log.jsonl"):
            argv = record.get("argv", [])
            if (record.get("origin") != "agent" or record.get("tool") != "moss"
                    or not argv or argv[0] not in QUERY_OPERATIONS):
                continue
            if record.get("exit_code") != 0:
                failed += 1
            if "QUERY_TARGET_NOT_FOUND" in record.get("diagnostic_codes", []):
                target_not_found += 1
    return {"failed_calls": failed, "target_not_found": target_not_found}


def diagnostics_text(task: dict[str, Any]) -> str:
    return ", ".join(
        f"{code} x{count}" if count != 1 else code
        for code, count in task["diagnostic_codes"].items()
    ) or "none"


def selected_metrics(aggregate: dict[str, Any], root: Path) -> dict[str, Any]:
    return {
        "final_passes": aggregate["pass_count"],
        "final_failures": aggregate["fail_count"],
        "first_validation_successes": aggregate["first_validation_success_count"],
        "eventually_green": aggregate["eventually_green_count"],
        "failed_correctness_attempts_before_green": failed_correctness_attempts(aggregate),
        "attempts_to_green_mean": aggregate["attempts_to_green"]["mean"],
        "attempts_to_green_median": aggregate["attempts_to_green"]["median"],
        "attempts_to_green_max": aggregate["attempts_to_green"]["max"],
        "diagnostic_occurrences": diagnostic_occurrences(aggregate),
        "repeated_diagnostic_loop_tasks": len(aggregate["repeated_diagnostic_loops"]),
        "failed_semantic_query_calls": query_failures(root)["failed_calls"],
        "query_target_not_found_calls": query_failures(root)["target_not_found"],
        "agent_tool_calls": agent_tool_calls(aggregate),
        "moss_margo_invocations": tool_invocations(aggregate),
        "infrastructure_failures": aggregate["infrastructure_failure_count"],
        "timeouts": sum(item["state"] == "agent_timeout" for item in aggregate["task_results"]),
        "out_of_scope_modification_tasks": aggregate["out_of_scope_modification_task_count"],
    }


def build_comparison(pre_root: Path, post_root: Path) -> dict[str, Any]:
    pre = analysis.build_aggregate(pre_root, SUITE / "tasks")
    post = analysis.build_aggregate(post_root, SUITE / "tasks")
    pre_protocol = analysis.read_json(pre_root / "protocol.json")
    post_protocol = analysis.read_json(post_root / "protocol.json")
    pre_tasks = {item["task_id"]: item for item in pre["task_results"]}
    post_tasks = {item["task_id"]: item for item in post["task_results"]}
    before = selected_metrics(pre, pre_root)
    after = selected_metrics(post, post_root)
    task_comparison = []
    for task_id in TARGET_TASKS:
        left = pre_tasks[task_id]
        right = post_tasks[task_id]
        task_comparison.append({
            "task_id": task_id,
            "pre_diagnostics": left["diagnostic_codes"],
            "post_diagnostics": right["diagnostic_codes"],
            "pre_attempts_to_green": left["validation_attempts_to_green"],
            "post_attempts_to_green": right["validation_attempts_to_green"],
            "pre_failed_query_calls": query_failures(pre_root, task_id)["failed_calls"],
            "post_failed_query_calls": query_failures(post_root, task_id)["failed_calls"],
            "pre_moss_margo_invocations": sum(left["tool_usage"].values()),
            "post_moss_margo_invocations": sum(right["tool_usage"].values()),
            "pre_agent_tool_calls": left["agent_tool_calls"],
            "post_agent_tool_calls": right["agent_tool_calls"],
            "pre_final_status": left["final_status"],
            "post_final_status": right["final_status"],
        })
    protocol_matches = {
        key: pre[key] == post[key]
        for key in ("corpus_commit", "prompt_wrapper_sha256", "protocol_version", "limits")
    }
    protocol_matches["isolation"] = pre_protocol["isolation"] == post_protocol["isolation"]
    protocol_matches["metrics"] = pre_protocol["metrics"] == post_protocol["metrics"]
    for key in ("implementation", "runtime_version", "model", "reasoning_effort", "configuration"):
        protocol_matches[f"agent.{key}"] = pre["agent"][key] == post["agent"][key]
    return {
        "schema_version": SCHEMA_VERSION,
        "pre_baseline": pre["baseline"],
        "post_baseline": post["baseline"],
        "pre_compiler_commit": pre["compiler_commit"],
        "post_compiler_commit": post["compiler_commit"],
        "pre_orchestration_commit": pre["orchestration_commit"],
        "post_orchestration_commit": post["orchestration_commit"],
        "protocol_matches": protocol_matches,
        "comparison_confounders": [],
        "pre": before,
        "post": after,
        "delta": {key: delta(before[key], after[key]) for key in before},
        "target_tasks": task_comparison,
        "target_totals": {
            "pre_agent_tool_calls": sum(item["pre_agent_tool_calls"] for item in task_comparison),
            "post_agent_tool_calls": sum(item["post_agent_tool_calls"] for item in task_comparison),
            "pre_moss_margo_invocations": sum(item["pre_moss_margo_invocations"] for item in task_comparison),
            "post_moss_margo_invocations": sum(item["post_moss_margo_invocations"] for item in task_comparison),
            "pre_failed_query_calls": sum(item["pre_failed_query_calls"] for item in task_comparison),
            "post_failed_query_calls": sum(item["post_failed_query_calls"] for item in task_comparison),
        },
    }


def render_report(comparison: dict[str, Any]) -> str:
    before = comparison["pre"]
    after = comparison["post"]
    lines = [
        "# Moss Phase 22.1 Agent-Teaching Diagnostics Comparison",
        "",
        "## Protocol and interpretation",
        "",
        (f"This compares the frozen `{comparison['pre_baseline']}` run at compiler commit "
         f"`{comparison['pre_compiler_commit']}` with `{comparison['post_baseline']}` at "
         f"`{comparison['post_compiler_commit']}`. AB001-AB030, the corpus revision, prompts, "
         "wrapper, one-session protocol, Codex CLI 0.156.0, `gpt-6-sol`, medium reasoning, "
         "900-second limit, isolation, network policy, and validators match."),
        "",
        "There are no known model/runtime/protocol confounders. The compiler and orchestration commit "
        "changed together as the intended treatment. This is one stochastic trial per task, so changes "
        "are engineering evidence, not statistically significant estimates.",
        "",
        "## Overall comparison",
        "",
        "| Metric | Pre | Post | Delta |",
        "|---|---:|---:|---:|",
    ]
    labels = (
        ("Final passes", "final_passes"),
        ("Final failures", "final_failures"),
        ("First-validation successes", "first_validation_successes"),
        ("Eventually green", "eventually_green"),
        ("Failed correctness attempts before green", "failed_correctness_attempts_before_green"),
        ("Attempts-to-green mean", "attempts_to_green_mean"),
        ("Attempts-to-green median", "attempts_to_green_median"),
        ("Attempts-to-green max", "attempts_to_green_max"),
        ("Diagnostic occurrences", "diagnostic_occurrences"),
        ("Repeated-diagnostic-loop tasks", "repeated_diagnostic_loop_tasks"),
        ("Failed semantic-query calls", "failed_semantic_query_calls"),
        ("QUERY_TARGET_NOT_FOUND calls", "query_target_not_found_calls"),
        ("Agent tool calls", "agent_tool_calls"),
        ("Moss/Margo invocations", "moss_margo_invocations"),
        ("Infrastructure failures", "infrastructure_failures"),
        ("Timeouts", "timeouts"),
        ("Out-of-scope modification tasks", "out_of_scope_modification_tasks"),
    )
    for label, key in labels:
        lines.append(f"| {label} | {before[key]} | {after[key]} | {comparison['delta'][key]:+g} |")
    lines += [
        "",
        "Headline outcomes were unchanged at 28/30 final passes and 30/30 eventually green. First-validation "
        "success fell by one and total tool calls rose, so this run does not support a broad efficiency claim. "
        "Both post-run failures (AB014 and AB017) were again exact-output formatting errors after successful "
        "compilation, not failures to recover from a compiler diagnostic.",
        "",
        "The query-target result is the clearest targeted recovery change: AB015 used canonical qualified targets "
        "immediately, eliminating two `QUERY_TARGET_NOT_FOUND` calls and removing that repeated-diagnostic loop. "
        "AB022 still deliberately queried the same ownership error through check, ownership, and effects; its "
        "cross-command rule and cause were consistent, but the three recorded occurrences remained.",
        "",
        "## Targeted task comparison",
        "",
        "| Task | Pre diagnostic(s) | Post diagnostic(s) | Attempts pre/post | Failed queries pre/post | Moss/Margo pre/post | Agent tools pre/post | Final pre/post |",
        "|---|---|---|---:|---:|---:|---:|---|",
    ]
    for item in comparison["target_tasks"]:
        pre_task = {"diagnostic_codes": item["pre_diagnostics"]}
        post_task = {"diagnostic_codes": item["post_diagnostics"]}
        lines.append(
            f"| {item['task_id']} | `{diagnostics_text(pre_task)}` | `{diagnostics_text(post_task)}` | "
            f"{item['pre_attempts_to_green']}/{item['post_attempts_to_green']} | "
            f"{item['pre_failed_query_calls']}/{item['post_failed_query_calls']} | "
            f"{item['pre_moss_margo_invocations']}/{item['post_moss_margo_invocations']} | "
            f"{item['pre_agent_tool_calls']}/{item['post_agent_tool_calls']} | "
            f"{item['pre_final_status']}/{item['post_final_status']} |"
        )
    totals = comparison["target_totals"]
    lines += [
        "",
        (f"Across these 12 evidence-backed tasks, Moss/Margo invocations were unchanged at "
         f"{totals['pre_moss_margo_invocations']}, failed semantic-query calls fell "
         f"{totals['pre_failed_query_calls']}→{totals['post_failed_query_calls']}, and agent tool calls rose "
         f"{totals['pre_agent_tool_calls']}→{totals['post_agent_tool_calls']}. All reached green in at most two "
         "correctness attempts. Diagnostic differentiation—not a lower attempts-to-green result—is the main result."),
        "",
        "## Diagnostic-specific findings",
        "",
        "- AB008, AB009, and AB023 changed from generic `MOSS_COMPILE_ERROR` to `DOMAIN_SELF_MESSAGE`; AB021 "
        "changed to `DOMAIN_HANDLER_REQUIRES_MESSAGE`; AB010 now reports `DOMAIN_ROUTE_NOT_DECLARED` instead "
        "of the downstream `TYPE_INFERENCE_FAILED`. Each recovered in one additional correctness attempt.",
        "- AB018-AB020 changed from `FUNCTIONAL_SEMANTIC_ERROR` to, respectively, "
        "`FUNCTIONAL_PLACEHOLDER_REQUIRED`, `FUNCTIONAL_CALLABLE_INVOCATION_UNSUPPORTED`, and "
        "`FUNCTIONAL_CAPTURE_MUTATION`. AB019 performed extra formatting/check commands; the run does not "
        "show lower command cost for this family.",
        "- AB012 and AB022 retained `OWNERSHIP_CONFLICTING_ACCESS`, now with structured conflicting actuals, "
        "inferred modes, related locations, and sound separation guidance. AB022's repeated exploration remained.",
        "- AB015's failed target-resolution queries fell 2→0. The compiler-known qualified handler identities in "
        "bootstrap/API guidance were sufficient for this trial; no fuzzy lookup was added.",
        "- AB017 retained `TYPE_INFERENCE_FAILED` and used fewer Moss commands, but again failed only final output "
        "shape. The exact transient starter source was not retained in the frozen evidence, so Phase 22.1 did "
        "not invent a more specific inference class for it.",
        "",
        "## Scope and limitations",
        "",
        "The accepted/rejected language set and Moss semantics were intentionally unchanged. Raw task artifacts "
        "are retained adjacent to this report. `aggregate.json` is the deterministic post-run aggregate and "
        "`comparison.json` is the machine-readable source for this comparison. The frozen pre-run directory was "
        "not regenerated or modified. Generic diagnostics outside the evidence-backed families, including AB005's "
        "unrelated `MOSS_COMPILE_ERROR`, remain deferred.",
    ]
    return "\n".join(line.rstrip() for line in lines) + "\n"


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(prog="compare-agent-baselines")
    parser.add_argument("--pre-root", type=Path, default=DEFAULT_PRE)
    parser.add_argument("--post-root", type=Path, default=DEFAULT_POST)
    parser.add_argument("--check", action="store_true")
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    pre_root = args.pre_root.resolve()
    post_root = args.post_root.resolve()
    pre_digest = analysis.raw_tree_digest(pre_root)
    post_digest = analysis.raw_tree_digest(post_root)
    post_aggregate_text = analysis.stable_json(analysis.build_aggregate(post_root, SUITE / "tasks"))
    comparison = build_comparison(pre_root, post_root)
    comparison_text = analysis.stable_json(comparison)
    report_text = render_report(comparison)
    comparison_path = post_root / "comparison.json"
    aggregate_path = post_root / "aggregate.json"
    report_path = post_root / "REPORT.md"
    if args.check:
        stale = []
        if not aggregate_path.is_file() or aggregate_path.read_text(encoding="utf-8") != post_aggregate_text:
            stale.append(str(aggregate_path))
        if not comparison_path.is_file() or comparison_path.read_text(encoding="utf-8") != comparison_text:
            stale.append(str(comparison_path))
        if not report_path.is_file() or report_path.read_text(encoding="utf-8") != report_text:
            stale.append(str(report_path))
        if stale:
            raise analysis.AnalysisError(f"generated comparison is stale: {', '.join(stale)}")
    else:
        aggregate_path.write_text(post_aggregate_text, encoding="utf-8")
        comparison_path.write_text(comparison_text, encoding="utf-8")
        report_path.write_text(report_text, encoding="utf-8")
    if analysis.raw_tree_digest(pre_root) != pre_digest or analysis.raw_tree_digest(post_root) != post_digest:
        raise analysis.AnalysisError("comparison modified raw baseline inputs")
    print(json.dumps({
        "schema_version": SCHEMA_VERSION,
        "comparison": str(comparison_path),
        "report": str(report_path),
        "mode": "check" if args.check else "write",
    }, sort_keys=True))
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except analysis.AnalysisError as error:
        print(f"agent-baseline-comparison: {error}", file=sys.stderr)
        raise SystemExit(2)
