#!/usr/bin/env python3
"""Validate durable JSONL feedback inventories for Moss swarm experiments."""

from __future__ import annotations

import argparse
import copy
import json
import re
import sys
import tempfile
from pathlib import Path
from typing import Any


ROOT = Path(__file__).resolve().parents[1]
FEEDBACK_DIR = Path("examples/swarm/feedback")
SCHEMA_PATH = FEEDBACK_DIR / "schema.json"
TEMPLATE_PATH = FEEDBACK_DIR / "OBSERVATION_TEMPLATE.json"

CLASSIFICATIONS = (
    "language_rule", "agent_misunderstanding", "ergonomics", "documentation",
    "diagnostic", "frontend", "formatter", "ownership_effect", "specialization",
    "native_lowering", "fast_debug", "module_interface", "package_tooling",
    "semantic_tooling", "compiler_crash",
)
SEVERITIES = ("crash", "wrong_result", "correctness", "parity", "usability", "documentation")
CONFIDENCES = ("candidate", "reproduced", "confirmed", "rejected")
MATRIX_VALUES = ("pass", "fail", "unsupported", "not_tested", "not_applicable")
MATRIX_FIELDS = (
    "fmt", "check", "native_build", "native_run", "native_test", "fast_debug",
    "fast_debug_test",
)
REQUIRED_FIELDS = (
    "observation_id", "phase", "agent", "workload", "summary", "classification",
    "severity", "confidence", "discovered_naturally", "natural_source", "reproducer",
    "expected", "actual", "matrix", "diagnostic", "workaround", "existing_swarm", "notes",
    "baseline_commit",
)


def relative(path: Path, root: Path) -> str:
    try:
        return path.relative_to(root).as_posix()
    except ValueError:
        return str(path)


def report(path: Path, line: int, observation_id: str | None, reason: str, root: Path) -> str:
    identity = f" [{observation_id}]" if observation_id else ""
    return f"{relative(path, root)}:{line}{identity}: {reason}"


def enum_reason(field: str, value: Any, choices: tuple[str, ...]) -> str:
    return f"invalid {field} {value!r}; expected one of {', '.join(choices)}"


def validate_path(value: Any, field: str, path: Path, line: int,
                  observation_id: str | None, root: Path) -> list[str]:
    if value is None:
        return []
    if not isinstance(value, str) or not value:
        return [report(path, line, observation_id,
                       f"{field} must be a nonempty repository-relative path or null", root)]
    candidate = Path(value)
    if candidate.is_absolute() or ".." in candidate.parts or "tmp" in candidate.parts:
        return [report(path, line, observation_id,
                       f"{field} must be a repository-relative path with no tmp/ component: {value}", root)]
    if not (root / candidate).exists():
        return [report(path, line, observation_id,
                       f"{field} path does not exist: {value}", root)]
    return []


