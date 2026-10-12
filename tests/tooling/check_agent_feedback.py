#!/usr/bin/env python3
"""Validate repository-wide Moss developer-experience feedback JSONL files."""

from __future__ import annotations

import argparse
import copy
import json
import re
import shutil
import sys
import tempfile
from pathlib import Path
from typing import Any


ROOT = Path(__file__).resolve().parents[2]
FEEDBACK_DIR = Path("agent-feedback")
SCHEMA_PATH = FEEDBACK_DIR / "SCHEMA.json"
OBSERVATIONS_FILE = "observations.jsonl"
EXAMPLE_PATH = Path("examples") / OBSERVATIONS_FILE

CATEGORIES = (
    "language-friction",
    "language-positive",
    "surprise",
    "debugging-bottleneck",
    "ecosystem-friction",
    "tooling-positive",
    "tooling-negative",
    "performance-observation",
)
RESOLUTION_STATUSES = ("resolved", "unresolved", "workaround", "explanation")
SEVERITIES = ("low", "medium", "high", "critical")
TOOL_HELPFULNESS = ("helpful", "partially-helpful", "unhelpful")
REQUIRED_FIELDS = (
    "schema_version",
    "id",
    "phase",
    "project",
    "workstream",
    "agent_role",
    "category",
    "task",
    "observation",
    "impact",
    "evidence",
    "resolution",
    "commit",
)
OPTIONAL_FIELDS = (
    "severity",
    "time_lost_minutes",
    "tool_used",
    "tool_helpfulness",
    "reproducer",
    "suggestion",
    "tags",
    "related_issue",
    "comparison_language",
)
ID_RE = re.compile(r"^[A-Za-z0-9][A-Za-z0-9._-]*$")
PATH_COMPONENT_RE = re.compile(r"^[a-z0-9]+(?:-[a-z0-9]+)*$")
COMMIT_RE = re.compile(r"^[0-9a-fA-F]{40}$")


def relative(path: Path, root: Path) -> str:
    try:
        return path.relative_to(root).as_posix()
    except ValueError:
        return str(path)


def report(path: Path, line: int, observation_id: str | None, reason: str,
           root: Path) -> str:
    identity = f" [{observation_id}]" if observation_id else ""
    return f"{relative(path, root)}:{line}{identity}: {reason}"


def enum_reason(field: str, value: Any, choices: tuple[str, ...]) -> str:
    return f"invalid {field} {value!r}; expected one of {', '.join(choices)}"


def is_nonempty_string(value: Any) -> bool:
    return isinstance(value, str) and bool(value.strip())


def has_duplicates(values: list[Any]) -> bool:
    encoded = [json.dumps(value, sort_keys=True, separators=(",", ":")) for value in values]
    return len(encoded) != len(set(encoded))


def validate_nullable_string(record: dict[str, Any], field: str, path: Path,
                             line: int, observation_id: str | None,
                             root: Path) -> list[str]:
    if field not in record or record[field] is None:
        return []
    if not is_nonempty_string(record[field]):
        return [report(path, line, observation_id,
                       f"{field} must be a nonempty string or null", root)]
    return []


