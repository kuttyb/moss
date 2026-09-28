#!/usr/bin/env python3
"""Run the controlled Phase 22.3 semantic-query A/B experiment."""

from __future__ import annotations

import argparse
from concurrent.futures import ThreadPoolExecutor, as_completed
import hashlib
import io
import json
import os
from pathlib import Path
import platform
import random
import shutil
import signal
import subprocess
import sys
import tarfile
import time
from typing import Any

from runner import BenchmarkError, ROOT, SUITE, TASKS, find_task, load_tasks, tree_digest, validate_worktree
from telemetry import (
    classify_source_reads, commands_before_correct_query, event_commands, parse_agent_runtime, parse_jsonl,
    semantic_and_efficiency_metrics,
)


PROFILES = ("legacy", "phase22_3")
SCHEMA_VERSION = "moss-phase22.3-ab-run-1"
PROTOCOL_VERSION = "phase22.3-ab-v1"
DEFAULT_MODEL = "gpt-6-sol"
DEFAULT_REASONING = "medium"
DEFAULT_TIMEOUT = 900
DEFAULT_SEED = 223
LEGACY_COMMIT = "bb388d4aeb1df7f6dac682b427d666a44a748bf4"
RELEVANT_DOCS = (
    "AGENTS.md", ".codex/CURRENT_STATUS.md", "docs/AGENT_API.md",
    ".agents/skills/moss-language/SKILL.md",
    ".agents/skills/moss-agent-workflow/SKILL.md",
)


def write_json(path: Path, value: Any) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(value, indent=2, sort_keys=True) + "\n", encoding="utf-8")


def sha256_bytes(value: bytes) -> str:
    return hashlib.sha256(value).hexdigest()


def git_output(*arguments: str) -> str:
    process = subprocess.run(["git", *arguments], cwd=ROOT, text=True, capture_output=True, check=False)
    if process.returncode != 0:
        raise BenchmarkError("P223_GIT_ERROR", process.stderr.strip() or "git command failed")
    return process.stdout.strip()


def binary_version(argv: list[str]) -> str:
    process = subprocess.run(argv, text=True, capture_output=True, check=False)
    if process.returncode != 0:
        raise BenchmarkError("P223_TOOL_UNAVAILABLE", process.stderr.strip() or f"cannot run {argv[0]}")
    return (process.stdout or process.stderr).strip()


def git_file(commit: str, relative: str) -> bytes:
    process = subprocess.run(["git", "show", f"{commit}:{relative}"], cwd=ROOT, capture_output=True, check=False)
    if process.returncode != 0:
        raise BenchmarkError("P223_PROFILE_DOCS", process.stderr.decode(errors="replace"))
    return process.stdout


def profile_doc_hashes(profile: str) -> dict[str, str]:
    if profile == "legacy":
        return {relative: sha256_bytes(git_file(LEGACY_COMMIT, relative)) for relative in RELEVANT_DOCS}
    return {relative: sha256_bytes((ROOT / relative).read_bytes()) for relative in RELEVANT_DOCS}


def resolved_rustc() -> Path:
    configured = os.environ.get("RUSTC")
    if configured:
        found = shutil.which(configured)
        if found:
            return Path(found).absolute()
    rustup = shutil.which("rustup")
    if rustup:
        process = subprocess.run([rustup, "which", "rustc"], text=True, capture_output=True, check=False)
        if process.returncode == 0 and Path(process.stdout.strip()).is_file():
            return Path(process.stdout.strip()).absolute()
    found = shutil.which("rustc")
    if not found:
        raise BenchmarkError("P223_RUSTC_UNAVAILABLE", "cannot resolve the active Rust compiler")
    return Path(found).absolute()


def copy_git_archive(commit: str, stage: Path) -> None:
    process = subprocess.run(
        ["git", "archive", "--format=tar", commit, "docs", ".agents", ".codex/CURRENT_STATUS.md", "AGENTS.md", "README.md"],
        cwd=ROOT, capture_output=True, check=False,
    )
    if process.returncode != 0:
        raise BenchmarkError("P223_PROFILE_DOCS", process.stderr.decode(errors="replace"))
    with tarfile.open(fileobj=io.BytesIO(process.stdout), mode="r:") as archive:
        for member in archive.getmembers():
            target = (stage / member.name).resolve()
            if stage.resolve() not in target.parents and target != stage.resolve():
                raise BenchmarkError("P223_PROFILE_DOCS", "historical archive contains an unsafe path")
        archive.extractall(stage, filter="data")


