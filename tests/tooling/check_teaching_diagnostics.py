#!/usr/bin/env python3
"""Focused Phase 22.1 compiler-owned teaching-diagnostic regressions."""

from __future__ import annotations

import json
import pathlib
import subprocess
import sys


def fail(message: str) -> None:
    raise SystemExit(f"Phase 22.1 teaching diagnostic test failed: {message}")


if len(sys.argv) != 2:
    fail("usage: check_teaching_diagnostics.py <moss-compiler>")

root = pathlib.Path.cwd().resolve()
compiler = pathlib.Path(sys.argv[1]).resolve()
tasks = root / "benchmarks" / "agent" / "tasks"


def run(*arguments: str, expect: int = 0) -> subprocess.CompletedProcess[bytes]:
    completed = subprocess.run(
        [str(compiler), *arguments],
        cwd=root,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        check=False,
    )
    if completed.returncode != expect:
        fail(
            f"{' '.join(arguments)} exited {completed.returncode}, expected {expect}: "
            + completed.stderr.decode("utf-8", "replace")
        )
    return completed


def document(*arguments: str, expect: int = 0) -> dict[str, object]:
    completed = run(*arguments, expect=expect)
    try:
        return json.loads(completed.stdout)
    except json.JSONDecodeError as error:
        fail(f"{' '.join(arguments)} emitted invalid JSON: {error}")


def task_source(name: str, kind: str = "starter") -> pathlib.Path:
    return tasks / name / kind / "main.moss"


def check_error(name: str, code: str) -> dict[str, object]:
    result = document("check", str(task_source(name)), "--json", expect=1)
    error = result["error"]
    if error["code"] != code:
        fail(f"{name} reported {error['code']}, expected {code}")
    for field in ("source", "rule", "cause", "related", "guidance"):
        if field not in error:
            fail(f"{name} omitted additive field {field}")
    if not error["source"] or error["source"]["line"] <= 0:
        fail(f"{name} omitted its primary Moss source location")
    return error


def check_valid(name: str) -> None:
    result = document("check", str(task_source(name, "expected")), "--json")
    if not result["ok"]:
        fail(f"legal rewrite for {name} was rejected")


self_send = check_error("AB008_repair_self_send", "DOMAIN_SELF_MESSAGE")
if self_send["rule"]["id"] != "domains.no-self-message":
    fail("self-message rule identity drifted")
if self_send["cause"]["kind"] != "self-message":
    fail("self-message cause drifted")
if self_send["guidance"]["kind"] != "local-helper":
    fail("self-message omitted local-helper guidance")
if not any(entity["semantic_identity"] == "handler:Counter.Advance"
           for entity in self_send["cause"]["entities"]):
    fail("self-message omitted the checked target handler")
check_valid("AB008_repair_self_send")

route = check_error("AB010_repair_domain_dag", "DOMAIN_ROUTE_NOT_DECLARED")
if route["cause"]["kind"] != "undeclared-domain-route":
    fail("missing route cause drifted")
if route["guidance"]["kind"] != "declare-domain-route":
    fail("missing route omitted declaration/binding guidance")
if "annotation" in json.dumps(route).lower():
    fail("missing route incorrectly suggested a type annotation")
check_valid("AB010_repair_domain_dag")

cross_domain = check_error(
    "AB021_repair_cross_domain_call", "DOMAIN_HANDLER_REQUIRES_MESSAGE"
)
if cross_domain["guidance"]["kind"] != "use-message":
    fail("cross-domain call omitted message guidance")
if "local-helper" in json.dumps(cross_domain):
    fail("cross-domain call incorrectly suggested a local helper")
check_valid("AB021_repair_cross_domain_call")

ownership = check_error(
    "AB022_repair_effect_conflict", "OWNERSHIP_CONFLICTING_ACCESS"
)
if ownership["rule"]["id"] != "ownership.overlapping-access":
    fail("ownership overlap rule identity drifted")
if ownership["cause"]["kind"] != "overlapping-actual-arguments":
    fail("ownership overlap cause drifted")