def validate_observation(record: Any, path: Path, line: int,
                         root: Path) -> list[str]:
    if not isinstance(record, dict):
        return [report(path, line, None, "observation must be a JSON object", root)]

    raw_id = record.get("id")
    observation_id = raw_id if isinstance(raw_id, str) else None
    errors: list[str] = []
    missing = [field for field in REQUIRED_FIELDS if field not in record]
    if missing:
        errors.append(report(path, line, observation_id,
                             f"missing required field(s): {', '.join(missing)}", root))
    unknown = sorted(set(record) - set(REQUIRED_FIELDS) - set(OPTIONAL_FIELDS))
    if unknown:
        errors.append(report(path, line, observation_id,
                             f"unknown field(s): {', '.join(unknown)}", root))

    if record.get("schema_version") != "1.0":
        errors.append(report(path, line, observation_id,
                             "schema_version must be '1.0'", root))
    if not isinstance(raw_id, str) or not ID_RE.fullmatch(raw_id):
        errors.append(report(path, line, observation_id,
                             "id must start with an alphanumeric character and contain only letters, digits, '.', '_' or '-'",
                             root))
    for field in ("phase", "project", "workstream"):
        value = record.get(field)
        if not isinstance(value, str) or not PATH_COMPONENT_RE.fullmatch(value):
            errors.append(report(path, line, observation_id,
                                 f"{field} must be a lowercase kebab-case path component", root))
    for field in ("agent_role", "task", "observation", "impact"):
        if not is_nonempty_string(record.get(field)):
            errors.append(report(path, line, observation_id,
                                 f"{field} must be a nonempty string", root))

    category = record.get("category")
    if category not in CATEGORIES:
        errors.append(report(path, line, observation_id,
                             enum_reason("category", category, CATEGORIES), root))

    evidence = record.get("evidence")
    if not isinstance(evidence, list) or not evidence:
        errors.append(report(path, line, observation_id,
                             "evidence must be a nonempty array of nonempty strings", root))
    else:
        if any(not is_nonempty_string(item) for item in evidence):
            errors.append(report(path, line, observation_id,
                                 "evidence must contain only nonempty strings", root))
        if has_duplicates(evidence):
            errors.append(report(path, line, observation_id,
                                 "evidence must not contain duplicates", root))

    resolution = record.get("resolution")
    if not isinstance(resolution, dict):
        errors.append(report(path, line, observation_id,
                             "resolution must be an object with status and summary", root))
    else:
        resolution_unknown = sorted(set(resolution) - {"status", "summary"})
        resolution_missing = [key for key in ("status", "summary") if key not in resolution]
        if resolution_missing:
            errors.append(report(path, line, observation_id,
                                 f"resolution missing field(s): {', '.join(resolution_missing)}", root))
        if resolution_unknown:
            errors.append(report(path, line, observation_id,
                                 f"resolution unknown field(s): {', '.join(resolution_unknown)}", root))
        if resolution.get("status") not in RESOLUTION_STATUSES:
            errors.append(report(path, line, observation_id,
                                 enum_reason("resolution.status", resolution.get("status"),
                                             RESOLUTION_STATUSES), root))
        if not is_nonempty_string(resolution.get("summary")):
            errors.append(report(path, line, observation_id,
                                 "resolution.summary must be a nonempty string", root))

    commit = record.get("commit")
    if commit is not None and (not isinstance(commit, str) or not COMMIT_RE.fullmatch(commit)):
        errors.append(report(path, line, observation_id,
                             "commit must be a full 40-hex Git SHA or null", root))

    severity = record.get("severity")
    if severity is not None and severity not in SEVERITIES:
        errors.append(report(path, line, observation_id,
                             enum_reason("severity", severity, SEVERITIES), root))
    time_lost = record.get("time_lost_minutes")
    if time_lost is not None and (isinstance(time_lost, bool) or
                                  not isinstance(time_lost, int) or time_lost < 0):
        errors.append(report(path, line, observation_id,
                             "time_lost_minutes must be a nonnegative integer or null", root))
    helpfulness = record.get("tool_helpfulness")
    if helpfulness is not None and helpfulness not in TOOL_HELPFULNESS:
        errors.append(report(path, line, observation_id,
                             enum_reason("tool_helpfulness", helpfulness, TOOL_HELPFULNESS), root))
    for field in ("tool_used", "reproducer", "suggestion", "related_issue",
                  "comparison_language"):
        errors.extend(validate_nullable_string(record, field, path, line,
                                               observation_id, root))
    tags = record.get("tags")
    if tags is not None:
        if not isinstance(tags, list):
            errors.append(report(path, line, observation_id,
                                 "tags must be an array of nonempty strings or null", root))
        else:
            if any(not is_nonempty_string(tag) for tag in tags):
                errors.append(report(path, line, observation_id,
                                     "tags must contain only nonempty strings", root))
            if has_duplicates(tags):
                errors.append(report(path, line, observation_id,
                                     "tags must not contain duplicates", root))
    return errors


def validate_schema(root: Path) -> list[str]:
    path = root / SCHEMA_PATH
    try:
        schema = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as exc:
        return [f"{SCHEMA_PATH}: invalid or unreadable schema: {exc}"]
    properties = schema.get("properties")
    if not isinstance(properties, dict):
        return [f"{SCHEMA_PATH}: properties must be an object"]
    errors: list[str] = []
    if schema.get("additionalProperties") is not False:
        errors.append(f"{SCHEMA_PATH}: additionalProperties must be false")
    if schema.get("required") != list(REQUIRED_FIELDS):
        errors.append(f"{SCHEMA_PATH}: required fields disagree with validator")
    if set(properties) != set(REQUIRED_FIELDS) | set(OPTIONAL_FIELDS):
        errors.append(f"{SCHEMA_PATH}: properties disagree with validator fields")
    if properties.get("schema_version", {}).get("const") != "1.0":
        errors.append(f"{SCHEMA_PATH}: schema_version disagrees with validator")
    if properties.get("category", {}).get("enum") != list(CATEGORIES):
        errors.append(f"{SCHEMA_PATH}: category enum disagrees with validator")
    resolution = properties.get("resolution", {})
    if resolution.get("additionalProperties") is not False:
        errors.append(f"{SCHEMA_PATH}: resolution additionalProperties must be false")
    if resolution.get("required") != ["status", "summary"]:
        errors.append(f"{SCHEMA_PATH}: resolution fields disagree with validator")
    status_enum = resolution.get("properties", {}).get("status", {}).get("enum")
    if status_enum != list(RESOLUTION_STATUSES):
        errors.append(f"{SCHEMA_PATH}: resolution status enum disagrees with validator")
    return errors


