#!/usr/bin/env python3
"""Phase 22.4 structured Fast Debug trace slicing regressions."""

from __future__ import annotations

import json
import pathlib
import subprocess
import sys


def fail(message: str) -> None:
    raise SystemExit(f"Phase 22.4 debug query test failed: {message}")


if len(sys.argv) != 2:
    fail("usage: check_phase224_debug_query.py <moss-compiler>")

root = pathlib.Path.cwd().resolve()
compiler = pathlib.Path(sys.argv[1]).resolve()
basic = root / "tests" / "phase10_interpreter_basic.moss"
failure = root / "tests" / "phase224_debug_failure.moss"
domain = root / "tests" / "phase224_debug_domain.moss"
nested = root / "tests" / "phase224_debug_nested.moss"
specialization = root / "tests" / "phase224_debug_specialization.moss"
module_source = root / "examples" / "projects" / "phase10_modules" / "src" / "main.moss"


def invoke(*args: str, expect: int = 0) -> dict[str, object]:
    completed = subprocess.run(
        [str(compiler), *args],
        cwd=root,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        check=False,
    )
    if completed.returncode != expect:
        fail(
            f"{' '.join(args)} exited {completed.returncode}, expected {expect}: "
            + completed.stderr
            + completed.stdout
        )
    try:
        document = json.loads(completed.stdout)
    except json.JSONDecodeError as error:
        fail(f"{' '.join(args)} did not emit JSON: {error}\n{completed.stdout}")
    for field in ("protocol_version", "schema_version", "compiler_version", "command", "ok"):
        if field not in document:
            fail(f"debug query omitted envelope field {field}")
    return document


def query(operation: str, selector: str | None, source: pathlib.Path, *extra: str,
          expect: int = 0) -> dict[str, object]:
    args = ["debug-query", operation]
    if selector is not None:
        args.append(selector)
    args += ["--source", str(source), "--json", *extra]
    return invoke(*args, expect=expect)


def require_error(document: dict[str, object], code: str, context: str) -> None:
    actual = document["error"]["code"]
    if actual != code:
        fail(f"{context} used {actual}, expected {code}")


trace_a = subprocess.run(
    [str(compiler), "run", "--interp", "--trace", str(basic)],
    cwd=root,
    text=True,
    stdout=subprocess.PIPE,
    stderr=subprocess.PIPE,
    check=False,
)
trace_b = subprocess.run(
    [str(compiler), "run", "--interp", "--trace", str(basic)],
    cwd=root,
    text=True,
    stdout=subprocess.PIPE,
    stderr=subprocess.PIPE,
    check=False,
)
if trace_a.returncode != 0 or trace_b.returncode != 0:
    fail("baseline traced Fast Debug run failed")
if trace_a.stderr != trace_b.stderr:
    fail("trace output is not deterministic")
events = [json.loads(line) for line in trace_a.stderr.splitlines()]
if [event["event_id"] for event in events] != list(range(1, len(events) + 1)):
    fail("trace event IDs are not deterministic sequential IDs")
if not all("depth" in event for event in events):
    fail("trace events omitted depth")
if not any(event.get("parent_event_id") for event in events):
    fail("trace events omitted causal parent links")

single = query("event", "event:1", basic)["result"]
if single["events"][0]["event_id"] != 1:
    fail("event selector did not return event 1")

around = query(
    "event", "event:7", basic, "--before", "2", "--after", "1",
    "--max-events", "4",
)["result"]
if [event["event_id"] for event in around["events"]] != [5, 6, 7, 8]:
    fail("before/after bounds did not return the requested event context")
if around["truncated"] or around["available_more"]:
    fail("exact before/after max-events boundary was reported as truncated")
around_truncated = query(
    "event", "event:7", basic, "--before", "2", "--after", "1",
    "--max-events", "3",
)["result"]
if not around_truncated["truncated"] or around_truncated["returned_events"] != 3:
    fail("before/after context did not report an actually omitted event")

semantic = query("semantic", "entity-v1:function:transform", basic, "--max-events", "3")["result"]
if not semantic["truncated"] or semantic["returned_events"] != 3:
    fail("semantic query did not report explicit truncation")
