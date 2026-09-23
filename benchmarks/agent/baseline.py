#!/usr/bin/env python3
"""Phase 22.2C sanitized fresh-agent baseline orchestration."""

from __future__ import annotations

import argparse
from collections import Counter
from concurrent.futures import ThreadPoolExecutor, as_completed
import hashlib
import json
import os
from pathlib import Path
import platform
import shutil
import signal
import statistics
import subprocess
import sys
import time
from typing import Any

from runner import find_task, load_suite


ROOT = Path(__file__).resolve().parents[2]
SUITE = ROOT / "benchmarks" / "agent"
TASKS = SUITE / "tasks"
WRAPPER = SUITE / "prompt-wrapper.txt"
LOGGER = SUITE / "tool_logger.py"
BASELINE_SCHEMA = "moss-agent-baseline-1"
PROTOCOL_VERSION = "phase-22.2c-v1"
BASELINE_ID = "pre-22.1"
DEFAULT_MODEL = "gpt-6-sol"
DEFAULT_REASONING = "medium"
DEFAULT_TIMEOUT_SECONDS = 900


class BaselineError(Exception):
    pass


def write_json(path: Path, value: Any) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(value, indent=2, sort_keys=True) + "\n", encoding="utf-8")


def sha256_bytes(value: bytes) -> str:
    return hashlib.sha256(value).hexdigest()


def tree_digest(root: Path) -> str:
    digest = hashlib.sha256()
    if not root.is_dir():
        return digest.hexdigest()
    for path in sorted(candidate for candidate in root.rglob("*") if candidate.is_file()):
        relative = path.relative_to(root).as_posix()
        digest.update(relative.encode())
        digest.update(b"\0")
        digest.update(path.read_bytes())
        digest.update(b"\0")
    return digest.hexdigest()


def git_output(*arguments: str) -> str:
    process = subprocess.run(
        ["git", *arguments], cwd=ROOT, text=True, capture_output=True, check=False,
    )
    if process.returncode != 0:
        raise BaselineError(process.stderr.strip() or "git command failed")
    return process.stdout.strip()


def resolved_rustc() -> Path:
    process = subprocess.run(
        ["rustup", "which", "rustc"], text=True, capture_output=True, check=False,
    )
    if process.returncode != 0:
        raise BaselineError(process.stderr.strip() or "rustup could not resolve rustc")
    path = Path(process.stdout.strip()).resolve()
    if not path.is_file():
        raise BaselineError(f"resolved Rust compiler is unavailable: {path}")
    return path


def task_prompt(task: dict[str, Any]) -> str:
    wrapper = WRAPPER.read_text(encoding="utf-8").rstrip("\n")
    return f"{wrapper}\n\nTask ID: {task['id']}\n\nTask prompt (verbatim):\n{task['prompt']}\n"


def copy_environment(stage: Path) -> None:
    shutil.copytree(ROOT / "docs", stage / "docs")
    shutil.copytree(ROOT / ".agents", stage / ".agents")
    shutil.copy2(ROOT / "AGENTS.md", stage / "AGENTS.md")
    shutil.copy2(ROOT / "README.md", stage / "README.md")
    tools = stage / ".baseline-tools"
    tools.mkdir()
    shutil.copy2(ROOT / "moss", tools / "moss-real")
    shutil.copy2(ROOT / "margo", tools / "margo-real")
    for name in ("moss", "margo"):
        shutil.copy2(LOGGER, stage / name)
        (stage / name).chmod(0o755)
    (tools / "moss-real").chmod(0o755)
    (tools / "margo-real").chmod(0o755)


def prepare_task(task: dict[str, Any], destination: Path) -> dict[str, Any]:
    if destination.exists():
        shutil.rmtree(destination)
    destination.mkdir(parents=True)
    task_root = TASKS / task["task_directory"]
    shutil.copytree(task_root / task["starter"], destination / "task")
    copy_environment(destination)
    (destination / "logs").mkdir()
    (destination / ".home").mkdir()
    (destination / ".auth").mkdir()
    (destination / ".git").mkdir()
    prompt = task_prompt(task)
    (destination / "prompt.txt").write_text(prompt, encoding="utf-8")
    manifest = {
        "schema_version": BASELINE_SCHEMA,
        "task_id": task["id"],
        "trial": 1,
        "prompt_wrapper_sha256": sha256_bytes(WRAPPER.read_bytes()),
        "task_prompt_sha256": sha256_bytes(task["prompt"].encode()),
        "starter_tree_sha256": tree_digest(task_root / task["starter"]),
        "staged_tree_sha256": tree_digest(destination / "task"),
        "state": "prepared",
    }
    write_json(destination / "run-manifest.json", manifest)
    return manifest