def organization_components(path: Path, root: Path,
                            feedback_root: Path) -> tuple[tuple[str, str, str] | None,
                                                           list[str]]:
    relative_path = path.relative_to(feedback_root)
    if relative_path == EXAMPLE_PATH:
        return None, []
    if len(relative_path.parts) != 4 or relative_path.name != OBSERVATIONS_FILE:
        return None, [report(
            path, 1, None,
            "feedback file must be agent-feedback/<phase>/<project>/<workstream>/observations.jsonl",
            root)]
    phase, project, workstream, _ = relative_path.parts
    if not all(PATH_COMPONENT_RE.fullmatch(part) for part in (phase, project, workstream)):
        return None, [report(path, 1, None,
                             "feedback path components must use lowercase kebab-case", root)]
    return (phase, project, workstream), []


def validate_record_organization(record: Any, components: tuple[str, str, str] | None,
                                 path: Path, root: Path, line: int) -> list[str]:
    if components is None:
        return []
    if not isinstance(record, dict):
        return []
    phase, project, workstream = components
    observation_id = record.get("id") if isinstance(record.get("id"), str) else None
    errors: list[str] = []
    for field, expected in (("phase", phase), ("project", project),
                            ("workstream", workstream)):
        if record.get(field) != expected:
            errors.append(report(path, line, observation_id,
                                 f"{field} {record.get(field)!r} does not match path component {expected!r}",
                                 root))
    return errors


def validate_repository(root: Path) -> list[str]:
    root = root.resolve()
    errors = validate_schema(root)
    feedback_root = root / FEEDBACK_DIR
    if not feedback_root.exists():
        return errors + [f"{FEEDBACK_DIR}: feedback directory does not exist"]
    seen: dict[str, tuple[Path, int]] = {}
    for path in sorted(feedback_root.rglob("*.jsonl")):
        components, organization_errors = organization_components(path, root, feedback_root)
        errors.extend(organization_errors)
        try:
            lines = path.read_text(encoding="utf-8").splitlines()
        except (OSError, UnicodeError) as exc:
            errors.append(f"{relative(path, root)}: cannot read UTF-8 JSONL: {exc}")
            continue
        for line_number, raw in enumerate(lines, 1):
            if not raw.strip():
                continue
            try:
                record = json.loads(raw)
            except json.JSONDecodeError as exc:
                errors.append(report(path, line_number, None,
                                     f"invalid JSON: {exc.msg} at column {exc.colno}", root))
                continue
            errors.extend(validate_observation(record, path, line_number, root))
            errors.extend(validate_record_organization(record, components, path, root,
                                                       line_number))
            if not isinstance(record, dict):
                continue
            observation_id = record.get("id")
            if isinstance(observation_id, str):
                if observation_id in seen:
                    first_path, first_line = seen[observation_id]
                    errors.append(report(
                        path, line_number, observation_id,
                        f"duplicate id; first seen at {relative(first_path, root)}:{first_line}", root))
                else:
                    seen[observation_id] = (path, line_number)
    return errors


