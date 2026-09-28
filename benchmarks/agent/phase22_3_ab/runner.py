#!/usr/bin/env python3
"""Corpus validation, task execution, and semantic-answer scoring."""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path, PurePosixPath
import re
import shutil
import subprocess
import sys
from typing import Any


ROOT = Path(__file__).resolve().parents[3]
SUITE = Path(__file__).resolve().parent
TASKS = SUITE / "tasks"
CATEGORIES = {
    "resolution", "type-ownership", "effects-calls",
    "domain-synchronization", "control",
}
TOOLS = {"$MOSS": "moss", "$MARGO": "margo"}


class BenchmarkError(RuntimeError):
    def __init__(self, code: str, message: str):
        super().__init__(message)
        self.code = code


def sha256_bytes(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def canonical_json(value: Any) -> bytes:
    return (json.dumps(value, sort_keys=True, separators=(",", ":")) + "\n").encode()


def tree_digest(root: Path) -> str:
    digest = hashlib.sha256()
    if not root.is_dir():
        return digest.hexdigest()
    for path in sorted(item for item in root.rglob("*") if item.is_file()):
        digest.update(path.relative_to(root).as_posix().encode())
        digest.update(b"\0")
        digest.update(path.read_bytes())
        digest.update(b"\0")
    return digest.hexdigest()


def safe_relative(value: Any, field: str) -> str:
    if not isinstance(value, str) or not value:
        raise BenchmarkError("P223_METADATA_INVALID", f"{field} must be a non-empty string")
    path = PurePosixPath(value)
    if path.is_absolute() or "." in path.parts or ".." in path.parts:
        raise BenchmarkError("P223_METADATA_INVALID", f"{field} is not a safe normalized relative path")
    return value


def file_inventory(root: Path) -> list[str]:
    return sorted(path.relative_to(root).as_posix() for path in root.rglob("*") if path.is_file())


def read_json(path: Path) -> Any:
    try:
        return json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        raise BenchmarkError("P223_JSON_INVALID", f"cannot read {path}: {error}") from error


def validate_task(document: Any, directory: Path) -> dict[str, Any]:
    if not isinstance(document, dict):
        raise BenchmarkError("P223_METADATA_INVALID", f"{directory}/task.json must be an object")
    required = {
        "schema_version", "id", "name", "category", "prompt", "starter",
        "expected", "starter_files", "allowed_paths", "validation",
        "semantic_heavy", "relevant_capability", "requested_facts",
    }
    optional = {"notes"}
    if required - set(document):
        raise BenchmarkError("P223_METADATA_INVALID", f"{directory.name} missing {sorted(required - set(document))}")
    if set(document) - required - optional:
        raise BenchmarkError("P223_METADATA_INVALID", f"{directory.name} has unknown fields {sorted(set(document) - required - optional)}")
    task_id = document["id"]
    if document["schema_version"] != 1 or not isinstance(task_id, str) or not re.fullmatch(r"P223\d{3}", task_id):
        raise BenchmarkError("P223_METADATA_INVALID", f"{directory.name} has invalid schema or task ID")
    if not directory.name.startswith(task_id + "_"):
        raise BenchmarkError("P223_METADATA_INVALID", f"{directory.name} does not begin with {task_id}_")
    if document["category"] not in CATEGORIES:
        raise BenchmarkError("P223_METADATA_INVALID", f"{task_id} has unknown category")
    if not isinstance(document["semantic_heavy"], bool):
        raise BenchmarkError("P223_METADATA_INVALID", f"{task_id}.semantic_heavy must be Boolean")
    if document["semantic_heavy"] == (document["category"] == "control"):
        raise BenchmarkError("P223_METADATA_INVALID", f"{task_id} category/semantic_heavy mismatch")
    if not isinstance(document["prompt"], str) or not document["prompt"].strip():
        raise BenchmarkError("P223_METADATA_INVALID", f"{task_id} has an empty prompt")
    if document["starter"] != "starter" or document["expected"] != "expected":
        raise BenchmarkError("P223_METADATA_INVALID", f"{task_id} must use starter/ and expected/")
    for name in ("starter", "expected"):
        root = directory / name
        if not root.is_dir() or any(path.is_symlink() for path in root.rglob("*")):
            raise BenchmarkError("P223_TREE_INVALID", f"{task_id} {name} tree is missing or contains symlinks")
    inventory = document["starter_files"]
    if not isinstance(inventory, list) or not all(isinstance(item, str) for item in inventory):
        raise BenchmarkError("P223_METADATA_INVALID", f"{task_id}.starter_files must be strings")
    if sorted(inventory) != file_inventory(directory / "starter"):
        raise BenchmarkError("P223_TREE_INVALID", f"{task_id} starter inventory mismatch")
    allowed = document["allowed_paths"]
    if not isinstance(allowed, list) or not allowed or len(allowed) != len(set(allowed)):
        raise BenchmarkError("P223_METADATA_INVALID", f"{task_id}.allowed_paths is invalid")
    for index, value in enumerate(allowed):
        safe_relative(value, f"{task_id}.allowed_paths[{index}]")
    facts = document["requested_facts"]
    if not isinstance(facts, list) or not facts or len(facts) != len(set(facts)):
        raise BenchmarkError("P223_METADATA_INVALID", f"{task_id}.requested_facts is invalid")
    ground_path = directory / "semantic_ground_truth.json"
    ground = read_json(ground_path)
    if not isinstance(ground, dict) or ground.get("schema_version") != 1 or ground.get("task_id") != task_id or not isinstance(ground.get("facts"), dict):
        raise BenchmarkError("P223_GROUND_TRUTH_INVALID", f"{task_id} ground truth has invalid envelope")
    if sorted(ground["facts"]) != sorted(facts):
        raise BenchmarkError("P223_GROUND_TRUTH_INVALID", f"{task_id} requested facts differ from ground truth")
    validations = document["validation"]
    if not isinstance(validations, list) or not validations:
        raise BenchmarkError("P223_VALIDATION_INVALID", f"{task_id} validation must be non-empty")
    for index, validation in enumerate(validations):
        if not isinstance(validation, dict) or set(validation) - {"command", "cwd", "expect"}:
            raise BenchmarkError("P223_VALIDATION_INVALID", f"{task_id} validation[{index}] is invalid")
        argv = validation.get("command")
        if not isinstance(argv, list) or len(argv) < 2 or argv[0] not in TOOLS or not all(isinstance(item, str) and item for item in argv):
            raise BenchmarkError("P223_VALIDATION_INVALID", f"{task_id} validation[{index}].command is invalid")
        for argument in argv[1:]:
            if PurePosixPath(argument).is_absolute() or ".." in PurePosixPath(argument).parts:
                raise BenchmarkError("P223_VALIDATION_INVALID", f"{task_id} validation escapes worktree")
        if "cwd" in validation:
            safe_relative(validation["cwd"], f"{task_id}.validation[{index}].cwd")
        expect = validation.get("expect", {})
        if not isinstance(expect, dict) or set(expect) - {"exit_code", "stdout", "stdout_contains", "stderr_contains"}:
            raise BenchmarkError("P223_VALIDATION_INVALID", f"{task_id} validation expectation is invalid")
    result = dict(document)
    result["task_directory"] = directory.name
    result["task_metadata_sha256"] = sha256_bytes(canonical_json(document))
    result["ground_truth_sha256"] = sha256_bytes(canonical_json(ground))
    result["validator_sha256"] = sha256_bytes(canonical_json(validations))
    return result


def load_tasks() -> list[dict[str, Any]]:
    if not TASKS.is_dir():
        raise BenchmarkError("P223_SUITE_MISSING", f"task root is absent: {TASKS}")
    tasks = []
    seen: set[str] = set()
    for directory in sorted(path for path in TASKS.iterdir() if path.is_dir()):
        task = validate_task(read_json(directory / "task.json"), directory)
        if task["id"] in seen:
            raise BenchmarkError("P223_DUPLICATE_TASK", f"duplicate ID {task['id']}")
        seen.add(task["id"])
        tasks.append(task)
    if len(tasks) != 20:
        raise BenchmarkError("P223_CORPUS_SIZE", f"expected 20 tasks, found {len(tasks)}")
    if sum(task["semantic_heavy"] for task in tasks) != 16:
        raise BenchmarkError("P223_CORPUS_BALANCE", "expected 16 semantic-heavy and 4 control tasks")
    counts = {category: sum(task["category"] == category for task in tasks) for category in CATEGORIES}
    if any(value != 4 for value in counts.values()):
        raise BenchmarkError("P223_CORPUS_BALANCE", f"expected four tasks per category, got {counts}")
    return tasks


def find_task(task_id: str) -> dict[str, Any]:
    for task in load_tasks():
        if task["id"] == task_id:
            return task
    raise BenchmarkError("P223_TASK_NOT_FOUND", f"unknown task {task_id}")


def changed_paths(starter: Path, worktree: Path) -> list[str]:
    start = {path.relative_to(starter).as_posix(): sha256_bytes(path.read_bytes()) for path in starter.rglob("*") if path.is_file()}
    final = {path.relative_to(worktree).as_posix(): sha256_bytes(path.read_bytes()) for path in worktree.rglob("*") if path.is_file()}
    return sorted(path for path in set(start) | set(final) if start.get(path) != final.get(path))


def path_allowed(path: str, patterns: list[str]) -> bool:
    return any(path == pattern or (pattern.endswith("/**") and path.startswith(pattern[:-3].rstrip("/") + "/")) for pattern in patterns)


def score_answer(task: dict[str, Any], answer_path: Path) -> dict[str, Any]:
    ground = read_json(TASKS / task["task_directory"] / "semantic_ground_truth.json")
    try:
        answer = read_json(answer_path)
    except BenchmarkError as error:
        return {"schema_valid": False, "error": str(error), "correct": 0, "requested": len(task["requested_facts"]), "fields": {}}
    schema_valid = (
        isinstance(answer, dict) and set(answer) <= {"schema_version", "task_id", "facts", "reason"}
        and answer.get("schema_version") == 1 and answer.get("task_id") == task["id"]
        and isinstance(answer.get("facts"), dict)
        and set(answer["facts"]) == set(task["requested_facts"])
        and ("reason" not in answer or isinstance(answer["reason"], str))
    )
    fields: dict[str, Any] = {}
    correct = 0
    for key in task["requested_facts"]:
        actual = answer.get("facts", {}).get(key) if isinstance(answer, dict) else None
        expected = ground["facts"][key]
        match = actual == expected
        correct += int(match)
        fields[key] = {"correct": match, "actual": actual, "expected": expected}
    return {"schema_valid": schema_valid, "correct": correct, "requested": len(fields), "accuracy": correct / len(fields), "fields": fields}


def validate_worktree(task: dict[str, Any], worktree: Path, moss: Path, margo: Path) -> dict[str, Any]:
    validations = []
    all_pass = True
    for item in task["validation"]:
        argv = [str(moss if part == "$MOSS" else margo if part == "$MARGO" else part) for part in item["command"]]
        cwd = worktree / item.get("cwd", ".")
        process = subprocess.run(argv, cwd=cwd, text=True, capture_output=True, check=False, timeout=180)
        expect = item.get("expect", {})
        passed = process.returncode == expect.get("exit_code", 0)
        if "stdout" in expect:
            passed = passed and process.stdout == expect["stdout"]
        if "stdout_contains" in expect:
            passed = passed and expect["stdout_contains"] in process.stdout
        if "stderr_contains" in expect:
            passed = passed and expect["stderr_contains"] in process.stderr
        validations.append({"command": item["command"], "exit_code": process.returncode, "stdout": process.stdout, "stderr": process.stderr, "passed": passed})
        all_pass = all_pass and passed
    starter = TASKS / task["task_directory"] / "starter"
    changes = changed_paths(starter, worktree)
    outside = [path for path in changes if not path_allowed(path, task["allowed_paths"])]
    answer_score = score_answer(task, worktree / "semantic-answer.json")
    return {
        "task_id": task["id"], "validator_pass": all_pass and not outside,
        "validation": validations, "changed_paths": changes,
        "outside_allowed_paths": outside, "semantic_answer": answer_score,
    }


def validate_expected(tasks: list[dict[str, Any]], moss: Path, margo: Path) -> list[dict[str, Any]]:
    results = []
    for task in tasks:
        expected = TASKS / task["task_directory"] / "expected"
        result = validate_worktree(task, expected, moss, margo)
        if not result["validator_pass"] or not result["semantic_answer"]["schema_valid"] or result["semantic_answer"]["accuracy"] != 1:
            raise BenchmarkError("P223_EXPECTED_INVALID", f"{task['id']} expected solution failed: {result}")
        results.append(result)
    return results


def main() -> int:
    parser = argparse.ArgumentParser()
    sub = parser.add_subparsers(dest="command", required=True)
    sub.add_parser("validate")
    run = sub.add_parser("run")
    run.add_argument("task_id")
    run.add_argument("--workdir", type=Path, required=True)
    args = parser.parse_args()
    try:
        tasks = load_tasks()
        if args.command == "validate":
            results = validate_expected(tasks, ROOT / "moss", ROOT / "margo")
            output = {"ok": True, "task_count": len(tasks), "expected_valid": len(results)}
        else:
            task = next(task for task in tasks if task["id"] == args.task_id)
            output = validate_worktree(task, args.workdir.resolve(), ROOT / "moss", ROOT / "margo")
            output["ok"] = True
        print(json.dumps(output, indent=2, sort_keys=True))
        return 0
    except (BenchmarkError, StopIteration) as error:
        code = error.code if isinstance(error, BenchmarkError) else "P223_TASK_NOT_FOUND"
        print(json.dumps({"ok": False, "error": {"code": code, "message": str(error)}}, sort_keys=True))
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