def copy_profile_environment(stage: Path, profile: str) -> dict[str, str]:
    if profile == "legacy":
        copy_git_archive(LEGACY_COMMIT, stage)
    else:
        shutil.copytree(ROOT / "docs", stage / "docs")
        shutil.copytree(ROOT / ".agents", stage / ".agents")
        (stage / ".codex").mkdir()
        shutil.copy2(ROOT / ".codex" / "CURRENT_STATUS.md", stage / ".codex" / "CURRENT_STATUS.md")
        shutil.copy2(ROOT / "AGENTS.md", stage / "AGENTS.md")
        shutil.copy2(ROOT / "README.md", stage / "README.md")
    hashes = {}
    for relative in RELEVANT_DOCS:
        path = stage / relative
        if not path.is_file():
            raise BenchmarkError("P223_PROFILE_DOCS", f"profile {profile} omitted {relative}")
        hashes[relative] = sha256_bytes(path.read_bytes())
    tools = stage / ".ab-tools"
    tools.mkdir()
    shutil.copy2(ROOT / "moss", tools / "moss-real")
    shutil.copy2(ROOT / "margo", tools / "margo-real")
    proxy = SUITE / "profile_proxy.py"
    for name in ("moss", "margo"):
        shutil.copy2(proxy, stage / name)
        (stage / name).chmod(0o755)
    (tools / "moss-real").chmod(0o755)
    (tools / "margo-real").chmod(0o755)
    return hashes


def task_prompt(task: dict[str, Any]) -> str:
    wrapper = (SUITE / "prompt-wrapper.txt").read_text(encoding="utf-8").rstrip()
    allowed = ", ".join(task["allowed_paths"])
    facts = ", ".join(task["requested_facts"])
    return (
        f"{wrapper}\n\nTask ID: {task['id']}\nCategory: {task['category']}\n"
        f"Allowed paths: {allowed}\nRequested fact keys: {facts}\n\n"
        f"Task prompt (verbatim):\n{task['prompt']}\n"
    )


def prepare_stage(task: dict[str, Any], profile: str, destination: Path) -> dict[str, Any]:
    if destination.exists():
        shutil.rmtree(destination)
    destination.mkdir(parents=True)
    task_root = TASKS / task["task_directory"]
    shutil.copytree(task_root / "starter", destination / "task")
    docs_hashes = copy_profile_environment(destination, profile)
    for name in ("logs", ".home", ".auth", ".git"):
        (destination / name).mkdir()
    prompt = task_prompt(task)
    (destination / "prompt.txt").write_text(prompt, encoding="utf-8")
    return {
        "prompt_sha256": sha256_bytes(prompt.encode()),
        "prompt_wrapper_sha256": sha256_bytes((SUITE / "prompt-wrapper.txt").read_bytes()),
        "starter_tree_sha256": tree_digest(task_root / "starter"),
        "staged_tree_sha256": tree_digest(destination / "task"),
        "task_metadata_sha256": task["task_metadata_sha256"],
        "ground_truth_sha256": task["ground_truth_sha256"],
        "validator_sha256": task["validator_sha256"],
        "staged_docs_sha256": docs_hashes,
    }


def copy_auth(stage: Path) -> None:
    source = Path(os.environ.get("CODEX_HOME", Path.home() / ".codex")) / "auth.json"
    if not source.is_file():
        raise BenchmarkError("P223_AUTH_UNAVAILABLE", f"Codex credential file is unavailable: {source}")
    target = stage / ".auth" / "auth.json"
    shutil.copy2(source, target)
    target.chmod(0o600)


def environment_snapshot(stage: Path) -> dict[str, str]:
    ignored_prefixes = ("task/", "logs/", ".home/", ".auth/")
    ignored = {"prompt.txt"}
    result = {}
    for path in stage.rglob("*"):
        if not path.is_file():
            continue
        relative = path.relative_to(stage).as_posix()
        if relative.startswith(ignored_prefixes) or relative in ignored:
            continue
        result[relative] = sha256_bytes(path.read_bytes())
    return result


def environment_changes(stage: Path, before: dict[str, str]) -> list[str]:
    after = environment_snapshot(stage)
    return sorted(path for path in set(before) | set(after) if before.get(path) != after.get(path))