def self_test() -> None:
    scratch = ROOT / "tmp"
    scratch.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="agent-feedback-validator-", dir=scratch) as temporary:
        root = Path(temporary)
        feedback_root = root / FEEDBACK_DIR
        feedback_root.mkdir(parents=True)
        shutil.copyfile(ROOT / SCHEMA_PATH, feedback_root / "SCHEMA.json")
        if validate_repository(root):
            raise AssertionError("empty feedback repository should be valid")

        workstream = feedback_root / "phase24" / "team-t-text-encoding" / "validation"
        workstream.mkdir(parents=True)
        observations = workstream / OBSERVATIONS_FILE
        positive = {
            "schema_version": "1.0",
            "id": "phase24-team-t-validation-001",
            "phase": "phase24",
            "project": "team-t-text-encoding",
            "workstream": "validation",
            "agent_role": "independent validator",
            "category": "tooling-positive",
            "task": "Validate feedback records",
            "observation": "File-and-line diagnostics made the invalid record immediate to locate.",
            "impact": "The correction required one edit cycle.",
            "evidence": ["python3 tests/tooling/check_agent_feedback.py --self-test"],
            "resolution": {"status": "resolved", "summary": "Corrected the malformed record."},
            "commit": None,
            "tool_used": "check_agent_feedback.py",
            "tool_helpfulness": "helpful",
        }
        negative = copy.deepcopy(positive)
        negative.update({
            "id": "phase24-team-t-validation-002",
            "category": "tooling-negative",
            "observation": "A deliberately malformed record was rejected without subjective scoring.",
            "resolution": {"status": "explanation", "summary": "The rejection was structurally required."},
            "tool_helpfulness": "unhelpful",
        })

        def check(raw_lines: list[str], expected: str | None = None) -> None:
            observations.write_text("\n".join(raw_lines) + "\n", encoding="utf-8")
            found = validate_repository(root)
            if expected is None and found:
                raise AssertionError(f"valid observations rejected: {found}")
            if expected is not None and not any(expected in message for message in found):
                raise AssertionError(f"expected {expected!r}; got {found}")

        encode = lambda record: json.dumps(record, sort_keys=True)
        check([encode(positive), encode(negative)])
        check(["{not-json"], "invalid JSON")
        missing = copy.deepcopy(positive)
        del missing["impact"]
        check([encode(missing)], "missing required field(s): impact")
        invalid_category = copy.deepcopy(positive)
        invalid_category["category"] = "praise"
        check([encode(invalid_category)], "invalid category 'praise'")
        unsupported_version = copy.deepcopy(positive)
        unsupported_version["schema_version"] = "2.0"
        check([encode(unsupported_version)], "schema_version must be '1.0'")
        check([encode(positive), encode(positive)], "duplicate id")
        unknown = copy.deepcopy(positive)
        unknown["sentiment_score"] = 10
        check([encode(unknown)], "unknown field(s): sentiment_score")
        fabricated_time = copy.deepcopy(positive)
        fabricated_time["time_lost_minutes"] = True
        check([encode(fabricated_time)], "time_lost_minutes must be a nonnegative integer")
        short_commit = copy.deepcopy(positive)
        short_commit["commit"] = "abc123"
        check([encode(short_commit)], "commit must be a full 40-hex Git SHA or null")
        mismatched = copy.deepcopy(positive)
        mismatched["workstream"] = "utf8-codec"
        check([encode(mismatched)], "does not match path component 'validation'")

        misplaced = feedback_root / "phase24" / OBSERVATIONS_FILE
        misplaced.write_text(encode(positive) + "\n", encoding="utf-8")
        observations.write_text("", encoding="utf-8")
        found = validate_repository(root)
        if not any("feedback file must be" in message for message in found):
            raise AssertionError(f"expected invalid path error; got {found}")
        misplaced.unlink()

        wrong_name = workstream / "feedback.jsonl"
        wrong_name.write_text(encode(positive) + "\n", encoding="utf-8")
        found = validate_repository(root)
        if not any("feedback file must be" in message for message in found):
            raise AssertionError(f"expected wrong filename error; got {found}")
        wrong_name.unlink()

        second_workstream = (feedback_root / "phase24" / "team-t-text-encoding" /
                             "utf8-codec")
        second_workstream.mkdir(parents=True)
        second_observations = second_workstream / OBSERVATIONS_FILE
        observations.write_text(encode(positive) + "\n", encoding="utf-8")
        duplicate = copy.deepcopy(positive)
        duplicate["workstream"] = "utf8-codec"
        second_observations.write_text(encode(duplicate) + "\n", encoding="utf-8")
        found = validate_repository(root)
        if not any("duplicate id; first seen at" in message for message in found):
            raise AssertionError(f"expected cross-file duplicate error; got {found}")
        second_observations.unlink()

        example = feedback_root / EXAMPLE_PATH
        example.parent.mkdir(parents=True)
        observations.write_text("", encoding="utf-8")
        example.write_text(encode(positive) + "\n", encoding="utf-8")
        if validate_repository(root):
            raise AssertionError("canonical example path should be structurally validated and path-exempt")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=ROOT,
                        help="repository root to validate (default: current checkout)")
    parser.add_argument("--self-test", action="store_true",
                        help="exercise valid and invalid validator cases")
    args = parser.parse_args()
    if args.self_test:
        self_test()
        print("agent feedback validator self-test passed")
    errors = validate_repository(args.root)
    if errors:
        print("Agent feedback validation failed:", file=sys.stderr)
        print("\n".join(errors), file=sys.stderr)
        return 1
    print("Agent feedback validation passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