def validate_observation(observation: Any, path: Path, line: int, root: Path,
                         check_paths: bool = True) -> list[str]:
    if not isinstance(observation, dict):
        return [report(path, line, None, "observation must be a JSON object", root)]
    observation_id = observation.get("observation_id")
    observation_id = observation_id if isinstance(observation_id, str) else None
    errors: list[str] = []
    missing = [field for field in REQUIRED_FIELDS if field not in observation]
    if missing:
        errors.append(report(path, line, observation_id,
                             f"missing required field(s): {', '.join(missing)}", root))
    unknown = sorted(set(observation) - set(REQUIRED_FIELDS))
    if unknown:
        errors.append(report(path, line, observation_id,
                             f"unknown field(s): {', '.join(unknown)}", root))
    for field in ("observation_id", "phase", "agent", "workload", "summary", "expected", "actual"):
        if not isinstance(observation.get(field), str) or not observation.get(field):
            errors.append(report(path, line, observation_id,
                                 f"{field} must be a nonempty string", root))
    if isinstance(observation.get("observation_id"), str) and not re.fullmatch(
            r"[A-Za-z0-9][A-Za-z0-9._-]*", observation["observation_id"]):
        errors.append(report(path, line, observation_id,
                             "observation_id contains unsupported characters", root))
    for field, choices in (("classification", CLASSIFICATIONS), ("severity", SEVERITIES),
                           ("confidence", CONFIDENCES)):
        if observation.get(field) not in choices:
            errors.append(report(path, line, observation_id,
                                 enum_reason(field, observation.get(field), choices), root))
    if not isinstance(observation.get("discovered_naturally"), bool):
        errors.append(report(path, line, observation_id,
                             "discovered_naturally must be true or false", root))
    swarm = observation.get("existing_swarm")
    if swarm is not None and (not isinstance(swarm, str) or not re.fullmatch(r"SWARM-[0-9]+", swarm)):
        errors.append(report(path, line, observation_id,
                             "existing_swarm must be null or match SWARM-[0-9]+", root))
    for field in ("workaround", "notes"):
        if observation.get(field) is not None and not isinstance(observation.get(field), str):
            errors.append(report(path, line, observation_id,
                                 f"{field} must be a string or null", root))
    baseline = observation.get("baseline_commit")
    if not isinstance(baseline, str) or not re.fullmatch(r"[0-9a-fA-F]{40}", baseline):
        errors.append(report(path, line, observation_id,
                             "baseline_commit must be a 40-hex Git commit SHA", root))
    diagnostic = observation.get("diagnostic")
    if diagnostic is not None:
        if not isinstance(diagnostic, dict):
            errors.append(report(path, line, observation_id, "diagnostic must be an object or null", root))
        else:
            expected = {"code", "message", "source_location_correct"}
            absent = sorted(expected - set(diagnostic))
            extra = sorted(set(diagnostic) - expected)
            if absent:
                errors.append(report(path, line, observation_id,
                                     f"diagnostic missing field(s): {', '.join(absent)}", root))
            if extra:
                errors.append(report(path, line, observation_id,
                                     f"diagnostic unknown field(s): {', '.join(extra)}", root))
            for field in ("code", "message"):
                if not isinstance(diagnostic.get(field), str) or not diagnostic.get(field):
                    errors.append(report(path, line, observation_id,
                                         f"diagnostic.{field} must be a nonempty string", root))
            if not isinstance(diagnostic.get("source_location_correct"), bool):
                errors.append(report(path, line, observation_id,
                                     "diagnostic.source_location_correct must be true or false", root))
    matrix = observation.get("matrix")
    if not isinstance(matrix, dict):
        errors.append(report(path, line, observation_id, "matrix must be an object", root))
    else:
        absent = [field for field in MATRIX_FIELDS if field not in matrix]
        extra = sorted(set(matrix) - set(MATRIX_FIELDS))
        if absent:
            errors.append(report(path, line, observation_id,
                                 f"matrix missing field(s): {', '.join(absent)}", root))
        if extra:
            errors.append(report(path, line, observation_id,
                                 f"matrix unknown field(s): {', '.join(extra)}", root))
        for field in MATRIX_FIELDS:
            if field in matrix and matrix[field] not in MATRIX_VALUES:
                errors.append(report(path, line, observation_id,
                                     enum_reason(f"matrix.{field}", matrix[field], MATRIX_VALUES), root))
    if check_paths:
        for field in ("natural_source", "reproducer"):
            errors.extend(validate_path(observation.get(field), field, path, line, observation_id, root))
    return errors


