#!/usr/bin/env python3
"""Regression checks for the fixed Moss fresh-agent benchmark suite."""

from __future__ import annotations

import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import sys


ROOT = Path(__file__).resolve().parents[2]
SCRATCH = ROOT / "tmp" / "agent-benchmark-test"


def invoke(compiler: Path, *arguments: str, expected: int = 0) -> dict:
    process = subprocess.run(
        [str(compiler), "agent", "benchmark", *arguments],
        cwd=ROOT, text=True, capture_output=True, check=False, timeout=180,
    )
    if process.returncode != expected:
        raise AssertionError(
            f"benchmark {' '.join(arguments)} returned {process.returncode}, expected {expected}\n"
            f"stdout:\n{process.stdout}\nstderr:\n{process.stderr}"
        )
    try:
        return json.loads(process.stdout)
    except json.JSONDecodeError as error:
        raise AssertionError(f"benchmark {' '.join(arguments)} returned invalid JSON: {error}") from error


def digest_tree(root: Path) -> str:
    digest = hashlib.sha256()
    for path in sorted(candidate for candidate in root.rglob("*") if candidate.is_file()):
        digest.update(path.relative_to(root).as_posix().encode())
        digest.update(path.read_bytes())
    return digest.hexdigest()


def make_suite(name: str) -> Path:
    suite = SCRATCH / name
    (suite / "tasks").mkdir(parents=True)
    return suite


def reject_shortcut(compiler: Path, task_id: str, name: str,
                    files: dict[str, str], assertion_failure: bool = True) -> dict:
    workdir = SCRATCH / name
    for relative, contents in files.items():
        path = workdir / relative
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(contents, encoding="utf-8")
    result = invoke(
        compiler, "run", task_id, "--workdir", str(workdir), "--json", expected=1,
    )["result"]
    assert result["status"] == "fail"
    assert result["outside_allowed_paths"] == []
    if assertion_failure:
        assert any(
            record.get("assertion") is not None and record["status"] == "fail"
            for record in result["validation"]
        )
    return result


