#!/usr/bin/env python3
"""Check EST population for imported ABI-v9 and Section 2 Mossi bodies."""
import json
import os
from pathlib import Path
import subprocess
import sys
import time

ROOT = Path(__file__).resolve().parents[2]
MOSS = Path(sys.argv[1]).resolve()
MARGO = ROOT / "margo"
WORK = ROOT / "tmp" / "phase27-est-mossi" / str(time.time_ns())
LIB = WORK / "library"
APP = WORK / "app"


def run_margo(env):
    result = subprocess.run([str(MARGO), "build", "--check", "--json"],
                           cwd=APP, env=env, text=True,
                           stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    if result.returncode:
        raise SystemExit(f"margo .mossi EST probe failed:\n{result.stdout}\n{result.stderr}")
    return json.loads(result.stdout)


def run_moss(arguments, env):
    result = subprocess.run([str(MOSS), *arguments], cwd=APP, env=env, text=True,
                            stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    if result.returncode:
        raise SystemExit(f".mossi EST command failed {arguments}:\n"
                         f"{result.stdout}\n{result.stderr}")
    return result


def main():
    for root, package, deps in (
            (LIB, "est_est_library", ""),
            (APP, "est_est_app",
             '\n[dependencies]\nest_est_library = { path = "../library" }\n')):
        (root / "src").mkdir(parents=True, exist_ok=True)
        (root / "Moss.toml").write_text(
            f'[package]\nname = "{package}"\nversion = "0.1.0"\n'
            f'\n[build]\nsource = "src"\n{deps}', encoding="utf-8")
    (LIB / "src" / "est_est_library.moss").write_text(
        "module est_est_library\n"
        "export fn generic(value):\n  return value + 1\n"
        "export fn concrete(value: Int) -> Int:\n  return value * 2 + 1\n",
        encoding="utf-8")
    (APP / "src" / "main.moss").write_text(
        "module est_est_app\nimport est_est_library\n"
        "fn main():\n"
        "  echo est_est_library.generic(3)\n"
        "  echo est_est_library.concrete(4)\n", encoding="utf-8")
    build_env = os.environ.copy()
    build_env.update({"MOSS": str(MOSS), "MOSS_EST": "shadow"})
    report = run_margo(build_env)
    if not report.get("ok"):
        raise SystemExit("provider/app .mossi check did not complete")

    provider_path = LIB / "build" / "check-debug"
    dump_path = WORK / "est-dump.log"
    env = os.environ.copy()
    env.update({"MOSS_EST": "shadow", "MOSS_EST_DUMP": "1",
                "MOSS_EST_DUMP_FILE": str(dump_path),
                "MOSS_MODULE_PATH": str(provider_path)})
    checked = run_moss(["check", str(APP / "src" / "main.moss"), "--json"], env)
    if not json.loads(checked.stdout).get("ok"):
        raise SystemExit("source-free generic body check did not complete")
    generic_dumps = [line for line in dump_path.read_text(encoding="utf-8").splitlines()
                     if line.startswith("MOSS_EST_DUMP ") and "origin=0 " not in line]
    if not generic_dumps or not any("binary#" in line for line in generic_dumps):
        raise SystemExit("ABI-v9 generic_stmt body did not build with the EST grammar")

    dump_path.write_text("", encoding="utf-8")
    debug = run_moss(["debug", str(APP / "src" / "main.moss")], env)
    execution_dumps = [line for line in dump_path.read_text(encoding="utf-8").splitlines()
                       if line.startswith("MOSS_EST_DUMP ") and "origin=0 " not in line]
    if not execution_dumps or not any("binary#" in line for line in execution_dumps):
        raise SystemExit("Section 2 execution body did not build with the EST grammar")
    print(f"phase27 .mossi EST passed (generic={len(generic_dumps)}, "
          f"Section2={len(execution_dumps)} imported dumps)")


if __name__ == "__main__":
    main()
