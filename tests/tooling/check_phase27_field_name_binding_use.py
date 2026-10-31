#!/usr/bin/env python3
"""A field with the binding's name is not a use of that binding."""
import os
from pathlib import Path
import subprocess
import sys
import time

ROOT = Path(__file__).resolve().parents[2]
COMPILER = Path(sys.argv[1]).resolve()
SOURCE = ROOT / "tests/phase27/conformance/field_name_not_binding_use.moss"
CALLBACK_MARKER = "777777"


def run(args, env):
    return subprocess.run(args, cwd=ROOT, env=env, text=True,
                          capture_output=True)


def checked(args, env):
    result = run(args, env)
    if result.returncode:
        raise SystemExit(result.stdout + result.stderr)
    return result.stdout


def main():
    env = os.environ.copy()
    env["MOSS_EST"] = "off"
    explanation = checked(
        [str(COMPILER), str(SOURCE), "-O", "--explain-fusion"], env)
    expected = "intermediate total: materialized; reason: escapes optimization scope"
    if expected not in explanation:
        raise SystemExit("field access was counted as a binding use:\n" + explanation)

    fast = checked([str(COMPILER), "run", "--interp", str(SOURCE)], env)
    work = ROOT / "tmp" / "phase27-field-binding-use" / str(time.time_ns())
    work.mkdir(parents=True)
    rust = work / "program.rs"
    checked([str(COMPILER), str(SOURCE), "-O", "-o", str(rust)], env)
    outputs = [fast]
    for label, flags in (("native", []), ("optimized", ["-O"])):
        binary = work / label
        built = run(["rustc", "--edition=2021", *flags, str(rust), "-o", str(binary)], env)
        if built.returncode:
            raise SystemExit(built.stdout + built.stderr)
        outputs.append(checked([str(binary)], env))
    counts = [sum(line.strip() == CALLBACK_MARKER for line in output.splitlines())
              for output in outputs]
    if not (outputs[0] == outputs[1] == outputs[2] and
            counts[0] == counts[1] == counts[2] == 3):
        raise SystemExit("three-engine output/callback mismatch: " + repr(counts))
    print("field-name binding regression passed; callback invocations=3")


if __name__ == "__main__":
    main()