def copy_auth(destination: Path) -> None:
    source = Path(os.environ.get("CODEX_HOME", Path.home() / ".codex")) / "auth.json"
    if not source.is_file():
        raise BaselineError(f"Codex credential file is unavailable: {source}")
    target = destination / ".auth" / "auth.json"
    shutil.copy2(source, target)
    target.chmod(0o600)


def parse_jsonl(path: Path) -> list[dict[str, Any]]:
    records: list[dict[str, Any]] = []
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


def parse_agent_runtime(events: list[dict[str, Any]]) -> dict[str, Any]:
    thread_id: str | None = None
    tool_calls = 0
    item_types: Counter[str] = Counter()
    usage: dict[str, Any] | None = None
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
    return {
        "thread_id": thread_id,
        "tool_calls": tool_calls if events else None,
        "completed_item_types": dict(sorted(item_types.items())),
        "token_usage": usage,
    }


def validation_kind(record: dict[str, Any]) -> str | None:
    if record.get("origin") != "agent":
        return None
    argv = record.get("argv")
    if not isinstance(argv, list) or not argv:
        return None
    tool = record.get("tool")
    operation = argv[0]
    if tool == "margo" and operation in {"build", "test", "run"}:
        return f"margo-{operation}"
    if tool == "moss":
        if operation in {"check", "build", "test", "debug"}:
            return f"moss-{operation}"
        if operation == "run" and "--interp" in argv:
            return "moss-interpreter"
        if operation == "--check":
            return "moss-check"
    return None


def tool_capability(record: dict[str, Any]) -> str | None:
    if record.get("origin") != "agent":
        return None
    argv = record.get("argv")
    if not isinstance(argv, list) or not argv:
        return None
    tool = record.get("tool")
    operation = argv[0]
    if tool == "margo":
        return f"margo-{operation}"
    if operation == "agent" and len(argv) > 1 and argv[1] == "bootstrap":
        return "bootstrap"
    if operation in {"inspect", "type", "effects", "ownership", "calls", "why", "cost"}:
        return operation
    if operation == "run" and "--interp" in argv:
        return "trace" if "--trace" in argv else "interpreter"
    if operation == "debug":
        return "trace" if "--trace" in argv else "debug"
    if operation in {"check", "build", "test"} or operation == "--check":
        return "check" if operation == "--check" else operation
    return f"moss-{operation.lstrip('-')}"


def session_metrics(records: list[dict[str, Any]]) -> dict[str, Any]:
    records = sorted(records, key=lambda record: record.get("sequence", 0))
    direct = [record for record in records if record.get("origin") == "agent"]
    validation = [record for record in direct if validation_kind(record) is not None]
    first_success = None if not validation else validation[0].get("exit_code") == 0
    attempts_to_green: int | None = None
    for index, record in enumerate(validation, 1):
        if record.get("exit_code") == 0:
            attempts_to_green = index
            break
    capabilities = Counter(
        capability for record in direct
        if (capability := tool_capability(record)) is not None
    )
    diagnostic_events: list[dict[str, Any]] = []
    diagnostic_counts: Counter[str] = Counter()
    for record in records:
        for code in record.get("diagnostic_codes", []):
            diagnostic_counts[code] += 1
            diagnostic_events.append({
                "code": code,
                "sequence": record.get("sequence"),
                "tool": record.get("tool"),
                "argv": record.get("argv"),
            })
    return {
        "first_validation_success": first_success,
        "validation_attempts_to_green": attempts_to_green,
        "validation_attempts_total": len(validation),
        "moss_margo_invocations": len(direct),
        "tool_usage": dict(sorted(capabilities.items())),
        "diagnostic_codes": dict(sorted(diagnostic_counts.items())),
        "diagnostic_events": diagnostic_events,
    }


