#!/usr/bin/env python3
"""Verify checker-published selector argument effects in exact EST contexts."""
from __future__ import annotations

import os
from pathlib import Path
import re
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]
FIXTURE = ROOT / "tests/phase28/team_a_ownership/take_selector_argument_effects.moss"


def main() -> int:
    if len(sys.argv) != 2:
        raise SystemExit("usage: check_phase28_selector_argument_effects.py COMPILER")
    env = os.environ.copy()
    env["MOSS_EST_FACTS_DUMP"] = "1"
    proc = subprocess.run([sys.argv[1], "check", str(FIXTURE), "--json"],
                          cwd=ROOT, env=env, text=True, capture_output=True,
                          check=False)
    if proc.returncode:
        raise AssertionError(f"fixture rejected:\n{proc.stdout}\n{proc.stderr}")

    publications = re.findall(
        r'MOSS_EST_SELECTOR_ARGUMENT_EFFECT call=(\d+) argument=(\d+) '
        r'context="([^"]+)" target="([^"]+)" specialization="([^"]*)" '
        r'effect=(\d+)', proc.stderr)
    # The first three records exercise actual checked READ, WRITE and CONSUME
    # decisions, each nested in a take selector and published into its primary
    # checker context.
    for context, expected in (("fn:take_read", 0), ("fn:take_write", 1),
                              ("fn:take_consume", 2)):
        rows = [row for row in publications if row[2] == context]
        if len(rows) != 1 or int(rows[0][5]) != expected:
            raise AssertionError(f"expected {context} effect {expected}: {rows!r}")

    # The exact same retained EST call is checked in two distinct generic
    # specializations. Each primary context must retain its own argument fact.
    specialized = [row for row in publications
                   if row[2].startswith("specialization:specialization-v2:")]
    contexts = {row[2] for row in specialized}
    occurrences = {row[0] for row in specialized}
    if len(specialized) != 2 or len(contexts) != 2 or len(occurrences) != 1:
        raise AssertionError(
            f"expected same EST call in two isolated specialization contexts: "
            f"{specialized!r}")
    if any(row[5] != "0" for row in specialized):
        raise AssertionError(f"specialized READ effects changed: {specialized!r}")

    # Every publication must be retrievable from its precise child ExprId and
    # context in the EST fact dump, not merely from source:<file>:<line>.
    for call, argument, context, target, specialization, effect in publications:
        fact = re.compile(
            rf'MOSS_EST_FACT expr={argument} context="{re.escape(context)}" '
            rf'.*?target="" effect={effect}(?:\s|$)')
        if not fact.search(proc.stderr):
            raise AssertionError(
                f"missing exact primary EST effect for {(call, argument, context, effect)!r}")

    # Abstract trait dispatch has a target but no checked argument-effect vector
    # in the current checker interface. It must not fabricate a primary fact.
    if any("trait-method:Position.position" in row[3] for row in publications):
        raise AssertionError(f"unsupported abstract trait effect was published: {publications!r}")
    trait_calls = re.findall(
        r'MOSS_EST_FACT expr=(\d+) context="fn:index_specialized" '
        r'.*?target="trait-method:Position.position" effect=', proc.stderr)
    if len(trait_calls) != 1:
        raise AssertionError("fixture no longer exercises one abstract trait call")
    if re.search(r'MOSS_EST_FACT expr=\d+ context="fn:index_specialized" '
                 r'.*?effect=[012](?:\s|$)', proc.stderr):
        raise AssertionError("effect was fabricated in the unsupported abstract trait context")

    print("phase28 selector argument effects: PASS (READ/WRITE/CONSUME, exact EST identity, specialization isolation, unsupported trait dispatch fail-closed)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
