#!/usr/bin/env python3
"""Run Team T package and cross-package acceptance checks.

The default suite is bounded for the repository check gate.  ``--full`` also
runs the deterministic Python UTF-8 differential corpus.
"""

import argparse
import os
from pathlib import Path
import subprocess
import sys


PROJECT = Path(__file__).resolve().parent
REPO = PROJECT.parents[1]
MARGO = REPO / "margo"
TEXT = PROJECT.parent / "text"
ENCODING = PROJECT.parent / "encoding"


def run(command: list[str], cwd: Path, env: dict[str, str]) -> None:
    subprocess.run(command, cwd=cwd, env=env, check=True)


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--full", action="store_true")
    args = parser.parse_args()

    env = os.environ.copy()
    env.update(
        MOSS=str(REPO / "moss"),
        MARGO_HOME=str(REPO / "tmp" / "margo-text-encoding-acceptance"),
        TMPDIR=str(REPO / "tmp"),
    )
    (REPO / "tmp").mkdir(exist_ok=True)

    for package in (TEXT, ENCODING):
        run([str(MARGO), "test"], package, env)
        run([str(MARGO), "test", "--interp"], package, env)

    for command in (
        [str(MARGO), "test"],
        [str(MARGO), "test", "--interp"],
        [str(MARGO), "run"],
        [str(MARGO), "--release", "run"],
        [str(MARGO), "debug"],
    ):
        run(command, PROJECT, env)

    run([sys.executable, "tests/check_source_free.py"], TEXT, env)
    run([sys.executable, "tests/check_source_free.py"], ENCODING, env)
    if args.full:
        run([sys.executable, "tests/validate_python_oracle.py"], ENCODING, env)

    scope = "full differential corpus" if args.full else "bounded smoke suite"
    print(f"Team T Text & Encoding acceptance passed ({scope})")


if __name__ == "__main__":
    main()
