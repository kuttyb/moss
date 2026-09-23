#!/usr/bin/env python3
"""Focused regressions for deterministic Phase 22.2D aggregation."""

from __future__ import annotations

import importlib.util
import json
from pathlib import Path
import shutil
import subprocess
import sys


ROOT = Path(__file__).resolve().parents[2]
BASELINE = ROOT / "benchmarks" / "agent" / "baselines" / "pre-22.1"
TASKS = ROOT / "benchmarks" / "agent" / "tasks"
SCRATCH = ROOT / "tmp" / "agent-baseline-analysis-test"


def load_analysis():
    path = ROOT / "benchmarks" / "agent" / "analyze_baseline.py"
    spec = importlib.util.spec_from_file_location("moss_agent_baseline_analysis", path)
    assert spec and spec.loader
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def fixture(name: str) -> Path:
    destination = SCRATCH / name
    destination.mkdir(parents=True)
    shutil.copy2(BASELINE / "protocol.json", destination / "protocol.json")
    for index in range(1, 31):
        task_id = f"AB{index:03d}"
        source = BASELINE / task_id
        target = destination / task_id
        target.mkdir()
        for filename in ("manifest.json", "moss-tool-log.jsonl", "result.json"):
            shutil.copy2(source / filename, target / filename)
    return destination


def expect_error(analysis, baseline: Path, text: str) -> None:
    try:
        analysis.build_aggregate(baseline, TASKS)
    except analysis.AnalysisError as error:
        assert text in str(error), error
    else:
        raise AssertionError(f"expected analysis error containing {text!r}")


def rewrite(path: Path, update) -> None:
    value = json.loads(path.read_text(encoding="utf-8"))
    update(value)
    path.write_text(json.dumps(value, indent=2, sort_keys=True) + "\n", encoding="utf-8")


def main() -> int:
    analysis = load_analysis()
    shutil.rmtree(SCRATCH, ignore_errors=True)
    SCRATCH.mkdir(parents=True)

    raw_before = analysis.raw_tree_digest(BASELINE)
    aggregate = analysis.build_aggregate(BASELINE, TASKS)
    duplicate_aggregate = analysis.build_aggregate(BASELINE, TASKS)
    assert aggregate == duplicate_aggregate
    assert analysis.stable_json(aggregate) == (BASELINE / "aggregate.json").read_text(encoding="utf-8")
    assert analysis.render_report(aggregate) == (BASELINE / "REPORT.md").read_text(encoding="utf-8")
    assert analysis.raw_tree_digest(BASELINE) == raw_before

    task_ids = [item["task_id"] for item in aggregate["task_results"]]
    assert task_ids == [f"AB{index:03d}" for index in range(1, 31)]
    assert len(task_ids) == len(set(task_ids)) == 30
    assert aggregate["pass_count"] == 28
    assert aggregate["fail_count"] == 2
    assert aggregate["infrastructure_failure_count"] == 0
    assert aggregate["pass_count"] + aggregate["fail_count"] == aggregate["valid_task_count"]
    assert sum(item["task_count"] for item in aggregate["by_category"].values()) == 30
    assert sum(item["task_count"] for item in aggregate["by_task_type"].values()) == 30
    assert {key: value["task_count"] for key, value in aggregate["by_task_type"].items()} == {
        "repair": 10, "workflow": 10, "write": 10,
    }
    assert aggregate["attempts_to_green"] == {
        "count": 30, "mean": 1.433, "median": 1.0, "max": 2,
    }
    assert aggregate["diagnostics"] == duplicate_aggregate["diagnostics"]
    assert aggregate["tool_usage"] == duplicate_aggregate["tool_usage"]

    check = subprocess.run(
        [sys.executable, str(ROOT / "benchmarks" / "agent" / "analyze_baseline.py"), "--check"],
        cwd=ROOT, text=True, capture_output=True, check=False, timeout=60,
    )
    assert check.returncode == 0, (check.stdout, check.stderr)

    missing = fixture("missing")
    shutil.rmtree(missing / "AB030")
    expect_error(analysis, missing, "missing canonical task IDs")

    duplicate = fixture("duplicate")
    shutil.copytree(duplicate / "AB030", duplicate / "AB999_duplicate")
    expect_error(analysis, duplicate, "duplicate canonical task ID AB030")

    prompt_drift = fixture("prompt-drift")
    rewrite(prompt_drift / "AB030" / "manifest.json", lambda value: value.update({
        "task_prompt_sha256": "0" * 64,
    }))
    expect_error(analysis, prompt_drift, "task prompt hash mismatch for AB030")

    infrastructure = fixture("infrastructure")
    rewrite(infrastructure / "AB030" / "manifest.json", lambda value: value.update({
        "state": "infrastructure_failure",
        "infrastructure_error": "synthetic launch failure",
    }))
    separated = analysis.build_aggregate(infrastructure, TASKS)
    assert separated["task_count"] == 30
    assert separated["valid_task_count"] == 29
    assert separated["pass_count"] == 27
    assert separated["fail_count"] == 2
    assert separated["infrastructure_failure_count"] == 1
    assert separated["infrastructure_failure_task_ids"] == ["AB030"]
    assert next(item for item in separated["task_results"]
                if item["task_id"] == "AB030")["final_status"] is None

    no_green = fixture("no-green")
    rewrite(no_green / "AB030" / "manifest.json", lambda value: value.update({
        "first_validation_success": False,
        "validation_attempts_to_green": None,
    }))
    null_stats = analysis.build_aggregate(no_green, TASKS)
    assert null_stats["eventually_green_count"] == 29
    assert null_stats["never_green_count"] == 1
    assert null_stats["attempts_to_green"]["count"] == 29
    assert null_stats["attempts_to_green"]["max"] == 2

    print("Agent baseline aggregate reconciliation, determinism, and failure separation checks passed.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
