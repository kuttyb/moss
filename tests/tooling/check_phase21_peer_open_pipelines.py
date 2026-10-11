#!/usr/bin/env python3
"""Reconstructed external addendum D/E: checked chunk pipeline propagation.

The original p21_open_at_7775049.zip was unavailable. These Moss sources
retain the expressions and expected results from the L7 review description.
"""

import json
from pathlib import Path
import subprocess
import sys


ROOT = Path(__file__).resolve().parents[2]
COMPILER = Path(sys.argv[1] if len(sys.argv) > 1 else ROOT / "moss").resolve()
OUT = ROOT / "tmp/phase21-peer-open-pipelines"
OUT.mkdir(parents=True, exist_ok=True)


def run(*args, expected=0):
    result = subprocess.run([str(x) for x in args], cwd=ROOT,
                            capture_output=True, text=True, timeout=180)
    assert result.returncode == expected, (
        args, result.returncode, result.stdout, result.stderr)
    return result


cases = (
    ("d_placeholder_mapper", "d", "p4.bin", b"ABABC", "enum:FormatError:Bad"),
    ("e_reduce_seed", "e", "p6.bin", b"ABCD", "enum:SetupError:Bad"),
)
for case, letter, data_name, contents, variant in cases:
    data_dir = ROOT / f"tmp/phase21-peer-open-{letter}"
    data_dir.mkdir(parents=True, exist_ok=True)
    (data_dir / data_name).write_bytes(contents)
    source = ROOT / "tests/tooling/fixtures" / f"phase21_peer_open_{case}.moss"
    checked = json.loads(run(COMPILER, "check", source, "--json").stdout)
    assert checked["ok"], checked
    facts = json.loads(run(COMPILER, "effects", "Reader.Run", "--source",
                           source, "--json").stdout)["result"]["observable_effects"]
    file_errors = {f"enum:std__FileError:{name}" for name in (
        "NotFound", "PermissionDenied", "NotRegularFile", "InUse",
        "Full", "IO")}
    assert set(facts["raise_set"]) == file_errors | {variant}, facts
    assert not facts["unresolved"], facts

    rust = OUT / f"{case}.rs"
    run(COMPILER, source, "-o", rust)
    for optimization, suffix in (("0", "debug"), ("3", "optimized")):
        native = OUT / f"{case}-{suffix}"
        run("rustc", "--edition=2021", "-Dwarnings",
            "-C", f"opt-level={optimization}", rust, "-o", native)
        assert run(native).stdout == "-1\n", (case, suffix)

print("Phase 21 reconstructed D/E chunk pipeline propagation passed.")