if semantic["events"][0]["semantic_identity"] != "fn:transform@1":
    fail("entity-v1 semantic selector did not bridge to runtime source identity")

all_transform = query(
    "semantic", "entity-v1:function:transform", basic, "--max-events", "100"
)["result"]
if sum(event["event"] == "FunctionEnter" for event in all_transform["events"]) < 2:
    fail("semantic selection did not preserve repeated executions")

candidate_count = all_transform["returned_events"]
for limit, expected_truncated in (
    (candidate_count + 1, False),
    (candidate_count, False),
    (candidate_count - 1, True),
):
    bounded = query(
        "semantic", "entity-v1:function:transform", basic,
        "--max-events", str(limit),
    )["result"]
    if bounded["truncated"] is not expected_truncated:
        fail(f"semantic truncation was wrong at limit {limit}")
    if bounded["available_more"] is not expected_truncated:
        fail(f"semantic available_more was wrong at limit {limit}")
    if bounded["returned_events"] != min(candidate_count, limit):
        fail(f"semantic returned_events was wrong at limit {limit}")

control = query("control-flow", "event:7", basic)["result"]
if control["control_path"][0]["event"] != "BranchTaken":
    fail("control-flow query omitted the enclosing branch")

writes = query("writes", "local:y", basic, "--before-event", "7")["result"]
if writes["writes"][0]["event"] != "LocalWrite" or writes["writes"][0]["after"] != "12":
    fail("last-write query did not return the preceding local write")

no_writes = query("writes", "local:not_written", basic)["result"]
if no_writes["writes"]:
    fail("write query fabricated a write for an unwritten local")

failure_slice = query("failure-slice", None, failure, "--max-events", "20")["result"]
if failure_slice["execution"]["status"] != "failed":
    fail("failure-slice did not report failed execution")
if not any(event["event"] == "AssertionFailure" for event in failure_slice["events"]):
    fail("failure slice omitted the assertion failure")
if not failure_slice.get("control_path"):
    fail("failure slice omitted branch/loop control path")
if not failure_slice["truncated"] or not failure_slice["available_more"]:
    fail("bounded failure slice did not report omitted lead-up events")

loop_event = next(
    event for event in failure_slice["events"]
    if event["event"] == "LocalWrite" and event["detail"] == "total"
)
loop_path = query("control-flow", f"event:{loop_event['event_id']}", failure)["result"]
if not any(event["event"] == "LoopIteration" for event in loop_path["control_path"]):
    fail("control path omitted repeated loop iteration context")

full_failure = query("failure-slice", None, failure, "--max-events", "1000")["result"]
loop_ids = [
    event["event_id"] for event in full_failure["events"]
    if event["event"] == "LoopIteration"
]
if len(loop_ids) < 2 or len(loop_ids) != len(set(loop_ids)):
    fail("loop iterations did not retain distinct event identities")
if not any(event.get("control_event_id") in loop_ids for event in full_failure["events"]):
    fail("loop body events did not retain their iteration control identity")

static_calls = invoke(
    "calls", "main", "--source", str(domain), "--json"
)["result"]["direct_calls"]
message_edge = next(
    (edge for edge in static_calls
     if edge["boundary"] == "synchronous_message_by_value"), None
)
if not message_edge or message_edge["target"] != "handler:Service.Run":
    fail("static calls query omitted the domain/message relationship")
domain_slice = query(
    "message-subtree", message_edge["target_id"], domain, "--max-events", "50"
)["result"]
event_names = [event["event"] for event in domain_slice["events"]]
if "message_call" not in event_names or "handler_enter" not in event_names or "message_return" not in event_names:
    fail("message subtree did not include synchronous message structure")
if not any(event.get("message_event_id") for event in domain_slice["events"]):
    fail("message subtree omitted message causal links")

state_writes = query("writes", "balance", domain, "--before-event", "20")["result"]
if not state_writes["writes"] or state_writes["writes"][0]["path"] != "balance":
    fail("state write query did not return domain state history")

all_state_writes = query("writes", "balance", domain, "--max-events", "10")["result"]
if all_state_writes["returned_events"] != 2:
    fail("state write query omitted write history")
