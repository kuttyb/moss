#!/usr/bin/env python3
"""Lightweight regression suite for the Phase 22.3 A/B benchmark."""

from __future__ import annotations

import importlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile


ROOT = Path(__file__).resolve().parents[2]
SUITE = ROOT / "benchmarks" / "agent" / "phase22_3_ab"
sys.path.insert(0, str(SUITE))
baseline = importlib.import_module("baseline")
compare = importlib.import_module("compare")
runner = importlib.import_module("runner")
telemetry = importlib.import_module("telemetry")
COMPILER = Path(sys.argv[1] if len(sys.argv) > 1 else ROOT / "moss").resolve()


def fail(message: str) -> None:
    raise AssertionError(message)


def make_proxy(root: Path) -> Path:
    path = root / "moss"
    shutil.copy2(SUITE / "profile_proxy.py", path)
    path.chmod(0o755)
    return path


def invoke(proxy_path: Path, profile: str, cwd: Path, *arguments: str) -> subprocess.CompletedProcess[str]:
    environment = dict(os.environ)
    environment.update({
        "MOSS_AGENT_QUERY_PROFILE": profile,
        "MOSS_AB_REAL_MOSS": str(COMPILER),
        "MOSS_AB_REAL_MARGO": str((ROOT / "margo").resolve()),
        "MOSS_AB_TOOL_LOG": str(proxy_path.parent / f"{profile}.jsonl"),
        "MOSS_AB_WORKSPACE": str(proxy_path.parent),
        "RUSTC": shutil.which("rustc") or "rustc",
    })
    return subprocess.run([str(proxy_path), *arguments], cwd=cwd, env=environment, text=True, capture_output=True, check=False)


def document(process: subprocess.CompletedProcess[str]) -> dict:
    try:
        return json.loads(process.stdout)
    except json.JSONDecodeError as error:
        fail(f"invalid proxy JSON: {error}: {process.stdout} {process.stderr}")


tasks = runner.load_tasks()
if len(tasks) != 20 or sum(task["semantic_heavy"] for task in tasks) != 16:
    fail("task schema/corpus balance validation failed")
runner.validate_expected(tasks, COMPILER, (ROOT / "margo").resolve())
score = runner.score_answer(tasks[0], runner.TASKS / tasks[0]["task_directory"] / "expected" / "semantic-answer.json")
if not score["schema_valid"] or score["accuracy"] != 1:
    fail("semantic-answer scorer rejected reviewed ground truth")