def validate_contract() -> list[str]:
    """Ensure schema, fictional template, and implementation share one vocabulary."""
    errors: list[str] = []
    schema_path = ROOT / SCHEMA_PATH
    template_path = ROOT / TEMPLATE_PATH
    try:
        schema = json.loads(schema_path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as exc:
        return [f"{SCHEMA_PATH}: invalid schema.json: {exc}"]
    try:
        template = json.loads(template_path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as exc:
        return [f"{TEMPLATE_PATH}: invalid OBSERVATION_TEMPLATE.json: {exc}"]
    properties = schema.get("properties", {})
    if not isinstance(properties, dict):
        return [f"{SCHEMA_PATH}: properties must be an object"]
    for field, values in (("classification", CLASSIFICATIONS), ("severity", SEVERITIES),
                          ("confidence", CONFIDENCES)):
        if properties.get(field, {}).get("enum") != list(values):
            errors.append(f"{SCHEMA_PATH}: {field} enum disagrees with validator")
    if schema.get("required") != list(REQUIRED_FIELDS):
        errors.append(f"{SCHEMA_PATH}: required fields disagree with validator")
    matrix = properties.get("matrix", {})
    if matrix.get("required") != list(MATRIX_FIELDS):
        errors.append(f"{SCHEMA_PATH}: matrix fields disagree with validator")
    if schema.get("$defs", {}).get("matrix_value", {}).get("enum") != list(MATRIX_VALUES):
        errors.append(f"{SCHEMA_PATH}: matrix values disagree with validator")
    errors.extend(validate_observation(template, template_path, 1, ROOT, check_paths=False))
    return errors


def validate_repository(root: Path) -> list[str]:
    errors = validate_contract()
    seen: dict[str, tuple[Path, int]] = {}
    swarm_root = root / "examples" / "swarm"
    feedback_files = sorted(swarm_root.rglob("FEEDBACK.jsonl")) if swarm_root.exists() else []
    for feedback in feedback_files:
        for line_number, raw in enumerate(feedback.read_text(encoding="utf-8").splitlines(), 1):
            if not raw.strip():
                continue
            try:
                observation = json.loads(raw)
            except json.JSONDecodeError as exc:
                errors.append(report(feedback, line_number, None, f"invalid JSON: {exc.msg}", root))
                continue
            observation_id = observation.get("observation_id") if isinstance(observation, dict) else None
            errors.extend(validate_observation(observation, feedback, line_number, root))
            if isinstance(observation_id, str):
                if observation_id in seen:
                    first_path, first_line = seen[observation_id]
                    errors.append(report(feedback, line_number, observation_id,
                                         f"duplicate observation_id; first seen at {relative(first_path, root)}:{first_line}", root))
                else:
                    seen[observation_id] = (feedback, line_number)
    return errors


def self_test() -> None:
    with tempfile.TemporaryDirectory(prefix="moss-swarm-feedback-") as temporary:
        root = Path(temporary)
        experiment = root / "examples/swarm/example"
        source = experiment / "src/main.moss"
        reproducer = experiment / "repros/minimal.moss"
        source.parent.mkdir(parents=True)
        reproducer.parent.mkdir(parents=True)
        source.write_text("# evidence\n", encoding="utf-8")
        reproducer.write_text("# evidence\n", encoding="utf-8")
        record = {
            "observation_id": "15.99-example-001", "phase": "15.99", "agent": "test-agent",
            "workload": "validator self-test", "summary": "valid record", "classification": "frontend",
            "severity": "correctness", "confidence": "confirmed", "discovered_naturally": True,
            "natural_source": "examples/swarm/example/src/main.moss",
            "reproducer": "examples/swarm/example/repros/minimal.moss",
            "expected": "accepted", "actual": "accepted",
            "matrix": {field: "not_tested" for field in MATRIX_FIELDS}, "diagnostic": None,
            "workaround": None, "existing_swarm": None, "notes": None,
            "baseline_commit": "abcdef1234567890abcdef1234567890abcdef12",
        }
        feedback = experiment / "FEEDBACK.jsonl"

        def check(records: list[dict[str, Any]], expected: str | None = None) -> None:
            feedback.write_text("\n".join(json.dumps(item) for item in records) + "\n", encoding="utf-8")
            errors = validate_repository(root)
            if expected is None and errors:
                raise AssertionError(f"valid observation rejected: {errors}")
            if expected is not None and not any(expected in message for message in errors):
                raise AssertionError(f"expected {expected!r}; got {errors}")

        check([record])
        check([record, copy.deepcopy(record)], "duplicate observation_id")
        invalid_classification = copy.deepcopy(record)
        invalid_classification["classification"] = "lowering_bug"
        check([invalid_classification], "invalid classification 'lowering_bug'")
        invalid_matrix = copy.deepcopy(record)
        invalid_matrix["matrix"]["native_build"] = "broken"
        check([invalid_matrix], "invalid matrix.native_build 'broken'")
        missing_file = copy.deepcopy(record)
        missing_file["reproducer"] = "examples/swarm/example/repros/missing.moss"
        check([missing_file], "reproducer path does not exist")
        invalid_sha_short = copy.deepcopy(record)
        invalid_sha_short["baseline_commit"] = "abc123"
        check([invalid_sha_short], "baseline_commit must be a 40-hex Git commit SHA")
        invalid_sha_chars = copy.deepcopy(record)
        invalid_sha_chars["baseline_commit"] = "g" * 40
        check([invalid_sha_chars], "baseline_commit must be a 40-hex Git commit SHA")
        # Reproducer pointing into a nested tmp/ directory must be rejected.
        nested_tmp = experiment / "data" / "tmp"
        nested_tmp.mkdir(parents=True, exist_ok=True)
        nested_tmp_file = nested_tmp / "repro.moss"
        nested_tmp_file.write_text("# evidence\n", encoding="utf-8")
        tmp_path_record = copy.deepcopy(record)
        tmp_path_record["reproducer"] = "examples/swarm/example/data/tmp/repro.moss"
        check([tmp_path_record], "tmp/ component")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=ROOT, help="repository root to validate")
    parser.add_argument("--self-test", action="store_true", help="exercise validator acceptance and rejection cases")
    args = parser.parse_args()
    if args.self_test:
        self_test()
        print("swarm feedback validator self-test passed")
    errors = validate_repository(args.root.resolve())
    if errors:
        print("Swarm feedback validation failed:", file=sys.stderr)
        print("\n".join(errors), file=sys.stderr)
        return 1
    print("Swarm feedback validation passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
