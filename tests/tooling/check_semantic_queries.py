#!/usr/bin/env python3
"""Phase 22.3 compiler-owned semantic-query regression checks."""

from __future__ import annotations

import json
import pathlib
import subprocess
import sys


def fail(message: str) -> None:
    raise SystemExit(f"Phase 22.3 semantic query test failed: {message}")


if len(sys.argv) != 2:
    fail("usage: check_semantic_queries.py <moss-compiler>")

root = pathlib.Path.cwd().resolve()
compiler = pathlib.Path(sys.argv[1]).resolve()
source = root / "tests" / "phase6_agent_api.moss"
specialization_source = root / "tests" / "phase4_callable_specialization.moss"
capture_source = root / "tests" / "phase4_exact_pipeline_id.moss"


def invoke(*arguments: str, expect: int = 0) -> dict[str, object]:
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
    try:
        return json.loads(completed.stdout)
    except json.JSONDecodeError as error:
        fail(f"{' '.join(arguments)} did not emit valid JSON: {error}")


def query(command: str, target: str, *, source_file: pathlib.Path = source,
          extra: tuple[str, ...] = ()) -> dict[str, object]:
    document = invoke(
        command, target, "--source", str(source_file), *extra, "--json"
    )
    if not document["ok"]:
        fail(f"{command} {target} unexpectedly failed")
    return document["result"]


# Live discovery advertises the same resolver and stable schema used below.
bootstrap = invoke("agent", "bootstrap", "--json")["result"]
if ("semantic_query_expansion" not in bootstrap["capabilities"]
        or not bootstrap["capability_flags"]["robust_query_targeting"]
        or bootstrap["semantic_queries"][0]["name"] != "resolve"):
    fail("bootstrap did not advertise Phase 22.3 query discovery")
schema = invoke("agent", "schema", "--json")["result"]["schema"]
if schema["query_resolution"]["statuses"] != ["resolved", "ambiguous", "missing"]:
    fail("agent schema omitted semantic resolution statuses")


# Target identity: canonical target, durable ID, source location, explicit
# ambiguity, missing target, kind narrowing, and enclosing-entity narrowing.
resolved = query("resolve", "fn:inspect")
target = resolved["target"]
if resolved["status"] != "resolved" or target["kind"] != "function":
    fail("canonical function target did not resolve exactly")
durable_id = target["id"]
if query("resolve", durable_id)["target"]["semantic_identity"] != target["semantic_identity"]:
    fail("durable semantic ID lookup did not round-trip")

ambiguous = invoke(
    "resolve", "line:44", "--source", str(source), "--json", expect=1
)["error"]
resolution = ambiguous["details"]["resolution"]
if resolution["status"] != "ambiguous":
    fail("source location ambiguity was silently guessed")
if [item["kind"] for item in resolution["candidates"]] != ["binding", "call"]:
    fail("ambiguous location candidates are incomplete or nondeterministic")
binding = query("resolve", "at:44:10", extra=("--kind", "binding"))
if binding["target"]["kind"] != "binding":
    fail("kind filter did not narrow a source location")
call = query("resolve", "fn:inspect", extra=("--enclosing", "main"))
if call["target"]["kind"] != "call" or not call["target"]["enclosing_entity"]:
    fail("enclosing filter did not resolve a call entity")
missing = invoke(
    "resolve", "not-present", "--source", str(source), "--json", expect=1
)["error"]
if missing["details"]["resolution"] != {"status": "missing", "candidates": []}:
    fail("missing target did not return stable structured resolution")

# Types: explicit, inferred, specialization, and unsupported/no-value facts.
explicit_type = query("type", "Sample.value")
if explicit_type["type_facts"]["resolved"] != "int":
    fail("explicit field type was not reported")
inferred_type = query("type", "line:44", extra=("--kind", "binding"))
if inferred_type["type_facts"]["origin"] != "inferred":
    fail("inferred binding type was not identified")
specialized_type = query(
    "type", "fn:double<int>", source_file=specialization_source
)
if (specialized_type["type_facts"]["category"] != "specialized"
        or specialized_type["resolved_type"] != "int"):
    fail("specialized callable type was not reported")
call_type = query("type", "fn:inspect", extra=("--enclosing", "main"))
if call_type["type_facts"]["status"] != "not_available":
    fail("unsupported call-result type was not explicit")

# Ownership: READ, WRITE, CONSUME and message/reply by-value restrictions.
read = query("ownership", "fn:noisy")["ownership_facts"]
if read["parameters"][0]["effect"] != "READ":
    fail("READ parameter ownership was not reported")
write = query("ownership", "method:Sample.replace")["ownership_facts"]
if write["parameters"][0]["effect"] != "WRITE":
    fail("WRITE receiver ownership was not reported")
consume = query("ownership", "fn:transfer")["ownership_facts"]
if consume["parameters"][0]["effect"] != "CONSUME":
    fail("CONSUME parameter ownership was not reported")
