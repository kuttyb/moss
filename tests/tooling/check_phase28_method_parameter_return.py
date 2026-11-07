#!/usr/bin/env python3
"""Check exact ReturnValue authority for an owned method parameter."""
from __future__ import annotations

import json
import os
from pathlib import Path
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]
SCRATCH = ROOT / "tmp" / "phase28_method_parameter_return"


def run(command: list[str], env: dict[str, str], cwd: Path = ROOT):
    return subprocess.run(command, cwd=cwd, env=env, text=True,
                          stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                          check=False, timeout=180)


def main() -> int:
    if len(sys.argv) != 2:
        raise SystemExit("usage: check_phase28_method_parameter_return.py COMPILER")
    compiler = str(Path(sys.argv[1]).resolve())
    margo = str(ROOT / "margo")
    shutil.rmtree(SCRATCH, ignore_errors=True)
    project = SCRATCH / "transport"
    source = project / "src"
    source.mkdir(parents=True, exist_ok=True)
    (project / "moss.toml").write_text(
        '[project]\nname = "phase28_method_parameter_return"\nversion = "0.1.0"\n',
        encoding="utf-8")
    provider = source / "provider.moss"
    provider.write_text("""module provider
export type Payload:
  value: Int
export type Carrier:
  fn Echo(payload: Payload) -> Payload:
    return payload
""", encoding="utf-8")
    consumer = source / "main.moss"
    consumer.write_text("""module app
import provider
fn main():
  carrier = provider.Carrier()
  payload = provider.Payload(value: 9)
  echo carrier.Echo(payload).value
""", encoding="utf-8")

    base_env = os.environ.copy()
    env = {**base_env, "MOSS": compiler,
           "MOSS_TEST_DUMP_PHASE28_AUTHORITY_FACTS": "1",
           "MOSS_TEST_VALIDATE_PHASE28_AUTHORITY_FACTS": "1"}
    checked = run([compiler, "check", "--json", str(consumer)], env)
    response = json.loads(checked.stdout)
    if checked.returncode or not response.get("ok"):
        raise AssertionError(f"method return checker rejected source: {response!r}\n"
                             f"{checked.stderr}")

    built = run([margo, "build", "--json"], env, project)
    build_response = json.loads(built.stdout)
    if built.returncode or not build_response.get("ok"):
        raise AssertionError(f"method return source-free build failed: "
                             f"{build_response!r}\n{built.stderr}")
    artifact = project / "build" / "debug" / "provider.mossi"
    text = artifact.read_text(encoding="utf-8")
    expected = ('"method:provider__Carrier.Echo"' in text and
                '"method:provider__Carrier.Echo:parameter:0"' in text and
                '"method:provider__Carrier.Echo:parameter:0:definition"' in text)
    if not expected:
        raise AssertionError("source-free ReturnValue artifact lost exact method "
                             "parameter binding identity")
    executable = Path(build_response["artifacts"]["artifacts"]["executable"])
    native = run([str(executable)], env, project)
    fast_debug = run([margo, "debug"], env, project)
    if native.returncode or native.stdout != "9\n":
        raise AssertionError(f"native return mismatch: {native.returncode}, "
                             f"{native.stdout!r}, {native.stderr!r}")
    if fast_debug.returncode or fast_debug.stdout != "9\n":
        raise AssertionError(f"Fast Debug return mismatch: {fast_debug.returncode}, "
                             f"{fast_debug.stdout!r}, {fast_debug.stderr!r}")

    source_free_project = SCRATCH / "source_free_consumer"
    source_free_src = source_free_project / "src"
    source_free_src.mkdir(parents=True, exist_ok=True)
    (source_free_project / "moss.toml").write_text(
        '[project]\nname = "phase28_method_parameter_return_consumer"\n'
        'version = "0.1.0"\n', encoding="utf-8")
    source_free_main = source_free_src / "main.moss"
    source_free_main.write_text(consumer.read_text(encoding="utf-8"),
                                encoding="utf-8")
    source_free_env = {**env, "MOSS_MODULE_PATH":
                       str(project / "build" / "debug")}
    imported = run([compiler, "check", "--json", str(source_free_main)],
                   source_free_env, source_free_project)
    imported_response = json.loads(imported.stdout)
    if imported.returncode or not imported_response.get("ok"):
        raise AssertionError(f"source-free method return import failed: "
                             f"{imported_response!r}\n{imported.stderr}")

    for hook in ("MOSS_TEST_DROP_RETURN_VALUE_FACT",
                 "MOSS_TEST_DUPLICATE_RETURN_VALUE_FACT",
                 "MOSS_TEST_PERTURB_RETURN_VALUE_FACT"):
        failed = run([compiler, "check", "--json", str(provider)],
                     {**env, hook: "1"})
        result = json.loads(failed.stdout)
        if result.get("ok"):
            raise AssertionError(f"{hook} did not fail closed: {result!r}")

    for hook in ("MOSS_TEST_CONSUMER_DROP_RETURN_VALUE_REQUIREMENT",
                 "MOSS_TEST_CONSUMER_DUPLICATE_RETURN_VALUE_REQUIREMENT",
                 "MOSS_TEST_CONSUMER_DROP_RETURN_VALUE_FACT",
                 "MOSS_TEST_CONSUMER_DUPLICATE_RETURN_VALUE_FACT",
                 "MOSS_TEST_CONSUMER_PERTURB_RETURN_VALUE_FACT"):
        shutil.rmtree(project / "build", ignore_errors=True)
        failed = run([margo, "build", "--json"], {**env, hook: "1"}, project)
        result = json.loads(failed.stdout)
        if result.get("ok"):
            raise AssertionError(f"native ReturnValue consumer accepted {hook}: "
                                 f"{result!r}\n{failed.stderr}")

    print("Phase 28 method parameter ReturnValue authority: PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