for limit, expected_truncated in ((3, False), (2, False), (1, True)):
    bounded = query("writes", "balance", domain, "--max-events", str(limit))["result"]
    if bounded["truncated"] is not expected_truncated:
        fail(f"write truncation was wrong at limit {limit}")

middle_tree = query(
    "subtree", "entity-v1:function:middle", nested, "--max-events", "100"
)["result"]
middle_enter = next(event for event in middle_tree["events"] if event["event"] == "FunctionEnter")
leaf_enter = next(
    event for event in middle_tree["events"]
    if event["event"] == "FunctionEnter" and event["function"] == "leaf"
)
if leaf_enter["parent_event_id"] != middle_enter["event_id"]:
    fail("nested function call did not use the dynamic caller event as parent")
leaf_body = next(
    event for event in middle_tree["events"]
    if event["function"] == "leaf" and event["event"] == "LocalWrite"
)
if leaf_body["call_event_id"] != leaf_enter["event_id"]:
    fail("nested function body did not retain its call event")

depth_limited = query(
    "subtree", "entity-v1:function:middle", nested,
    "--max-events", "100", "--max-depth", "0",
)["result"]
if not depth_limited["truncated"] or depth_limited["returned_events"] != 1:
    fail("max-depth did not report actual omitted descendants")

nested_messages = query(
    "message-subtree", "entity-v1:handler:Outer.Run", nested,
    "--max-events", "100", "--max-depth", "20",
)["result"]
nested_events = nested_messages["events"]
message_calls = [event for event in nested_events if event["event"] == "message_call"]
handler_enters = [event for event in nested_events if event["event"] == "handler_enter"]
message_returns = [event for event in nested_events if event["event"] == "message_return"]
if len(message_calls) != 3 or len(handler_enters) != 3 or len(message_returns) != 3:
    fail("nested message subtree did not preserve all synchronous levels")
for message in message_calls:
    if not any(
        handler["parent_event_id"] == message["event_id"]
        for handler in handler_enters
    ):
        fail("nested handler entry was not parented by its message call")
    if not any(
        returned["parent_event_id"] == message["event_id"]
        and returned["message_event_id"] == message["event_id"]
        for returned in message_returns
    ):
        fail("nested message return lost its call parentage")

ambiguous_local = query("writes", "local:scratch", nested, expect=1)
require_error(
    ambiguous_local, "DEBUG_QUERY_SELECTOR_AMBIGUOUS", "ambiguous local selector"
)
ambiguous_state = query("writes", "total", nested, expect=1)
require_error(
    ambiguous_state, "DEBUG_QUERY_SELECTOR_AMBIGUOUS", "ambiguous state selector"
)
left_state = query("writes", "main::left.total", nested)["result"]
right_state = query("writes", "main::right.total", nested)["result"]
if {event["instance"] for event in left_state["writes"]} != {"main::left"}:
    fail("qualified state selector did not isolate the left instance")
if {event["instance"] for event in right_state["writes"]} != {"main::right"}:
    fail("qualified state selector did not isolate the right instance")

no_previous = query(
    "writes", "main::left.total", nested, "--before-event", "29"
)["result"]
if no_previous["writes"]:
    fail("before-event filtering fabricated a previous state write")

int_specialization = query(
    "semantic", "entity-v1:specialization:identity<int>", specialization
)["result"]
bool_specialization = query(
    "semantic", "entity-v1:specialization:identity<bool>", specialization
)["result"]
if {event["specialization_identity"] for event in int_specialization["events"]} != {
    "fn:identity<int>@1"
}:
    fail("Int specialization identity did not bridge to runtime events")
if {event["specialization_identity"] for event in bool_specialization["events"]} != {
    "fn:identity<bool>@1"
}:
    fail("Bool specialization identity did not bridge to runtime events")

handler_identity = query(
    "semantic", "entity-v1:handler:Outer.Run", nested, "--max-events", "100"
)["result"]
if not handler_identity["events"] or {
    event["semantic_identity"] for event in handler_identity["events"]
} != {"handler:Outer.Run@29"}:
    fail("handler durable identity did not bridge to runtime events")

unknown_event = query("event", "event:999999", basic, expect=1)
require_error(unknown_event, "DEBUG_QUERY_EVENT_NOT_FOUND", "unknown event ID")

