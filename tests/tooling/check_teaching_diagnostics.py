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


def fixture(name: str) -> pathlib.Path:
    return root / "tests" / name


def check_fixture_error(name: str) -> dict[str, object]:
    result = document("check", str(fixture(name)), "--json", expect=1)
    error = result["error"]
    for field in ("source", "rule", "cause", "related", "guidance"):
        if field not in error:
            fail(f"{name} omitted additive field {field}")
    return error


def check_fixture_valid(name: str) -> None:
    result = document("check", str(fixture(name)), "--json")
    if not result["ok"]:
        fail(f"legal control {name} was rejected")


def actual_identity(entity: dict[str, object], prefix: str) -> bool:
    identity = entity.get("semantic_identity")
    return isinstance(identity, str) and identity.startswith(prefix) and "@" in identity


self_send = check_error("AB008_repair_self_send", "DOMAIN_SELF_MESSAGE")
if self_send["rule"]["id"] != "domains.no-self-message":
    fail("self-message rule identity drifted")
if self_send["cause"]["kind"] != "self-message":
    fail("self-message cause drifted")
if self_send["guidance"]["kind"] != "local-helper":
    fail("self-message omitted local-helper guidance")
if not any(actual_identity(entity, "handler:Counter.Advance@")
           for entity in self_send["cause"]["entities"]):
    fail("self-message omitted the checked target handler identity")
check_valid("AB008_repair_self_send")

same_instance = check_fixture_error("negative/phase221_same_domain_handler.moss")
if same_instance["code"] != "DOMAIN_SAME_INSTANCE_MESSAGE":
    fail("same-instance handler message lost its specific diagnostic")
if same_instance["guidance"]["kind"] != "local-helper":
    fail("same-instance handler message omitted local-helper guidance")
if not any(actual_identity(entity, "handler:Worker.Read@")
           for entity in same_instance["cause"]["entities"]):
    fail("same-instance message omitted the resolved handler identity")

for source in (
    "negative/phase221_missing_domain_handler.moss",
    "negative/phase221_missing_self_handler.moss",
):
    error = check_fixture_error(source)
    if error["code"] in (
        "DOMAIN_HANDLER_REQUIRES_MESSAGE", "DOMAIN_SELF_MESSAGE",
        "DOMAIN_SAME_INSTANCE_MESSAGE",
    ):
        fail(f"{source} taught a domain rewrite before resolving the handler")
    if error["guidance"] is not None:
        fail(f"{source} received guidance for a nonexistent handler")
    if "handler:Worker.DoesNotExist" in json.dumps(error):
        fail(f"{source} fabricated a handler semantic entity")
check_fixture_valid("phase221_object_method_control.moss")

route = check_error("AB010_repair_domain_dag", "DOMAIN_ROUTE_NOT_DECLARED")
if route["cause"]["kind"] != "undeclared-domain-route":
    fail("missing route cause drifted")
if route["guidance"]["kind"] != "declare-domain-route":
    fail("missing route omitted declaration/binding guidance")
if "annotation" in json.dumps(route).lower():
    fail("missing route incorrectly suggested a type annotation")
route_entities = {entity["kind"]: entity for entity in route["cause"]["entities"]}
if not actual_identity(route_entities["domain"], "domain:Inventory@"):
    fail("missing route omitted the actual enclosing domain identity")
if not actual_identity(route_entities["handler"], "handler:Ledger.Read@"):
    fail("missing route omitted the actual resolved handler identity")
if route_entities["route"]["semantic_identity"] is not None:
    fail("missing route fabricated an identity for an unresolved route")
check_valid("AB010_repair_domain_dag")

cross_domain = check_error(
    "AB021_repair_cross_domain_call", "DOMAIN_HANDLER_REQUIRES_MESSAGE"
)
if cross_domain["guidance"]["kind"] != "use-message":
    fail("cross-domain call omitted message guidance")
if "local-helper" in json.dumps(cross_domain):
    fail("cross-domain call incorrectly suggested a local helper")
if not any(actual_identity(entity, "handler:Worker.Read@")
           for entity in cross_domain["cause"]["entities"]):
    fail("cross-domain call omitted the resolved handler identity")
domain_instances = [entity for entity in cross_domain["cause"]["entities"]
                    if entity["kind"] == "domain-instance"]
