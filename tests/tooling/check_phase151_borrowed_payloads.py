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
assert re.search(r"sink\.Process_shared\((?:packet|&\(\*packet\))\)", text)
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

# An exported/native route uses the same checked READ borrow as an internal
# message. The caller retains its payload and the public bridge accepts a
# borrow, so Root ingress does not materialize a snapshot.
external_source = repo / "tests/phase151_exported_payload.moss"
external_rust = out / "exported_payload.rs"
run([compiler, external_source, "-o", external_rust])
external = external_rust.read_text()
run(["rustc", "-D", "warnings", external_rust, "-o", out / "exported_payload"])
assert run([out / "exported_payload"]).strip() == "true ok"
assert "pub fn __moss_message_Receive(&self, payload: &impl MossAccess_Payload)" in external
assert "self.Receive_shared(&payload" in external
assert "let __moss_root_arg_1 = &(data); runtime_invoke_scoped(move || __moss_root_domain_0.__moss_message_Receive(__moss_root_arg_1))" in external
assert "let __moss_root_arg_1 = (data).clone()" not in external
fast = run([compiler, "run", "--interp", external_source]).strip()
assert fast == "true ok", fast
print("Phase 15.1 exported payload bridge preserves the sender through scoped READ ingress.")
# Large payload messages also borrow. The legacy cost fact remains empty.
large_source = repo / "tests/large_payload_warning.moss"
owned_cost = json.loads(run([
    compiler, "cost", "main", "--source", large_source, "--json"
]))
sizes = owned_cost["result"]["cost_facts"]["message_copy_sizes"]
assert sizes == [], sizes
print("Phase 15.1 message payload borrows do not materialize even at exported boundaries.")

# Executor submission has the opposite ownership effect: the exact nonprimitive
# argument is moved into queued work and a later read is rejected.
executor_source = out / "executor_consume.moss"
executor_source.write_text("""type Payload:
  value: Int

domain Worker:
  fn Accept(payload: Payload):
    echo payload.value

fn main():
  worker = Worker()
  executor = Executor().threads(1).start()
  payload = Payload(value = 20)
  executor.invoke(worker.Accept(payload))
  executor.join()
""")
executor_rust = out / "executor_consume.rs"
run([compiler, executor_source, "-o", executor_rust])
executor_text = executor_rust.read_text()
assert re.search(r"let __moss_invoke_arg_\d+ = payload;", executor_text)
assert not re.search(r"let __moss_invoke_arg_\d+ = \(payload\)\.clone\(\);", executor_text)
run(["rustc", "-D", "warnings", executor_rust, "-o", out / "executor_consume"])
assert run([out / "executor_consume"]).strip() == "20"
assert run([compiler, "run", "--interp", executor_source]).strip() == "20"
executor_after = out / "executor_consume_after.moss"
executor_after.write_text(executor_source.read_text().replace(
    "  executor.join()", "  echo payload.value\n  executor.join()"))
rejected = subprocess.run([str(compiler), "check", "--json", str(executor_after)],
                          capture_output=True, text=True, timeout=90)
assert rejected.returncode != 0
assert json.loads(rejected.stdout)["error"]["code"] == "OWNERSHIP_USE_AFTER_CONSUME"
print("Executor nonprimitive argument is moved and later use is rejected.")

mutation_source = out / "message_payload_mutation.moss"
mutation_source.write_text("""type Payload:
  value: Int

domain Worker:
  fn Accept(payload: Payload):
    payload.value = 1

fn main():
  worker = Worker()
  payload = Payload(value = 0)
  message worker.Accept(payload)
""")
mutation = subprocess.run([str(compiler), "check", "--json", str(mutation_source)],
                           capture_output=True, text=True, timeout=90)
assert mutation.returncode != 0
assert "incoming message payload 'payload'" in json.loads(mutation.stdout)["error"]["message"]
print("Incoming message payload mutation is rejected.")