def namespace_command(stage: Path, model: str, reasoning: str) -> list[str]:
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
export RUSTC="$6"
export MOSS_BASELINE_WORKSPACE="$repo"
export MOSS_BASELINE_TOOL_LOG="$repo/logs/moss-tool-log.jsonl"
export MOSS_BASELINE_REAL_MOSS="$repo/.baseline-tools/moss-real"
export MOSS_BASELINE_REAL_MARGO="$repo/.baseline-tools/margo-real"
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
        "unshare", "--user", "--map-root-user", "--mount",
        "--propagation", "private", "--fork", "/bin/bash", "-c", shell,
        "baseline", str(stage.resolve()), str(ROOT), str(auth), model, reasoning,
        str(resolved_rustc()),
    ]


def prune_generated(task_root: Path, starter_root: Path) -> list[str]:
    removed: list[str] = []
    starter_files = {
        path.relative_to(starter_root).as_posix()
        for path in starter_root.rglob("*") if path.is_file()
    }
    for directory in sorted(
            (path for path in task_root.rglob("*")
             if path.is_dir() and path.name in {"build", "target", "__pycache__", ".margo", ".moss"}),
            reverse=True):
        removed.append(directory.relative_to(task_root).as_posix() + "/")
        shutil.rmtree(directory, ignore_errors=True)
    for path in sorted(candidate for candidate in task_root.rglob("*") if candidate.is_file()):
        relative = path.relative_to(task_root).as_posix()
        generated = path.name == "Moss.lock" or path.suffix in {".rs", ".mossi", ".mossmap"}
        if generated and relative not in starter_files:
            removed.append(relative)
            path.unlink()
    return sorted(removed)


def authoritative_validation(task_id: str, workdir: Path) -> tuple[int, dict[str, Any]]:
    process = subprocess.run(
        [str(ROOT / "moss"), "agent", "benchmark", "run", task_id,
         "--workdir", str(workdir), "--json"],
        cwd=ROOT, text=True, capture_output=True, check=False, timeout=180,
    )
    try:
        document = json.loads(process.stdout)
    except json.JSONDecodeError as error:
        raise BaselineError(f"authoritative validator returned invalid JSON: {error}") from error
    if process.returncode not in {0, 1} or not document.get("ok"):
        raise BaselineError(f"authoritative validator failed: {process.stderr or process.stdout}")
    return process.returncode, document


def classify_outcome(*, launch_error: str | None, validation_error: str | None,
                     exit_code: int | None, timed_out: bool,
                     validator_status: str | None) -> tuple[str, str | None]:
    infrastructure_failure = launch_error is not None or validation_error is not None
    if exit_code not in {0, None} and not timed_out:
        infrastructure_failure = True
    if infrastructure_failure:
        return "infrastructure_failure", None
    if timed_out:
        return "agent_timeout", validator_status
    return "completed", validator_status


def environment_changes(stage: Path, prepared: dict[str, str]) -> list[str]:
    ignored = {"run-manifest.json", "prompt.txt"}
    current: dict[str, str] = {}
    for path in stage.rglob("*"):
        if not path.is_file():
            continue
        relative = path.relative_to(stage).as_posix()
        if relative.startswith(("task/", "logs/", ".home/", ".auth/")) or relative in ignored:
            continue
        current[relative] = sha256_bytes(path.read_bytes())
    return sorted(path for path in set(prepared) | set(current) if prepared.get(path) != current.get(path))


def environment_snapshot(stage: Path) -> dict[str, str]:
    snapshot: dict[str, str] = {}
    for path in stage.rglob("*"):
        if not path.is_file():
            continue
        relative = path.relative_to(stage).as_posix()
        if relative.startswith(("task/", "logs/", ".home/", ".auth/")):
            continue
        if relative not in {"run-manifest.json", "prompt.txt"}:
            snapshot[relative] = sha256_bytes(path.read_bytes())
    return snapshot