if consume["move_provenance"]["status"] != "not_available":
    fail("unretained move provenance was not identified explicitly")
boundary = query("ownership", "handler:Worker.Add")["ownership_facts"]["value_boundary"]
if (boundary["payload"] != "by_value_immutable_snapshot"
        or boundary["reply"] != "by_value"
        or boundary["domain_handles_allowed"] is not False):
    fail("message payload/reply restrictions were not reported")

# Effects: exact pipeline-local effects, transitive callable effects, captures,
# and an explicit direct-effect retention limitation.
effects = query("effects", "fn:noisy")
summary = effects["effect_summary"]
if (not summary["transitive"]["external_io"]
        or summary["direct_status"] != "not_retained"):
    fail("callable effect scope/retention was not reported")
pipeline = query(
    "effects", "main@42:expression:0", extra=("-O",)
)["effect_summary"]
if pipeline["direct_status"] != "exact" or not pipeline["direct"]["external_io"]:
    fail("pipeline-local direct effects were not reported")
if "local_capture_read" not in pipeline["direct"]:
    fail("capture contribution is absent from effect schema")
capture = query(
    "effects", "fn:local_total@4:result", source_file=capture_source,
    extra=("-O",),
)["effect_summary"]
if not capture["direct"]["local_capture_read"]:
    fail("captured-state read effect was not reported")
if summary["certainty"] not in ("resolved", "conservative_unresolved"):
    fail("effect certainty is not explicit")

# Calls: stable call-site IDs, resolved and specialized callees, helper edges,
# message boundaries, callers, and transitive closure.
calls = query("calls", "main", source_file=specialization_source)
if len(calls["direct_calls"]) != 2:
    fail("specialized direct calls were not retained")
if {edge["specialization_identity"] for edge in calls["direct_calls"]} != {
    "specialization:double<int>", "specialization:double<float>"
}:
    fail("selected callable specializations were not reported")
if not all(edge["call_site_id"].startswith("entity-v1:call:")
           for edge in calls["direct_calls"]):
    fail("call sites lack stable IDs")
main_calls = query("calls", "main")
if not any(edge["boundary"] == "synchronous_message_by_value"
           for edge in main_calls["direct_calls"]):
    fail("message/handler call resolution was not distinguished")
if not any(edge["target"] == "method:Sample.read"
           for edge in main_calls["transitive_calls"]):
    fail("transitive helper call edge was not reported")
if not query("calls", "method:Sample.read")["callers"]:
    fail("resolved caller edge was not reported")

# Domain/message and synchronization facts are projections of compiler-owned
# topology and SynchronizationPlan records.
handler = query("effects", "handler:Worker.Add")
if (not handler["effect_summary"]["transitive"]["domain_read"]
        or not handler["effect_summary"]["transitive"]["domain_write"]):
    fail("domain read/write effects were not reported")
sync = handler["synchronization"]
if (sync["analysis"] != "compiler_synchronization_plan"
        or sync["conservative_fallback"] is not True
        or sync["precision"] != "exact_footprint_conservative_placement"):
    fail("synchronization provenance/precision was not reported")
footprint = sync["instances"][0]["handlers"][0]
if (footprint["read_set"] != ["total"]
        or footprint["write_set"] != ["total"]
        or footprint["protected_read_set"] != ["total"]
        or footprint["acquisitions"][0]["mode"] != "EXCLUSIVE"):
    fail("handler synchronization footprint is incomplete")
domain = query("inspect", "handler:Router.Route")["domain"]
if (domain["domain"] != "Router" or domain["receiver_instance_precision"] != "exact_domain_instance"):
    fail("domain receiver facts were not reported")

# Why is structured evidence, while legacy explanations remain compatible.
why_call = query("why", "fn:inspect", extra=("--enclosing", "main"))
if why_call["reason"]["code"] != "STATIC_CALL_RESOLUTION":
    fail("call-resolution why code is not stable")
why_ownership = query("why", "fn:transfer")
if (why_ownership["reason"]["code"] != "INFERRED_VALUE_EFFECTS"
        or not any(item["kind"] == "ownership"
                   for item in why_ownership["reason"]["evidence"])):
    fail("ownership why evidence is missing")

# Illegal moved-value and ownership operations remain compiler diagnostics,
# and their semantic identities are directly queryable when the checked fact
# survives.  The query API does not re-run or approximate failed semantics.
use_after = invoke(
    "check", str(root / "examples" / "use_after_transfer.moss"), "--json",
    expect=1,
)["error"]
if (use_after["code"] != "OWNERSHIP_USE_AFTER_CONSUME"
        or use_after["details"]["ownership_actual"] != "CONSUMED"
        or not use_after["semantic_identity"]):
    fail("illegal consume operation lacks structured compiler evidence")

print("Phase 22.3 semantic query checks passed")
