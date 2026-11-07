#!/usr/bin/env python3
"""Check dynamic pipeline callback authority and Fast Debug corruption hooks."""
from __future__ import annotations

import os
from pathlib import Path
import shutil
import subprocess
import sys


ROOT = Path(__file__).resolve().parents[2]
FIXTURES = ROOT / "tests/tooling/fixtures/phase28_l8"
EXPECTED = {
    "cb_spec": "12",
    "cb_shadow_spec": "[3, 6]",
}


def run(args: list[str], *, env=None) -> subprocess.CompletedProcess[str]:
    result = subprocess.run(args, cwd=ROOT, env=env, capture_output=True,
                            text=True, timeout=120)
    if result.returncode:
        raise RuntimeError(
            f"command failed ({result.returncode}): {args}\n"
            f"{result.stdout}{result.stderr}")
    return result


def native_output(compiler: str, source: Path, scratch: Path,
                  name: str) -> str:
    rust = scratch / f"{name}.rs"
    binary = scratch / name
    run([compiler, str(source), "-o", str(rust)])
    run(["rustc", "--edition=2021", "-D", "warnings", str(rust), "-o",
         str(binary)])
    return run([str(binary)]).stdout.strip()


def main() -> int:
    if len(sys.argv) != 2:
        print("usage: check_phase28_l8_callback_authority.py COMPILER",
              file=sys.stderr)
        return 2
    compiler = str(Path(sys.argv[1]).resolve())
    scratch = ROOT / "tmp" / f"phase28-l8-callback-authority-{os.getpid()}"
    scratch.mkdir(parents=True, exist_ok=True)
    try:
        for name, expected in EXPECTED.items():
            source = FIXTURES / f"{name}.moss"
            fast = run([compiler, "run", "--interp", str(source)]).stdout.strip()
            native = native_output(compiler, source, scratch, name)
            if fast != expected or native != expected or fast != native:
                raise RuntimeError(
                    f"{name}: expected {expected!r}, Fast Debug={fast!r}, "
                    f"Native={native!r}")
            print(f"PASS {name}: both engines output {fast!r}")

        shadow = FIXTURES / "cb_shadow_spec.moss"
        for hook, diagnostic in (
            ("MOSS_TEST_CONSUMER_DROP_DYNAMIC_CALLBACK_FACT",
             "checked pipeline callback classification is missing"),
            ("MOSS_TEST_CONSUMER_PERTURB_DYNAMIC_CALLBACK_FACT",
             "static callback fact contradicts its checked value"),
        ):
            env = os.environ.copy()
            env[hook] = "1"
            result = subprocess.run(
                [compiler, "run", "--interp", str(shadow)], cwd=ROOT,
                env=env, capture_output=True, text=True, timeout=120)
            combined = result.stdout + result.stderr
            if result.returncode == 0 or "MOSS_INTERNAL_INVARIANT" not in combined or \
                    diagnostic not in combined:
                raise RuntimeError(
                    f"{hook} did not fail closed with the expected diagnostic; "
                    f"exit={result.returncode}\n{combined}")
            print(f"PASS {hook}: fail closed")
    finally:
        shutil.rmtree(scratch, ignore_errors=True)
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, RuntimeError, subprocess.TimeoutExpired) as error:
        print(f"FAIL check_phase28_l8_callback_authority: {error}", file=sys.stderr)
        raise SystemExit(1)