def namespace_command(stage: Path, profile: str, protocol: dict[str, Any]) -> list[str]:
    shell = r'''set -eu
stage=$1
repo=$2
auth=$3
mount --bind "$stage/.auth" "$auth"
mount --bind "$stage" "$repo"
mount -t tmpfs tmpfs "$repo/.auth"
mount -t tmpfs tmpfs "$repo/.git"
cd "$repo"
export HOME="$repo/.home"
export CODEX_HOME="$auth"
export PATH="$repo:$PATH"
export MOSS="$repo/moss"
export MARGO_HOME="$repo/.home/.margo"
export RUSTC="$7"
export MOSS_AGENT_QUERY_PROFILE="$6"
export MOSS_AB_WORKSPACE="$repo"
export MOSS_AB_TOOL_LOG="$repo/logs/tool-log.jsonl"
export MOSS_AB_REAL_MOSS="$repo/.ab-tools/moss-real"
export MOSS_AB_REAL_MARGO="$repo/.ab-tools/margo-real"
exec codex exec --ephemeral --json --skip-git-repo-check --ignore-user-config --ignore-rules \
  --sandbox workspace-write -c approval_policy="never" \
  --model "$4" -c "model_reasoning_effort=\"$5\"" \
  -c sandbox_workspace_write.network_access=false \
  --disable apps --disable plugins --disable browser_use \
  --disable browser_use_external --disable browser_use_full_cdp_access \
  --disable in_app_browser --disable computer_use --disable image_generation \
  --disable recommended_plugins --disable memories --disable multi_agent \
  --output-last-message "$repo/logs/last-message.txt" -
'''
    auth = Path(os.environ.get("CODEX_HOME", Path.home() / ".codex")).resolve()
    return [
        "unshare", "--user", "--map-root-user", "--mount", "--propagation", "private", "--fork",
        "/bin/bash", "-c", shell, "phase22.3-ab", str(stage.resolve()), str(ROOT), str(auth),
        protocol["agent"]["model"], protocol["agent"]["reasoning_effort"], profile,
        str(resolved_rustc()),
    ]


def prune_generated(task_root: Path, starter_root: Path) -> list[str]:
    removed = []
    starter_files = {path.relative_to(starter_root).as_posix() for path in starter_root.rglob("*") if path.is_file()}
    for directory in sorted((path for path in task_root.rglob("*") if path.is_dir() and path.name in {"build", "target", "__pycache__", ".margo", ".moss"}), reverse=True):
        removed.append(directory.relative_to(task_root).as_posix() + "/")
        shutil.rmtree(directory, ignore_errors=True)
    for path in sorted(item for item in task_root.rglob("*") if item.is_file()):
        relative = path.relative_to(task_root).as_posix()
        if (path.name == "Moss.lock" or path.suffix in {".rs", ".mossi", ".mossmap"}) and relative not in starter_files:
            removed.append(relative)
            path.unlink()
    return sorted(removed)


