#!/usr/bin/env python3
"""Exercise test-build checked-fact perturbation/removal fail-closed paths."""
from __future__ import annotations

import os
from pathlib import Path
import subprocess
import sys
import time

ROOT = Path(__file__).resolve().parents[2]
COMPILER = Path(sys.argv[1]).resolve()
OPT_COMPILER = Path(sys.argv[2]).resolve() if len(sys.argv) > 2 else None
FIXTURES = ROOT / "tests/tooling/fixtures/phase27_5_1"
CASES = {
    "call-targets": [
        ROOT / "tests/phase27/est/side_facts.moss",
        ROOT / "tests/phase27/est/specialized_call_facts.moss",
        FIXTURES / "call_edges.moss",
    ],
    "specializations": [
        ROOT / "tests/phase27/est/specialized_call_facts.moss",
        FIXTURES / "call_edges.moss",
        ROOT / "tests/tooling/fixtures/generic_specialization/identity.moss",
    ],
    "joins": [FIXTURES / "join_if.moss", FIXTURES / "join_match.moss",
              FIXTURES / "join_try.moss"],
    "patterns": [FIXTURES / "pattern_local_payload.moss",
                 FIXTURES / "pattern_parenthesized.moss",
                 ROOT / "tests/tooling/fixtures/phase27_5_match_copy.moss"],
}


def run(args: list[str], env: dict[str, str]) -> subprocess.CompletedProcess[str]:
    return subprocess.run(args, cwd=ROOT, env=env, text=True,
                          stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                          timeout=180)


def require_fail_closed(result: subprocess.CompletedProcess[str], label: str) -> None:
    if result.returncode == 0 or "MOSS_INTERNAL_INVARIANT" not in result.stderr:
        raise SystemExit(f"{label} did not fail closed:\n{result.stdout}{result.stderr}")


def main() -> None:
    work = ROOT / "tmp/phase27-5-4-fact-hooks" / str(time.time_ns())
    work.mkdir(parents=True)
    cases = {name: list(sources) for name, sources in CASES.items()}
    selected = os.environ.get("MOSS_TEST_AGENT2_FIXTURE")
    if selected:
        path = Path(selected)
        if not path.is_absolute():
            path = ROOT / path
        fact_class = os.environ.get("MOSS_TEST_AGENT2_CLASS")
        if fact_class not in cases:
            raise SystemExit("MOSS_TEST_AGENT2_CLASS must name one fact class")
        cases[fact_class].append(path)
    for fact_class, sources in cases.items():
        if len(sources) < 3:
            raise SystemExit(f"{fact_class}: fewer than three fixtures")
        for index, source in enumerate(sources):
            checked = run([str(COMPILER), "check", str(source), "--json"],
                          os.environ.copy())
            if checked.returncode:
                raise SystemExit(f"checker rejected fixture {source}: "
                                 f"{checked.stdout}{checked.stderr}")
            for action, variable in (("perturb", "MOSS_TEST_PERTURB"),
                                     ("drop", "MOSS_TEST_DROP_FACT")):
                env = os.environ.copy()
                env.pop("MOSS_TEST_DISABLE_GUARD_HOOKS", None)
                env.pop("MOSS_TEST_PERTURB", None)
                env.pop("MOSS_TEST_DROP_FACT", None)
                env[variable] = fact_class
                fast = run([str(COMPILER), "run", "--interp", str(source)], env)
                require_fail_closed(fast,
                    f"Fast Debug {action} {fact_class} fixture {source}")
                for label, flags in (("native", ["--debug"]),
                                     ("optimized", ["-O"])):
                    output = work / f"{fact_class}-{action}-{index}-{label}.rs"
                    lowered = run([str(COMPILER), str(source), *flags,
                                   "-o", str(output)], env)
                    require_fail_closed(lowered,
                        f"{label} {action} {fact_class} fixture {source}")
            if OPT_COMPILER and index == 0:
                for variable in ("MOSS_TEST_PERTURB", "MOSS_TEST_DROP_FACT"):
                    env = os.environ.copy()
                    env[variable] = fact_class
                    ignored = run([str(OPT_COMPILER), "run", "--interp",
                                   str(source)], env)
                    if ignored.returncode:
                        raise SystemExit(
                            f"optimized non-test build honored {variable}="
                            f"{fact_class} for {source}: "
                            f"{ignored.stdout}{ignored.stderr}")
        print(f"PASS {fact_class}: {len(sources)} fixtures, perturb/drop, "
              "Fast Debug/native/-O fail closed")
    print("Phase 27.5.4 checked-fact perturb/drop hooks passed")


if __name__ == "__main__":
    main()
