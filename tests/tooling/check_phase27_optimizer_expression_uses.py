#!/usr/bin/env python3
"""Exercise optimizer decisions and three-engine parity for EST occurrences."""
import os
from pathlib import Path
import re
import subprocess
import sys
import time

ROOT = Path(__file__).resolve().parents[2]
COMPILER = Path(sys.argv[1]).resolve()
BASELINE = Path(sys.argv[2]).resolve() if len(sys.argv) > 2 else None
MARKER = "777777"
CASES = [
    ("identical_pipelines_one_statement.moss", "", 6, False),
    ("field_name_not_binding_use.moss",
     "intermediate total: materialized; reason: escapes optimization scope", 3, False),
    ("rebound_pipeline_binding.moss",
     "reason: binding is mutable or reassigned", 9, False),
    ("message_argument_binding_use.moss",
     "reason: use is outside the adjacent functional region", 3, False),
    ("callback_reads_pipeline_binding.moss",
     "reason: multiple consumers", 3, False),
    ("missing_dependency_handle_blocks_fusion.moss",
     "incomplete expression dependency collection", 3, True),
]


def run(args, env):
    return subprocess.run(args, cwd=ROOT, env=env, text=True,
                          stdout=subprocess.PIPE, stderr=subprocess.PIPE)


def output(args, env):
    result = run(args, env)
    if result.returncode:
        raise SystemExit(result.stdout + result.stderr)
    return result.stdout, result.stderr


def three_engine(compiler, source, env, expected_callbacks):
    fast, _ = output([str(compiler), "run", "--interp", str(source)], env)
    work = ROOT / "tmp" / "phase27-optimizer-expression-uses" / str(time.time_ns())
    work.mkdir(parents=True)
    rust = work / "program.rs"
    output([str(compiler), str(source), "-O", "-o", str(rust)], env)
    emitted = rust.read_text(encoding="utf-8")
    values = [fast]
    for label, flags in (("native", []), ("optimized", ["-O"])):
        binary = work / label
        built = run(["rustc", "--edition=2021", *flags, str(rust), "-o", str(binary)], env)
        if built.returncode:
            raise SystemExit(built.stdout + built.stderr)
        values.append(output([str(binary)], env)[0])
    counts = [sum(line.strip() == MARKER for line in value.splitlines())
              for value in values]
    if not (values[0] == values[1] == values[2] and
            counts[0] == counts[1] == counts[2] == expected_callbacks):
        raise SystemExit(f"three-engine mismatch for {source.name}: {counts}")
    return values[0], counts[0]


def main():
    for name, expected, callback_count, inject_missing in CASES:
        source = ROOT / "tests/phase27/conformance" / name
        env = os.environ.copy()
        env.pop("MOSS_EST", None)
        if inject_missing:
            env["MOSS_TEST_MISSING_EXPRESSION_HANDLE"] = "11:7:0"
        else:
            env.pop("MOSS_TEST_MISSING_EXPRESSION_HANDLE", None)
        dump_output = (ROOT / "tmp" / "phase27-optimizer-expression-uses" /
                       name / "decision.rs")
        dump_output.parent.mkdir(parents=True, exist_ok=True)
        dump, diagnostics = output(
            [str(COMPILER), str(source), "-O", "--dump-functional-ir",
             "-o", str(dump_output)], env)
        if expected and expected not in dump:
            raise SystemExit(f"missing expected decision for {name}: {expected}\n{dump}")
        if inject_missing and "MOSS_INTERNAL_INVARIANT missing expression handle injected" not in diagnostics:
            raise SystemExit("missing-handle injection did not reach dependency collection")
        if name == "identical_pipelines_one_statement.moss":
            plans = re.findall(
                r"ExpressionPlan \[main\] line (\d+) pipeline=%(\d+) "
                r"slot=(\d+) argument=(\d+) ExprId=(\d+)", dump)
            pair = [entry for entry in plans if entry[3] in {"0", "1"}]
            if len(pair) != 2 or pair[0][0] != pair[1][0] or \
                    {entry[3] for entry in pair} != {"0", "1"} or \
                    len({entry[1] for entry in pair}) != 2 or \
                    len({entry[4] for entry in pair}) != 2:
                raise SystemExit("identical expression plans lost occurrence identity:\n" + dump)
        candidate_signature = three_engine(
            COMPILER, source, env, callback_count)
        print(f"{name}: decision and three-engine parity passed; callbacks={callback_count}")
        if name == "field_name_not_binding_use.moss" and BASELINE:
            baseline_dump, _ = output(
                [str(BASELINE), str(source), "-O", "--dump-functional-ir",
                 "-o", str(dump_output)], env)
            old_decision = "intermediate total: materialized; reason: multiple consumers"
            if old_decision not in baseline_dump:
                raise SystemExit("baseline field-name reproducer no longer reaches old reader")
            baseline_signature = three_engine(
                BASELINE, source, env, callback_count)
            if baseline_signature != candidate_signature:
                raise SystemExit("baseline/candidate field-name three-engine outputs differ")
            print("field-name LEGACY MISPARSE two-path decision delta reproduced")


if __name__ == "__main__":
    main()
