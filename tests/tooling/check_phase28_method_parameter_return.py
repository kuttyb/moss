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
    base_env = os.environ.copy()
    shutil.rmtree(SCRATCH, ignore_errors=True)
    provider_project = SCRATCH / "provider"
    provider_source = provider_project / "src"
    provider_source.mkdir(parents=True, exist_ok=True)
    (provider_project / "moss.toml").write_text(
        '[project]\nname = "phase28_method_parameter_return"\nversion = "0.1.0"\n',
        encoding="utf-8")
    provider = provider_source / "provider.moss"
    provider.write_text("""module provider
export type Payload:
  value: Int
export type Carrier:
  fn Echo(payload: Payload) -> Payload:
    return payload
""", encoding="utf-8")
    consumer_text = """module app
import provider
fn main():
  carrier = provider.Carrier()
  payload = provider.Payload(value: 9)
  echo carrier.Echo(payload).value
"""
    provider_build = run([margo, "build", "--json"],
                         {**base_env, "MOSS": compiler}, provider_project)
    provider_build_response = json.loads(provider_build.stdout)
    if provider_build.returncode or not provider_build_response.get("ok"):
        raise AssertionError(f"provider-only package build failed: "
                             f"{provider_build_response!r}\n"
                             f"{provider_build.stderr}")
    artifact = provider_project / "build" / "debug" / "provider.mossi"

    visible_project = SCRATCH / "visible_consumer"
    visible_source = visible_project / "src"
    visible_source.mkdir(parents=True, exist_ok=True)
    (visible_project / "moss.toml").write_text(
        '[project]\nname = "phase28_method_parameter_return_visible"\n'
        'version = "0.1.0"\n', encoding="utf-8")
    shutil.copyfile(provider, visible_source / "provider.moss")
    consumer = visible_source / "main.moss"
    consumer.write_text(consumer_text, encoding="utf-8")

    env = {**base_env, "MOSS": compiler,
           "MOSS_TEST_DUMP_PHASE28_AUTHORITY_FACTS": "1",
           "MOSS_TEST_VALIDATE_PHASE28_AUTHORITY_FACTS": "1"}
    checked = run([compiler, "check", "--json", str(consumer)], env,
                  visible_project)
    response = json.loads(checked.stdout)
    if checked.returncode or not response.get("ok"):
        raise AssertionError(f"method return checker rejected source: {response!r}\n"
                             f"{checked.stderr}")

    built = run([margo, "build", "--json"], env, visible_project)
    build_response = json.loads(built.stdout)
    if built.returncode or not build_response.get("ok"):
        raise AssertionError(f"method return source-free build failed: "
                             f"{build_response!r}\n{built.stderr}")
    text = artifact.read_text(encoding="utf-8")
    expected = ('"method:provider__Carrier.Echo"' in text and
                '"method:provider__Carrier.Echo:parameter:0"' in text and
                '"method:provider__Carrier.Echo:parameter:0:definition"' in text)
    if not expected:
        raise AssertionError("source-free ReturnValue artifact lost exact method "
                             "parameter binding identity")
    executable = Path(build_response["artifacts"]["artifacts"]["executable"])
    native = run([str(executable)], env, visible_project)
    fast_debug = run([margo, "debug"], env, visible_project)
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
    source_free_main.write_text(consumer_text, encoding="utf-8")
    source_free_env = {**env, "MOSS_MODULE_PATH":
                       str(provider_project / "build" / "debug")}
    imported = run([compiler, "check", "--json", str(source_free_main)],
                   source_free_env, source_free_project)
    imported_response = json.loads(imported.stdout)
    if imported.returncode or not imported_response.get("ok"):
        raise AssertionError(f"source-free method return import failed: "
                             f"{imported_response!r}\n{imported.stderr}")
    source_free_build = run([margo, "build", "--json"], source_free_env,
                            source_free_project)
    source_free_build_response = json.loads(source_free_build.stdout)
    if source_free_build.returncode or not source_free_build_response.get("ok"):
        raise AssertionError("source-free method return build failed: "
                             f"{source_free_build_response!r}\n"
                             f"{source_free_build.stderr}")
    source_free_artifact = artifact.read_text(encoding="utf-8")
    if ('params=1 "payload" "provider__Payload" CONSUME' not in
            source_free_artifact or
            'method_param 0 "payload" "provider__Payload" CONSUME' not in
            source_free_artifact or '"src/provider.moss"' not in
            source_free_artifact):
        raise AssertionError("imported .moss declaration lost its checked CONSUME "
                             "effect or source provenance")
    source_free_rust = (source_free_project / "build" / "debug" /
                        "app.rs").read_text(encoding="utf-8")
    if ("carrier.Echo(payload)" not in source_free_rust or
            "carrier.Echo(&(payload))" in source_free_rust):
        raise AssertionError("source-free Native method call did not pass the "
                             "checked CONSUME argument by value")
    source_free_executable = Path(
        source_free_build_response["artifacts"]["artifacts"]["executable"])
    source_free_native = run([str(source_free_executable)], source_free_env,
                             source_free_project)
    source_free_fast = run([margo, "debug"], source_free_env,
                           source_free_project)
    if (source_free_native.returncode or source_free_native.stdout != "9\n" or
            source_free_fast.returncode or source_free_fast.stdout != "9\n"):
        raise AssertionError("source-free method return output mismatch: "
                             f"Native={source_free_native.returncode}/"
                             f"{source_free_native.stdout!r}/"
                             f"{source_free_native.stderr!r}; Fast Debug="
                             f"{source_free_fast.returncode}/"
                             f"{source_free_fast.stdout!r}/"
                             f"{source_free_fast.stderr!r}")
    for hook in ("MOSS_TEST_CONSUMER_WRONG_RETURN_VALUE_SPECIALIZATION",
                 "MOSS_TEST_CONSUMER_PERTURB_IMPORTED_RETURN_MODULE_ID",
                 "MOSS_TEST_CONSUMER_DROP_RETURN_VALUE_REQUIREMENT",
                 "MOSS_TEST_CONSUMER_DUPLICATE_RETURN_VALUE_REQUIREMENT",
                 "MOSS_TEST_CONSUMER_DROP_RETURN_VALUE_FACT",
                 "MOSS_TEST_CONSUMER_DUPLICATE_RETURN_VALUE_FACT"):
        shutil.rmtree(source_free_project / "build", ignore_errors=True)
        corrupted = run([margo, "debug"],
                        {**source_free_env, hook: "1"}, source_free_project)
        if corrupted.returncode == 0:
            raise AssertionError(
                f"source-free ReturnValue consumer accepted contradictory {hook}")
        expected_failure = (
            "MOSS_INTERNAL_INVARIANT return-value requirement/fact missing or ambiguous")
        if hook in {
                "MOSS_TEST_CONSUMER_DROP_RETURN_VALUE_REQUIREMENT",
                "MOSS_TEST_CONSUMER_DUPLICATE_RETURN_VALUE_REQUIREMENT",
                "MOSS_TEST_CONSUMER_DROP_RETURN_VALUE_FACT",
                "MOSS_TEST_CONSUMER_DUPLICATE_RETURN_VALUE_FACT"} and \
                expected_failure not in corrupted.stderr:
            raise AssertionError(
                f"source-free {hook} failed without the expected invariant: "
                f"{corrupted.stderr!r}")

    for hook in ("MOSS_TEST_DROP_RETURN_VALUE_FACT",
                 "MOSS_TEST_DUPLICATE_RETURN_VALUE_FACT",
                 "MOSS_TEST_PERTURB_RETURN_VALUE_FACT"):
        failed = run([compiler, "check", "--json", str(provider)],
                     {**env, hook: "1"}, provider_project)
        result = json.loads(failed.stdout)
        if result.get("ok"):
            raise AssertionError(f"{hook} did not fail closed: {result!r}")

    for hook in ("MOSS_TEST_CONSUMER_DROP_RETURN_VALUE_REQUIREMENT",
                 "MOSS_TEST_CONSUMER_DUPLICATE_RETURN_VALUE_REQUIREMENT",
                 "MOSS_TEST_CONSUMER_DROP_RETURN_VALUE_FACT",
                 "MOSS_TEST_CONSUMER_DUPLICATE_RETURN_VALUE_FACT",
                 "MOSS_TEST_CONSUMER_PERTURB_RETURN_VALUE_FACT"):
        shutil.rmtree(visible_project / "build", ignore_errors=True)
        failed = run([margo, "build", "--json"],
                     {**env, hook: "1"}, visible_project)
        result = json.loads(failed.stdout)
        if result.get("ok"):
            raise AssertionError(f"native ReturnValue consumer accepted {hook}: "
                                 f"{result!r}\n{failed.stderr}")

    print("Phase 28 method parameter ReturnValue authority: PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
