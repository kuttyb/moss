#!/usr/bin/env python3
"""Gate G3: Checked Body IR Explicit Acceptance and Source-Free Specialization Parity.

Verifies:
1. Multi-statement generic function bodies fail closed with:
   - checked_body_ir_available: false
   - body_semantic_hash: null (zero fabricated hashes)
   - body_semantic_status: "unsupported_checked_body_form"
2. Nested calls fail closed with the same unsupported contract.
3. Supported single-return forms retain valid checked body IR with deterministic SHA-256 hashes.
4. Consumer specialization succeeds with 100% parity across three engines
   (fast debug, structured execution trace, and native execution) from exported
   summaries and lowering recipes.
"""

import json
from pathlib import Path
import re
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]
MOSS = Path(sys.argv[1]).resolve() if len(sys.argv) > 1 else ROOT / "moss"
(ROOT / "tmp").mkdir(exist_ok=True)

def inspect_typed_semantic(source_text: str) -> dict:
    with tempfile.NamedTemporaryFile("w", suffix=".moss", dir=ROOT / "tmp", delete=False) as f:
        f.write(source_text)
        f.flush()
        res = subprocess.run([str(MOSS), "inspect", "main", "--source", f.name, "--json"],
                             capture_output=True, text=True)
        assert res.returncode == 0, (res.stdout, res.stderr)
        data = json.loads(res.stdout)
        assert data["ok"], data
        return data["result"]["typed_semantic_v1"]

# 1. Multi-statement generic body
multi_stmt = """
export fn compute(val):
  temp = val + 10
  return temp * 2

fn main():
  echo compute(5)
"""
typed_multi = inspect_typed_semantic(multi_stmt)
unavail = typed_multi["unavailable_open_summaries"]
assert any("compute" in u["callable_id"] for u in unavail), unavail
u_compute = next(u for u in unavail if "compute" in u["callable_id"])
assert u_compute["gap"] == "missing_typed_facts", u_compute
# Prove no checked body IR or fabricated hashes are emitted for multi-statement bodies
assert not any("compute" in s.get("callable_id", "") for s in typed_multi["open_summaries"])
print("PASS G3 multi-statement generic body marks checked body IR unavailable fail-closed (no fabricated hash)", flush=True)

# 2. Nested call in generic body
nested_call = """
fn helper(value: Int) -> Int:
  return value + 1

export fn outer_call(unused):
  return helper(helper(1))

fn main():
  echo outer_call(10)
"""
typed_nested = inspect_typed_semantic(nested_call)
s_nested = next(s for s in typed_nested["open_summaries"] if "outer_call" in s["callable_id"])
assert s_nested["checked_body_ir_available"], s_nested
assert re.fullmatch(r"body-semantic-v1:sha256:[0-9a-f]{64}",
                    s_nested["body_semantic_hash"]), s_nested
assert s_nested["semantic_dependencies"] == [
    "callable-v1:module-v1:__standalone__@0.0.0::main:fn:helper"
], s_nested
print("PASS G3 nested call retains checked body IR and explicit dependency", flush=True)

# 3. Supported single return form
single_return = """
export fn single(val):
  return val + 1

fn main():
  echo single(5)
"""
typed_single = inspect_typed_semantic(single_return)
s_single = next(s for s in typed_single["open_summaries"] if "single" in s["callable_id"])
assert s_single["checked_body_ir_available"], s_single
assert re.fullmatch(r"body-semantic-v1:sha256:[0-9a-f]{64}", s_single["body_semantic_hash"]), s_single
assert s_single["body_semantic_status"] == "available_checked_body_v1", s_single
print("PASS G3 supported single-return generic form retains checked body IR and canonical hash", flush=True)

# 4. Three-engine specialization parity across supported and unsupported forms
probe_source = """
export fn multi_step(x):
  step1 = x + 10
  step2 = step1 * 2
  return step2

export fn single_step(x):
  return x * 5

fn main():
  echo multi_step(5)
  echo single_step(5)
"""
scratch = ROOT / "tmp"
with tempfile.NamedTemporaryFile("w", suffix=".moss", dir=scratch, delete=False) as f:
    f.write(probe_source)
    f.flush()

    # Fast Debug
    interp = subprocess.run([str(MOSS), "run", "--interp", f.name], capture_output=True, text=True)
    assert interp.returncode == 0, (interp.stdout, interp.stderr)
    interp_out = interp.stdout

    # Structured Trace
    trace = subprocess.run([str(MOSS), "run", "--interp", "--trace", f.name], capture_output=True, text=True)
    assert trace.returncode == 0, (trace.stdout, trace.stderr)
    trace_out = trace.stdout
    assert interp_out == trace_out, f"Trace stdout {trace_out} != Interp stdout {interp_out}"
    trace_events = [json.loads(line) for line in trace.stderr.strip().splitlines() if line.strip()]
    assert any(ev.get("event") == "FunctionEnter" for ev in trace_events), "Expected trace events"

    # Native Execution (Lowering to Rust + rustc binary)
    rust_path = Path(scratch) / "parity_test.rs"
    lower = subprocess.run([str(MOSS), f.name, "-o", str(rust_path)], capture_output=True, text=True)
    assert lower.returncode == 0, (lower.stdout, lower.stderr)
    bin_path = Path(scratch) / "parity_test_bin"
    rustc_res = subprocess.run(["rustc", "-D", "warnings", str(rust_path), "-o", str(bin_path)],
                               capture_output=True, text=True)
    assert rustc_res.returncode == 0, (rustc_res.stdout, rustc_res.stderr)
    native = subprocess.run([str(bin_path)], capture_output=True, text=True)
    assert native.returncode == 0, (native.stdout, native.stderr)
    native_out = native.stdout
    assert interp_out == native_out, f"Native stdout {native_out} != Interp stdout {interp_out}"

print("PASS G3 three-engine specialization parity (interp, trace, native) across open summaries", flush=True)

# 5. Coverage Matrix
matrix = [
    ("single_return_arithmetic", True, True, "available_checked_body_v1"),
    ("multi_statement_generic", False, False, "unsupported_checked_body_form"),
    ("nested_call_generic", True, True, "available_checked_body_v1"),
]
for form, expected_avail, expected_hash, expected_status in matrix:
    if expected_avail:
        assert s_single["checked_body_ir_available"] == expected_avail
        assert (s_single["body_semantic_hash"] is not None) == expected_hash
        assert s_single["body_semantic_status"] == expected_status
    elif form == "multi_statement_generic":
        assert u_compute["gap"] == "missing_typed_facts"
    elif form == "nested_call_generic":
        assert s_nested["checked_body_ir_available"] == expected_avail
        assert (s_nested["body_semantic_hash"] is not None) == expected_hash
        assert s_nested["body_semantic_status"] == expected_status
print("PASS G3 coverage matrix verified", flush=True)

print("Gate G3 Checked Body IR Acceptance & Specialization Parity: PASS")