def run_one(task: dict[str, Any], runtime_root: Path, output_root: Path,
            protocol: dict[str, Any]) -> dict[str, Any]:
    task_id = task["id"]
    stage = runtime_root / task_id
    artifact = output_root / task_id
    if artifact.exists():
        raise BaselineError(f"canonical artifact already exists: {artifact}")
    prepared = prepare_task(task, stage)
    snapshot = environment_snapshot(stage)
    copy_auth(stage)
    events_path = stage / "logs" / "agent-events.jsonl"
    stderr_path = stage / "logs" / "agent-stderr.txt"
    started_wall = time.time()
    started = time.monotonic()
    timed_out = False
    launch_error: str | None = None
    exit_code: int | None = None
    try:
        with events_path.open("w", encoding="utf-8") as events, \
                stderr_path.open("w", encoding="utf-8") as errors:
            process = subprocess.Popen(
                namespace_command(stage, protocol["agent"]["model"],
                                  protocol["agent"]["reasoning_effort"]),
                cwd=ROOT, text=True, stdin=subprocess.PIPE, stdout=events, stderr=errors,
                start_new_session=True,
            )
            try:
                process.communicate(
                    input=task_prompt(task), timeout=protocol["limits"]["wall_time_seconds"],
                )
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

    starter = TASKS / task["task_directory"] / task["starter"]
    pruned = prune_generated(stage / "task", starter)
    validation_error: str | None = None
    result_document: dict[str, Any] | None = None
    try:
        _, result_document = authoritative_validation(task_id, stage / "task")
    except BaselineError as error:
        validation_error = str(error)

    records = parse_jsonl(stage / "logs" / "moss-tool-log.jsonl")
    metrics = session_metrics(records)
    runtime = parse_agent_runtime(parse_jsonl(events_path))
    validator_status = result_document["result"]["status"] if result_document else None
    run_state, final_status = classify_outcome(
        launch_error=launch_error, validation_error=validation_error,
        exit_code=exit_code, timed_out=timed_out, validator_status=validator_status,
    )

    artifact.mkdir(parents=True)
    shutil.copytree(stage / "task", artifact / "final")
    shutil.copy2(stage / "prompt.txt", artifact / "prompt.txt")
    tool_log = stage / "logs" / "moss-tool-log.jsonl"
    if tool_log.is_file():
        shutil.copy2(tool_log, artifact / "moss-tool-log.jsonl")
    else:
        (artifact / "moss-tool-log.jsonl").write_text("", encoding="utf-8")
    last_message = stage / "logs" / "last-message.txt"
    if last_message.is_file():
        shutil.copy2(last_message, artifact / "last-message.txt")
    if stderr_path.is_file() and stderr_path.stat().st_size:
        shutil.copy2(stderr_path, artifact / "agent-stderr.txt")
    if result_document is not None:
        write_json(artifact / "result.json", result_document)

    result = result_document.get("result", {}) if result_document else {}
    manifest = {
        **prepared,
        "state": run_state,
        "corpus_commit": protocol["corpus_commit"],
        "compiler_commit": protocol["compiler_commit"],
        "orchestration_commit": protocol["orchestration_commit"],
        "agent": protocol["agent"],
        "started_at_unix": round(started_wall, 3),
        "wall_time_ms": wall_time_ms,
        "agent_exit_code": exit_code,
        "agent_timed_out": timed_out,
        "agent_runtime": runtime,
        "final_status": final_status,
        **metrics,
        "changed_paths": result.get("changed_paths", []),
        "outside_allowed_paths": result.get("outside_allowed_paths", []),
        "environment_changed_paths": environment_changes(stage, snapshot),
        "pruned_generated_paths": pruned,
        "infrastructure_error": launch_error or validation_error,
    }
    write_json(artifact / "manifest.json", manifest)
    return manifest


def protocol_document(args: argparse.Namespace, codex_version: str) -> dict[str, Any]:
    return {
        "schema_version": BASELINE_SCHEMA,
        "protocol_version": PROTOCOL_VERSION,
        "baseline_id": args.output_root.name,
        "corpus_commit": args.corpus_commit,
        "compiler_commit": args.compiler_commit,
        "orchestration_commit": git_output("rev-parse", "HEAD"),
        "prompt_wrapper_sha256": sha256_bytes(WRAPPER.read_bytes()),
        "task_count": 30,
        "trials_per_task": 1,
        "fresh_context": "one ephemeral Codex exec session per task; no resume or retry",
        "isolation": {
            "expected_trees_visible": False,
            "git_history_visible": False,
            "staging": "Git-free starter-only task plus whitelisted local tools and docs",
            "mount_namespace": "sanitized tree bind-mounted over source repository",
            "external_web_lookup": False,
            "shell_network_access": False,
        },
        "agent": {
            "implementation": "OpenAI Codex CLI",
            "runtime_version": codex_version,
            "model": args.model,
            "reasoning_effort": args.reasoning,
            "configuration": "ephemeral, ignore user config/rules, no approvals, workspace-write",
            "disabled_features": [
                "apps", "plugins", "browser_use", "browser_use_external",
                "browser_use_full_cdp_access", "in_app_browser", "computer_use",
                "image_generation", "recommended_plugins", "memories", "multi_agent",
            ],
        },
        "limits": {
            "wall_time_seconds": args.timeout,
            "agent_interaction_budget": None,
            "retry_failed_task": False,
            "canonical_trials": 1,
        },
        "metrics": {
            "meaningful_validation": {
                "moss": ["check", "--check", "build", "test", "debug", "run --interp"],
                "margo": ["build", "test", "run"],
                "excluded": ["bootstrap", "inspect", "type", "effects", "ownership", "calls", "why", "cost"],
            },
            "first_validation_success": "exit status of the first direct meaningful validation command",
            "validation_attempts_to_green": "1-based direct meaningful validation attempt ending at first exit 0; null if none",
            "final_pass": "authoritative moss agent benchmark run outside the agent namespace",
        },
        "execution": {
            "jobs": args.jobs,
            "host": platform.platform(),
            "python": platform.python_version(),
            "resolved_rustc": str(resolved_rustc()),
        },
    }


