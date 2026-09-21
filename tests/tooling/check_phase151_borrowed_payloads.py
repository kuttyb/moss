#!/usr/bin/env python3
"""Phase 15.1: internal synchronous messages borrow stable payloads."""
import json
from pathlib import Path
import re
import subprocess
import sys


repo = Path(__file__).resolve().parents[2]
compiler = Path(sys.argv[1]).resolve()
out = Path(sys.argv[2]).resolve()
out.mkdir(parents=True, exist_ok=True)
source = repo / "tests/phase151_borrowed_message_payloads.moss"
rust = out / "borrowed_payloads.rs"


def run(args, expected=0):
    process = subprocess.run([str(arg) for arg in args], capture_output=True,
                             text=True, timeout=90)
    assert process.returncode == expected, (args, process.stdout, process.stderr)
    return process.stdout


run([compiler, source, "-o", rust])
text = rust.read_text()
run(["rustc", "-D", "warnings", rust, "-o", out / "borrowed_payloads"])
assert run([out / "borrowed_payloads"]).strip() == "20"

# Internal object handlers use the private static access ABI, and direct local
# sends lend the owner rather than clone it.  The exported/native bridge stays
# owned and is intentionally not a test target here.
assert "fn Process_shared(&self, packet: &impl MossAccess_Packet)" in text
assert "fn Forward_shared(&self, packet: &impl MossAccess_Packet)" in text
assert "fn __moss_body_Sink_Process(state: &mut SinkProcessState<'_>, packet: &impl MossAccess_Packet)" in text
assert "sink.Process_shared(packet)" in text
assert "middle.Forward_shared(&(state.packet))" in text
assert "Forward_shared((state.packet).__moss_value())" not in text
assert "Forward_shared((state.packet).clone())" not in text
assert "Process_shared((packet).__moss_value())" not in text
assert "Process_shared((packet).clone())" not in text

# `moss cost` describes physical materialization, not every semantic message
# boundary. The internal borrowed hops above therefore have no large-payload
# materialization fact.
borrowed_cost = json.loads(run([
    compiler, "cost", "main", "--source", source, "--json"
]))
assert borrowed_cost["result"]["cost_facts"]["message_copy_sizes"] == []

# The only Packet materialization in this program is not a message argument:
# it is the independent reply produced by the normal Moss reply boundary.
start = text.index("fn __moss_body_Middle_Forward")
end = text.index("// Moss tooling end|handler:Middle.Forward", start)
forward_body = text[start:end]
assert ".clone()" not in forward_body
assert ".__moss_value()" not in forward_body

print("Phase 15.1 borrowed owned/local/state payload and nested forwarding checks passed.")

# An exported/native route is intentionally an owned ABI boundary.  The caller
# materializes the semantic message value, and the bridge then lends that owned
# value to the same internal shared handler contract.
external_source = repo / "tests/phase151_exported_payload.moss"
external_rust = out / "exported_payload.rs"
run([compiler, external_source, "-o", external_rust])
external = external_rust.read_text()
run(["rustc", "-D", "warnings", external_rust, "-o", out / "exported_payload"])
assert run([out / "exported_payload"]).strip() == "true"
assert "pub fn __moss_message_Receive(&self, payload: Payload)" in external
assert "self.Receive_shared(&(payload))" in external
assert "boundary.__moss_message_Receive((data).clone())" in external
print("Phase 15.1 exported payload bridge retains an owned materialization boundary.")

# The deliberately exported large-payload boundary remains owned. Its warning
# must round-trip through the machine-readable cost fact despite the legacy
# public `message_copy_sizes` field name.
large_source = repo / "tests/large_payload_warning.moss"
owned_cost = json.loads(run([
    compiler, "cost", "main", "--source", large_source, "--json"
]))
sizes = owned_cost["result"]["cost_facts"]["message_copy_sizes"]
assert sizes and any(
    item["bytes"] == 1088 and item["known_statically"] is True
    for item in sizes
), sizes
print("Phase 15.1 cost facts distinguish borrowed hops from owned materialization.")
