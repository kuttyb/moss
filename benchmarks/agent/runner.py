#!/usr/bin/env python3
"""Declarative, isolated runner for the Moss fresh-agent benchmark corpus."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import re
import shutil
import subprocess
import sys
import tempfile
from typing import Any


SCHEMA_VERSION = "moss-agent-benchmark-1"
TASK_SCHEMA_VERSION = 1
CATEGORIES = {
    "basic-language",
    "domains-messages",
    "ownership-effects",
    "functional-dataflow",
    "synchronization",
    "modules-margo",
    "fast-debug",
    "tooling-navigation",
}
TASK_TYPES = {"write", "repair", "workflow"}
COMMANDS = {
    "$MOSS": {
        "check", "inspect", "type", "effects", "ownership", "calls",
        "why", "cost", "run", "debug", "build", "test", "fmt", "agent",
    },
    "$MARGO": {"build", "run", "test"},
}


class BenchmarkError(Exception):
    """A stable benchmark configuration or invocation failure."""

    def __init__(self, code: str, message: str):
        super().__init__(message)
        self.code = code


def envelope(command: str, ok: bool, *, result: Any = None,
             error: BenchmarkError | None = None) -> dict[str, Any]:
    document: dict[str, Any] = {
        "schema_version": SCHEMA_VERSION,
        "command": command,
        "ok": ok,
    }
    if ok:
        document["result"] = result
    else:
        assert error is not None
        document["error"] = {"code": error.code, "message": str(error)}
    return document


def relative_path(value: Any, field: str) -> str:
    if not isinstance(value, str) or not value:
        raise BenchmarkError("BENCHMARK_METADATA_INVALID", f"{field} must be a non-empty string")
    path = PurePosixPath(value)
    if path.is_absolute() or ".." in path.parts or "." in path.parts:
        raise BenchmarkError("BENCHMARK_METADATA_INVALID", f"{field} must be a normalized relative path: {value!r}")
    return value


def task_summary(task: dict[str, Any]) -> dict[str, Any]:
    return {key: task[key] for key in ("id", "name", "category", "task_type", "prompt")}


def validate_command(task_id: str, item: Any, index: int) -> None:
    prefix = f"{task_id} validation[{index}]"
    if not isinstance(item, dict):
        raise BenchmarkError("BENCHMARK_METADATA_INVALID", f"{prefix} must be an object")
    command = item.get("command")
    if not isinstance(command, list) or len(command) < 2 or not all(isinstance(value, str) and value for value in command):
        raise BenchmarkError("BENCHMARK_VALIDATION_INVALID", f"{prefix}.command must be a non-empty argv array")
    tool, operation = command[0], command[1]
    if tool not in COMMANDS or operation not in COMMANDS[tool]:
        raise BenchmarkError("BENCHMARK_VALIDATION_INVALID", f"{prefix} uses unsupported command: {' '.join(command[:2])}")
    for argument in command[2:]:
        path = PurePosixPath(argument)
        if path.is_absolute() or ".." in path.parts:
            raise BenchmarkError("BENCHMARK_VALIDATION_INVALID", f"{prefix} command escapes the isolated workspace: {argument!r}")
    if "cwd" in item:
        relative_path(item["cwd"], f"{prefix}.cwd")
    expect = item.get("expect", {})
    if not isinstance(expect, dict) or not set(expect) <= {
        "exit_code", "stdout", "stdout_contains", "stderr_contains",
        "json_equals", "json_matches",
    }:
        raise BenchmarkError("BENCHMARK_VALIDATION_INVALID", f"{prefix}.expect has unsupported fields")
    if not isinstance(expect.get("exit_code", 0), int):
        raise BenchmarkError("BENCHMARK_VALIDATION_INVALID", f"{prefix}.expect.exit_code must be an integer")
    if "json_equals" in expect and (not isinstance(expect["json_equals"], dict) or
                                     not all(isinstance(key, str) for key in expect["json_equals"])):
        raise BenchmarkError("BENCHMARK_VALIDATION_INVALID", f"{prefix}.expect.json_equals must be an object")
    if "json_matches" in expect and (not isinstance(expect["json_matches"], dict) or
                                      not all(isinstance(key, str) and isinstance(value, str)
                                              for key, value in expect["json_matches"].items())):
        raise BenchmarkError("BENCHMARK_VALIDATION_INVALID", f"{prefix}.expect.json_matches must map fields to strings")
    for dotted, pattern in expect.get("json_matches", {}).items():
        try:
            re.compile(pattern)
        except re.error as error:
            raise BenchmarkError(
                "BENCHMARK_VALIDATION_INVALID",
                f"{prefix}.expect.json_matches[{dotted!r}] is not a valid regular expression: {error}",
            ) from error


def validate_task(task: Any, directory: Path) -> dict[str, Any]:
    if not isinstance(task, dict):
        raise BenchmarkError("BENCHMARK_METADATA_INVALID", f"{directory}/task.json must contain an object")
    required = {
        "schema_version", "id", "name", "category", "task_type", "prompt",
        "starter", "expected", "starter_files", "allowed_paths",
        "success_criteria", "validation",
    }
    missing = sorted(required - set(task))
    if missing:
        raise BenchmarkError("BENCHMARK_METADATA_INVALID", f"{directory.name} is missing fields: {', '.join(missing)}")
    unknown = sorted(set(task) - required - {"assertions", "notes"})
    if unknown:
        raise BenchmarkError("BENCHMARK_METADATA_INVALID", f"{directory.name} has unknown fields: {', '.join(unknown)}")
    if task["schema_version"] != TASK_SCHEMA_VERSION:
        raise BenchmarkError("BENCHMARK_METADATA_INVALID", f"{directory.name} has unsupported task schema version")
    if not isinstance(task["id"], str) or not re.fullmatch(r"AB\d{3}", task["id"]):
        raise BenchmarkError("BENCHMARK_METADATA_INVALID", f"{directory.name} has invalid task id")
    if not directory.name.startswith(task["id"] + "_"):
        raise BenchmarkError("BENCHMARK_METADATA_INVALID", f"{directory.name} does not begin with {task['id']}_")
    if not isinstance(task["name"], str) or not re.fullmatch(r"[a-z0-9]+(?:-[a-z0-9]+)*", task["name"]):
        raise BenchmarkError("BENCHMARK_METADATA_INVALID", f"{task['id']} has invalid kebab-case name")
    if task["category"] not in CATEGORIES:
        raise BenchmarkError("BENCHMARK_METADATA_INVALID", f"{task['id']} has unknown category {task['category']!r}")
    if task["task_type"] not in TASK_TYPES:
        raise BenchmarkError("BENCHMARK_METADATA_INVALID", f"{task['id']} has unknown task_type {task['task_type']!r}")
    if not isinstance(task["prompt"], str) or not task["prompt"].strip():
        raise BenchmarkError("BENCHMARK_METADATA_INVALID", f"{task['id']} has an empty prompt")
    starter_name = relative_path(task["starter"], f"{task['id']}.starter")
    expected_name = relative_path(task["expected"], f"{task['id']}.expected")
    for label, name in (("starter", starter_name), ("expected", expected_name)):
        path = directory / name
        if not path.is_dir():
            raise BenchmarkError("BENCHMARK_STARTER_MISSING", f"{task['id']} {label} directory is missing: {path}")
        if any(candidate.is_symlink() for candidate in path.rglob("*")):
            raise BenchmarkError("BENCHMARK_METADATA_INVALID", f"{task['id']} {label} directory contains a symlink")
    declared_starter = task["starter_files"]
    if not isinstance(declared_starter, list) or not all(isinstance(value, str) for value in declared_starter):
        raise BenchmarkError("BENCHMARK_METADATA_INVALID", f"{task['id']}.starter_files must be an array")
    for index, value in enumerate(declared_starter):
        relative_path(value, f"{task['id']}.starter_files[{index}]")
    if len(set(declared_starter)) != len(declared_starter):
        raise BenchmarkError("BENCHMARK_METADATA_INVALID", f"{task['id']} has duplicate starter files")
    actual_starter = tree_files(directory / starter_name)
    if sorted(declared_starter) != actual_starter:
        missing = sorted(set(declared_starter) - set(actual_starter))
        unexpected = sorted(set(actual_starter) - set(declared_starter))
        detail = []
        if missing:
            detail.append("missing " + ", ".join(missing))
        if unexpected:
            detail.append("undeclared " + ", ".join(unexpected))
        raise BenchmarkError("BENCHMARK_STARTER_MISSING", f"{task['id']} starter file inventory differs: {'; '.join(detail)}")
    allowed = task["allowed_paths"]
    if not isinstance(allowed, list) or not allowed:
        raise BenchmarkError("BENCHMARK_METADATA_INVALID", f"{task['id']}.allowed_paths must be a non-empty array")
    for index, value in enumerate(allowed):
        value = relative_path(value, f"{task['id']}.allowed_paths[{index}]")
        if "*" in value and not value.endswith("/**"):
            raise BenchmarkError("BENCHMARK_METADATA_INVALID", f"{task['id']} only supports directory /** path patterns")
    if len(set(allowed)) != len(allowed):
        raise BenchmarkError("BENCHMARK_METADATA_INVALID", f"{task['id']} has duplicate allowed paths")
    criteria = task["success_criteria"]
    if not isinstance(criteria, list) or not criteria or not all(isinstance(value, str) and value for value in criteria):
        raise BenchmarkError("BENCHMARK_METADATA_INVALID", f"{task['id']}.success_criteria must be a non-empty string array")
    validations = task["validation"]
    if not isinstance(validations, list) or not validations:
        raise BenchmarkError("BENCHMARK_VALIDATION_INVALID", f"{task['id']}.validation must be non-empty")
    for index, item in enumerate(validations):
        validate_command(task["id"], item, index)
    assertions = task.get("assertions", [])
    if not isinstance(assertions, list):
        raise BenchmarkError("BENCHMARK_METADATA_INVALID", f"{task['id']}.assertions must be an array")
    for index, assertion in enumerate(assertions):
        if not isinstance(assertion, dict) or not isinstance(assertion.get("path"), str):
            raise BenchmarkError("BENCHMARK_METADATA_INVALID", f"{task['id']}.assertions[{index}] is invalid")
        relative_path(assertion["path"], f"{task['id']}.assertions[{index}].path")
        if set(assertion) - {
            "path", "equals", "contains", "not_contains", "matches",
            "not_matches",
        }:
            raise BenchmarkError("BENCHMARK_METADATA_INVALID", f"{task['id']}.assertions[{index}] has unsupported fields")
        assertion_fields = {
            "equals", "contains", "not_contains", "matches", "not_matches",
        }
        if len(set(assertion) & assertion_fields) == 0:
            raise BenchmarkError("BENCHMARK_METADATA_INVALID", f"{task['id']}.assertions[{index}] has no check")
        for field in set(assertion) & assertion_fields:
            if not isinstance(assertion[field], str):
                raise BenchmarkError("BENCHMARK_METADATA_INVALID", f"{task['id']}.assertions[{index}].{field} must be a string")
        for field in set(assertion) & {"matches", "not_matches"}:
            try:
                re.compile(assertion[field])
            except re.error as error:
                raise BenchmarkError(
                    "BENCHMARK_METADATA_INVALID",
                    f"{task['id']}.assertions[{index}].{field} is not a valid regular expression: {error}",
                ) from error
    if "notes" in task and not isinstance(task["notes"], str):
        raise BenchmarkError("BENCHMARK_METADATA_INVALID", f"{task['id']}.notes must be a string")
    task = dict(task)
    task["task_directory"] = directory.name
    return task


def load_suite(suite_root: Path) -> list[dict[str, Any]]:
    tasks_root = suite_root / "tasks"
    if not tasks_root.is_dir():
        raise BenchmarkError("BENCHMARK_SUITE_MISSING", f"benchmark tasks directory is missing: {tasks_root}")
    tasks: list[dict[str, Any]] = []
    ids: dict[str, Path] = {}
    names: dict[str, Path] = {}
    directories = sorted(path for path in tasks_root.iterdir() if path.is_dir())
    if not directories:
        raise BenchmarkError("BENCHMARK_SUITE_EMPTY", f"no benchmark tasks found in {tasks_root}")
    for directory in directories:
        metadata = directory / "task.json"
        if not metadata.is_file():
            raise BenchmarkError("BENCHMARK_METADATA_MISSING", f"benchmark task lacks task.json: {directory}")
        try:
            document = json.loads(metadata.read_text(encoding="utf-8"))
        except (OSError, json.JSONDecodeError) as error:
            raise BenchmarkError("BENCHMARK_METADATA_INVALID", f"cannot parse {metadata}: {error}") from error
        task = validate_task(document, directory)
        for field, seen in (("id", ids), ("name", names)):
            value = task[field]
            if value in seen:
                raise BenchmarkError("BENCHMARK_DUPLICATE_TASK", f"duplicate task {field} {value!r}: {seen[value].name} and {directory.name}")
            seen[value] = directory
        tasks.append(task)
    return sorted(tasks, key=lambda item: item["id"])


def tree_files(root: Path) -> list[str]:
    if not root.is_dir():
        return []
    return sorted(path.relative_to(root).as_posix() for path in root.rglob("*")
                  if path.is_file() and path.name != ".gitkeep")


def tree_state(root: Path) -> dict[str, str]:
    state: dict[str, str] = {}
    for relative in tree_files(root):
        path = root / relative
        state[relative] = hashlib.sha256(path.read_bytes()).hexdigest()
    return state


def changed_paths(starter: Path, workdir: Path) -> list[str]:
    before, after = tree_state(starter), tree_state(workdir)
    return sorted(path for path in set(before) | set(after) if before.get(path) != after.get(path))


def path_allowed(path: str, patterns: list[str]) -> bool:
    for pattern in patterns:
        if pattern.endswith("/**"):
            prefix = pattern[:-3].rstrip("/")
            if path == prefix or path.startswith(prefix + "/"):
                return True
        elif path == pattern:
            return True
    return False


def nested_value(document: Any, dotted: str) -> Any:
    value = document
    for component in dotted.split("."):
        if isinstance(value, list) and component.isdigit():
            value = value[int(component)]
        elif isinstance(value, dict) and component in value:
            value = value[component]
        else:
            raise KeyError(dotted)
    return value


def collect_diagnostics(stdout: str) -> list[dict[str, Any]]:
    try:
        document = json.loads(stdout)
    except json.JSONDecodeError:
        return []
    if not isinstance(document, dict):
        return []
    diagnostics: list[dict[str, Any]] = []
    if isinstance(document.get("error"), dict):
        diagnostics.append(document["error"])
    result = document.get("result")
    if isinstance(result, dict) and isinstance(result.get("diagnostics"), list):
        diagnostics.extend(item for item in result["diagnostics"] if isinstance(item, dict))
    return diagnostics


def execute_validation(task: dict[str, Any], workspace: Path, compiler: Path,
                       margo: Path) -> tuple[list[dict[str, Any]], list[dict[str, Any]]]:
    records: list[dict[str, Any]] = []
    diagnostics: list[dict[str, Any]] = []
    replacements = {"$MOSS": str(compiler), "$MARGO": str(margo)}
    for index, validation in enumerate(task["validation"]):
        declared = validation["command"]
        command = [replacements.get(value, value) for value in declared]
        cwd = (workspace / validation.get("cwd", ".")).resolve()
        if workspace.resolve() not in (cwd, *cwd.parents) or not cwd.is_dir():
            raise BenchmarkError("BENCHMARK_VALIDATION_INVALID", f"{task['id']} validation cwd is unavailable: {cwd}")
        try:
            process = subprocess.run(command, cwd=cwd, text=True, capture_output=True,
                                     check=False, timeout=120,
                                     env=dict(os.environ, MOSS=str(compiler),
                                              PYTHONDONTWRITEBYTECODE="1"))
            actual_exit = process.returncode
            stdout, stderr = process.stdout, process.stderr
        except (OSError, subprocess.TimeoutExpired) as error:
            actual_exit, stdout, stderr = 127, "", str(error)
        expect = validation.get("expect", {})
        failures: list[str] = []
        expected_exit = expect.get("exit_code", 0)
        if actual_exit != expected_exit:
            failures.append(f"exit code {actual_exit}, expected {expected_exit}")
        if "stdout" in expect and stdout != expect["stdout"]:
            failures.append("stdout did not match exactly")
        if "stdout_contains" in expect and expect["stdout_contains"] not in stdout:
            failures.append(f"stdout omitted {expect['stdout_contains']!r}")
        if "stderr_contains" in expect and expect["stderr_contains"] not in stderr:
            failures.append(f"stderr omitted {expect['stderr_contains']!r}")
        if "json_equals" in expect or "json_matches" in expect:
            try:
                document = json.loads(stdout)
                for dotted, expected in expect.get("json_equals", {}).items():
                    try:
                        actual = nested_value(document, dotted)
                    except (KeyError, IndexError):
                        failures.append(f"JSON field {dotted!r} was missing")
                    else:
                        if actual != expected:
                            failures.append(f"JSON field {dotted!r} was {actual!r}, expected {expected!r}")
                for dotted, pattern in expect.get("json_matches", {}).items():
                    try:
                        actual = nested_value(document, dotted)
                    except (KeyError, IndexError):
                        failures.append(f"JSON field {dotted!r} was missing")
                    else:
                        if not isinstance(actual, str) or not re.search(pattern, actual):
                            failures.append(f"JSON field {dotted!r} did not match /{pattern}/")
            except json.JSONDecodeError as error:
                failures.append(f"stdout was not JSON: {error}")
        diagnostics.extend(collect_diagnostics(stdout))
        records.append({
            "index": index,
            "command": declared,
            "cwd": validation.get("cwd", "."),
            "status": "pass" if not failures else "fail",
            "exit_code": actual_exit,
            "expected_exit_code": expected_exit,
            "stdout": stdout,
            "stderr": stderr,
            "failures": failures,
        })
    for index, assertion in enumerate(task.get("assertions", [])):
        path = workspace / assertion["path"]
        failures: list[str] = []
        try:
            contents = path.read_text(encoding="utf-8")
        except OSError as error:
            contents = ""
            failures.append(f"cannot read {assertion['path']}: {error}")
        if "equals" in assertion and contents != assertion["equals"]:
            failures.append("file contents did not match exactly")
        if "contains" in assertion and assertion["contains"] not in contents:
            failures.append(f"file omitted {assertion['contains']!r}")
        if "not_contains" in assertion and assertion["not_contains"] in contents:
            failures.append(f"file retained {assertion['not_contains']!r}")
        if "matches" in assertion and not re.search(
                assertion["matches"], contents, re.MULTILINE):
            failures.append(f"file did not match /{assertion['matches']}/")
        if "not_matches" in assertion and re.search(
                assertion["not_matches"], contents, re.MULTILINE):
            failures.append(f"file unexpectedly matched /{assertion['not_matches']}/")
        records.append({
            "index": len(task["validation"]) + index,
            "command": None,
            "assertion": assertion,
            "cwd": ".",
            "status": "pass" if not failures else "fail",
            "exit_code": None,
            "expected_exit_code": None,
            "stdout": "",
            "stderr": "",
            "failures": failures,
        })
    return records, diagnostics


def run_task(task: dict[str, Any], suite_root: Path, source: Path,
             compiler: Path, margo: Path, repo_root: Path) -> dict[str, Any]:
    task_root = suite_root / "tasks" / task["task_directory"]
    starter = task_root / task["starter"]
    if not source.is_dir():
        raise BenchmarkError("BENCHMARK_WORKDIR_INVALID", f"prepared work directory does not exist: {source}")
    if source.is_symlink() or any(candidate.is_symlink() for candidate in source.rglob("*")):
        raise BenchmarkError("BENCHMARK_WORKDIR_INVALID", f"prepared work directory contains a symlink: {source}")
    changed = changed_paths(starter, source)
    outside = [path for path in changed if not path_allowed(path, task["allowed_paths"])]
    scratch_parent = repo_root / "tmp" / "agent-benchmark"
    scratch_parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix=task["id"] + "-", dir=scratch_parent) as temporary:
        isolated = Path(temporary) / "work"
        shutil.copytree(source, isolated)
        validation, diagnostics = execute_validation(task, isolated, compiler, margo)
    passed = not outside and all(item["status"] == "pass" for item in validation)
    return {
        "task_id": task["id"],
        "status": "pass" if passed else "fail",
        "validation": validation,
        "attempts": None,
        "tool_calls": None,
        "diagnostics": diagnostics,
        "changed_paths": changed,
        "outside_allowed_paths": outside,
        "agent": None,
        "wall_time_ms": None,
    }


def find_task(tasks: list[dict[str, Any]], selector: str) -> dict[str, Any]:
    matches = [task for task in tasks if task["id"] == selector or task["name"] == selector]
    if not matches:
        raise BenchmarkError("BENCHMARK_TASK_NOT_FOUND", f"unknown benchmark task {selector!r}")
    return matches[0]


def print_document(document: dict[str, Any], json_mode: bool) -> None:
    if json_mode:
        print(json.dumps(document, sort_keys=True, separators=(",", ":")))
        return
    if not document["ok"]:
        print(f"moss: error[{document['error']['code']}]: {document['error']['message']}", file=sys.stderr)
        return
    result = document["result"]
    command = document["command"]
    if command == "agent benchmark list":
        for task in result["tasks"]:
            print(f"{task['id']}\t{task['category']}\t{task['task_type']}\t{task['name']}")
    elif command == "agent benchmark show":
        print(f"{result['id']} {result['name']} [{result['category']}/{result['task_type']}]")
        print(result["prompt"])
        print("Allowed paths: " + ", ".join(result["allowed_paths"]))
    elif command == "agent benchmark validate":
        print(f"validated {result['task_count']} benchmark tasks ({result['expected_passed']} expected solutions passed)")
    elif command == "agent benchmark run":
        print(f"{result['task_id']}: {result['status']}")


def add_common(parser: argparse.ArgumentParser) -> None:
    parser.add_argument("--json", action="store_true")
    parser.add_argument("--suite-root", type=Path, help=argparse.SUPPRESS)
    parser.add_argument("--repo-root", type=Path, help=argparse.SUPPRESS)
    parser.add_argument("--compiler", type=Path, help=argparse.SUPPRESS)


def parse_args(argv: list[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(prog="moss agent benchmark")
    subparsers = parser.add_subparsers(dest="operation", required=True)
    listing = subparsers.add_parser("list")
    add_common(listing)
    showing = subparsers.add_parser("show")
    showing.add_argument("task")
    add_common(showing)
    validating = subparsers.add_parser("validate")
    validating.add_argument("--metadata-only", action="store_true")
    add_common(validating)
    running = subparsers.add_parser("run")
    running.add_argument("task")
    running.add_argument("--workdir", type=Path)
    add_common(running)
    return parser.parse_args(argv)


def main(argv: list[str] | None = None) -> int:
    args = parse_args(sys.argv[1:] if argv is None else argv)
    default_suite = Path(__file__).resolve().parent
    suite_root = (args.suite_root or default_suite).resolve()
    repo_root = (args.repo_root or suite_root.parents[1]).resolve()
    compiler = (args.compiler or repo_root / "moss").resolve()
    margo = (repo_root / "margo").resolve()
    command = f"agent benchmark {args.operation}"
    try:
        tasks = load_suite(suite_root)
        if args.operation == "list":
            result: Any = {"task_count": len(tasks), "tasks": [task_summary(task) for task in tasks]}
        elif args.operation == "show":
            result = find_task(tasks, args.task)
        elif args.operation == "validate":
            results: list[dict[str, Any]] = []
            if not args.metadata_only:
                for task in tasks:
                    task_root = suite_root / "tasks" / task["task_directory"]
                    results.append(run_task(task, suite_root, task_root / task["expected"], compiler, margo, repo_root))
                failed = [item["task_id"] for item in results if item["status"] != "pass"]
                if failed:
                    raise BenchmarkError("BENCHMARK_EXPECTED_VALIDATION_FAILED", "expected solutions failed: " + ", ".join(failed))
            result = {
                "task_count": len(tasks),
                "metadata_valid": True,
                "expected_checked": not args.metadata_only,
                "expected_passed": len(results),
                "categories": {category: sum(task["category"] == category for task in tasks)
                               for category in sorted(CATEGORIES)},
                "task_types": {kind: sum(task["task_type"] == kind for task in tasks)
                               for kind in sorted(TASK_TYPES)},
            }
        else:
            task = find_task(tasks, args.task)
            task_root = suite_root / "tasks" / task["task_directory"]
            source = (args.workdir.resolve() if args.workdir else task_root / task["starter"])
            result = run_task(task, suite_root, source, compiler, margo, repo_root)
        document = envelope(command, True, result=result)
    except BenchmarkError as error:
        document = envelope(command, False, error=error)
    print_document(document, args.json)
    if not document["ok"]:
        return 2
    if args.operation == "run" and document["result"]["status"] != "pass":
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
