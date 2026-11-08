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
    base_env.pop("MOSS_MODULE_PATH", None)
    base_env.pop("MOSS_EST", None)
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

    function_provider = SCRATCH / "function_provider"
    function_src = function_provider / "src"
    function_src.mkdir(parents=True, exist_ok=True)
    (function_provider / "moss.toml").write_text(
        '[project]\nname = "phase28_function_return_provider"\n'
        'version = "0.1.0"\n', encoding="utf-8")
    (function_src / "provider.moss").write_text(
        "module provider\nexport fn identity(value):\n  return value\n",
        encoding="utf-8")
    function_provider_build = run(
        [margo, "build", "--json"],
        {**base_env, "MOSS": compiler, "MOSS_STAGED_ABI": "v10"},
        function_provider)
    function_provider_response = json.loads(function_provider_build.stdout)
    if (function_provider_build.returncode or
            not function_provider_response.get("ok")):
        raise AssertionError("source-free generic function provider failed: "
                             f"{function_provider_response!r}\n"
                             f"{function_provider_build.stderr}")
    function_consumer = SCRATCH / "function_consumer"
    function_consumer_src = function_consumer / "src"
    function_consumer_src.mkdir(parents=True, exist_ok=True)
    (function_consumer / "moss.toml").write_text(
        '[project]\nname = "phase28_function_return_consumer"\n'
        'version = "0.1.0"\n', encoding="utf-8")
    (function_consumer_src / "main.moss").write_text(
        "module app\nimport provider\nfn main():\n"
        "  echo provider.identity(9)\n", encoding="utf-8")
    function_env = {
        **base_env,
        "MOSS": compiler,
        "MOSS_STAGED_ABI": "v10",
        "MOSS_MODULE_PATH": str(function_provider / "build" / "debug"),
    }
    function_build = run([margo, "build", "--json"], function_env,
                         function_consumer)
    function_build_response = json.loads(function_build.stdout)
    if function_build.returncode or not function_build_response.get("ok"):
        raise AssertionError("source-free generic function consumer build failed: "
                             f"{function_build_response!r}\n"
                             f"{function_build.stderr}")
    function_executable = Path(
        function_build_response["artifacts"]["artifacts"]["executable"])
    function_native = run([str(function_executable)], function_env,
                          function_consumer)
    function_fast = run([margo, "debug"], function_env, function_consumer)
    if (function_native.returncode or function_native.stdout != "9\n" or
            function_fast.returncode or function_fast.stdout != "9\n"):
        raise AssertionError("source-free generic function output mismatch: "
                             f"Native={function_native.returncode}/"
                             f"{function_native.stdout!r}/"
                             f"{function_native.stderr!r}; Fast Debug="
                             f"{function_fast.returncode}/"
                             f"{function_fast.stdout!r}/"
                             f"{function_fast.stderr!r}")
    owner_mutation_diagnostic = (
        "MOSS_INTERNAL_INVARIANT source-free ReturnValue fact does not bind its "
        "exact checked body occurrence")
    for hook in ("MOSS_TEST_CONSUMER_WRONG_RETURN_VALUE_SPECIALIZATION",
                 "MOSS_TEST_CONSUMER_PERTURB_IMPORTED_RETURN_MODULE_ID"):
        shutil.rmtree(function_consumer / "build", ignore_errors=True)
        native_corruption = run([margo, "build", "--json"],
                                {**function_env, hook: "1"}, function_consumer)
        if (native_corruption.returncode == 0 or
                owner_mutation_diagnostic not in
                native_corruption.stdout + native_corruption.stderr):
            raise AssertionError(
                f"Native {hook} did not reach the production owner validator: "
                f"{native_corruption.returncode}, {native_corruption.stderr!r}")
        shutil.rmtree(function_consumer / "build", ignore_errors=True)
        fast_corruption = run([margo, "debug"],
                              {**function_env, hook: "1"}, function_consumer)
        if (fast_corruption.returncode == 0 or
                owner_mutation_diagnostic not in
                fast_corruption.stdout + fast_corruption.stderr):
            raise AssertionError(
                f"Fast Debug {hook} did not reach the production owner validator: "
                f"{fast_corruption.returncode}, {fast_corruption.stderr!r}")

    paired_provider = SCRATCH / "paired_function_provider"
    paired_provider_src = paired_provider / "src"
    paired_provider_src.mkdir(parents=True, exist_ok=True)
    (paired_provider / "moss.toml").write_text(
        '[project]\nname = "phase28_paired_function_provider"\n'
        'version = "0.1.0"\n', encoding="utf-8")
    (paired_provider_src / "provider.moss").write_text(
        "module provider\n"
        "export type Payload:\n  value: Int\n"
        "export fn identity(payload: Payload) -> Payload:\n"
        "  return payload\n"
        "export fn normalize(value):\n  return value\n",
        encoding="utf-8")
    paired_consumer = SCRATCH / "paired_function_consumer"
    paired_consumer_src = paired_consumer / "src"
    paired_consumer_src.mkdir(parents=True, exist_ok=True)
    (paired_consumer / "moss.toml").write_text(
        '[project]\nname = "phase28_paired_function_consumer"\n'
        'version = "0.1.0"\n', encoding="utf-8")
    (paired_consumer_src / "main.moss").write_text(
        "module app\nimport provider\n"
        "fn pair(left, right):\n  echo left\n  echo right\n"
        "fn main():\n"
        "  payload = provider.Payload(value: 9)\n"
        "  echo provider.identity(payload).value\n"
        "  pair(provider.normalize(10), provider.normalize(3.5))\n",
        encoding="utf-8")
    paired_provider_build = run(
        [margo, "build", "--json"],
        {**base_env, "MOSS": compiler, "MOSS_STAGED_ABI": "v10"},
        paired_provider)
    paired_provider_response = json.loads(paired_provider_build.stdout)
    if (paired_provider_build.returncode or
            not paired_provider_response.get("ok")):
        raise AssertionError("source-free Payload/generic provider failed: "
                             f"{paired_provider_response!r}\n"
                             f"{paired_provider_build.stderr}")
    paired_env = {
        **base_env,
        "MOSS": compiler,
        "MOSS_STAGED_ABI": "v10",
        "MOSS_MODULE_PATH": str(paired_provider / "build" / "debug"),
    }
    paired_build = run([margo, "build", "--json"], paired_env,
                       paired_consumer)
    paired_response = json.loads(paired_build.stdout)
    if paired_build.returncode or not paired_response.get("ok"):
        raise AssertionError("source-free Payload/generic consumer build failed: "
                             f"{paired_response!r}\n{paired_build.stderr}")
    paired_executable = Path(
        paired_response["artifacts"]["artifacts"]["executable"])
    paired_native = run([str(paired_executable)], paired_env, paired_consumer)
    paired_fast = run([margo, "debug"], paired_env, paired_consumer)
    if (paired_native.returncode or paired_native.stdout != "9\n10\n3.5\n" or
            paired_fast.returncode or paired_fast.stdout != "9\n10\n3.5\n"):
        raise AssertionError("source-free Payload/generic output mismatch: "
                             f"Native={paired_native.returncode}/"
                             f"{paired_native.stdout!r}/"
                             f"{paired_native.stderr!r}; Fast Debug="
                             f"{paired_fast.returncode}/"
                             f"{paired_fast.stdout!r}/"
                             f"{paired_fast.stderr!r}")

    id_only_provider = SCRATCH / "id_only_provider"
    id_only_source_dir = id_only_provider / "src"
    id_only_source_dir.mkdir(parents=True, exist_ok=True)
    (id_only_provider / "moss.toml").write_text(
        '[project]\nname = "phase28_id_only_provider"\n'
        'version = "0.1.0"\n', encoding="utf-8")
    (id_only_source_dir / "provider.moss").write_text(
        "module provider\n"
        "export fn normalize(value):\n  return value\n"
        "export fn exercise():\n  echo normalize(1)\n",
        encoding="utf-8")
    id_only_provider_build = run(
        [margo, "build", "--json"],
        {**base_env, "MOSS": compiler, "MOSS_STAGED_ABI": "v10"},
        id_only_provider)
    id_only_response = json.loads(id_only_provider_build.stdout)
    if id_only_provider_build.returncode or not id_only_response.get("ok"):
        raise AssertionError("ID-only specialization provider failed to build: "
                             f"{id_only_response!r}\n"
                             f"{id_only_provider_build.stderr}")

    id_only_consumer = SCRATCH / "id_only_consumer"
    id_only_consumer_src = id_only_consumer / "src"
    id_only_consumer_src.mkdir(parents=True, exist_ok=True)
    (id_only_consumer / "moss.toml").write_text(
        '[project]\nname = "phase28_id_only_consumer"\n'
        'version = "0.1.0"\n', encoding="utf-8")
    id_only_main = id_only_consumer_src / "main.moss"
    id_only_main.write_text(
        "module app\nimport provider\nfn main():\n  echo 0\n",
        encoding="utf-8")
    id_only_env = {
        **base_env,
        "MOSS_STAGED_ABI": "v10",
        "MOSS_MODULE_PATH": str(id_only_provider / "build" / "debug"),
    }
    id_only_check = run(
        [compiler, "check", str(id_only_main), "--json"],
        id_only_env, id_only_consumer)
    try:
        id_only_check_response = json.loads(id_only_check.stdout)
    except json.JSONDecodeError as error:
        raise AssertionError(
            "ID-only imported specialization did not fail with structured "
            f"checker output: exit={id_only_check.returncode}, "
            f"stdout={id_only_check.stdout!r}, stderr={id_only_check.stderr!r}") from error
    id_only_error = id_only_check_response.get("error", {})
    if (id_only_check.returncode == 0 or id_only_error.get("code") !=
            "MOSS_INTERNAL_INVARIANT" or
            id_only_error.get("message") !=
            "checked function specialization has missing or contradictory parameter metadata"):
        raise AssertionError("ID-only imported specialization was not rejected "
                             f"at the metadata boundary: {id_only_check_response!r}")

    print("Phase 28 method/function ReturnValue, generic pair, and ID-only metadata guard: PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
