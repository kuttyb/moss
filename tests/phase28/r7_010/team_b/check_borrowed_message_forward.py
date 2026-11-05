#!/usr/bin/env python3
"""Check synchronous message forwarding borrows checked READ object views."""
import json
import os
from pathlib import Path
import subprocess
import sys
import shutil


REPO = Path(__file__).resolve().parents[4]
MOSS = Path(sys.argv[1] if len(sys.argv) > 1 else REPO / "moss").resolve()
MARGO = REPO / "margo"
OUT = Path(sys.argv[2] if len(sys.argv) > 2 else REPO / "tmp/team-b/borrowed-message-forward").resolve()
if OUT.exists():
    shutil.rmtree(OUT)
provider = OUT / "provider"
consumer = OUT / "consumer"
(provider / "src").mkdir(parents=True)
(consumer / "src").mkdir(parents=True)
(provider / "Moss.toml").write_text(
    '[project]\nname = "borrowed_message_provider"\nversion = "0.1.0"\n\n'
    '[build]\nsource = "src"\n')
(provider / "src/provider.moss").write_text('''module provider
export type Metadata:
  label: String
export type Record:
  metadata: Metadata
export domain Bank:
  fn Accept(value: Record):
    echo value.metadata.label
export domain Forward:
  domainroutes(bank: Bank)
  record: Record
  fn Send():
    message bank.Accept(record)
''')
(consumer / "Moss.toml").write_text(
    '[project]\nname = "borrowed_message_consumer"\nversion = "0.1.0"\n\n'
    '[build]\nsource = "src"\n\n[dependencies]\n'
    'borrowed_message_provider = { path = "../provider" }\n')
(consumer / "src/main.moss").write_text('''module app
import provider
fn main():
  bank = provider.Bank()
  forward = provider.Forward(bank: bank, record: provider.Record(metadata: provider.Metadata(label: "hello")))
  message forward.Send()
''')


def run(args, cwd):
    env = dict(os.environ)
    env["MOSS"] = str(MOSS)
    return subprocess.run(list(map(str, args)), cwd=cwd, env=env,
                          capture_output=True, text=True, check=True, timeout=90)


run([MARGO, "build", "--json"], provider)
result = json.loads(run([MARGO, "build", "--json"], consumer).stdout)
assert result["ok"] is True
provider_rust = (provider / "build/debug/provider.rs").read_text()
assert "MOSS_OBJECT_VALUE_HELPER_BEGIN" not in provider_rust
assert "__moss_value" not in provider_rust
start = provider_rust.index("pub fn __moss_body_provider__Forward_Send")
end = provider_rust.index("// Moss tooling end|handler:provider__Forward.Send", start)
forward_body = provider_rust[start:end]
assert "state.bank.__moss_message_Accept(&(state.record));" in forward_body
assert ".clone()" not in forward_body and "__moss_value" not in forward_body
executable = Path(result["artifacts"]["artifacts"]["executable"])
native_output = run([executable], consumer).stdout.strip()
fast_debug_output = run([MARGO, "debug"], consumer).stdout.strip()
assert native_output == fast_debug_output == "hello"
print("synchronous cross-crate object-view message forwarding borrows READ view: PASS")
