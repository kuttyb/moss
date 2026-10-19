#!/bin/sh
set -eu

compiler=${1:-./moss}
test_build=${2:-build/tests/phase106_runtime}
mkdir -p "$test_build"

. "$(dirname "$0")/../lib_test.sh"

PYTHONDONTWRITEBYTECODE=1 python3 tests/tooling/check_handler_2pl.py "$compiler" "$test_build/phase106d"
PYTHONDONTWRITEBYTECODE=1 python3 tests/tooling/check_borrowed_reads.py "$compiler" "$test_build/phase106d1"
PYTHONDONTWRITEBYTECODE=1 python3 tests/tooling/check_phase151_borrowed_payloads.py "$compiler" "$test_build/phase151"
PYTHONDONTWRITEBYTECODE=1 python3 tests/tooling/check_phase106e_domains.py "$compiler" "$test_build/phase106e"
# Phase 20 Agent C executor/root runtime proof regressions (full suite).
PYTHONDONTWRITEBYTECODE=1 python3 tests/tooling/check_phase20c_executor.py "$compiler"

PYTHONDONTWRITEBYTECODE=1 python3 tests/tooling/check_phase106f.py "$compiler" "$test_build/phase106f"


if command -v python3 >/dev/null 2>&1; then
  python3 tests/tooling/check_phase106f1.py "$compiler" "$test_build/phase106f1" ||
    fail 'static typed synchronization lowering regression'
  python3 tests/tooling/check_phase15_14_swarm_038_051.py "$compiler" "$test_build/phase15_14" ||
    fail 'Phase 15.14 SWARM-038/SWARM-051 regression'
fi
