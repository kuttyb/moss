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
    parser.add_argument(
        "--step",
        choices=[
            "all",
            "cbor",
            "oracle-debug",
            "oracle-release",
            "oracle-fast-debug",
            "margo-run",
            "margo-release",
            "margo-debug",
            "source-free",
            "cbor2",
        ],
        default="all",
        help="Run a specific acceptance step (default: all)",
    )
    args = parser.parse_args()

    step_tag = f"-{args.step}" if args.step != "all" else ""
    SCRATCH.mkdir(exist_ok=True)
    env = os.environ.copy()
    env.update(
        MOSS=os.environ.get("MOSS", str(REPO / "moss")),
        MARGO_HOME=str(SCRATCH / f"margo-advanced-data-acceptance{step_tag}"),
        TMPDIR=str(SCRATCH),
        PYTHONDONTWRITEBYTECODE="1",
    )

    if args.step in ("cbor", "all"):
        run([sys.executable, "validate_cbor_fixtures.py"], PROJECT, env)
    if args.step == "all":
        run([sys.executable, "validate_ndarray_tables_oracle.py"], PROJECT, env)
    elif args.step == "oracle-debug":
        run([sys.executable, "validate_ndarray_tables_oracle.py", "--mode", "debug"], PROJECT, env)
    elif args.step == "oracle-release":
        run([sys.executable, "validate_ndarray_tables_oracle.py", "--mode", "release"], PROJECT, env)
    elif args.step == "oracle-fast-debug":
        run([sys.executable, "validate_ndarray_tables_oracle.py", "--mode", "fast-debug"], PROJECT, env)
    if args.step in ("margo-run", "all"):
        run([str(MARGO), "run"], PROJECT, env)
    if args.step in ("margo-release", "all"):
        run([str(MARGO), "--release", "run"], PROJECT, env)
    if args.step in ("margo-debug", "all"):
        run([str(MARGO), "debug"], PROJECT, env)
    if args.step in ("source-free", "all"):
        run([sys.executable, "check_source_free.py"], PROJECT, env)
    if args.step in ("cbor2",) or (args.step == "all" and args.live_cbor2):
        run([sys.executable, "cbor2_oracle.py"], PROJECT, env)

    suffix = " plus live cbor2" if args.live_cbor2 else ""
    if args.step == "all":
        print(
            "Advanced-data acceptance passed: 7 scenarios, seeded ndarray/table "
            f"oracle, 12 RFC vectors{suffix}"
        )
    else:
        print(f"Advanced-data acceptance step '{args.step}' passed")


if __name__ == "__main__":
    main()
