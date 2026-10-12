#!/usr/bin/env python3
"""One command for Foundations cross-package acceptance.

Use --full for the larger randomized corpus; the default is a bounded smoke
suite suitable for the repository's regular check gate.
"""

import argparse
import subprocess
import sys
from pathlib import Path


PROJECT = Path(__file__).resolve().parent
REPO = PROJECT.parents[2]
FOUNDATIONS = PROJECT.parent


def run(args):
    subprocess.run([sys.executable, *map(str, args)], cwd=REPO, check=True)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--full", action="store_true")
    args = parser.parse_args()
    run([PROJECT / "check_acceptance.py"])
    oracle = [PROJECT / "validate_corpus.py", "--foundations-root", FOUNDATIONS,
              "--workdir", REPO / "tmp/foundations-acceptance-oracle",
              "--margo", REPO / "margo"]
    if not args.full:
        oracle.append("--quick")
    run(oracle)
    run([PROJECT / "validate_source_free.py", "--foundations-root", FOUNDATIONS,
         "--workdir", REPO / "tmp/foundations-acceptance-sourcefree",
         "--moss", REPO / "moss"])
    print("Foundations acceptance passed (full corpus)" if args.full else
          "Foundations acceptance passed (bounded smoke corpus)")


if __name__ == "__main__":
    main()
