#!/usr/bin/env python3
"""Check bounded leading-condition placement against 2PL and action barriers."""

import json
from pathlib import Path
import subprocess
import sys


repo = Path(__file__).resolve().parents[2]
compiler = Path(sys.argv[1]).resolve()
fixtures = repo / "tests/tooling/fixtures"
scratch = repo / "tmp/phase21-early-placement-proof"
scratch.mkdir(parents=True, exist_ok=True)


def run(*args):
    return subprocess.run([str(arg) for arg in args], cwd=repo,
                          capture_output=True, text=True, timeout=180)


for name, expected in (("state_write", "1\n2\n"),
                       ("observable", "1\n1\n")):
    source = fixtures / f"phase21_early_condition_{name}.moss"
    checked = run(compiler, "check", source, "--json")
    assert checked.returncode == 0, (name, checked.stdout, checked.stderr)
    queried = run(compiler, "effects", "handler:Worker.Run", "--source",
                  source, "--json")
    assert queried.returncode == 0, (name, queried.stdout, queried.stderr)
    result = json.loads(queried.stdout)["result"]
    domain = result["synchronization_plan"]["domains"][0]
    handler = next(item for item in domain["handlers"] if item["name"] == "Run")
    placement = handler["path_placement"]
    classes = domain["leaf_to_class"]
    low = classes["a_low"]
    condition = classes["b_condition.value"]
    high = classes["c_high"]
    if name == "state_write":
        assert result["observable_effects"]["external_io"] is False, result
        assert placement["kind"] == "leading_conditional_continuation_split", placement
        assert low in placement["entry"] and condition in placement["entry"], placement
        assert placement["cancel_else"] == [low], placement
        assert condition not in placement["cancel_else"], placement
        assert high in placement["else"], placement
    else:
        assert result["observable_effects"]["external_io"] is True, result
        assert placement["kind"] == "entry", placement
        assert set(handler["class_modes"]) == {low, condition, high}, handler
        assert not placement["cancel_else"], placement

    generated = scratch / f"{name}.rs"
    lowered = run(compiler, source, "-o", generated)
    assert lowered.returncode == 0, (name, lowered.stdout, lowered.stderr)
    binary = scratch / name
    compiled = run("rustc", "--edition=2021", "-D", "warnings",
                   generated, "-o", binary)
    assert compiled.returncode == 0, (name, compiled.stderr)
    native = run(binary)
    assert native.returncode == 0 and native.stdout == expected, (
        name, native.returncode, native.stdout, native.stderr)
    debug = run(compiler, "run", "--interp", source)
    assert debug.returncode == 0 and debug.stdout == expected, (
        name, debug.returncode, debug.stdout, debug.stderr)

print("Phase 21 bounded early-placement plan and barrier witnesses passed.")
