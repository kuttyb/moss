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
python3 tests/phase25/integration/run_acceptance.py --strict
```

## Gate Areas Overview

1. **`GATE-TYPEIR-01..11` (11 gates)**: All 11 Type IR conformance requirements from `TYPE_IR_CONTRACT.md` §10.
2. **`GATE-ABIV10-01..11` (11 gates)**: All 11 ABI-v10 proof matrix areas from `ABI_V10_DESIGN.md` §11.
3. **`GATE-G1-01..15` (15 gates)**: All 15 G1 parity and correctness defects (#1–#15) from `TEAM_S_DESIGN_AND_ACCEPTANCE.md` §2 and `run_ledger.py`.
4. **`GATE-G3-01..08` (8 gates)**: G3 ordering, equality, hashing, scalar key protocol v1, Float Julia-like semantics, and Map/Set key proof.
5. **`GATE-G4-01..06` (6 gates)**: G4 String, Char, Unicode scalar, and MossStr indexed-v1 layout.
6. **`GATE-G5-01..06` (6 gates)**: Final G5 compiler and runtime integration, `moss_runtime` crate, full SDK matrix, and differential CI parity.
7. **`GATE-G6-01..05` (5 gates)**: Final G6 adversarial falsification, corrupt proof rejection, 64-bit edge-case audit, memory safety, and whole-system Architect sign-off.


## Strict release evidence freshness

`run_acceptance.py --strict` validates all 62 gate identities and ledger fields
before considering status, including when `--gate` selects a single gate.
Historical `implementation_sha` entries must be ancestors of the candidate.
Narrative `test_evidence` remains useful history but cannot establish release PASS.

For every PASS gate, strict mode requires a machine report named
`<gate-id>.json` in `tmp/phase25-acceptance/` (override with `--evidence-dir`).
The release executor must create these reports and logs **after committing the
candidate**, in the clean checkout where its acceptance commands actually ran.
Reports are disposable CI artifacts, not committed ledger updates; this avoids a
self-referential evidence commit. Every later commit, including documentation-only
changes, requires fresh reports at its exact HEAD. No source files are excluded
from freshness. Failed reports and logs should remain in CI artifacts/history;
the validator never rewrites them or upgrades a failed status.

Report version 1 has the following shape (replace all illustrative values with
actual execution records):

```json
{
  "schema_version": 1,
  "gate_id": "GATE-TYPEIR-01",
  "status": "PASS",
  "evidence_sha": "<full candidate HEAD>",
  "results": [
    {
      "engine": "<exact engine_configs entry>",
      "command": ["<acceptance executable>", "<argument>"],
      "returncode": 0,
      "log_path": "tmp/phase25-acceptance/typeir-01-native.log",
      "log_sha256": "<SHA-256 of captured nonempty execution log>"
    }
  ]
}
```

`results` must cover every declared engine exactly once. Commands must be argument
arrays; each acceptance command must exit zero, including test wrappers that verify
expected negative compiler diagnostics. Each log must exist within the repository,
be nonempty, and match its digest. Missing reports, stale SHAs, dirty checkouts,
failed commands, wrong engines, missing logs and tampered output all fail closed.
The report producer still owns exercising the actual invariant and recording honest
commands/output; a JSON validator cannot substitute for the L6 review of those paths.
This integrity change does not provide the still-required 62-gate release executor
or upgrade any inherited BLOCKED gate.
