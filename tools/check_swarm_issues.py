#!/usr/bin/env python3
"""Validate the canonical Moss dogfood issue classification database."""

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


ROOT = Path(__file__).resolve().parents[1]
ISSUES_PATH = Path("examples/swarm/ISSUES.jsonl")
SCHEMA_PATH = Path("examples/swarm/issue_tracking/schema.json")
README_PATH = Path("examples/swarm/issue_tracking/README.md")
FINDINGS_PATH = Path("examples/swarm/FINDINGS.md")

SEMANTIC_CATEGORIES = (
    "false_acceptance",
    "ambiguous_spec",
    "lost_semantics",
    "missing_expressiveness",
)
TRACKING_SCOPES = ("semantic", "tooling")
STATUSES = ("open", "decision_needed", "deferred", "fixed", "rejected")
DISPOSITIONS = (
    "reject_earlier",
    "specify_then_fix",
    "preserve_semantics",
    "design_needed",
    "defer",
    "tooling_only",
)
SOURCE_KINDS = ("swarm", "cross_swarm")
REQUIRED_FIELDS = (
    "issue_id",
    "tracking_scope",
    "category",
    "title",
    "status",
    "area",
    "swarm_ids",
    "source_kind",
    "disposition",
    "target_phase",
    "related_issue_ids",
    "notes",
)
ISSUE_ID_RE = re.compile(r"^(SWARM|EXPRESS)-([0-9]+)$")
SWARM_ID_RE = re.compile(r"^SWARM-[0-9]+$")
FINDING_HEADING_RE = re.compile(r"^##\s+(SWARM-[0-9]+)\b")
STATUS_RE = re.compile(r"^-\s+Status:\s*(.+?)\s*$")


def relative(path: Path, root: Path) -> str:
    try:
        return path.relative_to(root).as_posix()
    except ValueError:
        return str(path)


def report(path: Path, line: int, issue_id: str | None, reason: str, root: Path) -> str:
    identity = f" [{issue_id}]" if issue_id else ""
    return f"{relative(path, root)}:{line}{identity}: {reason}"


def enum_reason(field: str, value: Any, choices: tuple[str, ...]) -> str:
    return f"invalid {field} {value!r}; expected one of {', '.join(choices)}"


def has_duplicates(values: list[Any]) -> bool:
    """Detect duplicates without assuming malformed JSON values are hashable."""
    encoded = [json.dumps(value, sort_keys=True, separators=(",", ":")) for value in values]
    return len(encoded) != len(set(encoded))


