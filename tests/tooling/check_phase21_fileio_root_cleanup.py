#!/usr/bin/env python3
"""Run the generated Root FileIO teardown ordering witness."""

import os
import subprocess
import sys


REPO_ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "../.."))
SOURCE = os.path.join(REPO_ROOT, "tests/phase21_fileio_root_cleanup.moss")
TMP_ROOT = os.path.join(REPO_ROOT, "tmp/phase21-fileio-root-cleanup")
DATA = os.path.join(REPO_ROOT, "tmp/phase21-fileio-root-cleanup.dat")


def run(command, **kwargs):
    result = subprocess.run(command, capture_output=True, text=True, **kwargs)
    if result.returncode:
        sys.stderr.write(result.stderr)
        if result.stdout:
            sys.stderr.write(result.stdout)
        raise SystemExit(result.returncode)
    return result


def main() -> int:
    compiler = os.path.abspath(sys.argv[1] if len(sys.argv) > 1 else "./moss")
    os.makedirs(TMP_ROOT, exist_ok=True)
    if os.path.exists(DATA):
        os.unlink(DATA)
    generated = os.path.join(TMP_ROOT, "generated_root_cleanup.rs")
    executable = os.path.join(TMP_ROOT, "generated_root_cleanup")

    try:
        run([compiler, SOURCE, "-o", generated], cwd=REPO_ROOT)
        run(["rustc", "-D", "warnings", generated, "-o", executable], cwd=REPO_ROOT)
        result = run([executable], cwd=REPO_ROOT, timeout=20)
        if result.stdout != "11\n":
            sys.stderr.write(result.stderr)
            raise SystemExit(
                "generated Root cleanup witness expected reply 11; "
                f"got stdout {result.stdout!r}"
            )
    finally:
        if os.path.exists(DATA):
            os.unlink(DATA)
    print("phase21 generated Root FileIO cleanup ordering passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
