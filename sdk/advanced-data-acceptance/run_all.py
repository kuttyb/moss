#!/usr/bin/env python3
"""Run the bounded Phase 24 Team A cross-package acceptance gates."""

import argparse
import os
from pathlib import Path
import subprocess
import sys


PROJECT = Path(__file__).resolve().parent
REPO = PROJECT.parents[1]
MARGO = REPO / "margo"
SCRATCH = REPO / "tmp"


def run(command: list[str], cwd: Path, env: dict[str, str]) -> None:
    print(f"Running {' '.join(command)} in {cwd.relative_to(REPO)}", flush=True)
    subprocess.run(command, cwd=cwd, env=env, check=True)


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "--live-cbor2",
        action="store_true",
        help="also run the optional cbor2 5.7.1 two-way oracle",
    )
    args = parser.parse_args()

    SCRATCH.mkdir(exist_ok=True)
    env = os.environ.copy()
    env.update(
        MOSS=str(REPO / "moss"),
        MARGO_HOME=str(SCRATCH / "margo-advanced-data-acceptance"),
        TMPDIR=str(SCRATCH),
        PYTHONDONTWRITEBYTECODE="1",
    )

    run([sys.executable, "validate_cbor_fixtures.py"], PROJECT, env)
    run([str(MARGO), "run"], PROJECT, env)
    run([str(MARGO), "--release", "run"], PROJECT, env)
    run([str(MARGO), "debug"], PROJECT, env)
    run([sys.executable, "check_source_free.py"], PROJECT, env)
    if args.live_cbor2:
        run([sys.executable, "cbor2_oracle.py"], PROJECT, env)

    suffix = " plus live cbor2" if args.live_cbor2 else ""
    print(f"Advanced-data acceptance passed: 7 scenarios, 12 RFC vectors{suffix}")


if __name__ == "__main__":
    main()
