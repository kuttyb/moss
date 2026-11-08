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
    for label, module, provider in (("lower", "app", "provider"),
                                    ("upper", "App", "Provider")):
        project = SCRATCH / label
        source = project / "src"
        source.mkdir(parents=True)
        (project / "moss.toml").write_text(
            '[project]\nname = "phase28_native_call_argument_cursor_' + label + '"\n'
            'version = "0.1.0"\n', encoding="utf-8")
        (source / "provider.moss").write_text(
            f"module {provider}\n"
            "export type Payload:\n  value: Int\n"
            "export fn identity(payload: Payload) -> Payload:\n"
            "  return payload\n"
            "export fn normalize(value):\n  return value\n", encoding="utf-8")
        (source / "main.moss").write_text(
            f"module {module}\nimport {provider}\n"
            "fn pair(first, second):\n  echo first\n  echo second\n"
            "fn main():\n"
            f"  payload = {provider}.Payload(value: 9)\n"
            f"  echo {provider}.identity(payload).value\n"
            f"  pair({provider}.normalize(10), {provider}.normalize(3.5))\n",
            encoding="utf-8")
        env = {**os.environ, "MOSS": compiler, "MOSS_STAGED_ABI": "v10"}
        built = run([margo, "build", "--json"], env, project)
        try:
            response = json.loads(built.stdout)
        except json.JSONDecodeError as error:
            raise AssertionError(f"margo build returned invalid JSON: {built.stdout!r}") from error
        if built.returncode or not response.get("ok"):
            raise AssertionError(f"Native call-cursor {label} project failed to build: "
                                 f"{response!r}\n{built.stderr}")
        executable = Path(response["artifacts"]["artifacts"]["executable"])
        native = run([str(executable)], env, project)
        fast = run([margo, "debug"], env, project)
        expected = "9\n10\n3.5\n"
        if (native.returncode or native.stdout != expected or
                fast.returncode or fast.stdout != expected):
            raise AssertionError(
                f"{label}: Native={native.returncode}/{native.stdout!r}/{native.stderr!r}; "
                f"Fast Debug={fast.returncode}/{fast.stdout!r}/{fast.stderr!r}")
        print(f"Phase 28 exact call cursor {label}: PASS (constructor, member, nested pair)")
    # Function/type registries may share a spelling. Qualification must leave
    # nominal syntax intact so the checker retains the cross-kind decision.
    project = SCRATCH / "collision"
    source = project / "src"
    source.mkdir(parents=True)
    (project / "moss.toml").write_text(
        '[project]\nname = "phase28_call_kind_collision"\nversion = "0.1.0"\n',
        encoding="utf-8")
    (source / "main.moss").write_text(
        "module Collision\n"
        "type Payload:\n  value: Int\n"
        "fn Payload(value: Int) -> Int:\n  return value + 100\n"
        "fn main():\n  payload = Payload(value: 9)\n  echo payload.value\n",
        encoding="utf-8")
    env = {**os.environ, "MOSS": compiler, "MOSS_STAGED_ABI": "v10"}
    built = run([margo, "build", "--json"], env, project)
    response = json.loads(built.stdout)
    if built.returncode or not response.get("ok"):
        raise AssertionError(f"cross-kind collision control failed: {response!r}\n{built.stderr}")
    executable = Path(response["artifacts"]["artifacts"]["executable"])
    native = run([str(executable)], env, project)
    fast = run([margo, "debug"], env, project)
    if native.returncode or fast.returncode or native.stdout != "9\n" or fast.stdout != "9\n":
        raise AssertionError(f"collision: Native={native!r}; Fast Debug={fast!r}")
    print("Phase 28 qualified function/type collision: PASS (nominal constructor)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
