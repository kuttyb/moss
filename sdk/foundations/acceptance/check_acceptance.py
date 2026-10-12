#!/usr/bin/env python3
"""Run the Foundations package composition and boundary contract gates."""

import os
import subprocess
from pathlib import Path


ROOT = Path(__file__).resolve().parents[3]
PROJECT = Path(__file__).resolve().parent
MARGO = ROOT / "margo"
ENV = os.environ.copy()
ENV["PATH"] = str(ROOT) + os.pathsep + ENV.get("PATH", "")
ENV["TMPDIR"] = str(ROOT / "tmp")


def run(folder, args, expected=0, token=None):
    result = subprocess.run([str(MARGO), *args], cwd=folder, env=ENV,
                            text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    output = result.stdout + result.stderr
    if expected == 0 and result.returncode != 0:
        raise AssertionError((folder, args, result.returncode, output))
    if expected != 0 and result.returncode == 0:
        raise AssertionError((folder, args, "expected failure", output))
    if token and token not in output:
        raise AssertionError((folder, args, f"missing {token!r}", output))
    return output


for args in (["run"], ["run", "--release"], ["debug"]):
    run(PROJECT, args, token="foundations acceptance ok")
run(PROJECT, ["test", "--json"], token='"passed": 4')

for name in ("invalid_byte_copy", "inverted_clamp"):
    folder = PROJECT / "repros" / name
    for args in (["run"], ["run", "--release"], ["debug"]):
        run(folder, args, expected=1, token="assertion failed")

for name in ("nested_generic_int", "nested_generic_string"):
    folder = PROJECT / "repros" / name
    for args in (["run"], ["run", "--release"], ["debug"]):
        run(folder, args)

print("Foundations cross-package O0/release/Fast Debug and negative contracts passed")
