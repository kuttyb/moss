#!/usr/bin/env python3
"""Focused Phase 22.2C baseline isolation and telemetry checks."""

from __future__ import annotations

import importlib.util
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys


ROOT = Path(__file__).resolve().parents[2]
SCRATCH = ROOT / "tmp" / "agent-baseline-test"


def load_baseline():
    path = ROOT / "benchmarks" / "agent" / "baseline.py"
    spec = importlib.util.spec_from_file_location("moss_agent_baseline", path)
    assert spec and spec.loader
    module = importlib.util.module_from_spec(spec)
    sys.path.insert(0, str(path.parent))
    spec.loader.exec_module(module)
    return module


def run(arguments, *, cwd=ROOT, env=None, expected=0):
    process = subprocess.run(
        [str(value) for value in arguments], cwd=cwd, env=env,
        text=True, capture_output=True, check=False, timeout=180,
    )
    assert process.returncode == expected, (arguments, process.stdout, process.stderr)
    return process


def main() -> int:
    compiler = Path(sys.argv[1]).resolve() if len(sys.argv) == 2 else ROOT / "moss"
    baseline = load_baseline()
    rustc = baseline.resolved_rustc()
    assert rustc.is_file()
    shutil.rmtree(SCRATCH, ignore_errors=True)
    SCRATCH.mkdir(parents=True)
    tasks = baseline.load_suite(baseline.SUITE)

    task = baseline.find_task(tasks, "AB001")
    stage = SCRATCH / "prepared"
    manifest = baseline.prepare_task(task, stage)
    starter = baseline.TASKS / task["task_directory"] / task["starter"]
    assert manifest["task_id"] == "AB001"
    assert manifest["trial"] == 1
    assert manifest["starter_tree_sha256"] == baseline.tree_digest(starter)
    assert manifest["staged_tree_sha256"] == baseline.tree_digest(stage / "task")
    assert (stage / ".git").is_dir()
    assert not list((stage / ".git").iterdir())
    assert not list(stage.rglob("expected"))
    assert (stage / "moss").is_file()
    assert (stage / "margo").is_file()
    assert (stage / "docs" / "GENTLE_INTRODUCTION_TO_MOSS.md").is_file()
    assert (stage / ".agents" / "skills" / "moss-language" / "SKILL.md").is_file()

    environment = dict(os.environ)
    environment.update({
        "MOSS_BASELINE_WORKSPACE": str(stage),
        "MOSS_BASELINE_TOOL_LOG": str(stage / "logs" / "moss-tool-log.jsonl"),
        "MOSS_BASELINE_REAL_MOSS": str(compiler),
        "MOSS_BASELINE_REAL_MARGO": str(ROOT / "margo"),
        "RUSTC": str(rustc),
    })
    wrapped = run([stage / "moss", "check", "missing.moss", "--json"],
                  cwd=stage / "task", env=environment, expected=1)
    direct = run([compiler, "check", "missing.moss", "--json"],
                 cwd=stage / "task", expected=1)
    assert wrapped.stdout == direct.stdout
    assert wrapped.stderr == direct.stderr
    records = baseline.parse_jsonl(stage / "logs" / "moss-tool-log.jsonl")
    assert len(records) == 1
    assert records[0]["tool"] == "moss"
    assert records[0]["argv"] == ["check", "missing.moss", "--json"]
    assert records[0]["exit_code"] == 1
    assert records[0]["diagnostic_codes"]

    workflow = baseline.find_task(tasks, "AB029")
    workflow_stage = SCRATCH / "workflow"
    baseline.prepare_task(workflow, workflow_stage)
    validated = run([
        compiler, "agent", "benchmark", "run", "AB029",
        "--workdir", workflow_stage / "task", "--json",
    ])
    assert json.loads(validated.stdout)["result"]["status"] == "pass"

    state, status = baseline.classify_outcome(
        launch_error="could not start", validation_error=None,
        exit_code=None, timed_out=False, validator_status="pass",
    )
    assert (state, status) == ("infrastructure_failure", None)
    state, status = baseline.classify_outcome(
        launch_error=None, validation_error=None,
        exit_code=-15, timed_out=True, validator_status="fail",
    )
    assert (state, status) == ("agent_timeout", "fail")

    print("Agent baseline isolation, staging, logging, validation, and outcome checks passed.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
