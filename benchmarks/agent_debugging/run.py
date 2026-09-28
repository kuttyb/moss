#!/usr/bin/env python3
"""Phase 22.4 agent debugging context-reduction benchmark."""

from __future__ import annotations

import json
import pathlib
import subprocess
import sys


ROOT = pathlib.Path(__file__).resolve().parents[2]
OUT = ROOT / "tmp" / "phase224_agent_debugging_results.json"


CASES = [
    {
        "name": "local_mutation",
        "category": "A. Incorrect local mutation",
        "source": ROOT / "benchmarks/agent_debugging/cases/local_mutation.moss",
        "query": ["failure-slice", "--max-events", "8"],
        "expect": ["AssertionFailure", "LocalWrite"],
    },
    {
        "name": "branch_dependent",
        "category": "B. Branch-dependent bug",
        "source": ROOT / "benchmarks/agent_debugging/cases/branch_bug.moss",
        "query": ["failure-slice", "--max-events", "8"],
        "expect": ["BranchTaken", "AssertionFailure"],
    },
    {
        "name": "loop_late_failure",
        "category": "C. Loop bug",
        "source": ROOT / "benchmarks/agent_debugging/cases/loop_bug.moss",
        "query": ["failure-slice", "--max-events", "20"],
        "expect": ["LoopIteration", "AssertionFailure"],
    },
    {
        "name": "domain_message",
        "category": "D. Domain/message bug",
        "source": ROOT / "benchmarks/agent_debugging/cases/domain_message_bug.moss",
        "query": ["message-subtree", "handler:Coordinator.Run", "--max-events", "20"],
        "expect": ["message_call", "handler_enter", "state_write", "message_return"],
    },
    {
        "name": "helper_state_access",
        "category": "E. Cross-helper state access",
        "source": ROOT / "benchmarks/agent_debugging/cases/helper_state_bug.moss",
        "query": ["writes", "total", "--max-events", "5"],
        "expect": ["state_write"],
    },
    {
        "name": "multi_module",
        "category": "F. Multi-module execution",
        "source": ROOT / "benchmarks/agent_debugging/multimodule/src/main.moss",
        "query": ["semantic", "entity-v1:function:Math__normalize", "--max-events", "6"],
        "expect": ["FunctionEnter", "Return"],
    },
]


def run(args: list[str], *, expect_any: bool = False) -> subprocess.CompletedProcess[str]:
    completed = subprocess.run(
        args,
        cwd=ROOT,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        check=False,
    )
    if not expect_any and completed.returncode != 0:
        raise AssertionError(f"{args} failed:\n{completed.stdout}\n{completed.stderr}")
    return completed


def trace_count(stderr: str) -> int:
    count = 0
    for line in stderr.splitlines():
        try:
            json.loads(line)
        except json.JSONDecodeError:
            continue
        count += 1
    return count


def main() -> int:
    if len(sys.argv) != 2:
        raise SystemExit("usage: run.py <moss-compiler>")
    compiler = str(pathlib.Path(sys.argv[1]).resolve())
    results = []
    successes = 0
    for case in CASES:
        source = str(case["source"])
        raw = run([compiler, "run", "--interp", "--trace", source], expect_any=True)
        raw_events = trace_count(raw.stderr)
        query_args = [compiler, "debug-query", *case["query"], "--source", source, "--json"]
        queried = run(query_args, expect_any=True)
        document = json.loads(queried.stdout)
        if not document.get("ok"):
            raise AssertionError(f"debug-query failed for {case['name']}: {document}")
        result = document["result"]
        events = result.get("events") or result.get("writes") or result.get("control_path") or []
        event_names = {event["event"] for event in events}
        success = all(expected in event_names for expected in case["expect"])
        successes += int(success)
        sliced_events = result["returned_events"]
        ratio = 0.0 if raw_events == 0 else sliced_events / raw_events
        results.append({
            "name": case["name"],
            "category": case["category"],
            "success": success,
            "tool_calls_to_slice": 1,
            "raw_trace_event_count": raw_events,
            "sliced_event_count": sliced_events,
            "slice_full_trace_ratio": ratio,
            "query": " ".join(case["query"]),
        })
    summary = {
        "benchmark": "phase22.4-agent-debugging",
        "success_rate": successes / len(CASES),
        "cases": results,
        "totals": {
            "raw_trace_event_count": sum(item["raw_trace_event_count"] for item in results),
            "sliced_event_count": sum(item["sliced_event_count"] for item in results),
        },
    }
    total_raw = summary["totals"]["raw_trace_event_count"]
    total_sliced = summary["totals"]["sliced_event_count"]
    summary["totals"]["slice_full_trace_ratio"] = (
        0.0 if total_raw == 0 else total_sliced / total_raw
    )
    OUT.parent.mkdir(parents=True, exist_ok=True)
    OUT.write_text(json.dumps(summary, indent=2, sort_keys=True) + "\n")
    print(json.dumps(summary, indent=2, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
