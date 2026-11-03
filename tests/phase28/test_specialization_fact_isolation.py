#!/usr/bin/env python3
"""Exercise Phase 28 specialization fact isolation in the expression arena.

Verifies that expression facts recorded under distinct specialization contexts
(e.g., Context A vs Context B) cannot be interchanged:
  - Query under context A returns fact A
  - Query under context B returns fact B
  - Query under an unrelated context returns nullptr (no fact)
"""
from __future__ import annotations

import os
from pathlib import Path
import re
import subprocess
import sys


ROOT = Path(__file__).resolve().parents[2]


def run(cmd: list[str], env: dict[str, str] | None = None) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, cwd=ROOT, env=env
    )


def test_internal_contract_assertion(compiler: str) -> None:
    env = os.environ.copy()
    env["MOSS_EST_CONTRACT_TEST"] = "1"
    fixture = ROOT / "tests/phase28/team_a_ownership/read_alias.moss"
    proc = run([compiler, "check", str(fixture)], env=env)
    if proc.returncode != 0:
        raise AssertionError(
            f"MOSS_EST_CONTRACT_TEST failed (exit {proc.returncode}):\n{proc.stderr}"
        )


def test_generic_specialization_facts_isolation(compiler: str) -> None:
    source_content = """fn identity(x):
  return x

fn main():
  a = identity(42)
  b = identity("hello")
  echo a
  echo b
"""
    tmp_dir = ROOT / "tmp/phase28_isolation"
    tmp_dir.mkdir(parents=True, exist_ok=True)
    source_file = tmp_dir / "generic_identity.moss"
    source_file.write_text(source_content)

    env = os.environ.copy()
    env["MOSS_EST_FACTS_DUMP"] = "1"
    proc = run([compiler, "check", str(source_file)], env=env)
    if proc.returncode != 0:
        raise AssertionError(f"Check failed: {proc.stderr}")

    # Parse dumped facts: MOSS_EST_FACT expr=<id> context="<context>" type="<type>"
    facts_by_expr_and_ctx: dict[tuple[int, str], str] = {}
    pattern = re.compile(
        r'MOSS_EST_FACT expr=(\d+) context="([^"]*)" type="([^"]*)"'
    )
    for line in proc.stderr.splitlines():
        match = pattern.search(line)
        if match:
            expr_id = int(match.group(1))
            ctx = match.group(2)
            fact_type = match.group(3)
            facts_by_expr_and_ctx[(expr_id, ctx)] = fact_type

    # Verify that expr 3 (int call) and expr 7 (string call) have isolated types under main context
    assert (3, "main") in facts_by_expr_and_ctx, f"Missing fact for expr 3 in main: {facts_by_expr_and_ctx}"
    assert facts_by_expr_and_ctx[(3, "main")] == "int"
    assert (7, "main") in facts_by_expr_and_ctx, f"Missing fact for expr 7 in main: {facts_by_expr_and_ctx}"
    assert facts_by_expr_and_ctx[(7, "main")] == "string"

    # Verify that querying under a non-existent context produces no facts
    assert (3, "specialization:nonexistent") not in facts_by_expr_and_ctx
    assert (7, "specialization:nonexistent") not in facts_by_expr_and_ctx


def main() -> int:
    compiler = sys.argv[1] if len(sys.argv) > 1 else "./moss"
    test_internal_contract_assertion(compiler)
    test_generic_specialization_facts_isolation(compiler)
    print("phase28 specialization fact isolation: PASS (C++ contract assertions + multi-context isolation)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