if len(domain_instances) != 1 or domain_instances[0]["semantic_identity"] != "main::worker":
    fail("cross-domain call omitted the actual concrete instance identity")
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
if not any(actual_identity(entity, "fn:update_and_read@")
           for entity in ownership["cause"]["entities"]):
    fail("ownership overlap omitted the actual function identity")
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

compatible = check_error(
    "AB019_repair_named_callable", "FUNCTIONAL_CALLABLE_INVOCATION_UNSUPPORTED"
)
if not any(actual_identity(entity, "fn:double@")
           for entity in compatible["cause"]["entities"]):
    fail("compatible invoked callable omitted the actual function identity")

for source in (
    "negative/phase221_callable_wrong_arity.moss",
    "negative/phase221_callable_wrong_type.moss",
):
    error = check_fixture_error(source)
    if error["code"] == "FUNCTIONAL_CALLABLE_INVOCATION_UNSUPPORTED":
        fail(f"{source} received an invalid remove-parentheses rewrite")
    if error["guidance"] is not None or "without parentheses" in json.dumps(error):
        fail(f"{source} received unsound named-callable guidance")
check_fixture_valid("phase221_inference_legal.moss")

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
candidate_entities = query["cause"]["entities"]
candidates = [item["name"] for item in candidate_entities]
if candidates != ["handler:Left.Read", "handler:Right.Read"]:
    fail(f"query candidates are missing or nondeterministic: {candidates}")
for candidate in candidate_entities:
    if not actual_identity(candidate, candidate["name"] + "@"):
        fail(f"query candidate lacks its actual semantic identity: {candidate}")
    if str(candidate["semantic_identity"]).startswith("entity-v1:"):
        fail("query candidate placed a durable identity in semantic_identity")
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

for source, rule, cause, guidance in (
    ("negative/phase221_reduce_seed_inference.moss", "types.reduce-seed",
     "unresolved-reduce-seed", "use-statically-typed-expression"),
    ("negative/phase221_callable_result_inference.moss",
     "types.functional-callable-result", "unresolved-functional-callable-result",
     "use-statically-typed-callable"),
    ("negative/phase221_return_inference.moss", "types.return-expression",
     "unresolved-return-expression", "use-statically-typed-expression"),
    ("negative/phase221_reply_inference.moss", "types.reply-expression",
     "unresolved-reply-expression", "use-statically-typed-expression"),
):
    error = check_fixture_error(source)
    if error["code"] != "TYPE_INFERENCE_FAILED":
        fail(f"{source} reported {error['code']}, expected TYPE_INFERENCE_FAILED")
    if error["rule"]["id"] != rule or error["cause"]["kind"] != cause:
        fail(f"{source} omitted its concrete inference rule/cause")
    if error["guidance"]["kind"] != guidance:
        fail(f"{source} omitted legal inference guidance")
    expressions = [entity for entity in error["cause"]["entities"]
                   if entity.get("expression")]
    if not expressions or not expressions[0]["expression"]:
        fail(f"{source} omitted the unresolved source expression")

callable_result = check_fixture_error(
    "negative/phase221_callable_result_inference.moss"
)
if not any(actual_identity(entity, "fn:missing_result@")
           for entity in callable_result["cause"]["entities"]):
    fail("callable-result inference omitted the resolved function identity")

return_error = check_fixture_error("negative/phase221_return_inference.moss")
if not any(actual_identity(entity, "fn:broken@")
           for entity in return_error["cause"]["entities"]):
    fail("return inference omitted the resolved function identity")
if not any(entity["kind"] == "expected-type" and entity["name"] == "int"
           for entity in return_error["cause"]["entities"]):
    fail("return inference omitted the expected type")

reply_error = check_fixture_error("negative/phase221_reply_inference.moss")
if not any(actual_identity(entity, "handler:Worker.Read@")
           for entity in reply_error["cause"]["entities"]):
    fail("reply inference omitted the resolved handler identity")

unsupported = check_fixture_error(
    "negative/phase221_unsupported_return_operator.moss"
)
if unsupported["code"] == "TYPE_INFERENCE_FAILED":
    fail("unsupported operator was misreported as an inference problem")
if unsupported["guidance"] is not None:
    fail("unsupported operator received misleading inference guidance")
if "annotation" in json.dumps(unsupported).lower():
    fail("unsupported operator suggested an unavailable annotation")

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