def run_one(task: dict[str, Any], profile: str, trial: int, runtime_root: Path, output_root: Path, protocol: dict[str, Any]) -> dict[str, Any]:
    trial_name = f"trial-{trial:03d}"
    artifact = output_root / profile / task["id"] / trial_name
    if (artifact / "manifest.json").is_file():
        return json.loads((artifact / "manifest.json").read_text(encoding="utf-8"))
    if artifact.exists():
        raise BenchmarkError("P223_ARTIFACT_INCOMPLETE", f"refusing to overwrite incomplete trial {artifact}")
    stage = runtime_root / f"{profile}-{task['id']}-{trial_name}"
    prepared = prepare_stage(task, profile, stage)
    if prepared["starter_tree_sha256"] != prepared["staged_tree_sha256"]:
        raise BenchmarkError("P223_STARTER_MISMATCH", f"staged starter differs for {task['id']}")
    if prepared["staged_docs_sha256"] != protocol["profile_docs_sha256"][profile]:
        raise BenchmarkError("P223_PROFILE_DOCS", f"staged documentation hash mismatch for {profile}")
    copy_auth(stage)
    snapshot = environment_snapshot(stage)
    events_path = stage / "logs" / "agent-events.jsonl"
    stderr_path = stage / "logs" / "agent-stderr.txt"
    started_at = time.time()
    started = time.monotonic()
    timed_out = False
    launch_error = None
    exit_code = None
    try:
        with events_path.open("w", encoding="utf-8") as events, stderr_path.open("w", encoding="utf-8") as errors:
            process = subprocess.Popen(
                namespace_command(stage, profile, protocol), cwd=ROOT, text=True,
                stdin=subprocess.PIPE, stdout=events, stderr=errors, start_new_session=True,
            )
            try:
                process.communicate(input=task_prompt(task), timeout=protocol["limits"]["wall_time_seconds"])
            except subprocess.TimeoutExpired:
                timed_out = True
                os.killpg(process.pid, signal.SIGTERM)
                try:
                    process.wait(timeout=10)
                except subprocess.TimeoutExpired:
                    os.killpg(process.pid, signal.SIGKILL)
                    process.wait()
            exit_code = process.returncode
    except OSError as error:
        launch_error = str(error)
    wall_time_ms = round((time.monotonic() - started) * 1000)

    tool_records = parse_jsonl(stage / "logs" / "tool-log.jsonl")
    events = parse_jsonl(events_path)
    commands = event_commands(events)
    ground = json.loads((TASKS / task["task_directory"] / "semantic_ground_truth.json").read_text(encoding="utf-8"))["facts"]
    tool_metrics = semantic_and_efficiency_metrics(tool_records, ground)
    read_metrics = classify_source_reads(commands, stage)
    bounded_commands = commands_before_correct_query(commands, tool_metrics["semantic_fact_query_ordinal"])
    bounded_reads = classify_source_reads(bounded_commands, stage)["source_read_events"]
    detour_read_classes = {
        "moss_source_inspection_commands", "mossi_inspection_commands",
        "generated_rust_inspection_commands",
    }
    read_before_fact = sum(bool(set(event["classes"]) & detour_read_classes) for event in bounded_reads)
    semantic_detour_cost = (
        tool_metrics["semantic_detour_cost_tool_actions"] + read_before_fact
        if task["semantic_heavy"] else None
    )

    starter = TASKS / task["task_directory"] / "starter"
    removed = prune_generated(stage / "task", starter)
    validation_error = None
    try:
        validator = validate_worktree(task, stage / "task", ROOT / "moss", ROOT / "margo")
    except (BenchmarkError, OSError, subprocess.SubprocessError) as error:
        validation_error = str(error)
        validator = {"task_id": task["id"], "validator_pass": False, "semantic_answer": {"schema_valid": False, "correct": 0, "requested": len(task["requested_facts"]), "accuracy": 0.0}, "validation": [], "changed_paths": [], "outside_allowed_paths": []}

    artifact.mkdir(parents=True)
    shutil.copytree(stage / "task", artifact / "final")
    shutil.copy2(stage / "prompt.txt", artifact / "prompt.txt")
    for source_name, target_name in (
        ("tool-log.jsonl", "tool-log.jsonl"), ("agent-events.jsonl", "agent-events.jsonl"),
        ("last-message.txt", "last-message.txt"), ("agent-stderr.txt", "agent-stderr.txt"),
    ):
        source = stage / "logs" / source_name
        target = artifact / target_name
        if source.is_file():
            shutil.copy2(source, target)
        elif target_name in {"tool-log.jsonl", "agent-events.jsonl", "last-message.txt"}:
            target.write_text("", encoding="utf-8")
    answer = stage / "task" / "semantic-answer.json"
    if answer.is_file():
        shutil.copy2(answer, artifact / "semantic-answer.json")
    write_json(artifact / "validator-result.json", validator)

    runtime = parse_agent_runtime(events)
    run_state = "completed"
    if launch_error or validation_error or (exit_code not in {0, None} and not timed_out):
        run_state = "infrastructure_failure"
    elif timed_out:
        run_state = "agent_timeout"
    token_usage = runtime.get("token_usage") or {}
    manifest = {
        "schema_version": SCHEMA_VERSION, "protocol_version": PROTOCOL_VERSION,
        "state": run_state, "condition": profile, "query_profile": profile,
        "task_id": task["id"], "category": task["category"], "semantic_heavy": task["semantic_heavy"],
        "trial_id": trial_name, "trial": trial, **prepared,
        "benchmark_commit": protocol["benchmark_commit"], "compiler_commit": protocol["compiler_commit"],
        "compiler_binary_sha256": protocol["compiler_binary_sha256"], "agent": protocol["agent"],
        "started_at_unix": round(started_at, 3), "ended_at_unix": round(started_at + wall_time_ms / 1000, 3),
        "wall_time_ms": wall_time_ms, "agent_exit_code": exit_code, "agent_timed_out": timed_out,
        "agent_runtime": runtime, "agent_tool_calls": runtime.get("tool_calls"),
        "input_tokens": token_usage.get("input_tokens"), "cached_input_tokens": token_usage.get("cached_input_tokens"),
        "output_tokens": token_usage.get("output_tokens"),
        "reasoning_tokens": token_usage.get("reasoning_tokens", token_usage.get("reasoning_output_tokens")),
        **tool_metrics, **{key: value for key, value in read_metrics.items() if key != "source_read_events"},
        "semantic_detour_cost": semantic_detour_cost,
        "final_validator_pass": validator["validator_pass"],
        "semantic_answer_schema_valid": validator["semantic_answer"].get("schema_valid", False),
        "semantic_facts_correct": validator["semantic_answer"].get("correct", 0),
        "semantic_facts_requested": validator["semantic_answer"].get("requested", len(task["requested_facts"])),
        "semantic_fact_accuracy": validator["semantic_answer"].get("accuracy", 0.0),
        "queries_per_correct_fact": (
            round(tool_metrics["semantic_query_invocations"] / validator["semantic_answer"].get("correct", 0), 6)
            if validator["semantic_answer"].get("correct", 0) else None
        ),
        "changed_paths": validator.get("changed_paths", []), "outside_allowed_paths": validator.get("outside_allowed_paths", []),
        "environment_changed_paths": environment_changes(stage, snapshot), "pruned_generated_paths": removed,
        "infrastructure_error": launch_error or validation_error or (
            f"agent exited with status {exit_code}" if exit_code not in {0, None} and not timed_out else None
        ),
    }
    if manifest["environment_changed_paths"]:
        manifest["state"] = "infrastructure_failure"
        manifest["infrastructure_error"] = "agent modified staged tools or documentation"
    write_json(artifact / "manifest.json", manifest)
    return manifest


