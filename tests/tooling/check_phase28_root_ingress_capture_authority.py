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
    ("tests/phase28/oracle/fixtures/oracle_43_handler_primitive_reply.moss", 1),
    ("tests/phase28/oracle/fixtures/oracle_45_handler_primitive_rebind_local.moss", 1),
    ("tests/tooling/fixtures/phase28_root_same_line_messages.moss", 2),
]
ALIAS_REJECTED = "tests/tooling/fixtures/phase28_root_domain_handle_alias_rejected.moss"
UNKNOWN_HANDLER = "tests/tooling/fixtures/phase28_root_unknown_handler.moss"


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
    scenario_count = 0
    for source, expected_fact_count in FIXTURES:
        baseline = check(source, {"MOSS_TEST_DUMP_ROOT_INGRESS_DOMAIN_CAPTURE_FACT": "1"})
        response = parse_result(baseline)
        assert baseline.returncode == 0 and response.get("ok"), baseline.stdout
        facts = [line for line in baseline.stderr.splitlines()
                 if line.startswith("ROOT_INGRESS_DOMAIN_CAPTURE_FACT ")]
        assert len(facts) == expected_fact_count, (source, facts)
        for fact in facts:
            for field in ("stmt=", "arena=", "expr=", "receiver=", "context=main",
                          "est_context=main", "instance=main::w", "handler=handler:Worker.",
                          "call_edge=main|handler:Worker."):
                assert field in fact, (source, field, fact)
        if expected_fact_count == 2:
            assert len(set(facts)) == 2, (source, facts)
            assert len({part.split("expr=", 1)[1].split()[0] for part in facts}) == 2, facts
            edge_occurrences = {
                fact.split("call_edge=", 1)[1].split("|receiver=", 1)[0].rsplit(":", 1)[1]
                for fact in facts
            }
            assert len(edge_occurrences) == 2, facts
        scenario_count += 1

        dropped = check(source, {"MOSS_TEST_DROP_ROOT_INGRESS_DOMAIN_CAPTURE_FACT": "1"})
        assert dropped.returncode != 0, (source, dropped.stdout)
        assert "Root-ingress domain capture checker fact/requirement set is incomplete" in (
            dropped.stdout + dropped.stderr)
        scenario_count += 1

        duplicate = check(source, {
            "MOSS_TEST_DUPLICATE_ROOT_INGRESS_DOMAIN_CAPTURE_FACT": "1"
        })
        assert duplicate.returncode != 0, (source, duplicate.stdout)
        assert "Root-ingress domain capture checker fact/requirement set is incomplete" in (
            duplicate.stdout + duplicate.stderr)
        scenario_count += 1

        missing_edge = check(source, {
            "MOSS_TEST_DROP_ROOT_INGRESS_MESSAGE_EDGE": "1"
        })
        assert missing_edge.returncode != 0, (source, missing_edge.stdout)
        assert "exact checked Root-ingress message call edge" in missing_edge.stdout
        scenario_count += 1
        perturbed_edge = check(source, {
            "MOSS_TEST_PERTURB_ROOT_INGRESS_MESSAGE_EDGE": "1"
        })
        assert perturbed_edge.returncode != 0, (source, perturbed_edge.stdout)
        assert "exact checked Root-ingress message call edge" in perturbed_edge.stdout
        scenario_count += 1

        for field in ("instance", "route", "handler", "call-edge",
                      "occurrence", "context"):
            corrupted = check(source, {
                "MOSS_TEST_PERTURB_ROOT_INGRESS_DOMAIN_CAPTURE_FACT": field
            })
            assert corrupted.returncode != 0, (source, field, corrupted.stdout)
            assert "Root-ingress domain capture" in (
                corrupted.stdout + corrupted.stderr), corrupted.stdout + corrupted.stderr
            scenario_count += 1

    alias = check(ALIAS_REJECTED)
    alias_response = parse_result(alias)
    assert alias.returncode != 0 and not alias_response.get("ok"), alias.stdout
    assert "domain handles cannot be used as ordinary values or payloads" in (
        alias.stdout + alias.stderr)
    scenario_count += 1

    unknown = check(UNKNOWN_HANDLER)
    unknown_response = parse_result(unknown)
    assert unknown.returncode != 0 and not unknown_response.get("ok"), unknown.stdout
    assert unknown_response.get("error", {}).get("code") != "MOSS_INTERNAL_INVARIANT", unknown.stdout
    assert "no message handler" in unknown_response.get("error", {}).get("message", "").lower(), unknown.stdout
    scenario_count += 1

    print(f"Phase 28 Root-ingress domain capture authority: {scenario_count} scenarios PASS")


if __name__ == "__main__":
    main()