def summarize(output_root: Path) -> dict[str, Any]:
    manifests = [
        json.loads((output_root / f"AB{index:03d}" / "manifest.json").read_text(encoding="utf-8"))
        for index in range(1, 31)
    ]
    green = [item["validation_attempts_to_green"] for item in manifests
             if item["validation_attempts_to_green"] is not None]
    diagnostics: Counter[str] = Counter()
    tools: Counter[str] = Counter()
    for item in manifests:
        diagnostics.update(item["diagnostic_codes"])
        tools.update(item["tool_usage"])
    agent_tool_calls = [item["agent_runtime"].get("tool_calls") for item in manifests]
    return {
        "schema_version": BASELINE_SCHEMA,
        "baseline_id": output_root.name,
        "tasks_attempted": len(manifests),
        "final_passes": sum(item["final_status"] == "pass" for item in manifests),
        "final_failures": sum(item["final_status"] == "fail" for item in manifests),
        "first_validation_successes": sum(item["first_validation_success"] is True for item in manifests),
        "tasks_reaching_green": len(green),
        "attempts_to_green_mean": round(statistics.mean(green), 3) if green else None,
        "attempts_to_green_median": statistics.median(green) if green else None,
        "tasks_with_outside_allowed_paths": sum(bool(item["outside_allowed_paths"]) for item in manifests),
        "agent_tool_calls": (sum(agent_tool_calls)
                             if all(value is not None for value in agent_tool_calls) else None),
        "moss_margo_invocations": sum(item["moss_margo_invocations"] for item in manifests),
        "infrastructure_failures": [item["task_id"] for item in manifests if item["state"] == "infrastructure_failure"],
        "timed_out_tasks": [item["task_id"] for item in manifests if item["state"] == "agent_timeout"],
        "tool_usage": dict(sorted(tools.items())),
        "diagnostic_codes": dict(sorted(diagnostics.items())),
    }