for malformed_selector in (
    "event:not-an-int", "event:", "event:0", "event:-1",
    "event:999999999999999999999999",
):
    malformed = query("event", malformed_selector, basic, expect=2)
    require_error(malformed, "DEBUG_QUERY_SELECTOR_INVALID", "malformed selector")

unknown_semantic = query("semantic", "entity-v1:function:not_present", basic, expect=1)
require_error(
    unknown_semantic, "DEBUG_QUERY_TARGET_NOT_FOUND", "unknown semantic identity"
)

not_executed = query("semantic", "entity-v1:function:unused_value", failure, expect=1)
require_error(not_executed, "DEBUG_QUERY_NOT_EXECUTED", "unexecuted identity")

not_executed_nested = query(
    "subtree", "entity-v1:function:unused_nested", nested, expect=1
)
require_error(
    not_executed_nested, "DEBUG_QUERY_NOT_EXECUTED", "unexecuted subtree identity"
)

module_identity = query(
    "semantic", "entity-v1:function:First__answer", module_source, "--max-events", "2"
)["result"]
if module_identity["events"][0]["semantic_identity"] != "fn:First__answer@3":
    fail("multi-module semantic identity did not select the runtime event")
if not module_identity["events"][0]["source_file"].endswith("src/first.moss"):
    fail("multi-module runtime event lost its source/module provenance")

unsupported = query("provenance", "local:y", basic, expect=2)
require_error(
    unsupported, "DEBUG_QUERY_UNSUPPORTED_OPERATION", "unsupported operation"
)

no_failure = query("failure-slice", None, basic, expect=1)
require_error(no_failure, "DEBUG_QUERY_NO_FAILURE", "failure-free execution")

missing_operation = invoke("debug-query", "--json", expect=2)
require_error(
    missing_operation, "DEBUG_QUERY_OPERATION_REQUIRED", "missing operation"
)
missing_source = invoke("debug-query", "event", "event:1", "--json", expect=2)
require_error(missing_source, "DEBUG_QUERY_SOURCE_REQUIRED", "missing source")
missing_source_value = invoke(
    "debug-query", "event", "event:1", "--source", "--json", expect=2
)
require_error(
    missing_source_value, "DEBUG_QUERY_SOURCE_REQUIRED", "missing source value"
)
missing_selector = invoke(
    "debug-query", "event", "--source", str(basic), "--json", expect=2
)
require_error(
    missing_selector, "DEBUG_QUERY_SELECTOR_REQUIRED", "missing selector"
)
unexpected = invoke(
    "debug-query", "event", "event:1", "extra",
    "--source", str(basic), "--json", expect=2,
)
require_error(unexpected, "DEBUG_QUERY_ARGUMENT_INVALID", "unexpected argument")
unexpected_failure_selector = invoke(
    "debug-query", "failure-slice", "event:1",
    "--source", str(failure), "--json", expect=2,
)
require_error(
    unexpected_failure_selector, "DEBUG_QUERY_ARGUMENT_INVALID",
    "unexpected failure-slice selector",
)

for option, value in (
    ("--max-events", ""),
    ("--max-events", "-1"),
    ("--max-events", "0"),
    ("--max-events", "999999999999999999999999"),
    ("--before-event", "xyz"),
    ("--before-event", "0"),
    ("--max-depth", "nonsense"),
    ("--max-depth", "-1"),
):
    malformed_numeric = query(
        "event", "event:1", basic, option, value, expect=2
    )
    require_error(
        malformed_numeric, "DEBUG_QUERY_ARGUMENT_INVALID",
        f"malformed numeric option {option}={value!r}",
    )

unknown_before = query(
    "writes", "local:y", basic, "--before-event", "999999", expect=1
)
require_error(
    unknown_before, "DEBUG_QUERY_EVENT_NOT_FOUND", "unknown before-event"
)

without_json = subprocess.run(
    [str(compiler), "debug-query", "event", "event:1", "--source", str(basic)],
    cwd=root,
    text=True,
    stdout=subprocess.PIPE,
    stderr=subprocess.PIPE,
    check=False,
)
if without_json.returncode != 2 or without_json.stdout or "requires --json" not in without_json.stderr:
    fail("debug-query did not consistently require --json")

print("Phase 22.4 structured debug query checks passed")
