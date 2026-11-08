#!/usr/bin/env python3
"""Keep Native EST child identity while lowering a field over a call."""
from __future__ import annotations

import json
import os
from pathlib import Path
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]
SCRATCH = ROOT / "tmp" / "phase28_native_call_argument_cursor"


def run(command: list[str], env: dict[str, str], cwd: Path):
    return subprocess.run(command, cwd=cwd, env=env, text=True,
                          stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                          check=False, timeout=180)


def main() -> int:
    if len(sys.argv) != 2:
        raise SystemExit(
            "usage: check_phase28_native_call_argument_cursor.py COMPILER")
    compiler = str(Path(sys.argv[1]).resolve())
    margo = str(ROOT / "margo")
    shutil.rmtree(SCRATCH, ignore_errors=True)
    source = SCRATCH / "src"
    source.mkdir(parents=True)
    (SCRATCH / "moss.toml").write_text(
        '[project]\nname = "phase28_native_call_argument_cursor"\n'
        'version = "0.1.0"\n', encoding="utf-8")
    (source / "provider.moss").write_text(
        "module provider\n"
        "export type Payload:\n  value: Int\n"
        "export fn identity(payload: Payload) -> Payload:\n"
        "  return payload\n", encoding="utf-8")
    (source / "main.moss").write_text(
        "module app\nimport provider\n"
        "fn main():\n"
        "  payload = provider.Payload(value: 9)\n"
        "  echo provider.identity(payload).value\n", encoding="utf-8")
    env = {**os.environ, "MOSS": compiler, "MOSS_STAGED_ABI": "v10"}
    built = run([margo, "build", "--json"], env, SCRATCH)
    try:
        response = json.loads(built.stdout)
    except json.JSONDecodeError as error:
        raise AssertionError(f"margo build returned invalid JSON: {built.stdout!r}") from error
    if built.returncode or not response.get("ok"):
        raise AssertionError(f"Native call-cursor project failed to build: "
                             f"{response!r}\n{built.stderr}")
    executable = Path(response["artifacts"]["artifacts"]["executable"])
    native = run([str(executable)], env, SCRATCH)
    fast = run([margo, "debug"], env, SCRATCH)
    if (native.returncode or native.stdout != "9\n" or
            fast.returncode or fast.stdout != "9\n"):
        raise AssertionError(
            f"Native={native.returncode}/{native.stdout!r}/{native.stderr!r}; "
            f"Fast Debug={fast.returncode}/{fast.stdout!r}/{fast.stderr!r}")
    print("Phase 28 Native exact call argument cursor: PASS (9 / 9)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
