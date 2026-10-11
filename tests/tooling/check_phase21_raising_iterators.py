#!/usr/bin/env python3
"""Implicit iterator calls must propagate typed raises or fail at Moss checking."""

import json
from pathlib import Path
import subprocess
import sys


repo = Path(__file__).resolve().parents[2]
compiler = Path(sys.argv[1]).resolve()
fixtures = repo / "tests/tooling/fixtures"
scratch = repo / "tmp/phase21-raising-iterators"
scratch.mkdir(parents=True, exist_ok=True)


def run(*args):
    return subprocess.run([str(arg) for arg in args], cwd=repo,
                          capture_output=True, text=True, timeout=180)


for kind in ("next", "factory"):
    source = fixtures / f"phase21_iterator_{kind}_raise.moss"
    checked = run(compiler, "check", source, "--json")
    result = json.loads(checked.stdout)
    if checked.returncode != 0:
        # A conservative checker restriction is sound while checked iterator
        # lowering is unavailable. A native Rust type error is never allowed.
        assert result["error"]["code"] == "RAISING_ITERATOR_UNSUPPORTED", result
        continue

    assert result["ok"] is True, result
    assert not any(item["code"] == "ON_FAIL_UNREACHABLE_VARIANT"
                   for item in result["result"]["diagnostics"]), result
    effects = run(compiler, "effects", "handler:Worker.Run", "--source",
                  source, "--json")
    assert effects.returncode == 0, (effects.stdout, effects.stderr)
    facts = json.loads(effects.stdout)["result"]["observable_effects"]
    assert facts["raise_set"] == ["enum:Failure:Bad"], facts
    assert facts["may_panic"] is False, facts

    generated = scratch / f"{kind}.rs"
    lowered = run(compiler, source, "-o", generated)
    assert lowered.returncode == 0, (kind, lowered.stdout, lowered.stderr)
    for opt in ("0", "3"):
        binary = scratch / f"{kind}-o{opt}"
        compiled = run("rustc", "--edition=2021", "-D", "warnings",
                       f"-Copt-level={opt}", generated, "-o", binary)
        assert compiled.returncode == 0, (kind, opt, compiled.stderr)
        native = run(binary)
        assert native.returncode == 0 and native.stdout == "7\n", (
            kind, opt, native.returncode, native.stdout, native.stderr)

print("Phase 21 raising iterator effects and native boundary passed.")