def protocol_document(args: argparse.Namespace, tasks: list[dict[str, Any]]) -> dict[str, Any]:
    compiler_commit = git_output("rev-parse", args.compiler_commit)
    benchmark_commit = git_output("rev-parse", args.benchmark_commit)
    head = git_output("rev-parse", "HEAD")
    if compiler_commit != benchmark_commit or compiler_commit != head:
        raise BenchmarkError("P223_PROTOCOL_SHA_MISMATCH", "compiler, benchmark implementation, and checked-out HEAD must be the same commit")
    compiler_hash = sha256_bytes((ROOT / "moss").read_bytes())
    return {
        "schema_version": "moss-phase22.3-ab-protocol-1", "protocol_version": PROTOCOL_VERSION,
        "compiler_commit": compiler_commit, "benchmark_commit": benchmark_commit,
        "compiler_binary_sha256": compiler_hash,
        "language_version": "moss-0.1", "profiles": list(args.profiles),
        "profile_definitions": {
            "legacy": {"query_profile": "legacy", "docs_commit": LEGACY_COMMIT, "resolve": False, "query_projection": "post-22.1"},
            "phase22_3": {"query_profile": "phase22_3", "docs_commit": benchmark_commit, "resolve": True, "query_projection": "full-current"},
        },
        "profile_docs_sha256": {profile: profile_doc_hashes(profile) for profile in args.profiles},
        "task_count": len(tasks), "trials_per_task": args.trials,
        "total_agent_runs": len(tasks) * len(args.profiles) * args.trials,
        "task_ids": [task["id"] for task in tasks], "random_seed": args.seed,
        "agent": {
            "implementation": "OpenAI Codex CLI", "runtime_version": binary_version(["codex", "--version"]),
            "model": args.model, "reasoning_effort": args.reasoning,
            "configuration": "ephemeral; no resume; ignore user config/rules; workspace-write; no approvals",
            "disabled_features": ["apps", "plugins", "browser", "networked shell", "memories", "multi_agent"],
        },
        "limits": {"wall_time_seconds": args.timeout, "retry_failed_trial": False},
        "environment": {
            "host": platform.platform(), "python": platform.python_version(),
            "rustc_path": str(resolved_rustc()), "rustc_version": binary_version([str(resolved_rustc()), "--version", "--verbose"]),
            "network_policy": "Codex shell workspace sandbox network_access=false",
            "sandbox": "workspace-write in a fresh mount namespace with Git metadata replaced by empty tmpfs",
        },
        "integrity": {
            "same_compiler_binary_both_profiles": True, "expected_trees_visible": False,
            "git_history_visible": False, "fresh_context_per_trial": True,
            "prompt_identical_between_profiles": True, "validator_identical_between_profiles": True,
        },
    }


def print_plan(protocol: dict[str, Any]) -> None:
    print("Phase 22.3 A/B experiment plan")
    print(f"tasks: {protocol['task_count']}")
    print(f"profiles: {' '.join(protocol['profiles'])}")
    print(f"trials: {protocol['trials_per_task']}")
    print(f"total agent runs: {protocol['total_agent_runs']}")
    print(f"model: {protocol['agent']['model']}")
    print(f"reasoning: {protocol['agent']['reasoning_effort']}")
    print(f"timeout: {protocol['limits']['wall_time_seconds']}")


