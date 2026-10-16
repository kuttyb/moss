#!/usr/bin/env python3
"""Compiler-path checks for the canonical specialization-v2 key."""
import json
from pathlib import Path
import subprocess
import sys

root = Path(__file__).resolve().parents[2]
moss = Path(sys.argv[1]).resolve() if len(sys.argv) > 1 else root / "moss"
scratch = root / "tmp" / "phase25-a3-specialization"
scratch.mkdir(parents=True, exist_ok=True)
source = scratch / "identity.moss"
source.write_text('''fn first(value):
  return value

fn second(value):
  return value

fn main():
  echo first(1)
  echo first("text")
  echo second(1)
''')

checked = subprocess.run([str(moss), "check", str(source), "--json"],
                         text=True, capture_output=True)
assert checked.returncode == 0, (checked.stdout, checked.stderr)
query = subprocess.run([str(moss), "calls", "main", "--source", str(source),
                        "--json"], text=True, capture_output=True)
assert query.returncode == 0, (query.stdout, query.stderr)
edges = json.loads(query.stdout)["result"]["direct_calls"]
assert len(edges) == 3, edges
keys = [edge["specialization_key"] for edge in edges]
assert all(key and key.startswith("specialization-v2:sha256:")
           for key in keys), edges
assert len(set(keys)) == 3, edges
print("Phase 25 A3 compiler specialization keys: PASS")