arguments = [
    entity for entity in ownership["cause"]["entities"]
    if entity["kind"] == "argument"
]
if [(item["argument_index"], item["expression"], item["access"])
        for item in arguments] != [(1, "value", "WRITE"), (2, "value", "READ")]:
    fail("ownership overlap omitted concrete argument roles")
if len(ownership["related"]) != 2:
    fail("ownership overlap omitted related access locations")
if "copy" in json.dumps(ownership).lower():
    fail("ownership overlap emitted an unsound copy suggestion")
check_valid("AB022_repair_effect_conflict")
for query in ("effects", "ownership"):
    repeated = document(
        query,
        "update_and_read",
        "--source",
        str(task_source("AB022_repair_effect_conflict")),
        "--json",
        expect=1,
    )["error"]
    if (repeated["code"], repeated["rule"], repeated["cause"],
            repeated["guidance"]) != (
                ownership["code"], ownership["rule"], ownership["cause"],
                ownership["guidance"]):
        fail(f"{query} disagreed with check about the ownership conflict")

for task, code, cause, guidance in (
    ("AB018_repair_placeholder", "FUNCTIONAL_PLACEHOLDER_REQUIRED",
     "unsupported-placeholder-expression", "supported-pipeline-placeholder"),
    ("AB019_repair_named_callable", "FUNCTIONAL_CALLABLE_INVOCATION_UNSUPPORTED",
     "invoked-function-in-callable-position", "named-pipeline-callable"),
    ("AB020_repair_pipeline_capture", "FUNCTIONAL_CAPTURE_MUTATION",
     "mutable-pipeline-capture", "pure-pipeline-callback"),
):
    error = check_error(task, code)
    if error["cause"]["kind"] != cause:
        fail(f"{task} cause drifted")
    if error["guidance"]["kind"] != guidance:
        fail(f"{task} guidance drifted")
    stages = [entity for entity in error["cause"]["entities"]
              if entity["kind"] == "pipeline-stage"]
    if not stages or stages[0]["name"] != "map":
        fail(f"{task} omitted the source-level pipeline operation")
    check_valid(task)

unknown = document(
    "check",
    str(root / "tests" / "negative" / "phase221_unknown_pipeline_callable.moss"),
    "--json",
    expect=1,
)["error"]
if unknown["code"] != "FUNCTIONAL_SEMANTIC_ERROR":
    fail("unrelated unknown callable changed diagnostic family")
if unknown["guidance"] is not None:
    fail("unknown callable received unsupported placeholder/capture guidance")

for valid in ("phase221_read_overlap.moss", "phase221_read_capture.moss"):
    result = document("check", str(root / "tests" / valid), "--json")
    if not result["ok"]:
        fail(f"legal negative-guidance control {valid} was rejected")

query_source = root / "tests" / "phase221_teaching_query.moss"
query = document(
    "effects", "Read", "--source", str(query_source), "--json", expect=1
)["error"]
if query["code"] != "QUERY_TARGET_NOT_FOUND":
    fail("bare handler query changed stable diagnostic code")
if query["guidance"]["kind"] != "qualify-query-target":
    fail("bare handler query omitted qualification guidance")
candidates = [item["name"] for item in query["cause"]["entities"]]
if candidates != ["handler:Left.Read", "handler:Right.Read"]:
    fail(f"query candidates are missing or nondeterministic: {candidates}")
for candidate in candidates:
    if not document(
        "effects", candidate, "--source", str(query_source), "--json"
    )["ok"]:
        fail(f"suggested canonical query target {candidate} did not resolve")

missing = document(
    "effects", "Absent", "--source", str(query_source), "--json", expect=1
)["error"]
if missing["guidance"] is not None or missing["cause"] is not None:
    fail("unrelated missing query target received a guessed suggestion")

human = run("check", str(task_source("AB012_repair_write_alias")), expect=1)
text = human.stderr.decode("utf-8", "replace")
for expected in (
    "error[OWNERSHIP_CONFLICTING_ACCESS]",
    "rule: overlapping access",
    "help: pass distinct storage",
    str(task_source("AB012_repair_write_alias")),
):
    if expected not in text:
        fail(f"human teaching diagnostic omitted {expected!r}")

print("Phase 22.1 teaching diagnostic checks passed")