def sanity(task: dict[str, Any], runtime_root: Path) -> dict[str, Any]:
    stage = runtime_root / "sanity"
    prepared = prepare_task(task, stage)
    repo = str(ROOT)
    expected = str(TASKS / task["task_directory"] / task["expected"])
    shell = r'''set -eu
mount --bind "$1" "$2"
cd "$2"
test -d .git
test -z "$(find .git -mindepth 1 -print -quit)"
if git rev-parse --is-inside-work-tree >/dev/null 2>&1; then exit 31; fi
if git show HEAD >/dev/null 2>&1; then exit 32; fi
test ! -e "$3"
test ! -e benchmarks/agent/tasks
./moss agent bootstrap --json >/dev/null
cd task
margo build --json >/dev/null
margo test --json >/dev/null
'''
    environment = dict(os.environ)
    environment.update({
        "PATH": f"{ROOT}:{environment['PATH']}",
        "HOME": str(ROOT / ".home"),
        "MOSS": str(ROOT / "moss"),
        "MARGO_HOME": str(ROOT / ".home" / ".margo"),
        "RUSTC": str(resolved_rustc()),
        "MOSS_BASELINE_WORKSPACE": str(ROOT),
        "MOSS_BASELINE_TOOL_LOG": str(ROOT / "logs" / "moss-tool-log.jsonl"),
        "MOSS_BASELINE_REAL_MOSS": str(ROOT / ".baseline-tools" / "moss-real"),
        "MOSS_BASELINE_REAL_MARGO": str(ROOT / ".baseline-tools" / "margo-real"),
    })
    process = subprocess.run(
        ["unshare", "--user", "--map-root-user", "--mount", "--propagation",
         "private", "--fork", "/bin/bash", "-c", shell, "sanity",
         str(stage.resolve()), repo, expected],
        cwd=ROOT, env=environment, text=True, capture_output=True, check=False,
    )
    if process.returncode != 0:
        raise BaselineError(f"sanity namespace failed: {process.stderr}")
    starter = TASKS / task["task_directory"] / task["starter"]
    prune_generated(stage / "task", starter)
    _, validation = authoritative_validation(task["id"], stage / "task")
    records = parse_jsonl(stage / "logs" / "moss-tool-log.jsonl")
    if not records or records[-1].get("exit_code") != 0:
        raise BaselineError("sanity tool logging did not capture successful Moss use")
    result = {
        **prepared,
        "expected_absent": True,
        "git_history_inaccessible": True,
        "tools_and_docs_present": (stage / "docs").is_dir() and (stage / "moss").is_file(),
        "tool_logging": True,
        "external_validation_status": validation["result"]["status"],
    }
    write_json(runtime_root / "sanity.json", result)
    return result


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(prog="agent-baseline")
    subparsers = parser.add_subparsers(dest="operation", required=True)
    prepare = subparsers.add_parser("prepare")
    prepare.add_argument("task")
    prepare.add_argument("--output", type=Path, required=True)
    checking = subparsers.add_parser("sanity")
    checking.add_argument("--runtime-root", type=Path, default=ROOT / "tmp" / "agent-baseline")
    running = subparsers.add_parser("run-all")
    running.add_argument("--runtime-root", type=Path, default=ROOT / "tmp" / "agent-baseline")
    running.add_argument("--output-root", type=Path, default=SUITE / "baselines" / BASELINE_ID)
    running.add_argument("--model", default=DEFAULT_MODEL)
    running.add_argument("--reasoning", default=DEFAULT_REASONING)
    running.add_argument("--timeout", type=int, default=DEFAULT_TIMEOUT_SECONDS)
    running.add_argument("--jobs", type=int, default=3)
    running.add_argument("--corpus-commit", required=True)
    running.add_argument("--compiler-commit", required=True)
    summary = subparsers.add_parser("summarize")
    summary.add_argument("--output-root", type=Path, default=SUITE / "baselines" / BASELINE_ID)
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    tasks = load_suite(SUITE)
    if args.operation == "prepare":
        print(json.dumps(prepare_task(find_task(tasks, args.task), args.output.resolve()), sort_keys=True))
        return 0
    if args.operation == "sanity":
        task = find_task(tasks, "AB029")
        print(json.dumps(sanity(task, args.runtime_root.resolve()), sort_keys=True))
        return 0
    if args.operation == "summarize":
        print(json.dumps(summarize(args.output_root.resolve()), sort_keys=True))
        return 0

    output_root = args.output_root.resolve()
    if output_root.exists() and any(output_root.iterdir()):
        raise BaselineError(f"baseline output is not empty: {output_root}")
    output_root.mkdir(parents=True, exist_ok=True)
    version = subprocess.run(["codex", "--version"], text=True, capture_output=True, check=True).stdout.strip()
    protocol = protocol_document(args, version)
    shutil.copy2(WRAPPER, output_root / "prompt-wrapper.txt")
    write_json(output_root / "protocol.json", protocol)
    runtime_root = args.runtime_root.resolve()
    runtime_root.mkdir(parents=True, exist_ok=True)
    manifests: list[dict[str, Any]] = []
    with ThreadPoolExecutor(max_workers=args.jobs) as executor:
        futures = {
            executor.submit(run_one, task, runtime_root, output_root, protocol): task["id"]
            for task in tasks
        }
        for future in as_completed(futures):
            task_id = futures[future]
            manifest = future.result()
            manifests.append(manifest)
            print(f"{task_id}: {manifest['state']} / {manifest['final_status']}", flush=True)
    result = summarize(output_root)
    write_json(output_root / "summary.json", result)
    print(json.dumps(result, sort_keys=True))
    return 2 if result["infrastructure_failures"] else 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except BaselineError as error:
        print(f"agent-baseline: {error}", file=sys.stderr)
        raise SystemExit(2)