def main() -> int:
    if len(sys.argv) != 2:
        raise SystemExit("usage: check_agent_benchmark.py <moss>")
    compiler = Path(sys.argv[1]).resolve()
    shutil.rmtree(SCRATCH, ignore_errors=True)
    SCRATCH.mkdir(parents=True)

    listing = invoke(compiler, "list", "--json")
    assert listing["schema_version"] == "moss-agent-benchmark-1"
    assert listing["ok"] is True
    assert listing["result"]["task_count"] == 30
    tasks = listing["result"]["tasks"]
    assert [task["id"] for task in tasks] == [f"AB{index:03d}" for index in range(1, 31)]
    assert set(tasks[0]) == {"id", "name", "category", "task_type", "prompt"}

    shown = invoke(compiler, "show", "AB007", "--json")["result"]
    assert shown["name"] == "cross-domain-reply"
    assert shown["category"] == "domains-messages"
    assert shown["allowed_paths"] == ["main.moss"]
    assert shown["validation"][0]["command"][0] == "$MOSS"
    assert "starter_files" in shown

    metadata = invoke(compiler, "validate", "--metadata-only", "--json")["result"]
    assert metadata["metadata_valid"] is True
    assert metadata["task_types"] == {"repair": 10, "workflow": 10, "write": 10}
    assert sum(metadata["categories"].values()) == 30

    validated = invoke(compiler, "validate", "--json")["result"]
    assert validated["expected_checked"] is True
    assert validated["expected_passed"] == 30

    passing_root = ROOT / "benchmarks" / "agent" / "tasks" / "AB029_discover_project_workflow" / "starter"
    before = digest_tree(passing_root)
    passing = invoke(compiler, "run", "AB029", "--json")["result"]
    assert passing["status"] == "pass"
    assert passing["changed_paths"] == []
    assert passing["outside_allowed_paths"] == []
    assert set(passing) == {
        "task_id", "status", "validation", "attempts", "tool_calls",
        "diagnostics", "changed_paths", "outside_allowed_paths", "agent",
        "wall_time_ms",
    }
    assert digest_tree(passing_root) == before
    assert not (passing_root / "build").exists()

    failing = invoke(compiler, "run", "AB003", "--json", expected=1)["result"]
    assert failing["status"] == "fail"
    assert failing["validation"][1]["failures"] == ["stdout did not match exactly"]

    prepared = SCRATCH / "prepared"
    shutil.copytree(
        ROOT / "benchmarks" / "agent" / "tasks" / "AB001_simple_computation" / "expected",
        prepared,
    )
    (prepared / "rogue.txt").write_text("outside allowed paths\n", encoding="utf-8")
    disallowed = invoke(
        compiler, "run", "AB001", "--workdir", str(prepared), "--json", expected=1
    )["result"]
    assert disallowed["status"] == "fail"
    assert disallowed["outside_allowed_paths"] == ["rogue.txt"]

    reject_shortcut(
        compiler, "AB002", "shortcut-named-functions",
        {"main.moss": "fn main():\n  echo 49\n"},
    )
    reject_shortcut(
        compiler, "AB006", "shortcut-domain",
        {"main.moss": "fn main():\n  echo 5\n"},
    )
    reject_shortcut(
        compiler, "AB008", "shortcut-helper-repair",
        {"main.moss": "fn main():\n  echo 42\n"},
    )
    unused_helper = reject_shortcut(
        compiler, "AB008", "shortcut-unused-helper",
        {"main.moss": (
            "fn advanced(value: Int) -> Int:\n  return value + 1\n\n"
            "domain Counter:\n  value = 41\n\n"
            "  fn Increment() -> Int:\n"
            "    value = value + 1\n    reply value\n\n"
            "fn main():\n  counter = Counter()\n"
            "  echo message counter.Increment()\n"
        )},
        assertion_failure=False,
    )
    assert unused_helper["validation"][2]["status"] == "fail"
    assert unused_helper["validation"][2]["failures"] == [
        "JSON field 'result.direct_calls.0.target' was missing"
    ]
    reject_shortcut(
        compiler, "AB016", "shortcut-pipeline",
        {"main.moss": "fn main():\n  echo 14\n"},
    )
    reject_shortcut(
        compiler, "AB024", "shortcut-modules",
        {
            "Moss.toml": (
                "[project]\nname = \"shortcut\"\nversion = \"0.1.0\"\n\n"
                "[build]\nsource = \"src\"\n"
            ),
            "src/main.moss": "fn main():\n  echo 42\n",
        },
    )
    alternate_modules = SCRATCH / "alternate-module-files"
    (alternate_modules / "src").mkdir(parents=True)
    (alternate_modules / "Moss.toml").write_text(
        '[project]\nname = "alternate"\nversion = "0.1.0"\n\n'
        '[build]\nsource = "src"\n', encoding="utf-8",
    )
    (alternate_modules / "src" / "Math.moss").write_text(
        "module Math\n\nexport fn answer() -> Int:\n  return 42\n", encoding="utf-8",
    )
    (alternate_modules / "src" / "App.moss").write_text(
        "module App\nimport Math\n\nfn main():\n  echo Math.answer()\n", encoding="utf-8",
    )
    alternate_result = invoke(
        compiler, "run", "AB024", "--workdir", str(alternate_modules), "--json",
    )["result"]
    assert alternate_result["status"] == "pass"

    pure_filter = SCRATCH / "alternate-pure-pipeline"
    pure_filter.mkdir()
    (pure_filter / "main.moss").write_text(
        "fn main():\n"
        "  values = [20, 22]\n"
        "  result = values |> filter(_ > 0)\n"
        "  echo result |> sum\n",
        encoding="utf-8",
    )
    filter_result = invoke(
        compiler, "run", "AB020", "--workdir", str(pure_filter), "--json",
    )["result"]
    assert filter_result["status"] == "pass"

    source_task = ROOT / "benchmarks" / "agent" / "tasks" / "AB001_simple_computation"
    duplicate = make_suite("duplicate")
    shutil.copytree(source_task, duplicate / "tasks" / "AB001_first")
    shutil.copytree(source_task, duplicate / "tasks" / "AB001_second")
    duplicate_result = invoke(
        compiler, "validate", "--metadata-only", "--suite-root", str(duplicate),
        "--json", expected=2,
    )
    assert duplicate_result["error"]["code"] == "BENCHMARK_DUPLICATE_TASK"

    missing = make_suite("missing")
    (missing / "tasks" / "AB999_missing").mkdir()
    missing_result = invoke(
        compiler, "validate", "--metadata-only", "--suite-root", str(missing),
        "--json", expected=2,
    )
    assert missing_result["error"]["code"] == "BENCHMARK_METADATA_MISSING"

    missing_starter = make_suite("missing-starter")
    shutil.copytree(source_task, missing_starter / "tasks" / "AB001_missing")
    missing_task = missing_starter / "tasks" / "AB001_missing" / "task.json"
    missing_document = json.loads(missing_task.read_text(encoding="utf-8"))
    missing_document["starter_files"] = ["main.moss"]
    missing_task.write_text(json.dumps(missing_document), encoding="utf-8")
    missing_starter_result = invoke(
        compiler, "validate", "--metadata-only", "--suite-root", str(missing_starter),
        "--json", expected=2,
    )
    assert missing_starter_result["error"]["code"] == "BENCHMARK_STARTER_MISSING"

    invalid = make_suite("invalid-command")
    shutil.copytree(source_task, invalid / "tasks" / "AB001_invalid")
    task_file = invalid / "tasks" / "AB001_invalid" / "task.json"
    document = json.loads(task_file.read_text(encoding="utf-8"))
    document["validation"][0]["command"] = ["$SHELL", "anything"]
    task_file.write_text(json.dumps(document), encoding="utf-8")
    invalid_result = invoke(
        compiler, "validate", "--metadata-only", "--suite-root", str(invalid),
        "--json", expected=2,
    )
    assert invalid_result["error"]["code"] == "BENCHMARK_VALIDATION_INVALID"

    invalid_regex = make_suite("invalid-regex")
    shutil.copytree(source_task, invalid_regex / "tasks" / "AB001_invalid")
    regex_task = invalid_regex / "tasks" / "AB001_invalid" / "task.json"
    regex_document = json.loads(regex_task.read_text(encoding="utf-8"))
    regex_document["assertions"] = [{"path": "main.moss", "matches": "("}]
    regex_task.write_text(json.dumps(regex_document), encoding="utf-8")
    invalid_regex_result = invoke(
        compiler, "validate", "--metadata-only", "--suite-root", str(invalid_regex),
        "--json", expected=2,
    )
    assert invalid_regex_result["error"]["code"] == "BENCHMARK_METADATA_INVALID"

    print("Agent benchmark metadata, concept, execution, isolation, and path checks passed.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
