#!/usr/bin/env python3
"""Exercise exact checker authority for compiler-generated Root captures."""

import json
import os
import subprocess
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
COMPILER = str(Path(sys.argv[1]).resolve())
FIXTURES = [
    "tests/phase28/oracle/fixtures/oracle_43_handler_primitive_reply.moss",
    "tests/phase28/oracle/fixtures/oracle_45_handler_primitive_rebind_local.moss",
]
ALIAS_REJECTED = "tests/tooling/fixtures/phase28_root_domain_handle_alias_rejected.moss"


def check(source, extra_env=None):
    env = os.environ.copy()
    for key in list(env):
        if key.startswith("MOSS_TEST_"):
            env.pop(key)
    env.update(extra_env or {})
    return subprocess.run(
        [COMPILER, "check", source, "--json"], cwd=ROOT, env=env,
        text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE, check=False
    )


def parse_result(result):
    try:
        return json.loads(result.stdout)
    except json.JSONDecodeError as error:
        raise AssertionError(
            f"checker returned malformed JSON: {result.stdout}\n{result.stderr}") from error


def main():
    for source in FIXTURES:
        baseline = check(source, {"MOSS_TEST_DUMP_ROOT_INGRESS_DOMAIN_CAPTURE_FACT": "1"})
        response = parse_result(baseline)
        assert baseline.returncode == 0 and response.get("ok"), baseline.stdout
        facts = [line for line in baseline.stderr.splitlines()
                 if line.startswith("ROOT_INGRESS_DOMAIN_CAPTURE_FACT ")]
        assert len(facts) == 1, (source, facts)
        fact = facts[0]
        for field in ("stmt=", "arena=", "expr=", "receiver=", "context=main",
                      "est_context=main", "instance=main::w", "handler=handler:Worker.",
                      "call_edge=main:"):
            assert field in fact, (source, field, fact)

        dropped = check(source, {"MOSS_TEST_DROP_ROOT_INGRESS_DOMAIN_CAPTURE_FACT": "1"})
        assert dropped.returncode != 0, (source, dropped.stdout)
        assert "Root-ingress domain capture checker fact/requirement set is incomplete" in (
            dropped.stdout + dropped.stderr)

        for field in ("instance", "route", "handler", "call-edge",
                      "occurrence", "context"):
            corrupted = check(source, {
                "MOSS_TEST_PERTURB_ROOT_INGRESS_DOMAIN_CAPTURE_FACT": field
            })
            assert corrupted.returncode != 0, (source, field, corrupted.stdout)
            assert "Root-ingress domain capture" in (
                corrupted.stdout + corrupted.stderr), corrupted.stdout + corrupted.stderr

    alias = check(ALIAS_REJECTED)
    alias_response = parse_result(alias)
    assert alias.returncode != 0 and not alias_response.get("ok"), alias.stdout
    assert "domain handles cannot be used as ordinary values or payloads" in (
        alias.stdout + alias.stderr)

    print("Phase 28 Root-ingress domain capture authority: 17 scenarios PASS")


if __name__ == "__main__":
    main()