def validate_record(record: Any, path: Path, line: int, root: Path) -> list[str]:
    if not isinstance(record, dict):
        return [report(path, line, None, "issue must be a JSON object", root)]

    raw_issue_id = record.get("issue_id")
    issue_id = raw_issue_id if isinstance(raw_issue_id, str) else None
    errors: list[str] = []
    missing = [field for field in REQUIRED_FIELDS if field not in record]
    if missing:
        errors.append(report(path, line, issue_id,
                             f"missing required field(s): {', '.join(missing)}", root))
    unknown = sorted(set(record) - set(REQUIRED_FIELDS))
    if unknown:
        errors.append(report(path, line, issue_id,
                             f"unknown field(s): {', '.join(unknown)}", root))

    if not isinstance(raw_issue_id, str) or not ISSUE_ID_RE.fullmatch(raw_issue_id):
        errors.append(report(path, line, issue_id,
                             "issue_id must match SWARM-N or EXPRESS-N", root))
    for field in ("title", "area", "notes"):
        if not isinstance(record.get(field), str) or not record[field]:
            errors.append(report(path, line, issue_id,
                                 f"{field} must be a nonempty string", root))

    scope = record.get("tracking_scope")
    if scope not in TRACKING_SCOPES:
        errors.append(report(path, line, issue_id,
                             enum_reason("tracking_scope", scope, TRACKING_SCOPES), root))
    category = record.get("category")
    if scope == "semantic" and category not in SEMANTIC_CATEGORIES:
        errors.append(report(path, line, issue_id,
                             enum_reason("category", category, SEMANTIC_CATEGORIES), root))
    if scope == "tooling" and category is not None:
        errors.append(report(path, line, issue_id,
                             "tooling-only entries must use category null", root))

    for field, choices in (("status", STATUSES), ("disposition", DISPOSITIONS),
                           ("source_kind", SOURCE_KINDS)):
        if record.get(field) not in choices:
            errors.append(report(path, line, issue_id,
                                 enum_reason(field, record.get(field), choices), root))
    target_phase = record.get("target_phase")
    if target_phase is not None and (not isinstance(target_phase, str) or not target_phase):
        errors.append(report(path, line, issue_id,
                             "target_phase must be a nonempty string or null", root))

    swarm_ids = record.get("swarm_ids")
    if not isinstance(swarm_ids, list):
        errors.append(report(path, line, issue_id, "swarm_ids must be an array", root))
        swarm_ids = []
    elif has_duplicates(swarm_ids):
        errors.append(report(path, line, issue_id, "swarm_ids must not contain duplicates", root))
    for swarm_id in swarm_ids:
        if not isinstance(swarm_id, str) or not SWARM_ID_RE.fullmatch(swarm_id):
            errors.append(report(path, line, issue_id,
                                 f"invalid swarm reference {swarm_id!r}", root))

    related = record.get("related_issue_ids")
    if not isinstance(related, list):
        errors.append(report(path, line, issue_id,
                             "related_issue_ids must be an array", root))
        related = []
    elif has_duplicates(related):
        errors.append(report(path, line, issue_id,
                             "related_issue_ids must not contain duplicates", root))
    for related_id in related:
        if not isinstance(related_id, str) or not ISSUE_ID_RE.fullmatch(related_id):
            errors.append(report(path, line, issue_id,
                                 f"invalid related issue reference {related_id!r}", root))

    if isinstance(raw_issue_id, str) and raw_issue_id.startswith("SWARM-"):
        if swarm_ids != [raw_issue_id]:
            errors.append(report(
                path, line, issue_id,
                "SWARM issue record must use swarm_ids containing exactly its own issue_id",
                root))
        if record.get("source_kind") != "swarm":
            errors.append(report(path, line, issue_id,
                                 "SWARM issue_id must use source_kind swarm", root))
    elif isinstance(raw_issue_id, str) and raw_issue_id.startswith("EXPRESS-"):
        if swarm_ids:
            errors.append(report(path, line, issue_id,
                                 "EXPRESS issue_id must have an empty swarm_ids array", root))
        if record.get("source_kind") != "cross_swarm":
            errors.append(report(path, line, issue_id,
                                 "EXPRESS issue_id must use source_kind cross_swarm", root))

    if category == "false_acceptance" and record.get("disposition") != "reject_earlier":
        errors.append(report(path, line, issue_id,
                             "false_acceptance must use disposition reject_earlier", root))
    if scope == "tooling" and record.get("disposition") != "tooling_only":
        errors.append(report(path, line, issue_id,
                             "tooling-only entries must use disposition tooling_only", root))
    return errors


def validate_schema(root: Path) -> list[str]:
    path = root / SCHEMA_PATH
    try:
        schema = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as exc:
        return [f"{SCHEMA_PATH}: invalid schema.json: {exc}"]
    properties = schema.get("properties", {})
    errors: list[str] = []
    if schema.get("required") != list(REQUIRED_FIELDS):
        errors.append(f"{SCHEMA_PATH}: required fields disagree with validator")
    if properties.get("tracking_scope", {}).get("enum") != list(TRACKING_SCOPES):
        errors.append(f"{SCHEMA_PATH}: tracking_scope enum disagrees with validator")
    category_enum = properties.get("category", {}).get("anyOf", [{}])[0].get("enum")
    if category_enum != list(SEMANTIC_CATEGORIES):
        errors.append(f"{SCHEMA_PATH}: category enum disagrees with validator")
    if properties.get("status", {}).get("enum") != list(STATUSES):
        errors.append(f"{SCHEMA_PATH}: status enum disagrees with validator")
    if properties.get("disposition", {}).get("enum") != list(DISPOSITIONS):
        errors.append(f"{SCHEMA_PATH}: disposition enum disagrees with validator")
    if properties.get("source_kind", {}).get("enum") != list(SOURCE_KINDS):
        errors.append(f"{SCHEMA_PATH}: source_kind enum disagrees with validator")
    return errors