with tempfile.TemporaryDirectory(dir=ROOT / "tmp", prefix="phase223-ab-test-") as temporary:
    scratch = Path(temporary)
    tool = make_proxy(scratch)
    fixture = scratch / "query.moss"
    fixture.write_text("fn identity(value):\n  return value\n\nfn main():\n  echo identity(2)\n", encoding="utf-8")

    legacy_bootstrap = document(invoke(tool, "legacy", scratch, "agent", "bootstrap", "--json"))
    treatment_bootstrap = document(invoke(tool, "phase22_3", scratch, "agent", "bootstrap", "--json"))
    if "semantic_query_expansion" in legacy_bootstrap["result"]["capabilities"] or legacy_bootstrap["result"]["semantic_queries"][0]["name"] == "resolve":
        fail("legacy bootstrap leaked Phase 22.3 discovery")
    if "semantic_query_expansion" not in treatment_bootstrap["result"]["capabilities"] or treatment_bootstrap["result"]["semantic_queries"][0]["name"] != "resolve":
        fail("treatment bootstrap omitted Phase 22.3 discovery")

    legacy_schema = document(invoke(tool, "legacy", scratch, "agent", "schema", "--json"))
    treatment_schema = document(invoke(tool, "phase22_3", scratch, "agent", "schema", "--json"))
    if "query_resolution" in legacy_schema["result"]["schema"]:
        fail("legacy schema leaked robust resolution contract")
    if treatment_schema["result"]["schema"]["query_resolution"]["statuses"] != ["resolved", "ambiguous", "missing"]:
        fail("treatment schema omitted resolution statuses")

    unavailable = document(invoke(tool, "legacy", scratch, "resolve", "main", "--source", str(fixture), "--json"))
    if unavailable.get("error", {}).get("code") != "QUERY_PROFILE_UNAVAILABLE":
        fail("legacy resolve was not intentionally unavailable")
    resolved = document(invoke(tool, "phase22_3", scratch, "resolve", "main", "--source", str(fixture), "--json"))
    if resolved.get("result", {}).get("status") != "resolved":
        fail("treatment resolve did not resolve")

    treatment_effects = document(invoke(tool, "phase22_3", scratch, "effects", "fn:identity", "--source", str(fixture), "--json"))
    legacy_effects = document(invoke(tool, "legacy", scratch, "effects", "fn:identity", "--source", str(fixture), "--json"))
    if "effect_summary" not in treatment_effects["result"] or "effect_summary" in legacy_effects["result"]:
        fail("query profile did not isolate expanded effect facts")

    legacy_check = invoke(tool, "legacy", scratch, "check", str(fixture), "--json")
    treatment_check = invoke(tool, "phase22_3", scratch, "check", str(fixture), "--json")
    if legacy_check.returncode != 0 or treatment_check.returncode != 0 or document(legacy_check) != document(treatment_check):
        fail("profiles changed check behavior")
    legacy_run = invoke(tool, "legacy", scratch, "run", "--interp", str(fixture))
    treatment_run = invoke(tool, "phase22_3", scratch, "run", "--interp", str(fixture))
    if (legacy_run.returncode, legacy_run.stdout, legacy_run.stderr) != (treatment_run.returncode, treatment_run.stdout, treatment_run.stderr):
        fail("profiles changed Fast Debug behavior")

    project = scratch / "project"
    (project / "src").mkdir(parents=True)
    (project / "Moss.toml").write_text('[project]\nname = "profile-equivalence"\nversion = "0.1.0"\n\n[build]\nsource = "src"\n', encoding="utf-8")
    (project / "src" / "main.moss").write_text("fn main():\n  echo 6\n", encoding="utf-8")
    outputs, generated = [], []
    for profile in ("legacy", "phase22_3"):
        shutil.rmtree(project / "build", ignore_errors=True)
        built = invoke(tool, profile, project, "build", "--json")
        if built.returncode != 0:
            fail(f"{profile} native build failed: {built.stdout} {built.stderr}")
        result = document(built)["result"]
        executable = Path(result["artifacts"]["executable"])
        generated_path = Path(result["artifacts"]["generated_rust"])
        outputs.append(subprocess.run([str(executable)], text=True, capture_output=True, check=False).stdout)
        generated.append(generated_path.read_bytes())
    if outputs != ["6\n", "6\n"] or generated[0] != generated[1]:
        fail("profiles changed native behavior or generated Rust")

    legacy_stage = scratch / "legacy-stage"
    legacy_stage.mkdir()
    hashes = baseline.copy_profile_environment(legacy_stage, "legacy")
    historical = subprocess.run(["git", "show", f"{baseline.LEGACY_COMMIT}:docs/AGENT_API.md"], cwd=ROOT, capture_output=True, check=True).stdout
    if hashes["docs/AGENT_API.md"] != runner.sha256_bytes(historical) or (legacy_stage / "docs" / "PHASE_22_3_QUERY_MAPPING.md").exists():
        fail("legacy documentation snapshot is contaminated")

reads = telemetry.classify_source_reads([
    "sed -n '1,20p' task/main.moss", "nl -ba task/other.moss", "cat build/App.rs", "head docs/AGENT_API.md", "rg value task/module.mossi",
], ROOT)
if reads["moss_source_inspection_commands"] != 2 or reads["generated_rust_inspection_commands"] != 1 or reads["documentation_inspection_commands"] != 1 or reads["mossi_inspection_commands"] != 1:
    fail("source-read telemetry classification failed")

legacy_trials = [{"task_id": "P223001", "trial_id": "trial-001", "semantic_fact_accuracy": 0.0}]
treatment_trials = [{"task_id": "P223001", "trial_id": "trial-001", "semantic_fact_accuracy": 1.0}]
first = compare.paired_metric(legacy_trials, treatment_trials, compare.PRIMARY["semantic_fact_accuracy"])
second = compare.paired_metric(legacy_trials, treatment_trials, compare.PRIMARY["semantic_fact_accuracy"])
if first != second or first["absolute_delta"] != 1.0:
    fail("deterministic paired comparison failed")