def run_all(args: argparse.Namespace) -> int:
    tasks = load_tasks()
    if args.tasks:
        selected = set(args.tasks)
        tasks = [task for task in tasks if task["id"] in selected]
        missing = selected - {task["id"] for task in tasks}
        if missing:
            raise BenchmarkError("P223_TASK_NOT_FOUND", f"unknown selected tasks: {sorted(missing)}")
    protocol = protocol_document(args, tasks)
    print_plan(protocol)
    output_root = args.output_root.resolve()
    output_root.mkdir(parents=True, exist_ok=True)
    protocol_path = output_root / "protocol.json"
    if protocol_path.is_file():
        existing = json.loads(protocol_path.read_text(encoding="utf-8"))
        invariant_keys = ("compiler_commit", "benchmark_commit", "compiler_binary_sha256", "agent", "limits", "profiles", "trials_per_task", "task_ids")
        if any(existing.get(key) != protocol.get(key) for key in invariant_keys):
            raise BenchmarkError("P223_PROTOCOL_MISMATCH", "existing output protocol differs; refusing mixed experiment")
    else:
        write_json(protocol_path, protocol)
    runtime_root = ROOT / "tmp" / "phase22_3_ab"
    runtime_root.mkdir(parents=True, exist_ok=True)
    work = [(task, profile, trial) for task in tasks for trial in range(1, args.trials + 1) for profile in args.profiles]
    random.Random(args.seed).shuffle(work)
    failures = []
    with ThreadPoolExecutor(max_workers=args.jobs) as pool:
        futures = {
            pool.submit(run_one, task, profile, trial, runtime_root, output_root, protocol): (task["id"], profile, trial)
            for task, profile, trial in work
        }
        for future in as_completed(futures):
            identity = futures[future]
            try:
                manifest = future.result()
                print(f"{identity[1]} {identity[0]} trial-{identity[2]:03d}: {manifest['state']} validator={manifest['final_validator_pass']} semantic={manifest['semantic_facts_correct']}/{manifest['semantic_facts_requested']}", flush=True)
                if manifest["state"] == "infrastructure_failure":
                    failures.append(identity)
            except Exception as error:  # preserve remaining independent trials
                print(f"{identity[1]} {identity[0]} trial-{identity[2]:03d}: orchestration error: {error}", file=sys.stderr, flush=True)
                failures.append(identity)
    if git_output("rev-parse", "HEAD") != protocol["compiler_commit"] or sha256_bytes((ROOT / "moss").read_bytes()) != protocol["compiler_binary_sha256"]:
        raise BenchmarkError("P223_COMPILER_CHANGED", "compiler commit or binary changed during experiment")
    return 1 if failures else 0


def main() -> int:
    parser = argparse.ArgumentParser()
    sub = parser.add_subparsers(dest="command", required=True)
    run = sub.add_parser("run-all")
    run.add_argument("--trials", type=int, default=5)
    run.add_argument("--profiles", nargs="+", choices=PROFILES, default=list(PROFILES))
    run.add_argument("--tasks", nargs="*")
    run.add_argument("--model", default=DEFAULT_MODEL)
    run.add_argument("--reasoning", default=DEFAULT_REASONING)
    run.add_argument("--timeout", type=int, default=DEFAULT_TIMEOUT)
    run.add_argument("--jobs", type=int, default=2)
    run.add_argument("--seed", type=int, default=DEFAULT_SEED)
    run.add_argument("--compiler-commit", default="HEAD")
    run.add_argument("--benchmark-commit", default="HEAD")
    run.add_argument("--output-root", type=Path, default=SUITE / "runs")
    plan = sub.add_parser("plan")
    for option in ():
        pass
    args = parser.parse_args()
    try:
        if args.command == "run-all":
            if args.trials < 1 or args.jobs < 1 or args.timeout < 1:
                raise BenchmarkError("P223_ARGUMENT_INVALID", "trials, jobs, and timeout must be positive")
            return run_all(args)
        tasks = load_tasks()
        print(json.dumps({"tasks": len(tasks), "profiles": list(PROFILES), "trials": 5, "total_agent_runs": len(tasks) * 10}, indent=2))
        return 0
    except BenchmarkError as error:
        print(f"error[{error.code}]: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