def finding_ids_and_open_ids(findings: str) -> tuple[set[str], set[str]]:
    finding_ids: set[str] = set()
    open_ids: set[str] = set()
    current_id: str | None = None

    for line in findings.splitlines():
        heading = FINDING_HEADING_RE.match(line)
        if heading:
            current_id = heading.group(1)
            finding_ids.add(current_id)
            continue
        if current_id is None:
            continue
        status = STATUS_RE.match(line)
        if status and status.group(1).startswith("Open"):
            open_ids.add(current_id)
    return finding_ids, open_ids


def validate_repository(root: Path) -> list[str]:
    errors = validate_schema(root)
    issues_path = root / ISSUES_PATH
    findings_path = root / FINDINGS_PATH
    try:
        raw_lines = issues_path.read_text(encoding="utf-8").splitlines()
    except OSError as exc:
        return errors + [f"{ISSUES_PATH}: cannot read issue database: {exc}"]
    try:
        findings = findings_path.read_text(encoding="utf-8")
    except OSError as exc:
        return errors + [f"{FINDINGS_PATH}: cannot read findings ledger: {exc}"]

    finding_ids, open_finding_ids = finding_ids_and_open_ids(findings)
    records: list[dict[str, Any]] = []
    seen: dict[str, tuple[Path, int]] = {}
    issue_id_counts: dict[str, int] = {}
    for line_number, raw in enumerate(raw_lines, 1):
        if not raw.strip():
            continue
        try:
            record = json.loads(raw)
        except json.JSONDecodeError as exc:
            errors.append(report(issues_path, line_number, None,
                                 f"invalid JSON: {exc.msg}", root))
            continue
        errors.extend(validate_record(record, issues_path, line_number, root))
        if not isinstance(record, dict):
            continue
        issue_id = record.get("issue_id")
        if isinstance(issue_id, str):
            issue_id_counts[issue_id] = issue_id_counts.get(issue_id, 0) + 1
            if issue_id in seen:
                first_path, first_line = seen[issue_id]
                errors.append(report(
                    issues_path, line_number, issue_id,
                    f"duplicate issue_id; first seen at {relative(first_path, root)}:{first_line}", root))
            else:
                seen[issue_id] = (issues_path, line_number)
            records.append(record)

    express_numbers: list[int] = []
    for record in records:
        issue_id = record.get("issue_id")
        swarm_ids = record.get("swarm_ids")
        if not isinstance(swarm_ids, list):
            swarm_ids = []
        related_ids = record.get("related_issue_ids")
        if not isinstance(related_ids, list):
            related_ids = []
        references = list(swarm_ids) + [
            item for item in related_ids
            if isinstance(item, str) and item.startswith("SWARM-")
        ]
        if isinstance(issue_id, str) and issue_id.startswith("SWARM-"):
            references.append(issue_id)
        for reference in references:
            if reference not in finding_ids:
                errors.append(f"{ISSUES_PATH}: {reference} does not exist in {FINDINGS_PATH}")
        if isinstance(issue_id, str) and issue_id.startswith("EXPRESS-"):
            match = ISSUE_ID_RE.fullmatch(issue_id)
            if match:
                express_numbers.append(int(match.group(2)))

        for related_id in related_ids:
            if isinstance(related_id, str) and related_id not in seen:
                errors.append(f"{ISSUES_PATH}: {related_id} does not exist in issue database")

    if express_numbers != sorted(set(express_numbers)):
        errors.append(f"{ISSUES_PATH}: EXPRESS IDs must be unique and monotonic")

    for swarm_id in sorted(open_finding_ids):
        count = issue_id_counts.get(swarm_id, 0)
        if count == 0:
            errors.append(
                f"{FINDINGS_PATH}: {swarm_id} is Open but has no issue_id record in {ISSUES_PATH}")
        elif count > 1:
            errors.append(
                f"{FINDINGS_PATH}: {swarm_id} is Open but has multiple issue_id records "
                f"in {ISSUES_PATH}")
    return errors


