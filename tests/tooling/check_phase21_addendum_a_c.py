#!/usr/bin/env python3
"""Reconstructed peer addendum A/C effects and exact recovery regressions.

The original p21_review_repros.zip is unavailable. These fixtures were
reconstructed from the written descriptions and are not an archive retest.
"""

import json
from pathlib import Path
import subprocess
import sys


repo = Path(__file__).resolve().parents[2]
compiler = Path(sys.argv[1]).resolve()
fixtures = repo / "tests/tooling/fixtures"
scratch = repo / "tmp/phase21-addendum-a-c"
scratch.mkdir(parents=True, exist_ok=True)


def run(*args):
    return subprocess.run([str(arg) for arg in args], cwd=repo,
                          capture_output=True, text=True, timeout=180)


for name in ("trailing_method_unhandled", "trailing_generic_unhandled"):
    unhandled = fixtures / f"phase21_addendum_{name}.moss"
    checked = run(compiler, "check", unhandled, "--json")
    assert checked.returncode != 0, (name, checked.stdout, checked.stderr)
    error = json.loads(checked.stdout)["error"]
    assert "main may not let typed errors escape" in error["message"], (name, error)

cases = (
    ("trailing_method_handled", "fn:via_local", ["enum:Failure:Bad"],
     "7\n8\n"),
    ("grouped_a", "fn:regroup_a", ["enum:Failure:A"], "outer A\n"),
    ("grouped_b", "fn:regroup_b", ["enum:Failure:B"], "B\n"),
    ("grouped_both", "fn:regroup_both",
     ["enum:Failure:A", "enum:Failure:B"], "A\nB\n"),
    ("grouped_root", "handler:Worker.Run", ["enum:Failure:A"], "1\n"),
)
for name, target, variants, output in cases:
    source = fixtures / f"phase21_addendum_{name}.moss"
    checked = run(compiler, "check", source, "--json")
    assert checked.returncode == 0, (name, checked.stdout, checked.stderr)
    result = json.loads(checked.stdout)
    assert result["ok"], (name, result)
    effects = run(compiler, "effects", target, "--source", source, "--json")
    assert effects.returncode == 0, (name, effects.stdout, effects.stderr)
    facts = json.loads(effects.stdout)["result"]["observable_effects"]
    assert facts["raise_set"] == variants and not facts["unresolved"], (
        name, facts)
    if name == "grouped_root":
        plan = json.loads(effects.stdout)["result"]["synchronization_plan"]
        handlers = plan["domains"][0]["handlers"]
        normal = next(handler for handler in handlers if handler["name"] == "Run")
        failure = next(handler for handler in handlers if handler["name"] == "Run$fail")
        assert normal["write_set"] == ["left"], normal
        assert failure["write_set"] == ["right"], failure
        assert normal["class_set"] != failure["class_set"], (normal, failure)
    lowered = scratch / f"{name}.rs"
    generated = run(compiler, source, "-o", lowered)
    assert generated.returncode == 0, (name, generated.stdout, generated.stderr)
    for optimization in ("0", "3"):
        executable = scratch / f"{name}-o{optimization}"
        built = run("rustc", "--edition=2021", "-D", "warnings",
                    f"-Copt-level={optimization}", lowered, "-o", executable)
        assert built.returncode == 0, (name, optimization, built.stderr)
        native = run(executable)
        assert native.returncode == 0 and native.stdout == output, (
            name, optimization, native.returncode, native.stdout, native.stderr)
    debug = run(compiler, "run", "--interp", source)
    assert debug.returncode == 0 and debug.stdout == output, (
        name, debug.returncode, debug.stdout, debug.stderr)

print("Phase 21 reconstructed peer addendum A/C effects and recovery passed.")