weighted_legacy = [
    {"task_id":"P223001","trial_id":"trial-001","semantic_facts_correct":1,"semantic_facts_requested":1},
    {"task_id":"P223002","trial_id":"trial-001","semantic_facts_correct":0,"semantic_facts_requested":3},
]
weighted_treatment = [
    {"task_id":"P223001","trial_id":"trial-001","semantic_facts_correct":1,"semantic_facts_requested":1},
    {"task_id":"P223002","trial_id":"trial-001","semantic_facts_correct":3,"semantic_facts_requested":3},
]
weighted = compare.paired_fact_accuracy(weighted_legacy, weighted_treatment)
if weighted["legacy_mean"] != 0.25 or weighted["phase22_3_mean"] != 1.0 or weighted["absolute_delta"] != 0.75:
    fail("semantic fact accuracy was not pooled as correct/requested")

protocol = {"compiler_commit": "a", "benchmark_commit": "a", "compiler_binary_sha256": "b", "agent": {}, "limits": {}, "task_ids": ["P223001"], "trials_per_task": 1}
trial = {"task_id":"P223001","trial_id":"trial-001","compiler_commit":"a","compiler_binary_sha256":"b","prompt_sha256":"p","prompt_wrapper_sha256":"w","starter_tree_sha256":"s","task_metadata_sha256":"m","validator_sha256":"v","agent":{}}
left = {"protocol": protocol, "trials": [trial]}
right = {"protocol": dict(protocol), "trials": [dict(trial)]}
compare.verify_integrity(left, right)
right["trials"][0]["prompt_sha256"] = "different"
try:
    compare.verify_integrity(left, right)
except RuntimeError:
    pass
else:
    fail("profile protocol mismatch was not rejected")

with tempfile.TemporaryDirectory(dir=ROOT / "tmp", prefix="phase223-resume-") as temporary:
    artifact_root = Path(temporary)
    artifact = artifact_root / "legacy" / "P223001" / "trial-001"
    artifact.mkdir(parents=True)
    marker = {"state": "completed", "final_validator_pass": False}
    (artifact / "manifest.json").write_text(json.dumps(marker), encoding="utf-8")
    resumed = baseline.run_one({"id": "P223001"}, "legacy", 1, artifact_root / "runtime", artifact_root, {})
    if resumed != marker:
        fail("completed trial was rerun instead of resumed")

with tempfile.TemporaryDirectory(dir=ROOT / "tmp", prefix="phase223-analysis-") as temporary:
    runs = Path(temporary)
    protocol = {
        "compiler_commit":"a","benchmark_commit":"a","compiler_binary_sha256":"b",
        "agent":{},"limits":{},"task_ids":["P223001"],"trials_per_task":1,
        "profile_docs_sha256":{"legacy":{},"phase22_3":{}},
    }
    (runs / "protocol.json").write_text(json.dumps(protocol), encoding="utf-8")
    for profile, accuracy in (("legacy", 0.0), ("phase22_3", 1.0)):
        directory = runs / profile / "P223001" / "trial-001"
        directory.mkdir(parents=True)
        manifest = {
            "task_id":"P223001","trial_id":"trial-001","condition":profile,
            "category":"resolution","semantic_heavy":True,"state":"completed",
            "compiler_commit":"a","compiler_binary_sha256":"b","final_validator_pass":True,
            "prompt_sha256":"p","prompt_wrapper_sha256":"w","starter_tree_sha256":"s",
            "task_metadata_sha256":"m","validator_sha256":"v","agent":{},
            "semantic_facts_correct":int(accuracy),"semantic_facts_requested":1,
            "semantic_fact_accuracy":accuracy,"semantic_query_invocations":int(accuracy),
            "resolve_invocations":int(accuracy),"query_type_distribution":({"resolve":1} if accuracy else {}),
        }
        (directory / "manifest.json").write_text(json.dumps(manifest), encoding="utf-8")
    aggregate = importlib.import_module("analyze").build_aggregate(runs, "phase22_3")
    if aggregate["summary"]["semantic_fact_accuracy"] != 1.0:
        fail("aggregate generation failed")
    comparison_one = compare.build_comparison(runs)
    comparison_two = compare.build_comparison(runs)
    if comparison_one != comparison_two or comparison_one["metrics"]["semantic_fact_accuracy"]["absolute_delta"] != 1.0:
        fail("deterministic comparison generation failed")

print("Phase 22.3 A/B schemas, profiles, equivalence, scoring, telemetry, integrity, and resume checks passed.")
