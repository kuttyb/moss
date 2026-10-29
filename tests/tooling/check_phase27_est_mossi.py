#!/usr/bin/env python3
"""Check EST population for imported ABI-v9 and Section 2 Mossi bodies."""
import json
import hashlib
import os
from pathlib import Path
import shutil
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
    mossi_by_mode = {}
    for mode in ("off", "shadow"):
        for root in (LIB, APP):
            shutil.rmtree(root / "build", ignore_errors=True)
        build_env = os.environ.copy()
        build_env.update({"MOSS": str(MOSS), "MOSS_EST": mode})
        report = run_margo(build_env)
        if not report.get("ok"):
            raise SystemExit(f"provider/app .mossi check failed in {mode} mode")
        mossi_by_mode[mode] = {
            "library": (LIB / "build" / "check-debug" /
                        "est_est_library.mossi").read_bytes(),
            "app": (APP / "build" / "check-debug" /
                   "est_est_app.mossi").read_bytes(),
        }
    for package in ("library", "app"):
        off_bytes = mossi_by_mode["off"][package]
        shadow_bytes = mossi_by_mode["shadow"][package]
        if off_bytes != shadow_bytes:
            off_hash = hashlib.sha256(off_bytes).hexdigest()
            shadow_hash = hashlib.sha256(shadow_bytes).hexdigest()
            raise SystemExit(
                f"same-binary raw .mossi mismatch for {package}: "
                f"off={off_hash} shadow={shadow_hash}")

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
    parity_hashes = {package: hashlib.sha256(mossi_by_mode["off"][package]).hexdigest()
                     for package in ("library", "app")}
    print(f"phase27 .mossi EST passed (raw off/shadow hashes={parity_hashes}, "
          f"generic={len(generic_dumps)}, Section2={len(execution_dumps)} imported dumps)")


if __name__ == "__main__":
    main()
