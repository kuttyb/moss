# Phase 25 Integration Acceptance Gates & Ledger

This directory contains the machine-checkable acceptance ledger and runner for Phase 25.

## Contents

- **`acceptance_matrix.json`**: Canonical JSON database defining all 62 Phase 25 acceptance gates.
- **`run_acceptance.py`**: Command-line acceptance harness and reporter.
- **`README.md`**: This specification and usage guide.

Related documentation: [`docs/phase25/ACCEPTANCE_GATES.md`](../../../docs/phase25/ACCEPTANCE_GATES.md).

## Acceptance Matrix Schema

Every entry in `acceptance_matrix.json` conforms to the following schema:

```json
{
  "id": "GATE-TYPEIR-01",
  "group": "TYPEIR",
  "requirement": "independent Box[Int] and Box[String], plus nominal inequality for two declarations with the same layout",
  "owning_team": "Team G",
  "test_path": "tests/phase25/g0/conformance/nominal_instance_coexistence.moss",
  "engine_configs": ["Fast Debug", "Native Debug", "Native Release"],
  "current_status": "BLOCKED",
  "blocking_dependency": "origin/phase25-generic-types: Type IR & Option B closure",
  "test_evidence": "tests/tooling/check_phase25_conformance.py on Team G branch at a50fa30; fails with TYPE_MISMATCH on prep branch baseline.",
  "implementation_sha": "a50fa30c8420538534471c4404afb0c645ae9b0c",
  "source_contract": "TYPE_IR_CONTRACT.md §10.1"
}
```

### Required Fields

- `id`: Unique gate identifier (`GATE-TYPEIR-01..11`, `GATE-ABIV10-01..11`, `GATE-G1-01..15`, `GATE-G3-01..08`, `GATE-G4-01..06`, `GATE-G5-01..06`, `GATE-G6-01..05`).
- `group`: Functional area code (`TYPEIR`, `ABIV10`, `G1`, `G3`, `G4`, `G5`, `G6`).
- `requirement`: Exact requirement text extracted directly from canonical contracts.
- `owning_team`: Designated owner (`Team G`, `Team S`, `Team I`, `Joint`).
- `test_path`: Executable test script or fixture path relative to repository root.
- `engine_configs`: List of required execution configurations (e.g. Fast Debug, Native Debug, Native Release, Margo Package Build).
- `current_status`: Exactly one of `PASS`, `FAIL`, `BLOCKED`, `NOT IMPLEMENTED`.
- `blocking_dependency`: Specific named dependency when `current_status` is `BLOCKED` (or `null` otherwise).
- `test_evidence`: Concrete command, log citation, or observable evidence.
- `implementation_sha`: 40-character Git commit hash where implementation was verified (or `null` if not yet implemented).
- `source_contract`: Exact section in canonical specification.

## Runner Usage

The runner is self-contained and executed using standard Python 3:

```bash
# Execute self-test (checks schema, invariants, gate count, and 4 fault injections; exits 0):
python3 tests/phase25/integration/run_acceptance.py --self-test

# Execute deliberate fault-injection suite (anti-false-green proof):
python3 tests/phase25/integration/run_acceptance.py --fault-injection

# Run combined acceptance + cross-crate fault-injection test suite:
python3 tests/phase25/integration/test_fault_injection.py

# Generate tabular/markdown summary of all gates:
python3 tests/phase25/integration/run_acceptance.py --summary

# Output machine-readable JSON acceptance report:
python3 tests/phase25/integration/run_acceptance.py --json

# Inspect a specific gate by ID:
python3 tests/phase25/integration/run_acceptance.py --gate GATE-G1-01

# Strict release gating (exits 1 if any gate is not PASS):
MOSS_STAGED_ABI=v10 python3 tests/phase25/integration/run_acceptance.py --strict --json

# Inspect provisional live results while editing (never promotes a dirty tree):
MOSS_STAGED_ABI=v10 python3 tests/phase25/integration/run_acceptance.py --live --json
```

`--strict` implies `--live`. The live runner first builds this checkout's
`./moss` and verifies its local `./margo`, then runs the G2, G1 ledger and
three-engine regressions, E3 (debug and release), activated key-protocol, and
Team B suites serially. It promotes only gates with an exact
case mapping in `live_acceptance.py` and requires a clean, unchanged Git HEAD.
The JSON report records the HEAD, executed commands, individual case results,
and output hashes. Unmapped requirements retain their ledger status even when
another suite passes. A passing suite never substitutes for a distinct test of
a broader frozen requirement. The static `--summary` and `--json` views remain
historical ledger views; use `--live` for current executable evidence.

## Gate Areas Overview

1. **`GATE-TYPEIR-01..11` (11 gates)**: All 11 Type IR conformance requirements from `TYPE_IR_CONTRACT.md` §10.
2. **`GATE-ABIV10-01..11` (11 gates)**: All 11 ABI-v10 proof matrix areas from `ABI_V10_DESIGN.md` §11.
3. **`GATE-G1-01..15` (15 gates)**: All 15 G1 parity and correctness defects (#1–#15) from `TEAM_S_DESIGN_AND_ACCEPTANCE.md` §2 and `run_ledger.py`.
4. **`GATE-G3-01..08` (8 gates)**: G3 ordering, equality, hashing, scalar key protocol v1, Float Julia-like semantics, and Map/Set key proof.
5. **`GATE-G4-01..06` (6 gates)**: G4 String, Char, Unicode scalar, and MossStr indexed-v1 layout.
6. **`GATE-G5-01..06` (6 gates)**: Final G5 compiler and runtime integration, `moss_runtime` crate, full SDK matrix, and differential CI parity.
7. **`GATE-G6-01..05` (5 gates)**: Final G6 adversarial falsification, corrupt proof rejection, 64-bit edge-case audit, memory safety, and whole-system Architect sign-off.
