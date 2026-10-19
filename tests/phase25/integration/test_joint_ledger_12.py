#!/usr/bin/env python3
"""G5-02: close an open Vector receiver through structured member admission."""

from __future__ import annotations

import json
import os
from pathlib import Path
import subprocess


ROOT = Path(__file__).resolve().parents[3]
SOURCE = Path(__file__).with_suffix(".moss")
MOSS = ROOT / "moss"
SCRATCH = ROOT / "tmp/phase25-g5-joint-ledger-12"
EXPECTED = "3\n"


def run(command: list[str], *, output: str = EXPECTED) -> str:
    process = subprocess.run(command, cwd=ROOT, text=True, capture_output=True,
                             check=False, env=dict(os.environ, TMPDIR=str(ROOT / "tmp")))
    assert process.returncode == 0, (command, process.stdout, process.stderr)
    assert process.stdout == output, (command, process.stdout, output)
    return process.stdout


def main() -> None:
    assert MOSS.is_file() and SOURCE.is_file()
    SCRATCH.mkdir(parents=True, exist_ok=True)
    checked = subprocess.run([str(MOSS), "check", str(SOURCE), "--json"],
                             cwd=ROOT, text=True, capture_output=True, check=False)
    assert checked.returncode == 0, checked.stdout + checked.stderr
    assert json.loads(checked.stdout)["ok"] is True
    run([str(MOSS), "run", "--interp", str(SOURCE)])
    print("G5-02 Fast Debug open Vector member admission PASS")
    for profile, moss_flag, rustc_flag in (
        ("debug", "--debug", "-g"),
        ("release", "-O", "-O"),
    ):
        generated = SCRATCH / f"ledger_12_{profile}.rs"
        executable = SCRATCH / f"ledger_12_{profile}"
        lowering = subprocess.run([str(MOSS), str(SOURCE), moss_flag,
                                   "-o", str(generated)], cwd=ROOT, text=True,
                                  capture_output=True, check=False)
        assert lowering.returncode == 0, lowering.stdout + lowering.stderr
        built = subprocess.run(["rustc", "--edition=2021", "-D", "warnings",
                                rustc_flag, str(generated), "-o", str(executable)],
                               cwd=ROOT, text=True, capture_output=True, check=False)
        assert built.returncode == 0, built.stdout + built.stderr
        run([str(executable)])
        print(f"G5-02 Native {profile} open Vector member admission PASS")
    print("G5-02 three-engine joint ledger #12 PASS")


if __name__ == "__main__":
    main()