def self_test() -> None:
    with tempfile.TemporaryDirectory(prefix="moss-swarm-issues-") as temporary:
        root = Path(temporary)
        (root / SCHEMA_PATH).parent.mkdir(parents=True)
        (root / ISSUES_PATH).parent.mkdir(parents=True, exist_ok=True)
        (root / FINDINGS_PATH).parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(ROOT / SCHEMA_PATH, root / SCHEMA_PATH)
        (root / FINDINGS_PATH).write_text(
            "## SWARM-001 — test finding\n\n"
            "- Status: Open\n\n"
            "## SWARM-002 — missing classification\n\n"
            "- Status: Fixed\n",
            encoding="utf-8")
        valid = {
            "issue_id": "SWARM-001", "tracking_scope": "semantic",
            "category": "false_acceptance", "title": "test", "status": "open",
            "area": "frontend_validation", "swarm_ids": ["SWARM-001"],
            "source_kind": "swarm", "disposition": "reject_earlier",
            "target_phase": None, "related_issue_ids": [], "notes": "test",
        }
        issues = root / ISSUES_PATH

        def check(records: list[dict[str, Any]], expected: str | None = None) -> None:
            issues.write_text("\n".join(json.dumps(item) for item in records) + "\n", encoding="utf-8")
            errors = validate_repository(root)
            if expected is None and errors:
                raise AssertionError(f"valid issue rejected: {errors}")
            if expected is not None and not any(expected in message for message in errors):
                raise AssertionError(f"expected {expected!r}; got {errors}")

        check([valid])
        duplicate = copy.deepcopy(valid)
        check([valid, duplicate], "duplicate issue_id")
        bad_category = copy.deepcopy(valid)
        bad_category["category"] = "language_gap"
        check([bad_category], "invalid category 'language_gap'")
        bad_tooling = copy.deepcopy(valid)
        bad_tooling.update({"tracking_scope": "tooling", "category": "lost_semantics",
                            "disposition": "tooling_only"})
        check([bad_tooling], "tooling-only entries must use category null")
        bad_disposition = copy.deepcopy(valid)
        bad_disposition["disposition"] = "design_needed"
        check([bad_disposition], "false_acceptance must use disposition reject_earlier")
        bad_reference = copy.deepcopy(valid)
        bad_reference["swarm_ids"] = ["SWARM-999"]
        check([bad_reference], "SWARM-999 does not exist")

        # An open SWARM must have its own canonical issue_id record. Merely naming
        # that SWARM in another record's swarm_ids must not satisfy completeness.
        (root / FINDINGS_PATH).write_text(
            "## SWARM-001 — test finding\n\n"
            "- Status: Open\n\n"
            "## SWARM-002 — missing classification\n\n"
            "- Status: Open\n",
            encoding="utf-8")
        wrong_cover = copy.deepcopy(valid)
        wrong_cover["swarm_ids"] = ["SWARM-001", "SWARM-002"]
        check([wrong_cover], "SWARM-002 is Open but has no issue_id record")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=ROOT, help="repository root to validate")
    parser.add_argument("--self-test", action="store_true", help="exercise validator acceptance and rejection cases")
    args = parser.parse_args()
    if args.self_test:
        self_test()
        print("swarm issue validator self-test passed")
    errors = validate_repository(args.root.resolve())
    if errors:
        print("Swarm issue validation failed:", file=sys.stderr)
        print("\n".join(errors), file=sys.stderr)
        return 1
    print("Swarm issue validation passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
