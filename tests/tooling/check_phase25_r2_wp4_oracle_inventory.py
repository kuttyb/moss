#!/usr/bin/env python3
"""Self-test the combined Phase 25.1 compatibility oracle inventory."""

import importlib.util
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
ORACLE_PATH = ROOT / "tests/phase25/compatibility/oracle.py"
SPEC = importlib.util.spec_from_file_location("phase25_oracle", ORACLE_PATH)
assert SPEC and SPEC.loader
ORACLE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(ORACLE)

cases = ORACLE.combined_inventory(include_surface=True)
ids = [case["id"] for case in cases]
assert len(ids) == len(set(ids)), "combined corpus/surface IDs must be unique"
assert "generic-map-index-mutation" in ids
assert "surface-map-index-mutation" in ids

by_id = {case["id"]: case for case in cases}
assert ORACLE.expected_output_for(by_id["returned-callback"], "source") == "31\nmoss\n"
assert ORACLE.expected_output_for(by_id["returned-callback"], "v9") == "31\nmoss\n"
assert ORACLE.expected_output_for(
    by_id["collection-count-nonloop-fail-closed"], "source") == "2\n"
assert ORACLE.expected_output_for(
    by_id["collection-count-nonloop-fail-closed"], "v9") == "2\n"

try:
    ORACLE.validate_inventory([by_id["generic-map-index-mutation"],
                               by_id["generic-map-index-mutation"]])
except ValueError as error:
    assert "duplicate oracle case id" in str(error)
else:
    raise AssertionError("duplicate IDs must fail inventory validation")

print(f"PASS combined oracle inventory self-test ({len(cases)} unique cases)")
