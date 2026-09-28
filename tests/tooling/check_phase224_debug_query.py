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

semantic = query("semantic", "entity-v1:function:transform", basic, "--max-events", "3")["result"]
if not semantic["truncated"] or semantic["returned_events"] != 3:
    fail("semantic query did not report explicit truncation")
if semantic["events"][0]["semantic_identity"] != "fn:transform@1":
    fail("entity-v1 semantic selector did not bridge to runtime source identity")

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

loop_event = next(
    event for event in failure_slice["events"]
    if event["event"] == "LocalWrite" and event["detail"] == "total"
)
loop_path = query("control-flow", f"event:{loop_event['event_id']}", failure)["result"]
if not any(event["event"] == "LoopIteration" for event in loop_path["control_path"]):
    fail("control path omitted repeated loop iteration context")

domain_slice = query("message-subtree", "handler:Service.Run", domain, "--max-events", "50")["result"]
event_names = [event["event"] for event in domain_slice["events"]]
if "message_call" not in event_names or "handler_enter" not in event_names or "message_return" not in event_names:
    fail("message subtree did not include synchronous message structure")
if not any(event.get("message_event_id") for event in domain_slice["events"]):
    fail("message subtree omitted message causal links")

state_writes = query("writes", "balance", domain, "--before-event", "20")["result"]
if not state_writes["writes"] or state_writes["writes"][0]["path"] != "balance":
    fail("state write query did not return domain state history")

unknown_event = query("event", "event:999999", basic, expect=1)
if unknown_event["error"]["code"] != "DEBUG_QUERY_EVENT_NOT_FOUND":
    fail("unknown event ID did not use stable error code")

malformed = query("event", "event:not-an-int", basic, expect=2)
if malformed["error"]["code"] != "DEBUG_QUERY_SELECTOR_INVALID":
    fail("malformed selector did not use stable error code")

unknown_semantic = query("semantic", "entity-v1:function:not_present", basic, expect=1)
if unknown_semantic["error"]["code"] != "DEBUG_QUERY_TARGET_NOT_FOUND":
    fail("unknown semantic identity did not use stable error code")

not_executed = query("semantic", "entity-v1:function:unused_value", failure, expect=1)
if not_executed["error"]["code"] != "DEBUG_QUERY_NOT_EXECUTED":
    fail("unexecuted semantic identity did not use stable error code")

module_identity = query(
    "semantic", "entity-v1:function:First__answer", module_source, "--max-events", "2"
)["result"]
if module_identity["events"][0]["semantic_identity"] != "fn:First__answer@3":
    fail("multi-module semantic identity did not select the runtime event")
if not module_identity["events"][0]["source_file"].endswith("src/first.moss"):
    fail("multi-module runtime event lost its source/module provenance")

unsupported = query("provenance", "local:y", basic, expect=2)
if unsupported["error"]["code"] != "DEBUG_QUERY_UNSUPPORTED_OPERATION":
    fail("unsupported provenance request did not use stable error code")

print("Phase 22.4 structured debug query checks passed")
